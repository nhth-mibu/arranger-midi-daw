#pragma once

#include <deque>

#include "arranger/core/Clipboard.h"
#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Document.h"
#include "arranger/core/Quantise.h"
#include "arranger/core/Arranging.h"
#include "arranger/core/ChordInput.h"
#include "arranger/core/KidsMode.h"
#include "arranger/core/MidiInputPolicy.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleChord.h"
#include "arranger/engine/Engine.h"
#include "arranger/soundfont/SoundFontPlayer.h"
#include "../platform/juce/JuceDeviceHost.h"
#include "plugins/PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace arranger
{

/**
    The open project and everything around it that isn't drawing: the song
    with undo, selection, transport and recording, the SoundFont, and keeping
    the engine and hardware in step with the song.

    Views read from it and call its methods; it tells them to refresh through
    a synchronous change message.
*/
class AnalysisService;
class StyleFromAudioService;

class Session final : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Session (Engine&, platform::DeviceHost&);
    ~Session() override;

    /** Lifetime token that expires when this Session begins destruction. */
    std::weak_ptr<std::atomic<bool>> lifetime() const noexcept { return alive; }

    // ---- the song ------------------------------------------------------------
    const Song& song() const noexcept { return document.song(); }
    Document& doc() noexcept { return document; }

    /** Changes the song as one undo step (or merges into the last with the same key). */
    void edit (const std::string& description, const std::function<void (Song&)>& change, const std::string& mergeKey = {});
    void undo();
    void redo();
    void endMerge() { document.closeMerge(); }

    /** Replaces the song (new, open, import). Clears undo. */
    void load (Song, const juce::File& file = {});
    juce::File file() const { return currentFile; }
    void setFile (const juce::File& f) { currentFile = f; }

    // ---- selection -----------------------------------------------------------
    TrackId selectedTrack() const noexcept { return selTrack; }
    void selectTrack (TrackId);
    const std::set<ClipId>& selectedClips() const noexcept { return selClips; }
    void selectClips (std::set<ClipId>);
    ClipId editedClip() const noexcept { return editClip; } ///< open in the piano roll; 0 = none
    void openInEditor (ClipId);

    /** What Cut or Copy kept last, in any view. Paste uses it. */
    clipboard::Content clipboard;

    // ---- transport -----------------------------------------------------------
    void togglePlay();
    void stop();
    void locate (Tick);
    void toggleRecord();
    bool isRecording() const noexcept { return recordingOn; }
    bool isPlaying() const noexcept { return engine.isPlaying(); }
    double position() const noexcept { return previewPosition >= 0.0 ? previewPosition : engine.positionTicks(); }

    /** For screenshots without an audio device: pretend the playhead is here. */
    void setPreviewPosition (double ticks) { previewPosition = ticks; }

    /** MIDI recorded so far in the current take, for drawing. */
    const std::vector<RecordedEvent>& takeInProgress() const noexcept { return take; }
    TrackId recordTarget() const;
    /** Audio recording: true while the take goes to an audio track. */
    bool isRecordingAudio() const noexcept { return recordingOn && audioTake; }
    /** Hear the audio input while the track to record on is an audio track. Not saved with the song. */
    void setInputMonitor (bool on);
    bool inputMonitor() const noexcept { return monitorWanted; }

    /** The audio inputs an audio track can record from on the open device (named by its device profile, if any). */
    std::vector<AudioInputChoice> audioInputChoices() const;
    /** The name of the input a track records from: "CH5 (stereo)", or "In 1+2" for the default. */
    juce::String audioInputName (const Track&) const;
    /** Sets the audio input of an audio track (count 0: the default, the first two inputs). */
    void setAudioInput (TrackId, int first, int count);
    /** Where the audio take starts in the song (less than 0: no audio has arrived yet). */
    double audioTakeStart() const noexcept { return engine.captureStartTicks(); }
    /** The level of the audio take so far: one peak per kTakePeakFrames frames. */
    const std::vector<float>& audioTakePeaks() const noexcept { return takePeaks; }
    double audioTakeSampleRate() const noexcept { return takeRate; }
    static constexpr int kTakePeakFrames = 512;

    // ---- clip grid -----------------------------------------------------------
    /** Starts a slot at the next launch point (see Song::launchQuantise). It loops until stopped. */
    void launchSlot (TrackId, int scene);
    void stopSlot (TrackId);
    /** A pad press: starts the slot, or stops it when it is already playing or waiting. */
    void toggleSlot (TrackId, int scene);
    void launchScene (int scene);
    void stopAllSlots();
    SlotStatus slotStatus (TrackId id) const noexcept { return engine.slotStatus (id); }

    /** While on, what the Clip grid plays is written to the Timeline when it stops. */
    void setJamRecording (bool);
    bool isJamRecording() const noexcept { return jamOn; }

    /** Called after Record to Timeline writes one or more clips. */
    std::function<void()> onJamFinished;

    // ---- arranger style --------------------------------------------------------
    /** The chord that the style parts follow. Set from the keys held below the split, or by hand. */
    Chord chord() const noexcept { return currentChord; }
    void setChord (Chord);

    /** Keys below this note (on any input) name the chord while the song is a style. */
    int chordSplit = 54; // F#3, as on the keyboard
    bool chordFromKeys = true;

    // ---- smart chord input -------------------------------------------------------
    /** How chords played on keyboards and pads reach the chord lane. Kept while the app runs. */
    struct ChordInputSettings
    {
        bool fromKeys = false;        ///< chords played on the keyboard go to the chord lane
        bool belowSplitOnly = false;  ///< only keys below chordSplit name the chord (left hand)
        Tick grid = 0;                ///< chords move to this grid; 0 = a bar
        bool padsPlayChords = false;  ///< controller pads play the chord palette; knobs 1-3 set inversion, voicing and octave
        chord_input::VoiceOptions voicing;
    };
    const ChordInputSettings& chordInput() const noexcept { return chordSettings; }
    void setChordInput (const ChordInputSettings&);

    /** The grid chords move to (a bar when not set). */
    Tick chordGrid() const noexcept;
    /** The key of the song (C major when its text is no key). */
    theory::Key songKey() const { return chord_input::songKey (song()); }
    /** The sixteen chords of the palette for the song's key. */
    std::vector<chord_input::PaletteChord> chordPalette() const { return chord_input::palette (songKey()); }

    /** The last chord heard from the keyboard or the pads, and its pad (-1: not a pad). */
    std::optional<Chord> heardChord() const noexcept { return heard; }
    int heldPad() const noexcept { return padDown; }

    /**
        A chord from the pads (on screen or on a controller): it sounds on the
        selected track while held. While recording it is written to the chord
        lane (and its notes into the take); with "chords from keys" on and the
        transport stopped, it is written at the playhead when it goes up and
        the playhead moves on (step entry).
    */
    void padChord (int index, bool down);
    void chordDown (const Chord&, int pad = -1);
    void chordUp();

    /** Called when a chord is heard or the chord settings change. */
    std::function<void()> onChordHeard;

    /** Step entry: called for each note-on (velocity 1..127) and note-off (velocity 0) from the keyboards. */
    std::function<void (int pitch, int velocity)> onStepNote;

    // ---- kids mode ------------------------------------------------------------------
    /**
        Pretend play for children: any key on any MIDI input plays the song,
        and the song pauses when no key is pressed for `pauseAfterSeconds`.
        The next key goes on from there; at the end, the song starts again.
        Kept in the app settings, not in the song.
    */
    struct KidsModeSettings
    {
        double pauseAfterSeconds = kids_mode::kDefaultPauseAfter;
        bool keysSound = false; ///< the keys also play their own notes on the track to record on
    };
    void setKidsMode (bool on);
    bool kidsMode() const noexcept { return kidsOn; }
    const KidsModeSettings& kidsModeSettings() const noexcept { return kidsSettings; }
    void setKidsModeSettings (KidsModeSettings);
    /** A key from the screen (a computer key or a click) in Kids Mode. */
    void kidsTap();
    /** Counts the keys pressed in Kids Mode, for the screen. */
    int kidsPresses() const noexcept { return kidsKeys.presses(); }
    /** Called when Kids Mode goes on or off, or its settings change. */
    std::function<void()> onKidsModeChanged;

    QuantiseSettings quantise;
    bool quantiseOnRecord = false;
    Tick snapGrid = kTicksPerQuarter / 4; ///< for editing; 0 = off

    // ---- sound ---------------------------------------------------------------
    /** Loads an SF2 in the background and hands it to the engine. */
    void loadSoundFont (const juce::File&);
    std::shared_ptr<SoundFontPlayer> soundFont() const { return font; }
    juce::String soundFontStatus() const { return fontStatus; }

    /** The name to show for a track's sound: the SoundFont preset, or the General MIDI name. */
    juce::String soundNameFor (const Track&) const;
    /** Shows a message where the SoundFont name goes, such as download progress. */
    void setSoundFontStatus (const juce::String& text)
    {
        fontStatus = text;
        sendChangeMessage();
    }

    /** A WAV file's audio, loaded once and shared. Null if the file can't be read. */
    // ---- plugins ------------------------------------------------------------
    /** Where the plugins come from. Without a host, plugin slots stay silent. */
    void setPluginHost (PluginHost* host);
    PluginHost* pluginHost() const noexcept { return plugins; }
    /** The live plugin of a slot: track 0 is the master; index -1 is the track's instrument. Null when it is not loaded. */
    std::shared_ptr<HostedProcessor> pluginAt (TrackId track, int index) const;
    /** Why a slot's plugin is not loaded, or an empty text. */
    juce::String pluginProblem (TrackId track, int index) const;
    /** Copies the settings of all live plugins into the song (before a save, after a plugin window closes). */
    void capturePluginStates();
    /** True when the track can have insert effects (its sound is made in the app, on its own bus). */
    static bool canHaveEffects (const Track&);

    std::shared_ptr<const Sample> sampleFor (const std::string& path);
    /** Why the last attempt to read this file failed. */
    juce::String sampleProblem (const std::string& path) const;
    /** The same file at another speed with the same pitch (made once, then kept). */
    std::shared_ptr<const Sample> stretchedFor (const std::string& path, double speed);

    /** The sampler of a sampler track (for its meter); null for other tracks. */
    DrumSampler* samplerFor (TrackId) const;

    /** Sample files that kits ask for but that could not be read. */
    const std::set<std::string>& missingSamples() const noexcept { return missing; }

    /** Plays a note on a track's output (preview while editing). Off after `ms`. */
    void previewNote (TrackId, int pitch, int velocity = 100, int ms = 250);

    /** Engine port number for a track's output in the current snapshot. */
    std::uint16_t portFor (const Track&) const;

    /** Muted, or silenced because other tracks are soloed. */
    bool isMuted (const Track&) const;

    /** Master output peak since the last call (0 = left). */
    float masterPeak (int channel) noexcept { return engine.takeMasterPeak (channel); }
    /** The peak of a bus since the last call, 0 or 1 for the channel. */
    float busPeak (std::uint32_t bus, int channel) noexcept
    {
        const auto it = busMeters.find (bus);
        if (it == busMeters.end() || it->second == nullptr)
            return 0.0f;
        return (channel == 0 ? it->second->left : it->second->right).exchange (0.0f, std::memory_order_relaxed);
    }

    // ---- devices -----------------------------------------------------------------
    /** Device profiles: which sounds and mixer controllers each MIDI port's device has. */
    const DeviceLibrary& deviceLibrary() const noexcept { return library; }
    platform::DeviceHost& deviceHost() noexcept { return devices; }
    void setDeviceLibrary (DeviceLibrary);

    /** The profile for a track's output: its device for a MIDI port, General MIDI otherwise. */
    const DeviceProfile& profileFor (const Track&) const;

    /** Sends the device's reset messages (GM/GS/XG on) to a track's MIDI port. */
    void resetDevice (TrackId);

    // ---- control surfaces -----------------------------------------------------------
    /** Ready-made mixer pages for hardware controllers. They connect by port name. */
    void setControllers (std::vector<ControllerProfile>);

    /** First track the controller's strip 1 points at. */
    int surfaceBank() const noexcept { return bankOffset; }
    /** Moves the controller strips to a bank, in steps of 8 tracks. */
    void setSurfaceBank (int firstTrack);

    /** First scene the controller's top pad row points at. */
    int surfaceScene() const noexcept { return padScene; }
    void setSurfaceScene (int);

    /** Pad rows of the connected controllers (0 = no controller with clip pads). */
    int surfacePadRows() const;

    /** Names of the controllers that are plugged in. */
    juce::StringArray connectedControllers() const;

    const std::vector<ControllerProfile>& controllerPages() const noexcept { return controllers; }

    /** The controller page a MIDI input uses now (chosen by hand, else by its name), or null. */
    const ControllerProfile* controllerFor (const std::string& port) const;

    // ---- port setup (kept in the app settings, not in the song) -------------------------
    /** Chooses the device profile of a MIDI output by hand. An empty id goes back to the name match. */
    void assignDeviceProfile (const std::string& port, const std::string& profileId);
    /** Chooses the controller page of a MIDI input by hand: a page id, "none", or empty for the name match. */
    void assignControllerPage (const std::string& port, const std::string& pageId);
    const std::map<std::string, std::string>& deviceAssignments() const noexcept { return deviceChoice; }
    const std::map<std::string, std::string>& controllerAssignments() const noexcept { return controllerChoice; }
    /** MIDI inputs the user turned off by hand: they stay off when plugged in again.
        Other inputs of known devices come on by themselves (see MidiInputPolicy). */
    const std::set<std::string>& inputsTurnedOff() const noexcept { return inputPolicy.turnedOff(); }
    void setInputsTurnedOff (std::set<std::string> names) { inputPolicy.setTurnedOff (std::move (names)); }
    /** True when a port belongs to a known instrument or controller (a device profile or controller page). */
    bool isKnownDevicePort (const std::string& port) const;
    /** Called after a choice above changes, to save it. */
    std::function<void()> onPortSetupChanged;

    // ---- arranging help (kept in the app settings, not in the song) ----------------------
    /** The user's earlier choices of arrangement proposals: they put what the user takes first. */
    const arranging::Preferences& arrangingPreferences() const noexcept { return arrangingPrefs; }
    /** Remembers an explicit choice: an option taken (0, 1), or -1 for a dismissed proposal. */
    void rememberArrangingChoice (const arranging::Proposal&, int option);
    void setArrangingPreferences (arranging::Preferences p) { arrangingPrefs = std::move (p); }
    /** What a device can play, as the user typed it. It wins over the numbers in the profile. */
    void setDeviceCapabilities (const std::string& profileId, const DeviceCapabilities&);
    const std::map<std::string, DeviceCapabilities>& userDeviceCapabilities() const noexcept { return userCapabilities; }
    /** Called after a choice or a capability above changes, to save it. */
    std::function<void()> onArrangingSettingsChanged;

    /**
        Plays another version of the song in place of the song (a proposal, to
        compare it with the original), looping [from, to). It starts playback.
        With no song, the song itself plays again and playback stops. An edit of
        the song ends the audition too.
    */
    void audition (std::optional<Song>, Tick from = 0, Tick to = 0);
    bool isAuditioning() const noexcept { return auditionSong.has_value(); }

    /** Incoming MIDI of the last two minutes, with the time it came (Time::getMillisecondCounterHiRes). */
    struct LoggedMidi
    {
        double ms = 0.0;
        platform::IncomingMidi message;
    };
    std::vector<LoggedMidi> recentMidi (double seconds) const;

    // ---- synth sound editors ----------------------------------------------------------
    /** Gets every message from the MIDI inputs (SysEx too), on the UI thread. Returns an id for unwatchMidi. */
    using MidiWatcher = std::function<void (const std::string& port, const std::vector<std::uint8_t>& bytes)>;
    int watchMidi (MidiWatcher);
    void unwatchMidi (int id);

    /** True when the port (input or output) belongs to the device: chosen in the MIDI setup, or by its name. */
    bool isDevicePort (const std::string& port, const std::string& profileId) const;

    /** The MIDI output the device is plugged into, or an empty text. */
    std::string outputFor (const DeviceProfile&) const;

    /** The MIDI channel a synth's editor uses (remembered while the app runs). */
    int synthChannel (const std::string& profileId) const;
    void setSynthChannel (const std::string& profileId, int channel);

    /** The last value of each sound setting, by profile id and "Page/Name". */
    std::optional<int> synthValue (const std::string& profileId, const std::string& key) const;
    void rememberSynthValue (const std::string& profileId, const std::string& key, int value);
    /** Counts every change, so editor windows know to redraw. */
    int synthValuesVersion() const noexcept { return synthVersion; }

    /** Sends a sound setting to the synth and remembers it. False when the synth isn't plugged in. */
    bool sendSynthParameter (const DeviceProfile&, const SynthPage&, const SynthParameter&, int value);

    /**
        Reads the sound names stored in the synth and adds them to the user's
        sound list (saveUserSounds). Calls `done` with a short report. Returns
        an empty text when it started, or why it can't.
    */
    juce::String readSoundNames (const std::string& profileId, std::function<void (const juce::String&)> done = {});
    bool isReadingSoundNames() const noexcept { return nameJob != nullptr; }
    /** 0..1 while reading. */
    double soundNameProgress() const noexcept;

    /** Saves the user's sounds for a device and loads the profiles again; the main window sets it. */
    std::function<void (const SoundAddOn&)> saveUserSounds;

    /** Incoming MIDI since the last call, for the activity display. */
    std::vector<platform::IncomingMidi> takeMonitor();

    /**
        Plays the song from the start without the audio device and writes the
        sound to a 24-bit WAV file. Tracks on hardware synths play on the
        SoundFont with the nearest General MIDI sound. Returns the length in
        seconds. Throws std::runtime_error.
    */
    double renderToWav (const juce::File&);

    std::function<void (const juce::String&)> onError;

    /** Audio analysis (stems, notes, chords, instruments, lyrics). */
    AnalysisService& analysis();
    /** Style from Audio: a PSR style made from an analysed audio clip. */
    StyleFromAudioService& styleFromAudio();

    /** While on, only the selected track plays: for the Beat view. */
    void setDrumsOnly (bool on);
    bool drumsOnlyPlayback() const noexcept { return drumsOnly; }

    /** Asks the window to show the Beat view (a double-click on a drum clip). */
    std::function<void()> onOpenBeat;

private:
    void timerCallback() override;
    void songChanged (const Song& before);
    void publish();
    void publishSong (const Song&);
    void syncSamplers (const Song&);
    void syncRacks (const Song&);
    void updateThru();
    void finishTake();
    void finishAudioTake();
    void drainAudioTake();
    void pollSlots();
    void finishJam();
    void pollInput();
    void pollControllers();
    void applySurface (const SurfaceEvent&, int strips);
    void updatePadLights();
    void refreshControlFilters (bool force);
    void startPlayback();
    void kidsKeyHeard();
    void pollKidsMode();

    Engine& engine;
    platform::DeviceHost& devices;
    Document document;
    DeviceLibrary library;

    std::vector<ControllerProfile> controllers;
    std::map<std::string, std::string> deviceChoice;     ///< output port -> device profile id
    std::map<std::string, std::string> controllerChoice; ///< input port -> controller page id or "none"
    std::vector<std::string> filteredInputs;             ///< the inputs the control filters were made for
    std::map<std::string, ControllerSurface> surfaces;    ///< by input port name
    std::map<std::string, std::string> surfaceOutputs;    ///< output port name -> controller id, once greeted
    std::vector<platform::IncomingMidi> monitor;
    std::deque<LoggedMidi> midiLog;
    arranging::Preferences arrangingPrefs;
    std::map<std::string, DeviceCapabilities> userCapabilities;
    std::optional<Song> auditionSong;
    bool drumsOnly = false;
    bool kidsOn = false;
    KidsModeSettings kidsSettings;
    kids_mode::Keys kidsKeys;
    TrackId drumsOnlyTrack = 0; ///< the selected track when drums-only playback started
    std::map<int, MidiWatcher> watchers;
    int nextWatcher = 1;
    std::map<std::string, int> synthChannels;
    std::map<std::string, std::map<std::string, int>> synthValues;
    int synthVersion = 0;
    struct NameJob;
    std::unique_ptr<NameJob> nameJob;
    std::unique_ptr<AnalysisService> analysisService;
    std::unique_ptr<StyleFromAudioService> styleFromAudioService;
    void stepNameJob();
    void tellWatchers (const std::string& port, const std::vector<std::uint8_t>&);
    int bankOffset = 0;
    int padScene = 0;
    std::map<std::string, std::vector<PadLight>> padsShown; ///< by output port: what the pads show now
    int controllerPollCountdown = 0;
    MidiInputPolicy inputPolicy;
    void enableKnownInputs();
    juce::File currentFile;

    TrackId selTrack = 0;
    std::set<ClipId> selClips;
    ClipId editClip = 0;

    double previewPosition = -1.0;
    bool recordingOn = false;
    bool wasPlaying = false;
    std::vector<RecordedEvent> take;
    TrackId takeTrack = 0;
    bool audioTake = false;
    bool monitorWanted = false;
    std::vector<float> takeLeft, takeRight, takePeaks;
    double takeRate = 48000.0;
    double takeLatency = 0.0; ///< seconds between the song and what comes back in
    bool takeStereo = true;

    void chordHeard (const Chord&);
    void stepChordUp();
    void finishChordTake();
    void updateChordPadLights();
    void sendChord (const std::vector<int>& notes, bool on);

    ChordInputSettings chordSettings;
    chord_input::ChordListener chordListener;
    std::optional<Chord> heard;
    std::optional<Chord> stepPending;            ///< step entry: written when the keys go up
    std::vector<chord_input::ChordHit> chordTake; ///< chords heard while recording
    std::vector<int> soundingChord, lastVoiced;
    TrackId soundingTrack = 0;
    int padDown = -1;

    Chord currentChord { 0, ChordType::major7, -1 }; // the chord most styles are written in
    std::set<int> chordKeys;

    struct JamSpan { TrackId track; int scene; double from, to; };
    bool jamOn = false;
    std::map<TrackId, JamSpan> jamOpen;
    std::vector<JamSpan> jamDone;

    std::shared_ptr<SoundFontPlayer> font;
    juce::String fontStatus = TRANS ("No SoundFont loaded");
    std::shared_ptr<const PlaybackSnapshot> snapshot;

    struct BuiltKit { KitSettings settings; std::shared_ptr<DrumSampler> sampler; };
    std::map<TrackId, BuiltKit> kits;
    struct LivePlugin
    {
        std::string id;           ///< PluginSlot::id
        std::string appliedState; ///< the PluginSlot::state that the plugin has now
        std::shared_ptr<HostedProcessor> processor; ///< null: could not load
        juce::String problem;
    };
    static std::string pluginKey (TrackId track, int index);
    PluginHost* plugins = nullptr;
    std::map<std::string, LivePlugin> livePlugins;
    std::string rackShape; ///< which processors are in the engine now
    std::map<std::uint32_t, std::shared_ptr<RackSet::Meter>> busMeters;

    std::map<std::string, std::shared_ptr<const Sample>> samples;
    std::map<std::pair<std::string, long>, std::shared_ptr<const Sample>> stretchedSamples; ///< by file and speed * 10000
    std::set<std::string> missing;
    std::map<std::string, std::string> sampleProblems;
    std::shared_ptr<const SamplerSet> samplerSet;
    std::unique_ptr<juce::Thread> loader;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::uint64_t soundFontGeneration = 0;
    struct PendingOff { double dueMs; std::uint16_t port; std::uint8_t status, pitch; };
    std::vector<PendingOff> previewOffs;
};

} // namespace arranger
