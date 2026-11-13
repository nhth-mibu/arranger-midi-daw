#pragma once

#include "arranger/core/ControllerProfile.h"
#include "arranger/core/DeviceProfile.h"
#include "arranger/core/Document.h"
#include "arranger/core/Quantise.h"
#include "arranger/core/SongEdit.h"
#include "arranger/engine/Engine.h"
#include "arranger/soundfont/SoundFontPlayer.h"
#include "../platform/juce/JuceDeviceHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
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
class Session final : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Session (Engine&, platform::DeviceHost&);
    ~Session() override;

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

    QuantiseSettings quantise;
    bool quantiseOnRecord = false;
    Tick snapGrid = kTicksPerQuarter / 4; ///< for editing; 0 = off

    // ---- sound ---------------------------------------------------------------
    /** Loads an SF2 in the background and hands it to the engine. */
    void loadSoundFont (const juce::File&);
    std::shared_ptr<SoundFontPlayer> soundFont() const { return font; }
    juce::String soundFontStatus() const { return fontStatus; }

    /** A WAV file's audio, loaded once and shared. Null if the file can't be read. */
    std::shared_ptr<const Sample> sampleFor (const std::string& path);

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

    // ---- devices -----------------------------------------------------------------
    /** Device profiles: which sounds and mixer controllers each MIDI port's device has. */
    const DeviceLibrary& deviceLibrary() const noexcept { return library; }
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

    /** Names of the controllers that are plugged in. */
    juce::StringArray connectedControllers() const;

    /** Incoming MIDI since the last call, for the activity display. */
    std::vector<platform::IncomingMidi> takeMonitor();

    std::function<void (const juce::String&)> onError;

private:
    void timerCallback() override;
    void songChanged (const Song& before);
    void publish();
    void syncSamplers();
    void updateThru();
    void finishTake();
    void pollInput();
    void pollControllers();
    void applySurface (const SurfaceEvent&, int strips);

    Engine& engine;
    platform::DeviceHost& devices;
    Document document;
    DeviceLibrary library;

    std::vector<ControllerProfile> controllers;
    std::map<std::string, ControllerSurface> surfaces;    ///< by input port name
    std::map<std::string, std::string> surfaceOutputs;    ///< output port name -> controller id, once greeted
    std::vector<platform::IncomingMidi> monitor;
    int bankOffset = 0;
    int controllerPollCountdown = 0;
    juce::File currentFile;

    TrackId selTrack = 0;
    std::set<ClipId> selClips;
    ClipId editClip = 0;

    double previewPosition = -1.0;
    bool recordingOn = false;
    bool wasPlaying = false;
    std::vector<RecordedEvent> take;
    TrackId takeTrack = 0;

    std::shared_ptr<SoundFontPlayer> font;
    juce::String fontStatus = "No SoundFont loaded";
    std::shared_ptr<const PlaybackSnapshot> snapshot;

    struct BuiltKit { KitSettings settings; std::shared_ptr<DrumSampler> sampler; };
    std::map<TrackId, BuiltKit> kits;
    std::map<std::string, std::shared_ptr<const Sample>> samples;
    std::set<std::string> missing;
    std::shared_ptr<const SamplerSet> samplerSet;
    std::unique_ptr<juce::Thread> loader;
    struct PendingOff { double dueMs; std::uint16_t port; std::uint8_t status, pitch; };
    std::vector<PendingOff> previewOffs;
};

} // namespace arranger
