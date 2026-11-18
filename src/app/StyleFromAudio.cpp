#include "StyleFromAudio.h"

#include "AudioAnalysis.h"
#include "McpService.h"
#include "Session.h"

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

StyleFromAudioService::StyleFromAudioService (Session& s) : session (s) {}

StyleFromAudioService::~StyleFromAudioService()
{
    stopTimer();
    job.reset();
}

bool StyleFromAudioService::isRunning() const noexcept { return job != nullptr || waitingForAnalysis; }

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
    if (job != nullptr)
        job->signalThreadShouldExit(); // the loop stops and keeps the best so far
    else if (waitingForAnalysis)
        session.analysis().cancel();
}

juce::File StyleFromAudioService::defaultFile() const
{
    auto folder = SessionTools::styleFolder();
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("Styles");
    const auto name = current.made != nullptr ? juce::String::fromUTF8 (current.made->style.name.c_str()) : juce::String ("Style from Audio");
    return folder.getChildFile (juce::File::createLegalFileName (name) + ".sty");
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
