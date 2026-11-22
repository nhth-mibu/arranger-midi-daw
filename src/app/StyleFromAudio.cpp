#include "StyleFromAudio.h"

#include "AudioAnalysis.h"
#include "McpService.h"
#include "PythonTools.h"
#include "Session.h"

#include "arranger/core/LinkAudio.h"
#include "arranger/core/SongEdit.h"

#include <juce_core/juce_core.h>

namespace arranger
{

struct StyleFromAudioService::Job final : juce::Thread
{
    Job (StyleFromAudioService& o, analysis::Result r, style_from_audio::Options opt)
        : juce::Thread ("Style from Audio"), owner (o), result (std::move (r)), options (std::move (opt))
    {
    }
    ~Job() override { stopThread (10000); }

    void run() override
    {
        std::shared_ptr<const style_from_audio::Made> made;
        juce::String error;
        auto opt = options;
        opt.progress = [this] (double progress, const std::string& step) {
            juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Job> (this), progress, s = juce::String::fromUTF8 (step.c_str())] {
                if (alive == nullptr)
                    return;
                owner.current.progress = progress;
                owner.current.step = s;
                owner.changed();
            });
            return ! threadShouldExit();
        };
        try
        {
            made = std::make_shared<const style_from_audio::Made> (style_from_audio::make (result, opt));
        }
        catch (const std::exception& e)
        {
            error = juce::String::fromUTF8 (e.what());
        }
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Job> (this), made, error] {
            if (alive != nullptr)
                owner.finished (made, error);
        });
    }

    StyleFromAudioService& owner;
    analysis::Result result;
    style_from_audio::Options options;
    JUCE_DECLARE_WEAK_REFERENCEABLE (Job)
};

juce::String StyleFromAudioService::fetch (const juce::String& link, const juce::File& folder, const LinkTools& tools, juce::Thread& thread, juce::File& file,
                                          const std::function<void (double, const juce::String&)>& progress)
{
    juce::String error, last;
    folder.createDirectory();
    juce::StringArray args { tools.ytDlp.getFullPathName() };
    for (const auto& a : link_audio::arguments (link.toStdString(), folder.getFullPathName().toStdString(), tools.ffmpeg.getParentDirectory().getFullPathName().toStdString()))
        args.add (juce::String::fromUTF8 (a.c_str()));
    double lastShown = -1.0;
    const int code = python_tools::run (args, thread, [&] (const juce::String& line) {
        const auto text = line.toStdString();
        if (const auto p = link_audio::progress (text))
        {
            if (progress && (*p - lastShown >= 0.01 || *p >= 1.0))
            {
                lastShown = *p;
                progress (*p * 0.95, "Downloading the audio");
            }
        }
        else if (const auto f = link_audio::fileFrom (text); ! f.empty())
            file = juce::File (juce::String::fromUTF8 (f.c_str()));
        else if (const auto e = link_audio::errorFrom (text); ! e.empty())
            error = juce::String::fromUTF8 (e.c_str());
        else if (line.contains ("[ExtractAudio]") && progress)
            progress (0.97, "Making a WAV file");
        if (line.trim().isNotEmpty())
            last = line.trim();
    });
    if (thread.threadShouldExit())
        return "Stopped.";
    if (error.isEmpty() && (code != 0 || ! file.existsAsFile()))
        error = "yt-dlp could not get the audio" + (last.isNotEmpty() ? ": " + last : juce::String ("."));
    return error;
}

/** Runs yt-dlp for one link. */
struct StyleFromAudioService::Download final : juce::Thread
{
    Download (StyleFromAudioService& o, juce::String l, juce::File f, LinkTools t)
        : juce::Thread ("Audio from a link"), owner (o), link (std::move (l)), folder (std::move (f)), tools (std::move (t))
    {
    }
    ~Download() override { stopThread (10000); }

    void run() override
    {
        juce::File file;
        auto error = fetch (link, folder, tools, *this, file, [this] (double p, const juce::String& step) { report (p, step); });
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Download> (this), file, error] {
            if (alive != nullptr)
                owner.downloaded (file, error);
        });
    }

    void report (double progress, const juce::String& step)
    {
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Download> (this), progress, step] {
            if (alive == nullptr)
                return;
            owner.current.progress = progress;
            owner.current.step = step;
            owner.changed();
        });
    }

    StyleFromAudioService& owner;
    juce::String link;
    juce::File folder;
    LinkTools tools;
    JUCE_DECLARE_WEAK_REFERENCEABLE (Download)
};

StyleFromAudioService::StyleFromAudioService (Session& s) : session (s) {}

StyleFromAudioService::~StyleFromAudioService()
{
    stopTimer();
    download.reset();
    job.reset();
}

bool StyleFromAudioService::isRunning() const noexcept { return job != nullptr || download != nullptr || waitingForAnalysis; }

namespace
{
    link_audio::Platform thisPlatform()
    {
#if JUCE_MAC
        return link_audio::Platform::mac;
#elif JUCE_WINDOWS
        return link_audio::Platform::windows;
#else
        return link_audio::Platform::linux;
#endif
    }

    juce::File findProgram (const juce::String& base)
    {
        const auto name = juce::String::fromUTF8 (link_audio::programName (base.toStdString(), thisPlatform()).c_str());
        const auto separator = thisPlatform() == link_audio::Platform::windows ? ";" : ":";
        juce::StringArray folders = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}), separator, {});
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
        for (const auto& f : link_audio::usualFolders (thisPlatform(), home))
            folders.add (juce::String::fromUTF8 (f.c_str()));
        for (const auto& folder : folders)
        {
            if (folder.trim().isEmpty() || ! juce::File::isAbsolutePath (folder.trim()))
                continue;
            const auto file = juce::File (folder.trim()).getChildFile (name);
            if (file.existsAsFile())
                return file;
        }
        return {};
    }
}

StyleFromAudioService::LinkTools StyleFromAudioService::findLinkTools()
{
    return { findProgram ("yt-dlp"), findProgram ("ffmpeg") };
}

juce::String StyleFromAudioService::linkToolsHelp()
{
    const auto tools = findLinkTools();
    if (tools.ready())
        return {};
    juce::String text;
    if (! tools.ytDlp.existsAsFile() && ! tools.ffmpeg.existsAsFile())
        text << "To get audio from a link, the app needs yt-dlp and ffmpeg (free programs).";
    else
        text << "To get audio from a link, the app also needs " << (tools.ytDlp.existsAsFile() ? "ffmpeg" : "yt-dlp") << " (a free program).";
    for (const auto& line : link_audio::installSteps (thisPlatform()))
        text << "\n" << juce::String::fromUTF8 (line.c_str());
    return text;
}

juce::File StyleFromAudioService::downloadFolder() const
{
    const auto project = session.file();
    if (project != juce::File())
        return project.getSiblingFile (project.getFileNameWithoutExtension() + " Audio");
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("Downloads");
}

void StyleFromAudioService::downloaded (const juce::File& file, const juce::String& error)
{
    download.reset();
    if (error.isNotEmpty())
    {
        finished (nullptr, error);
        return;
    }
    // The audio on a new track at bar 1, as File > Import Audio does.
    const auto path = file.getFullPathName().toStdString();
    const auto sample = session.sampleFor (path);
    if (sample == nullptr || sample->frames() == 0)
    {
        finished (nullptr, "The downloaded audio can't be read: " + file.getFullPathName());
        return;
    }
    ClipId added = 0;
    TrackId track = 0;
    const double seconds = sample->seconds();
    session.edit ("Add Audio from Link", [&] (Song& s) {
        auto name = file.getFileNameWithoutExtension().upToLastOccurrenceOf (" [", false, false).replaceCharacter ('_', ' ').trim();
        if (name.isEmpty())
            name = file.getFileNameWithoutExtension();
        track = edit::addAudioTrack (s, name.toStdString()).id;
        AudioClip clip;
        clip.name = name.toStdString();
        clip.file = path;
        clip.start = 0;
        clip.length = std::max<Tick> (1, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (seconds))));
        added = edit::addAudioClip (s, track, std::move (clip));
    });
    session.selectTrack (track);
    session.selectClips ({ added });
    auto next = request;
    next.link.clear();
    if (next.name.empty())
        if (const auto* t = edit::findTrack (session.song(), track))
            next.name = t->name; // the title, without the video id
    next.clip = added;
    current = {};
    const auto why = start (next);
    if (why.isNotEmpty())
        finished (nullptr, why);
}

StyleFromAudioService::Status StyleFromAudioService::status() const { return current; }

void StyleFromAudioService::changed()
{
    if (onChange)
        onChange();
    session.sendChangeMessage();
}

juce::String StyleFromAudioService::start (const Request& r)
{
    if (isRunning())
        return "A style is being made already.";
    if (! r.link.empty())
    {
        const auto link = link_audio::clean (r.link);
        if (link.empty())
            return "That is not a link. Copy the whole address, for example https://www.youtube.com/watch?v=...";
        const auto tools = findLinkTools();
        if (! tools.ready())
            return linkToolsHelp();
        request = r;
        current = {};
        current.state = Status::State::downloading;
        current.step = "Getting the audio from the link";
        download = std::make_unique<Download> (*this, juce::String::fromUTF8 (link.c_str()), downloadFolder(), tools);
        download->startThread (juce::Thread::Priority::low);
        changed();
        return {};
    }
    if (edit::findAudioClip (session.song(), r.clip) == nullptr)
        return "There is no audio clip with that id.";
    request = r;
    current = {};
    current.clip = r.clip;

    if (const auto result = session.analysis().resultFor (r.clip))
    {
        make (*result);
        return {};
    }
    // Not analysed yet: stems, notes, chords and instruments first.
    AnalysisService::Request a;
    a.clip = r.clip;
    a.lyrics = false;
    const auto why = session.analysis().start (a);
    if (why.isNotEmpty())
    {
        current = {};
        return why;
    }
    waitingForAnalysis = true;
    current.state = Status::State::analysing;
    current.step = "Analysing the audio";
    startTimer (250);
    changed();
    return {};
}

void StyleFromAudioService::timerCallback()
{
    if (! waitingForAnalysis)
    {
        stopTimer();
        return;
    }
    const auto st = session.analysis().status();
    using State = AnalysisService::Status::State;
    if (st.state == State::running)
    {
        current.progress = st.progress;
        current.step = "Analysing: " + st.step;
        changed();
        return;
    }
    stopTimer();
    waitingForAnalysis = false;
    if (st.state == State::failed)
    {
        finished (nullptr, st.error.isNotEmpty() ? st.error : juce::String ("The analysis failed."));
        return;
    }
    const auto result = session.analysis().resultFor (request.clip);
    if (! result)
    {
        juce::String why = "The analysis found no notes.";
        for (const auto& w : st.warnings)
            why << " " << juce::String::fromUTF8 (w.c_str());
        finished (nullptr, why);
        return;
    }
    make (*result);
}

void StyleFromAudioService::make (const analysis::Result& result)
{
    style_from_audio::Options o;
    o.bpm = request.bpm;
    o.mainBars = request.mainBars;
    o.vocals = request.vocals;
    o.name = request.name;
    current.state = Status::State::making;
    current.progress = 0.0;
    current.step = "Making the style";
    job = std::make_unique<Job> (*this, result, o);
    job->startThread (juce::Thread::Priority::low);
    changed();
}

void StyleFromAudioService::finished (std::shared_ptr<const style_from_audio::Made> made, const juce::String& error)
{
    job.reset();
    current.made = std::move (made);
    current.error = error;
    current.state = current.made != nullptr ? Status::State::done : Status::State::failed;
    current.progress = 1.0;
    current.step = current.made != nullptr ? "Done" : "";
    changed();
}

juce::String StyleFromAudioService::makeNow (const Request& r, const analysis::Result& result)
{
    if (isRunning())
        return "A style is being made already.";
    request = r;
    current = {};
    current.clip = r.clip;
    style_from_audio::Options o;
    o.bpm = r.bpm;
    o.mainBars = r.mainBars;
    o.vocals = r.vocals;
    o.name = r.name;
    try
    {
        finished (std::make_shared<const style_from_audio::Made> (style_from_audio::make (result, o)), {});
    }
    catch (const std::exception& e)
    {
        finished (nullptr, juce::String::fromUTF8 (e.what()));
    }
    return current.error;
}

void StyleFromAudioService::cancel()
{
    if (download != nullptr)
        download->signalThreadShouldExit();
    else if (job != nullptr)
        job->signalThreadShouldExit(); // the loop stops and keeps the best so far
    else if (waitingForAnalysis)
        session.analysis().cancel();
}

juce::File StyleFromAudioService::styleFolder() const
{
    auto folder = SessionTools::styleFolder();
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("Styles");
    return folder;
}

juce::File StyleFromAudioService::defaultFile() const
{
    const auto name = current.made != nullptr ? juce::String::fromUTF8 (current.made->style.name.c_str()) : juce::String ("Style from Audio");
    return styleFolder().getChildFile (juce::File::createLegalFileName (name) + ".sty");
}

juce::File StyleFromAudioService::confinedStyleFile (const juce::String& rawPath) const
{
    if (rawPath.isEmpty())
        return {};
    std::string normalized = rawPath.toStdString();
    std::replace (normalized.begin(), normalized.end(), '\\', '/');
    for (const auto& part : std::filesystem::path (normalized))
        if (part == "..")
            throw std::runtime_error ("The style path must stay inside the Styles folder.");

    const auto folder = styleFolder();
    const auto file = juce::File::isAbsolutePath (rawPath) ? juce::File (rawPath) : folder.getChildFile (rawPath);
    if (! file.isAChildOf (folder) || file == folder)
        throw std::runtime_error ("The style path must stay inside the Styles folder.");

    std::error_code ec1, ec2;
    const auto canonicalRoot = std::filesystem::weakly_canonical (std::filesystem::path (folder.getFullPathName().toStdString()), ec1);
    const auto canonicalTarget = std::filesystem::weakly_canonical (std::filesystem::path (file.getFullPathName().toStdString()), ec2);
    if (ec1 || ec2)
        throw std::runtime_error ("The style path must stay inside the Styles folder.");
    const auto rel = canonicalTarget.lexically_relative (canonicalRoot);
    if (rel.empty() || rel == "." || *rel.begin() == "..")
        throw std::runtime_error ("The style path must stay inside the Styles folder.");
    return file;
}

juce::String StyleFromAudioService::save (juce::File& file)
{
    if (current.made == nullptr)
        return "Make a style first.";
    if (file == juce::File())
        file = defaultFile().getNonexistentSibling (false);
    if (! file.hasFileExtension ("sty;prs;sst"))
        file = file.withFileExtension ("sty");
    try
    {
        file.getParentDirectory().createDirectory();
        style_file::save (current.made->style, std::filesystem::path (file.getFullPathName().toStdString()));
    }
    catch (const std::exception& e)
    {
        return "The style can't be saved: " + juce::String::fromUTF8 (e.what());
    }
    current.saved = file;
    changed();
    return {};
}

juce::String StyleFromAudioService::play (style_from_audio::Listen listen, int fromBar, int toBar)
{
    if (current.made == nullptr)
        return "Make a style first.";
    auto song = style_from_audio::preview (*current.made, listen);
    const Tick bar = song.timeSignature.ticksPerBar();
    const int bars = current.made->plan.bars;
    fromBar = std::clamp (fromBar, 0, std::max (0, bars - 1));
    toBar = toBar <= fromBar ? bars : std::min (toBar, bars);
    session.audition (std::move (song), bar * fromBar, bar * toBar);
    return {};
}

void StyleFromAudioService::stopPlaying()
{
    session.audition (std::nullopt);
}

} // namespace arranger
