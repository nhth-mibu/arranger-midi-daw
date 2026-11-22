#include "Speech.h"

#include "AudioAnalysis.h"

#if ARRANGER_HAS_WHISPER
    #include "arranger/whisper/Lyrics.h"
#endif

#include <algorithm>
#include <cstdlib>

namespace arranger
{

namespace
{
    constexpr const char* kEnabled = "speechEnabled";
    constexpr const char* kSpeak = "speechSpeakReplies";
    constexpr const char* kVoiceInput = "speechVoiceInput";
    constexpr const char* kEngine = "speechEngine";
    constexpr const char* kVoice = "speechVoice";

    juce::String runtimeUrl()
    {
#if JUCE_MAC
        return "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-osx-universal2-shared.tar.bz2";
#elif JUCE_WINDOWS
        return "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-win-x64-shared-MD-Release.tar.bz2";
#else
        return "https://github.com/k2-fsa/sherpa-onnx/releases/download/v2.0.1/sherpa-onnx-v2.0.1-linux-x64-shared.tar.bz2";
#endif
    }

    constexpr const char* kKittenModelUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kitten-nano-en-v0_1-fp16.tar.bz2";
    constexpr const char* kKittenLexiconUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/lexicon-en-us.txt";
    constexpr const char* kKokoroModelUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-int8-multi-lang-v1_1.tar.bz2";
    constexpr const char* kKokoroLexiconUrl = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/lexicon-us-en.txt";

}

SpeechService::SpeechService (Session& s, platform::JuceDeviceHost& d, juce::PropertiesFile* p)
    : session (s), devices (d), settings (p)
{
    startTimerHz (8);
}

SpeechService::~SpeechService()
{
    onTranscript = nullptr;
    onError = nullptr;
    onStateChanged = nullptr;
    *alive = false;
    cancelInstall();
    stopSpeaking();
    if (listening)
    {
        double rate = 48000.0;
        devices.stopSpeechCapture (rate);
    }
    if (worker.joinable())
        worker.join();
    if (installer.joinable())
        installer.join();
}

void SpeechService::cancelInstall()
{
    cancelInstalling.store (true);
    const juce::ScopedLock lock (installProcessLock);
    if (installProcess != nullptr)
        installProcess->kill();
}

juce::File SpeechService::folder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Arranger MIDI DAW")
        .getChildFile ("Speech");
}

juce::File SpeechService::engineFolder (Engine e) const
{
    return folder().getChildFile ("Engines").getChildFile (e == Engine::kitten ? "Kitten" : "Kokoro");
}

juce::File SpeechService::runtimeFolder() const
{
    return folder().getChildFile ("Runtime");
}

juce::File SpeechService::runtimeBinary() const
{
    return runtimeFolder().getChildFile ("bin").getChildFile (
#if JUCE_WINDOWS
        "sherpa-onnx-offline-tts.exe"
#else
        "sherpa-onnx-offline-tts"
#endif
    );
}

SpeechService::Engine SpeechService::engine() const
{
    const auto value = settings != nullptr ? settings->getValue (kEngine, "system") : "system";
    if (value == "kitten")
        return Engine::kitten;
    if (value == "kokoro")
        return Engine::kokoro;
    return Engine::system;
}

void SpeechService::setEngine (Engine e)
{
    if (settings != nullptr)
        settings->setValue (kEngine, e == Engine::kitten ? "kitten" : e == Engine::kokoro ? "kokoro" : "system");
    stopSpeaking();
    if (onStateChanged)
        onStateChanged();
}

juce::String SpeechService::engineName (Engine e)
{
    return e == Engine::kitten ? "Kitten (small, offline)" : e == Engine::kokoro ? "Kokoro (high quality, offline)" : "System voice";
}

juce::StringArray SpeechService::voiceNames (Engine e)
{
    if (e == Engine::kitten)
    {
        juce::StringArray names;
        for (int i = 1; i <= 8; ++i)
            names.add ("Voice " + juce::String (i));
        return names;
    }
    if (e == Engine::kokoro)
    {
        juce::StringArray names;
        for (int i = 1; i <= 54; ++i)
            names.add ("Speaker " + juce::String (i));
        return names;
    }
    return { "System default" };
}

int SpeechService::voice() const
{
    return settings != nullptr ? juce::jlimit (0, 53, settings->getIntValue (kVoice, 0)) : 0;
}

void SpeechService::setVoice (int v)
{
    if (settings != nullptr)
        settings->setValue (kVoice, juce::jlimit (0, 53, v));
}

bool SpeechService::isEngineInstalled (Engine e) const
{
    if (e == Engine::system)
        return true;
    if (! runtimeBinary().existsAsFile())
        return false;
    const auto dir = engineFolder (e);
    if (e == Engine::kitten)
        return dir.getChildFile ("model.fp16.onnx").existsAsFile()
            && dir.getChildFile ("voices.bin").existsAsFile()
            && dir.getChildFile ("tokens.txt").existsAsFile()
            && dir.getChildFile ("lexicon.txt").existsAsFile();
    return dir.getChildFile ("model.int8.onnx").existsAsFile()
        && dir.getChildFile ("voices.bin").existsAsFile()
        && dir.getChildFile ("tokens.txt").existsAsFile()
        && dir.getChildFile ("lexicon.txt").existsAsFile();
}

bool SpeechService::download (const juce::String& url, const juce::File& target)
{
    if (cancelInstalling.load())
        return false;
    target.getParentDirectory().createDirectory();
    target.deleteFile();
    juce::ChildProcess p;
    if (! p.start (juce::StringArray { "curl", "-L", "--fail", "--silent", "--show-error",
                                       "--connect-timeout", "15", "--max-time", "300",
                                       "-o", target.getFullPathName(), "--", url }, 0))
        return false;
    {
        const juce::ScopedLock lock (installProcessLock);
        installProcess = &p;
        if (cancelInstalling.load())
            p.kill();
    }
    while (p.isRunning())
    {
        if (cancelInstalling.load())
        {
            p.kill();
            break;
        }
        juce::Thread::sleep (20);
    }
    p.waitForProcessToFinish (1000);
    {
        const juce::ScopedLock lock (installProcessLock);
        installProcess = nullptr;
    }
    if (cancelInstalling.load() || p.getExitCode() != 0)
    {
        target.deleteFile();
        return false;
    }
    return target.existsAsFile();
}

bool SpeechService::extract (const juce::File& archive, const juce::File& target)
{
    if (cancelInstalling.load())
        return false;
    target.createDirectory();
    juce::ChildProcess p;
#if JUCE_WINDOWS
    const juce::String tar = "tar.exe";
#else
    const juce::String tar = "tar";
#endif
    if (! p.start (juce::StringArray { tar, "-xjf", archive.getFullPathName(), "-C", target.getFullPathName(), "--strip-components=1" }, 0))
        return false;
    {
        const juce::ScopedLock lock (installProcessLock);
        installProcess = &p;
        if (cancelInstalling.load())
            p.kill();
    }
    while (p.isRunning())
    {
        if (cancelInstalling.load())
        {
            p.kill();
            break;
        }
        juce::Thread::sleep (20);
    }
    p.waitForProcessToFinish (1000);
    {
        const juce::ScopedLock lock (installProcessLock);
        installProcess = nullptr;
    }
    return ! cancelInstalling.load() && p.getExitCode() == 0;
}

bool SpeechService::installRuntime (juce::String& error)
{
    if (runtimeBinary().existsAsFile())
        return true;
    const auto archive = folder().getChildFile ("runtime.tar.bz2");
    if (! download (runtimeUrl(), archive))
    {
        error = "The speech runtime could not be downloaded. Check the internet connection.";
        return false;
    }
    if (! extract (archive, runtimeFolder()))
    {
        archive.deleteFile();
        error = "The speech runtime could not be unpacked.";
        return false;
    }
    archive.deleteFile();
    if (! runtimeBinary().existsAsFile())
    {
        error = "The speech runtime is missing its program after unpacking.";
        return false;
    }
    return true;
}

void SpeechService::installEngine (Engine e, std::function<void (bool, const juce::String&)> done)
{
    if (e == Engine::system)
    {
        if (done) done (true, "The system voice is ready.");
        return;
    }
    if (installing.exchange (true))
    {
        if (done) done (false, "A voice is already being installed.");
        return;
    }
    cancelInstalling.store (false);
    if (installer.joinable())
        installer.join();
    installer = std::thread ([this, e, done] {
        juce::String error;
        bool ok = installRuntime (error);
        if (ok)
        {
            const auto dir = engineFolder (e);
            dir.createDirectory();
            const auto archive = folder().getChildFile (e == Engine::kitten ? "kitten.tar.bz2" : "kokoro.tar.bz2");
            const auto modelUrl = e == Engine::kitten ? juce::String (kKittenModelUrl) : juce::String (kKokoroModelUrl);
            if (download (modelUrl, archive) && extract (archive, dir))
            {
                const auto lexiconSource = dir.getChildFile (e == Engine::kitten ? "lexicon-en-us.txt" : "lexicon-us-en.txt");
                if (! lexiconSource.existsAsFile())
                {
                    const auto lexiconUrl = e == Engine::kitten ? juce::String (kKittenLexiconUrl) : juce::String (kKokoroLexiconUrl);
                    download (lexiconUrl, dir.getChildFile ("lexicon.txt"));
                }
                else
                {
                    lexiconSource.moveFileTo (dir.getChildFile ("lexicon.txt"));
                }
                archive.deleteFile();
                ok = isEngineInstalled (e);
                if (! ok) error = "The voice files are incomplete after unpacking.";
            }
            else
            {
                archive.deleteFile();
                if (error.isEmpty())
                    error = "The voice model could not be downloaded.";
                ok = false;
            }
        }
        juce::MessageManager::callAsync ([alive = std::weak_ptr<bool> (alive), this, ok, error, done] {
            if (alive.expired())
                return;
            installing = false;
            if (onStateChanged)
                onStateChanged();
            if (done)
                done (ok, ok ? "The voice is ready." : error);
        });
    });
}

bool SpeechService::isEnabled() const
{
    return settings != nullptr && settings->getBoolValue (kEnabled, false);
}

void SpeechService::setEnabled (bool on)
{
    if (settings != nullptr)
        settings->setValue (kEnabled, on);
    if (! on)
    {
        stopSpeaking();
        if (listening)
        {
            listening = false;
            double rate = 48000.0;
            devices.stopSpeechCapture (rate);
        }
    }
    if (onStateChanged)
        onStateChanged();
}

bool SpeechService::speaksReplies() const
{
    return settings == nullptr || settings->getBoolValue (kSpeak, true);
}

void SpeechService::setSpeaksReplies (bool on)
{
    if (settings != nullptr)
        settings->setValue (kSpeak, on);
    if (! on)
        stopSpeaking();
    if (onStateChanged)
        onStateChanged();
}

bool SpeechService::takesVoiceInput() const
{
    return settings != nullptr && settings->getBoolValue (kVoiceInput, false);
}

void SpeechService::setTakesVoiceInput (bool on)
{
    if (settings != nullptr)
        settings->setValue (kVoiceInput, on);
    if (! on && listening)
    {
        listening = false;
        double rate = 48000.0;
        devices.stopSpeechCapture (rate);
    }
    if (onStateChanged)
        onStateChanged();
}

bool SpeechService::isSpeaking() const
{
    return speaking;
}

juce::StringArray SpeechService::spdSayArgs (const juce::String& text)
{
    return { "spd-say", "-w", "--", text };
}

juce::StringArray SpeechService::offlineTtsArgs (const juce::File& binary, Engine chosen, const juce::File& model,
                                                 const juce::File& outputWav, int voiceIndex, const juce::String& text)
{
    juce::StringArray args { binary.getFullPathName() };
    if (chosen == Engine::kitten)
    {
        args.add ("--kitten-model=" + model.getChildFile ("model.fp16.onnx").getFullPathName());
        args.add ("--kitten-voices=" + model.getChildFile ("voices.bin").getFullPathName());
        args.add ("--kitten-tokens=" + model.getChildFile ("tokens.txt").getFullPathName());
        args.add ("--kitten-lexicon=" + model.getChildFile ("lexicon.txt").getFullPathName());
    }
    else
    {
        args.add ("--kokoro-model=" + model.getChildFile ("model.int8.onnx").getFullPathName());
        args.add ("--kokoro-voices=" + model.getChildFile ("voices.bin").getFullPathName());
        args.add ("--kokoro-tokens=" + model.getChildFile ("tokens.txt").getFullPathName());
        args.add ("--kokoro-lexicon=" + model.getChildFile ("lexicon.txt").getFullPathName());
    }
    args.add ("--output-filename=" + outputWav.getFullPathName());
    const int sid = chosen == Engine::kitten ? juce::jlimit (0, 7, voiceIndex) : juce::jlimit (0, 53, voiceIndex);
    args.add ("--sid=" + juce::String (sid));
    args.add ("--");
    args.add (text);
    return args;
}

void SpeechService::speak (const juce::String& text)
{
    if (! isEnabled() || ! speaksReplies() || text.trim().isEmpty())
        return;

    stopSpeaking();
    const auto chosen = engine();
    if (chosen != Engine::system && ! isEngineInstalled (chosen))
    {
        fail (engineName (chosen) + " is not installed yet. Open Options > Speech... and install it.");
        return;
    }

    const auto dir = SpeechService::folder();
    dir.createDirectory();
    speechTextFile = dir.getChildFile ("speech.txt");
    speechTextFile.replaceWithText (text);
    speechWav = dir.getChildFile ("speech.wav");
    speechWav.deleteFile();

    auto command = std::make_unique<juce::ChildProcess>();
    bool started = false;
    if (chosen == Engine::system)
    {
#if JUCE_MAC
        started = command->start (juce::StringArray { "/usr/bin/say", "-f", speechTextFile.getFullPathName() }, 0);
#elif JUCE_WINDOWS
        const auto script = dir.getChildFile ("speak.ps1");
        script.replaceWithText (
            "Add-Type -AssemblyName System.Speech\n"
            "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer\n"
            "$s.Speak([IO.File]::ReadAllText($args[0]))\n");
        started = command->start (juce::StringArray { "powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                                       "-File", script.getFullPathName(), speechTextFile.getFullPathName() }, 0);
#else
        // speech-dispatcher is the desktop standard; espeak-ng is the small fallback.
        if (! (started = command->start (spdSayArgs (text), 0)))
            started = command->start (juce::StringArray { "espeak-ng", "-f", speechTextFile.getFullPathName() }, 0);
#endif
        phase = Phase::none;
    }
    else
    {
        const auto args = offlineTtsArgs (runtimeBinary(), chosen, engineFolder (chosen), speechWav, voice(), text);
        started = command->start (args, 0);
        phase = Phase::synthesise;
    }

    if (! started || ! command->isRunning())
    {
        cleanupSpeech();
        fail (chosen == Engine::system
                  ? "No system speech voice was found. Install a system speech engine or choose an offline voice in Options > Speech..."
                  : "The offline voice could not start. Install it again in Options > Speech...");
        return;
    }

    speechProcess = std::move (command);
    speaking = true;
    if (onStateChanged)
        onStateChanged();
}

void SpeechService::stopSpeaking()
{
    if (speechProcess != nullptr)
    {
        speechProcess->kill();
        speechProcess.reset();
    }
    cleanupSpeech();
}

void SpeechService::cleanupSpeech()
{
    speechTextFile.deleteFile();
    speechWav.deleteFile();
    phase = Phase::none;
    if (speaking)
    {
        speaking = false;
        if (onStateChanged)
            onStateChanged();
    }
}

void SpeechService::beginPlayback (const juce::File& wav)
{
    auto player = std::make_unique<juce::ChildProcess>();
    bool started = false;
#if JUCE_MAC
    started = player->start (juce::StringArray { "/usr/bin/afplay", wav.getFullPathName() }, 0);
#elif JUCE_WINDOWS
    const auto script = folder().getChildFile ("play.ps1");
    script.replaceWithText ("$p = New-Object System.Media.SoundPlayer($args[0])\n$p.PlaySync()\n");
    started = player->start (juce::StringArray { "powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                                                   "-File", script.getFullPathName(), wav.getFullPathName() }, 0);
#else
    if (! (started = player->start (juce::StringArray { "paplay", wav.getFullPathName() }, 0)))
        started = player->start (juce::StringArray { "aplay", "-q", wav.getFullPathName() }, 0);
#endif
    if (! started || ! player->isRunning())
    {
        cleanupSpeech();
        fail ("The speech was made, but no audio player was found to play it.");
        return;
    }
    speechProcess = std::move (player);
    phase = Phase::play;
    speaking = true;
    if (onStateChanged)
        onStateChanged();
}

void SpeechService::finishSpeech()
{
    if (speechProcess != nullptr && speechProcess->isRunning())
        return;

    if (phase == Phase::synthesise)
    {
        speechProcess.reset();
        if (speechWav.existsAsFile() && speechWav.getSize() > 44)
        {
            beginPlayback (speechWav);
            return;
        }
        cleanupSpeech();
        fail ("The offline voice did not make any audio.");
        return;
    }

    speechProcess.reset();
    cleanupSpeech();
}

void SpeechService::startListening()
{
    if (! isEnabled() || ! takesVoiceInput() || transcribing.load())
        return;
    if (listening)
        return;
    stopSpeaking();
    listening = true;
    devices.startSpeechCapture();
    if (onStateChanged)
        onStateChanged();
}

void SpeechService::stopListening()
{
    if (! listening || transcribing.load())
        return;
    listening = false;
    double rate = 48000.0;
    auto samples = devices.stopSpeechCapture (rate);
    if (onStateChanged)
        onStateChanged();
    if (samples.size() < static_cast<std::size_t> (rate * 0.2))
    {
        fail ("That was too short. Hold the microphone button while you speak.");
        return;
    }

#if ARRANGER_HAS_WHISPER
    const auto setup = session.analysis().setup();
    if (! setup.model.existsAsFile())
    {
        fail ("Speech input needs a Whisper model. Install one in Options > Audio Analysis Tools..., then try again.");
        return;
    }
    transcribing = true;
    if (onStateChanged)
        onStateChanged();
    if (worker.joinable())
        worker.join();
    worker = std::thread ([this, samples = std::move (samples), rate, model = setup.model]() mutable {
        transcribe (std::move (samples), rate, model, "auto");
    });
#else
    fail ("This build has no Whisper.");
#endif
}

void SpeechService::transcribe (std::vector<float> samples, double sampleRate, juce::File model, std::string language)
{
    juce::String text, error;
#if ARRANGER_HAS_WHISPER
    try
    {
        Sample sample;
        sample.name = "speech";
        sample.sampleRate = sampleRate;
        sample.channels.push_back (std::move (samples));
        lyrics::Settings s;
        s.model = model.getFullPathName().toStdString();
        s.language = language;
        const auto transcript = lyrics::transcribe (sample, s);
        for (const auto& w : transcript.words)
        {
            if (! text.isEmpty() && w.text != "." && w.text != "," && w.text != "!" && w.text != "?")
                text << " ";
            text << juce::String::fromUTF8 (w.text.c_str());
        }
        text = text.trim();
    }
    catch (const std::exception& e)
    {
        error = juce::String::fromUTF8 (e.what());
    }
#endif
    juce::MessageManager::callAsync ([alive = std::weak_ptr<bool> (alive), this, text, error] {
        if (alive.expired())
            return;
        transcribing = false;
        if (error.isNotEmpty())
            fail (error);
        else if (text.isNotEmpty() && onTranscript)
            onTranscript (text);
        else if (text.isEmpty())
            fail ("I could not hear any words. Try again a little closer to the microphone.");
        if (onStateChanged)
            onStateChanged();
    });
}

void SpeechService::fail (const juce::String& text)
{
    if (onError)
        onError (text);
}

void SpeechService::timerCallback()
{
    if (speaking)
        finishSpeech();
}

} // namespace arranger
