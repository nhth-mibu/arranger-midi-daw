#include "AudioAnalysis.h"

#include "PythonTools.h"
#include "Session.h"

#include "BinaryData.h"
#include "arranger/core/SongEdit.h"

#if ARRANGER_HAS_WHISPER
 #include "arranger/whisper/Lyrics.h"
#endif

#include <juce_events/juce_events.h>

#include <set>

namespace arranger
{

namespace
{
    std::string utf8 (const juce::String& s) { return s.toStdString(); }

    /** The helper script, written from the app's resources so that it is always the one of this version. */
    juce::File helperScript() { return python_tools::script ("arranger_analyse.py", BinaryData::arranger_analyse_py, static_cast<std::size_t> (BinaryData::arranger_analyse_pySize)); }

    juce::File modelsFolder() { return AnalysisService::folder().getChildFile ("whisper"); }

    /** The packages of the analysis tools. setuptools: pretty_midi still needs pkg_resources. */
    const juce::String kPackages = "demucs 'basic-pitch[onnx]' panns-inference soundfile 'setuptools<81'";

    struct Model
    {
        const char* name;
        const char* description;
        juce::int64 bytes;
    };
    constexpr Model kModels[] = {
        { "large-v3-turbo-q5_0", "Large turbo: best for songs, 550 MB", 574041195 },
        { "small", "Small: good, 470 MB", 487601967 },
        { "base", "Base: fast, less exact, 140 MB", 147951465 },
        { "medium-q5_0", "Medium: very good, slower, 540 MB", 539212467 },
    };

    juce::File modelFile (const juce::String& name) { return modelsFolder().getChildFile ("ggml-" + name + ".bin"); }

} // namespace

juce::File AnalysisService::folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Arranger MIDI DAW")
        .getChildFile ("Analysis");
}

// ---- the analysis job -----------------------------------------------------------------

struct AnalysisService::Job final : juce::Thread
{
    Job (AnalysisService& o, Request r, AudioClip c, std::shared_ptr<const Sample> s, juce::File stemsFolder)
        : juce::Thread ("Audio analysis"), owner (o), request (std::move (r)), clip (std::move (c)), sample (std::move (s)), stems (std::move (stemsFolder))
    {
    }
    ~Job() override { stopThread (10000); }

    void report (double progress, const juce::String& step)
    {
        if (onProgress)
            onProgress (progress, step);
        {
            const juce::ScopedLock l (owner.lock);
            owner.current.progress = progress;
            owner.current.step = step;
        }
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Job> (this)] {
            if (alive != nullptr)
                owner.changed();
        });
    }

    void run() override
    {
        analysis::Result result;
        juce::String error;
        try
        {
            result = analyse();
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
        if (threadShouldExit())
            error = "Stopped.";
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Job> (this), result, error] {
            if (alive != nullptr)
                owner.finished (result, request, error);
        });
    }

    analysis::Result analyse()
    {
        analysis::Result result;
        result.source = clip.file;
        const bool helper = request.stems || request.notes || request.instruments;

        // The parts that need the Python tools: about 70% of the time.
        double toolsShare = 0.0;
        if (helper)
        {
            const auto setup = owner.setup();
            if (! setup.python)
                result.warnings.push_back ("The analysis tools are not installed (Options > Audio Analysis Tools): no stems, notes or instrument names.");
            else
            {
                toolsShare = request.lyrics ? 0.7 : 0.9;
                stems.createDirectory();
                const auto input = stems.getChildFile ("source.wav");
                report (0.0, "Preparing the audio");
                wav::write (*sample, utf8 (input.getFullPathName()), 32);

                juce::StringArray args { python_tools::program().getFullPathName(), "-I", helperScript().getFullPathName(), "run",
                                         "--input", input.getFullPathName(), "--output", stems.getFullPathName() };
                if (request.stems) args.add ("--stems");
                if (request.notes) args.add ("--notes");
                if (request.instruments) args.add ("--instruments");

                juce::String resultFile, failure, lastLine;
                const int code = python_tools::run (args, *this, [&] (const juce::String& line) {
                    if (line.startsWith ("PROGRESS "))
                    {
                        const auto rest = line.fromFirstOccurrenceOf ("PROGRESS ", false, false);
                        report (rest.upToFirstOccurrenceOf (" ", false, false).getDoubleValue() * toolsShare, rest.fromFirstOccurrenceOf (" ", false, false));
                    }
                    else if (line.startsWith ("RESULT "))
                        resultFile = line.fromFirstOccurrenceOf ("RESULT ", false, false).trim();
                    else if (line.startsWith ("ERROR "))
                        failure = line.fromFirstOccurrenceOf ("ERROR ", false, false);
                    else if (line.trim().isNotEmpty())
                        lastLine = line.trim();
                });
                input.deleteFile();
                if (threadShouldExit())
                    return result;
                if (code != 0 || resultFile.isEmpty())
                    throw std::runtime_error (utf8 ("The analysis tools failed: " + (failure.isNotEmpty() ? failure : (lastLine.isNotEmpty() ? lastLine : "exit code " + juce::String (code)))));
                result = analysis::parse (utf8 (juce::File (resultFile).loadFileAsString()));
                result.source = clip.file;
            }
        }

        // Drum hits: the app finds them itself in the drum stem.
        std::map<std::string, std::shared_ptr<const Sample>> stemAudio;
        const auto audioOf = [&] (const analysis::Stem& s) -> std::shared_ptr<const Sample> {
            if (s.file.empty())
                return sample;
            if (auto it = stemAudio.find (s.name); it != stemAudio.end())
                return it->second;
            auto loaded = std::make_shared<const Sample> (wav::read (s.file));
            stemAudio[s.name] = loaded;
            return loaded;
        };
        if (request.notes)
            for (auto& s : result.stems)
                if (s.name == "drums")
                {
                    report (toolsShare, "Finding the drum hits");
                    s.notes = audio_to_midi::drums (*audioOf (s));
                    s.hasNotes = true;
                }

        // Chords: in the stems without drums and voice, or in all of the audio.
        if (request.chords)
        {
            report (toolsShare + 0.02, "Finding the chords");
            std::vector<std::shared_ptr<const Sample>> keep;
            for (const auto& s : result.stems)
                if (! s.file.empty() && s.name != "drums" && s.name != "vocals")
                    keep.push_back (audioOf (s));
            std::vector<const Sample*> parts;
            for (const auto& k : keep)
                parts.push_back (k.get());
            const auto mix = parts.empty() ? Sample() : analysis::harmonicMix (parts);
            result.chords = analysis::findChords (mix.frames() > 0 ? mix : *sample);
        }
        if (threadShouldExit())
            return result;

        // Lyrics: Whisper on the vocal stem, or on all of the audio.
        if (request.lyrics)
        {
#if ARRANGER_HAS_WHISPER
            const auto model = modelFile (chosenModel());
            if (! model.existsAsFile())
                result.warnings.push_back ("No Whisper model yet (Options > Audio Analysis Tools): no lyrics.");
            else
            {
                const double from = toolsShare + 0.05;
                report (from, "Writing down the words");
                std::shared_ptr<const Sample> voice = sample;
                if (const auto* v = result.stem ("vocals"); v != nullptr && ! v->file.empty())
                    voice = audioOf (*v);
                lyrics::Settings settings;
                settings.model = utf8 (model.getFullPathName());
                settings.language = utf8 (request.language.isEmpty() ? juce::String ("auto") : request.language);
                settings.prompt = utf8 (request.prompt);
                const auto transcript = lyrics::transcribe (*voice, settings, [&] (float p) {
                    report (from + (1.0 - from) * p, "Writing down the words");
                    return ! threadShouldExit();
                });
                result.words = transcript.words;
                result.language = transcript.language;
                if (result.words.empty())
                    result.warnings.push_back ("Whisper heard no words.");
            }
#else
            result.warnings.push_back ("This build of the app has no Whisper: no lyrics.");
#endif
        }
        report (1.0, "Putting it in the song");
        return result;
    }

    AnalysisService& owner;
    Request request;
    AudioClip clip;
    std::shared_ptr<const Sample> sample;
    juce::File stems;
    std::function<void (double, const juce::String&)> onProgress; ///< called on the job's own thread

    JUCE_DECLARE_WEAK_REFERENCEABLE (Job)
};

AnalysisService::AnalysisService (Session& s) : session (s) {}

AnalysisService::~AnalysisService()
{
    job.reset();
    installer.reset();
    download.reset();
}

bool AnalysisService::isRunning() const noexcept { return job != nullptr; }

AnalysisService::Status AnalysisService::status() const
{
    const juce::ScopedLock l (lock);
    return current;
}

void AnalysisService::changed()
{
    if (onChange)
        onChange();
    session.sendChangeMessage();
}

std::unique_ptr<AnalysisService::Job> AnalysisService::prepare (const Request& r, juce::String& why)
{
    if (job != nullptr)
        why = "An analysis is running already.";
    else if (installer != nullptr)
        why = "The analysis tools are being installed. Try again when that is done.";
    const auto clip = edit::findAudioClip (session.song(), r.clip);
    if (why.isEmpty() && clip == nullptr)
        why = "There is no audio clip with that id.";
    if (why.isEmpty() && ! (r.stems || r.notes || r.chords || r.instruments || r.lyrics))
        why = "Choose at least one thing to find.";
    const auto sample = why.isEmpty() ? session.sampleFor (clip->file) : nullptr;
    if (why.isEmpty() && sample == nullptr)
        why = "The audio file of the clip can't be read: " + juce::String::fromUTF8 (clip->file.c_str());
    if (why.isNotEmpty())
        return nullptr;

    // The stems go next to the project's own audio, or into the Music folder.
    const auto project = session.file();
    const auto base = project != juce::File() ? project.getSiblingFile (project.getFileNameWithoutExtension() + " Audio")
                                              : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("Stems");
    const auto name = juce::File::createLegalFileName (juce::File (juce::String::fromUTF8 (clip->file.c_str())).getFileNameWithoutExtension() + " stems");
    const auto stems = base.getNonexistentChildFile (name, {}, true);

    {
        const juce::ScopedLock l (lock);
        current = {};
        current.state = Status::State::running;
        current.clip = r.clip;
        current.step = "Starting";
    }
    return std::make_unique<Job> (*this, r, *clip, sample, stems);
}

juce::String AnalysisService::start (const Request& r)
{
    juce::String why;
    auto j = prepare (r, why);
    if (j == nullptr)
        return why;
    job = std::move (j);
    job->startThread (juce::Thread::Priority::low);
    changed();
    return {};
}

juce::String AnalysisService::runNow (const Request& r, std::function<void (double, const juce::String&)> progress)
{
    juce::String why;
    auto j = prepare (r, why);
    if (j == nullptr)
        return why;
    j->onProgress = std::move (progress);
    analysis::Result result;
    juce::String error;
    try
    {
        result = j->analyse();
    }
    catch (const std::exception& e)
    {
        error = e.what();
    }
    j.reset();
    finished (result, r, error);
    return status().error;
}

std::optional<analysis::Result> AnalysisService::resultFor (ClipId id) const
{
    if (const auto it = results.find (id); it != results.end())
        return it->second;
    const auto clip = edit::findAudioClip (session.song(), id);
    if (clip == nullptr)
        return std::nullopt;
    // A saved analysis next to the stems of an audio track in the song.
    std::set<juce::String> folders;
    for (const auto& t : session.song().tracks)
        for (const auto& c : t.audio)
            folders.insert (juce::File (juce::String::fromUTF8 (c->file.c_str())).getParentDirectory().getFullPathName());
    for (const auto& folder : folders)
    {
        const auto file = juce::File (folder).getChildFile ("analysis.json");
        if (! file.existsAsFile())
            continue;
        try
        {
            auto r = analysis::parse (file.loadFileAsString().toStdString());
            if (r.source == clip->file)
                return r;
        }
        catch (const std::exception&)
        {
        }
    }
    return std::nullopt;
}

void AnalysisService::cancel()
{
    if (job != nullptr)
        job->signalThreadShouldExit();
}

void AnalysisService::finished (const analysis::Result& result, const Request request, const juce::String& error)
{
    job.reset(); // `request` is a copy: the job owned the first one
    Status s;
    s.clip = request.clip;
    s.warnings = result.warnings;
    s.language = result.language;
    s.lyricLines = analysis::lines (result.words);
    for (const auto& c : result.chords)
        if (s.chords.empty() || s.chords.back() != style_chord::name (c.chord))
            s.chords.push_back (style_chord::name (c.chord));
    for (const auto& stem : result.stems)
        if (! stem.tags.empty())
            s.instruments.push_back ({ stem.name, analysis::instrumentFor (stem).name });

    if (error.isNotEmpty())
    {
        s.state = Status::State::failed;
        s.error = error;
    }
    else
    {
        try
        {
            analysis::ApplyOptions options;
            options.stemTracks = request.stems;
            options.noteTracks = request.notes;
            options.chords = request.chords;
            options.lyrics = request.lyrics;
            analysis::Applied applied;
            session.edit ("Analyse Audio", [&] (Song& song) { applied = analysis::apply (song, request.clip, result, options); });
            if (request.notes)
            {
                results[request.clip] = result;
                // Kept next to the stems, so that Style from Audio finds it again later.
                for (const auto& stem : result.stems)
                    if (! stem.file.empty())
                    {
                        juce::File (juce::String::fromUTF8 (stem.file.c_str())).getSiblingFile ("analysis.json").replaceWithText (juce::String::fromUTF8 (analysis::toJson (result).c_str()));
                        break;
                    }
            }
            s.applied = applied;
            s.state = Status::State::done;
            s.progress = 1.0;
            s.step = "Done";
        }
        catch (const std::exception& e)
        {
            s.state = Status::State::failed;
            s.error = e.what();
        }
    }
    {
        const juce::ScopedLock l (lock);
        current = s;
    }
    changed();
}

// ---- setup ------------------------------------------------------------------------------

AnalysisService::Setup AnalysisService::setup (bool checkAgain)
{
    Setup s;
    bool have = false;
    {
        const juce::ScopedLock l (lock);
        if (known && ! checkAgain)
        {
            s = *known;
            have = true;
        }
    }
#if ARRANGER_HAS_WHISPER
    s.whisper = true;
#endif
    s.model = modelFile (chosenModel());
    if (have)
        return s;

    s.python = python_tools::program().existsAsFile();
    if (s.python)
    {
        juce::ChildProcess p;
        if (p.start (juce::StringArray { python_tools::program().getFullPathName(), "-I", helperScript().getFullPathName(), "check" }, juce::ChildProcess::wantStdOut))
        {
            const auto out = p.readAllProcessOutput();
            const auto line = out.fromLastOccurrenceOf ("{", true, false).upToLastOccurrenceOf ("}", true, false);
            const auto json = juce::JSON::parse (line);
            s.demucs = json["demucs"];
            s.basicPitch = json["basic_pitch"];
            s.panns = json["panns"];
            s.device = json["device"].toString();
        }
        s.python = s.demucs || s.basicPitch || s.panns;
    }
    const juce::ScopedLock l (lock);
    known = s;
    return s;
}

struct AnalysisService::Installer final : juce::Thread
{
    Installer (AnalysisService& o, std::function<void (const juce::String&)> out, std::function<void (bool)> d)
        : juce::Thread ("Analysis tools"), owner (o), output (std::move (out)), done (std::move (d))
    {
    }
    ~Installer() override { stopThread (10000); }

    void say (const juce::String& line)
    {
        juce::MessageManager::callAsync ([o = output, line] {
            if (o)
                o (line);
        });
    }

    void run() override
    {
        const auto script = helperScript();
        const auto text = python_tools::installScript (kPackages)
#if JUCE_WINDOWS
                          + "; Write-Output 'Downloading the models'; & " + python_tools::quoted (python_tools::program()) + " -I " + python_tools::quoted (script) + " prefetch";
#else
                          + "; echo 'Downloading the models'; " + python_tools::quoted (python_tools::program()) + " -I " + python_tools::quoted (script) + " prefetch";
#endif
        const int code = python_tools::run (python_tools::shell (text), *this, [&] (const juce::String& line) { say (line); });
        const bool ok = code == 0;
        say (ok ? "The analysis tools are ready." : (threadShouldExit() ? "Stopped." : "The install failed (exit code " + juce::String (code) + ")."));
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Installer> (this), ok] {
            if (alive == nullptr)
                return;
            auto d = done;
            auto& service = owner;
            service.installer.reset(); // deletes this
            service.setup (true);
            if (d)
                d (ok);
            service.changed();
        });
    }

    AnalysisService& owner;
    std::function<void (const juce::String&)> output;
    std::function<void (bool)> done;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Installer)
};

juce::String AnalysisService::installTools (std::function<void (const juce::String&)> output, std::function<void (bool)> done)
{
    if (installer != nullptr)
        return "The tools are being installed already.";
    if (job != nullptr)
        return "Wait for the analysis to end first.";
    installer = std::make_unique<Installer> (*this, std::move (output), std::move (done));
    installer->startThread();
    return {};
}

bool AnalysisService::isInstalling() const noexcept { return installer != nullptr; }

// ---- Whisper models ---------------------------------------------------------------------

juce::StringArray AnalysisService::modelNames()
{
    juce::StringArray names;
    for (const auto& m : kModels)
        names.add (m.name);
    return names;
}

juce::String AnalysisService::modelDescription (const juce::String& name)
{
    for (const auto& m : kModels)
        if (name == m.name)
            return m.description;
    return {};
}

juce::String AnalysisService::chosenModel()
{
    const auto saved = modelsFolder().getChildFile ("model.txt").loadFileAsString().trim();
    return modelNames().contains (saved) ? saved : juce::String (kModels[0].name);
}

void AnalysisService::chooseModel (const juce::String& name)
{
    modelsFolder().createDirectory();
    modelsFolder().getChildFile ("model.txt").replaceWithText (name);
}

struct AnalysisService::Download final : juce::Thread
{
    Download (AnalysisService& o, juce::String n, std::function<void (double)> p, std::function<void (const juce::String&)> d)
        : juce::Thread ("Whisper model"), owner (o), name (std::move (n)), progress (std::move (p)), done (std::move (d))
    {
    }
    ~Download() override { stopThread (10000); }

    void run() override
    {
        const auto target = modelFile (name);
        target.getParentDirectory().createDirectory();
        const auto part = target.getSiblingFile (target.getFileName() + ".part");
        part.deleteFile();
        juce::int64 expected = 0;
        for (const auto& m : kModels)
            if (name == m.name)
                expected = m.bytes;

        const auto url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" + name + ".bin";
#if JUCE_WINDOWS
        const juce::String curl = "curl.exe";
#else
        const juce::String curl = juce::File ("/usr/bin/curl").existsAsFile() ? "/usr/bin/curl" : "curl";
#endif
        juce::String error;
        juce::ChildProcess p;
        if (! p.start (juce::StringArray { curl, "--fail", "--location", "--silent", "--show-error", "--retry", "2", "--output", part.getFullPathName(), url }, juce::ChildProcess::wantStdErr))
            error = "The curl program was not found.";
        else
        {
            while (p.isRunning())
            {
                if (threadShouldExit())
                {
                    p.kill();
                    part.deleteFile();
                    return;
                }
                if (expected > 0)
                    juce::MessageManager::callAsync ([pr = progress, v = static_cast<double> (part.getSize()) / static_cast<double> (expected)] {
                        if (pr)
                            pr (std::min (1.0, v));
                    });
                wait (250);
            }
            const auto output = p.readAllProcessOutput().trim();
            if (p.getExitCode() != 0)
                error = "The download failed. " + output;
            else if (part.getSize() < 10 * 1024 * 1024)
                error = "The downloaded file is not a Whisper model.";
            else if (! part.moveFileTo (target))
                error = "Could not save " + target.getFullPathName();
        }
        if (error.isNotEmpty())
            part.deleteFile();
        juce::MessageManager::callAsync ([this, alive = juce::WeakReference<Download> (this), error] {
            if (alive == nullptr)
                return;
            auto d = done;
            if (error.isEmpty())
                chooseModel (name);
            auto& service = owner;
            service.download.reset(); // deletes this
            if (d)
                d (error);
            service.changed();
        });
    }

    AnalysisService& owner;
    juce::String name;
    std::function<void (double)> progress;
    std::function<void (const juce::String&)> done;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Download)
};

juce::String AnalysisService::downloadModel (const juce::String& name, std::function<void (double)> progress, std::function<void (const juce::String&)> done)
{
    if (! modelNames().contains (name))
        return "There is no Whisper model with that name.";
    if (download != nullptr)
        return "A model is being downloaded already.";
    download = std::make_unique<Download> (*this, name, std::move (progress), std::move (done));
    download->startThread();
    return {};
}

} // namespace arranger
