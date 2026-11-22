#pragma once

#include "Session.h"

#include "arranger/platform/DeviceHost.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace arranger
{

class JuceDeviceHost;

/**
    Speech for the AI chat: spoken replies through the operating system's own
    voice, and microphone input written down by the built-in Whisper.
    One conversation-wide set of switches; there are no per-message buttons.
*/
class SpeechService final : private juce::Timer
{
public:
    SpeechService (Session&, platform::JuceDeviceHost&, juce::PropertiesFile*);
    ~SpeechService() override;

    enum class Engine { system, kitten, kokoro };

    bool isEnabled() const;
    void setEnabled (bool);
    Engine engine() const;
    void setEngine (Engine);
    static juce::String engineName (Engine);
    static juce::StringArray voiceNames (Engine);
    int voice() const;
    void setVoice (int);
    bool isEngineInstalled (Engine) const;
    bool isInstalling() const noexcept { return installing.load(); }
    /** Downloads the runtime and model for an engine, then calls `done` on the message thread. */
    void installEngine (Engine, std::function<void (bool, const juce::String&)> done);
    void cancelInstall();
    bool speaksReplies() const;
    void setSpeaksReplies (bool);
    bool takesVoiceInput() const;
    void setTakesVoiceInput (bool);

    /** Speaks a whole reply, replacing any speech already playing. */
    void speak (const juce::String& text);
    void stopSpeaking();
    bool isSpeaking() const;

    /** Command-line arguments for offline TTS and Linux spd-say (with "--" before spoken text). */
    static juce::StringArray offlineTtsArgs (const juce::File& binary, Engine, const juce::File& modelFolder,
                                             const juce::File& outputWav, int voiceIndex, const juce::String& text);
    static juce::StringArray spdSayArgs (const juce::String& text);

    /** Records the microphone until stopListening(); the words then arrive in onTranscript. */
    void startListening();
    void stopListening();
    bool isListening() const noexcept { return listening; }
    bool isTranscribing() const noexcept { return transcribing; }

    std::function<void (const juce::String&)> onTranscript;
    std::function<void (const juce::String&)> onError;
    std::function<void()> onStateChanged;

    /** Where the temporary speech files go. */
    static juce::File folder();

private:
    void timerCallback() override;
    void finishSpeech();
    void beginPlayback (const juce::File&);
    void cleanupSpeech();
    juce::File engineFolder (Engine) const;
    juce::File runtimeFolder() const;
    juce::File runtimeBinary() const;
    bool download (const juce::String& url, const juce::File& target);
    bool extract (const juce::File& archive, const juce::File& folder);
    bool installRuntime (juce::String& error);
    void transcribe (std::vector<float> samples, double sampleRate, juce::File model, std::string language);
    void fail (const juce::String&);

    Session& session;
    platform::JuceDeviceHost& devices;
    juce::PropertiesFile* settings;
    std::unique_ptr<juce::ChildProcess> speechProcess;
    juce::File speechTextFile, speechWav;
    bool speaking = false, listening = false;
    enum class Phase { none, synthesise, play };
    Phase phase = Phase::none;
    std::atomic<bool> installing { false };
    std::atomic<bool> cancelInstalling { false };
    std::thread installer;
    std::atomic<bool> transcribing { false };
    std::thread worker;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace arranger
