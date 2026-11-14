#include "Session.h"

#include "AudioFiles.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/PsrExport.h"
#include "arranger/core/Sample.h"
#include "arranger/core/TimeStretch.h"

#include "arranger/core/Style.h"

namespace arranger
{

Session::Session (Engine& e, platform::DeviceHost& d) : engine (e), devices (d)
{
    publish();
    startTimerHz (30);
}

Session::~Session()
{
    stopTimer();
    // Hand the controllers back (for example, leave the Launchkey's DAW mode).
    for (const auto& [port, id] : surfaceOutputs)
        for (const auto& c : controllers)
            if (c.id == id)
                for (const auto& m : c.disconnect)
                    devices.sendNow (port, m);
    if (loader != nullptr)
        loader->stopThread (10000);
}

// ---- song ----------------------------------------------------------------------
void Session::edit (const std::string& description, const std::function<void (Song&)>& change, const std::string& mergeKey)
{
    const Song before = document.song();
    document.edit (description, change, mergeKey);
    if (document.song() == before)
        return;
    songChanged (before);
}

void Session::undo()
{
    const Song before = document.song();
    if (document.undo())
        songChanged (before);
}

void Session::redo()
{
    const Song before = document.song();
    if (document.redo())
        songChanged (before);
}

void Session::load (Song s, const juce::File& f)
{
    engine.stop();
    engine.locate (0);
    recordingOn = false;
    engine.setRecording (false);
    take.clear();
    const Song before = document.song();
    document.reset (std::move (s));
    currentFile = f;
    selTrack = song().tracks.empty() ? 0 : song().tracks.front().id;
    selClips.clear();
    editClip = 0;
    if (! song().soundFont.empty() && (font == nullptr || font->path() != song().soundFont))
        loadSoundFont (juce::File (juce::String::fromUTF8 (song().soundFont.c_str())));
    songChanged (before);
}

void Session::songChanged (const Song& before)
{
    const auto& now = document.song();

    // Selection must point at things that still exist.
    if (edit::findTrack (now, selTrack) == nullptr)
        selTrack = now.tracks.empty() ? 0 : now.tracks.front().id;
    std::erase_if (selClips, [&] (ClipId id) { return edit::findClipPtr (now, id) == nullptr; });
    if (editClip != 0 && edit::findClipPtr (now, editClip) == nullptr)
        editClip = 0;

    publish();

    // Mixer and sound changes go to the hardware straight away, even when stopped.
    for (const auto& t : now.tracks)
    {
        const auto* old = edit::findTrack (before, t.id);
        if (old != nullptr && old->mixer == t.mixer && old->sound == t.sound && old->output == t.output && old->kit == t.kit)
            continue;
        if (t.output.kind == TrackOutput::Kind::none)
            continue;
        const auto port = portFor (t);
        const bool soundChanged = old == nullptr || old->sound != t.sound || old->output != t.output;
        for (const auto& m : trackSetupMessages (t, profileFor (t).mixer))
        {
            const int kind = m.bytes[0] & 0xf0;
            const bool isSound = kind == 0xc0 || (kind == 0xb0 && (m.bytes[1] == 0 || m.bytes[1] == 32));
            if (isSound && ! soundChanged)
                continue;
            engine.sendMidi (port, m.bytes[0], m.bytes[1], m.bytes[2]);
        }
    }

    updateThru();
    sendSynchronousChangeMessage();
}

void Session::publish()
{
    // A style plays its Clip grid moved to the current chord.
    publishSong (document.song().style ? style_convert::applyChord (document.song(), currentChord) : document.song());
}

void Session::publishSong (const Song& song)
{
    const auto mixer = [this] (const std::string& port) { return library.mixerFor (port); };
    const SampleSource audio = [this] (const std::string& file) { return sampleFor (file); };
    const StretchSource stretched = [this] (const std::string& file, double speed) { return stretchedFor (file, speed); };
    auto snap = std::make_shared<const PlaybackSnapshot> (buildSnapshot (song, mixer, audio, stretched));
    devices.setOutputPorts (snap->ports);
    syncSamplers();
    syncRacks();
    engine.setSnapshot (snap);
    snapshot = std::move (snap);
}

double Session::renderToWav (const juce::File& file)
{
    if (loader != nullptr && loader->isThreadRunning())
        throw std::runtime_error ("The SoundFont is still loading. Try again in a moment.");
    // Hardware synths can't be recorded here: their tracks play on the SoundFont.
    Song song = document.song();
    song.metronome.enabled = false;
    song.loop.enabled = false;
    // The SoundFont has 16 channels for all its tracks: a moved track takes a free one.
    std::set<int> used;
    for (const auto& t : song.tracks)
        if (t.output.kind == TrackOutput::Kind::soundFont)
            used.insert (t.output.channel);
    for (auto& t : song.tracks)
    {
        if (t.output.kind != TrackOutput::Kind::midiPort)
            continue;
        const auto& drumChannels = library.forPort (t.output.port).drumChannels;
        const bool drums = std::find (drumChannels.begin(), drumChannels.end(), t.output.channel) != drumChannels.end();
        if (drums)
            t.output.channel = 10;
        else if (used.count (t.output.channel) != 0 || t.output.channel == 10)
            for (int ch = 1; ch <= 16; ++ch)
                if (ch != 10 && used.count (ch) == 0)
                {
                    t.output.channel = ch;
                    break;
                }
        used.insert (t.output.channel);
        const bool gm = library.forPort (t.output.port).dialect != "none" && t.sound.program >= 0;
        int program = gm ? t.sound.program : psr_export::gmProgramFromName (t.sound.name);
        if (program < 0)
            program = psr_export::gmProgramFromName (t.name);
        t.output.kind = TrackOutput::Kind::soundFont;
        if (t.output.channel != 10)
            t.sound = { kGeneralMidiNames[std::max (0, program)], gm ? t.sound.bankMsb : 0, gm ? t.sound.bankLsb : 0, std::max (0, program) };
    }

    const auto status = devices.audioStatus();
    const double rate = status.sampleRate > 0 ? status.sampleRate : 48000.0;
    constexpr int kBlock = 512;
    const double seconds = song.tempo.secondsAt (static_cast<double> (song.contentEnd())) + 2.0;

    engine.stop();
    devices.pauseAudio (true);
    struct Restore
    {
        Session& s;
        ~Restore()
        {
            s.engine.stop();
            s.publish();
            s.devices.pauseAudio (false);
        }
    } restore { *this };

    publishSong (song);
    engine.prepare (rate, kBlock);
    Sample out;
    out.sampleRate = rate;
    out.channels.assign (2, {});
    std::vector<float> left (kBlock), right (kBlock);
    float* channels[] = { left.data(), right.data() };
    MidiEventBuffer midiIn (16), midiOut (4096);
    engine.locate (0);
    engine.play();
    for (double t = 0.0; t < seconds; t += kBlock / rate)
    {
        AudioBlock block { channels, 2, kBlock };
        midiOut.clear();
        engine.process (block, midiIn, midiOut);
        out.channels[0].insert (out.channels[0].end(), left.begin(), left.end());
        out.channels[1].insert (out.channels[1].end(), right.begin(), right.end());
    }
    wav::write (out, file.getFullPathName().toStdString(), 24);
    return seconds;
}

// ---- plugins ---------------------------------------------------------------------------
std::string Session::pluginKey (TrackId track, int index)
{
    return std::to_string (track) + (index < 0 ? "/i" : "/e" + std::to_string (index));
}

bool Session::canHaveEffects (const Track& t)
{
    // The sound of a MIDI port track is made outside the app.
    return t.output.kind == TrackOutput::Kind::audio || t.output.kind == TrackOutput::Kind::sampler
        || t.output.kind == TrackOutput::Kind::plugin || t.output.kind == TrackOutput::Kind::soundFont;
}

void Session::setPluginHost (PluginHost* host)
{
    plugins = host;
    // Try again the plugins that did not load (a scan may have found them).
    std::erase_if (livePlugins, [] (const auto& entry) { return entry.second.processor == nullptr; });
    syncRacks();
    sendChangeMessage();
}

std::shared_ptr<HostedProcessor> Session::pluginAt (TrackId track, int index) const
{
    const auto it = livePlugins.find (pluginKey (track, index));
    return it != livePlugins.end() ? it->second.processor : nullptr;
}

juce::String Session::pluginProblem (TrackId track, int index) const
{
    const auto it = livePlugins.find (pluginKey (track, index));
    return it != livePlugins.end() ? it->second.problem : juce::String();
}

void Session::syncRacks()
{
    if (plugins == nullptr)
        return;
    const double rate = engine.sampleRate() > 0.0 ? engine.sampleRate() : 48000.0;
    constexpr int kBlock = 4096;

    // Every slot of the song gets a live plugin; a live plugin stays while its slot holds the same plugin.
    std::map<std::string, LivePlugin> next;
    const auto want = [&] (const PluginSlot& slot, TrackId track, int index) -> std::shared_ptr<HostedProcessor> {
        if (slot.empty())
            return nullptr;
        const auto key = pluginKey (track, index);
        LivePlugin live;
        if (auto it = livePlugins.find (key); it != livePlugins.end() && it->second.id == slot.id)
            live = std::move (it->second);
        else
        {
            live.id = slot.id;
            live.processor = plugins->create (slot, index < 0, rate, kBlock, live.problem);
            live.appliedState = slot.state;
        }
        if (live.processor != nullptr)
        {
            if (live.appliedState != slot.state) // undo, redo
            {
                live.processor->setState (slot.state);
                live.appliedState = slot.state;
            }
            live.processor->setBypass (slot.bypass);
        }
        const auto processor = live.processor;
        next[key] = std::move (live);
        return processor;
    };

    auto set = std::make_shared<RackSet>();
    std::string shape;
    const auto note = [&] (const std::shared_ptr<HostedProcessor>& p) { shape += std::to_string (reinterpret_cast<std::uintptr_t> (p.get())) + ","; };
    for (const auto& t : song().tracks)
    {
        RackSet::Bus bus;
        bus.track = t.id;
        if (t.output.kind == TrackOutput::Kind::plugin)
        {
            bus.instrument = want (t.instrument, t.id, -1);
            note (std::static_pointer_cast<HostedProcessor> (bus.instrument));
        }
        if (canHaveEffects (t))
            for (std::size_t i = 0; i < t.effects.size(); ++i)
                if (auto p = want (t.effects[i], t.id, static_cast<int> (i)))
                {
                    note (p);
                    bus.effects.push_back (std::move (p));
                    bus.effectSlots.push_back (static_cast<int> (i));
                }
        // A track that goes to a bus or sends to one needs its own signal, so it gets a bus here too.
        const bool routed = canHaveEffects (t) && (t.bus != 0 || ! t.sends.empty());
        if (bus.instrument != nullptr || ! bus.effects.empty() || routed)
        {
            shape += "t" + std::to_string (t.id) + ";";
            set->buses.push_back (std::move (bus));
        }
    }
    for (const auto& b : song().buses)
    {
        RackSet::MixBus mix;
        mix.id = b.id;
        for (std::size_t i = 0; i < b.effects.size(); ++i)
            if (auto p = want (b.effects[i], b.id, static_cast<int> (i)))
            {
                note (p);
                mix.effects.push_back (std::move (p));
            }
        shape += "b" + std::to_string (b.id) + ";";
        set->mixBuses.push_back (std::move (mix));
    }
    for (std::size_t i = 0; i < song().master.effects.size(); ++i)
        if (auto p = want (song().master.effects[i], 0, static_cast<int> (i)))
        {
            note (p);
            set->master.push_back (std::move (p));
        }
    livePlugins = std::move (next);

    if (shape != rackShape)
    {
        rackShape = shape;
        busMeters.clear();
        for (const auto& b : set->mixBuses)
            busMeters[b.id] = b.meter;
        set->prepare (kBlock);
        engine.setRacks (std::move (set));
    }
}

void Session::capturePluginStates()
{
    // Read the settings first; write them into the song in one step.
    std::map<std::string, std::string> states;
    for (auto& [key, live] : livePlugins)
        if (live.processor != nullptr)
        {
            auto state = live.processor->state();
            if (state != live.appliedState)
            {
                live.appliedState = state;
                states[key] = std::move (state);
            }
        }
    if (states.empty())
        return;
    edit ("Plugin Settings", [&] (Song& s) {
        const auto apply = [&] (PluginSlot& slot, TrackId track, int index) {
            if (const auto it = states.find (pluginKey (track, index)); it != states.end() && ! slot.empty())
                slot.state = it->second;
        };
        for (auto& t : s.tracks)
        {
            apply (t.instrument, t.id, -1);
            for (std::size_t i = 0; i < t.effects.size(); ++i)
                apply (t.effects[i], t.id, static_cast<int> (i));
        }
        for (auto& b : s.buses)
            for (std::size_t i = 0; i < b.effects.size(); ++i)
                apply (b.effects[i], b.id, static_cast<int> (i));
        for (std::size_t i = 0; i < s.master.effects.size(); ++i)
            apply (s.master.effects[i], 0, static_cast<int> (i));
    });
}

std::shared_ptr<const Sample> Session::stretchedFor (const std::string& path, double speed)
{
    const auto key = std::make_pair (path, std::lround (speed * 10000.0));
    if (const auto it = stretchedSamples.find (key); it != stretchedSamples.end())
        return it->second;
    const auto source = sampleFor (path);
    if (source == nullptr)
        return nullptr;
    if (stretchedSamples.size() > 24)
        stretchedSamples.clear(); // old speeds from tempo changes; the ones in use are made again
    auto made = std::make_shared<const Sample> (stretch::changeSpeed (*source, speed));
    stretchedSamples[key] = made;
    return made;
}

std::shared_ptr<const Sample> Session::sampleFor (const std::string& path)
{
    if (path.empty())
        return nullptr;
    if (const auto it = samples.find (path); it != samples.end())
        return it->second;
    std::shared_ptr<const Sample> loaded;
    try
    {
        loaded = std::make_shared<const Sample> (audio_files::read (path));
        missing.erase (path);
    }
    catch (const std::exception&)
    {
        missing.insert (path);
        return nullptr; // not cached: the file may turn up later
    }
    samples[path] = loaded;
    return loaded;
}

DrumSampler* Session::samplerFor (TrackId id) const
{
    return samplerSet != nullptr ? samplerSet->forTrack (id) : nullptr;
}

void Session::syncSamplers()
{
    // One sampler per sampler track; rebuilt only when its kit changes.
    bool changed = false;
    std::map<TrackId, BuiltKit> next;
    for (const auto& t : song().tracks)
    {
        if (t.output.kind != TrackOutput::Kind::sampler)
            continue;
        if (auto it = kits.find (t.id); it != kits.end() && it->second.settings == t.kit)
        {
            next[t.id] = std::move (it->second);
            continue;
        }

        DrumKit kit;
        kit.name = t.kit.name;
        for (const auto& p : t.kit.pads)
        {
            DrumPad pad;
            pad.name = p.name;
            pad.note = p.note;
            pad.sample = sampleFor (p.sampleFile);
            pad.range = { static_cast<std::size_t> (p.sliceStart), static_cast<std::size_t> (p.sliceEnd) };
            pad.gain = std::pow (10.0f, p.gainDb / 20.0f);
            pad.pan = p.pan;
            pad.tuneSemitones = p.tuneSemitones;
            pad.chokeGroup = p.chokeGroup;
            pad.oneShot = p.oneShot;
            kit.pads.push_back (std::move (pad));
        }
        auto sampler = std::make_shared<DrumSampler> (std::move (kit));
        const double rate = engine.sampleRate();
        sampler->prepare ({ rate > 0 ? rate : 48000.0, 4096 });
        next[t.id] = { t.kit, std::move (sampler) };
        changed = true;
    }
    changed = changed || next.size() != kits.size();
    kits = std::move (next);
    if (! changed && samplerSet != nullptr)
        return;

    auto set = std::make_shared<SamplerSet>();
    for (const auto& [id, built] : kits)
        set->entries.push_back ({ id, built.sampler });
    samplerSet = set;
    engine.setSamplers (std::move (set));
}

std::uint16_t Session::portFor (const Track& t) const
{
    if (t.output.kind == TrackOutput::Kind::soundFont)
        return MidiEvent::kSoundFontPort;
    if (t.output.kind == TrackOutput::Kind::none || snapshot == nullptr)
        return MidiEvent::kNoPort;
    if (t.output.kind == TrackOutput::Kind::sampler || t.output.kind == TrackOutput::Kind::plugin)
        return snapshot->samplerPort (t.id);
    for (std::size_t i = 0; i < snapshot->ports.size(); ++i)
        if (snapshot->ports[i] == t.output.port)
            return static_cast<std::uint16_t> (i + 1);
    return MidiEvent::kNoPort;
}

bool Session::isMuted (const Track& t) const
{
    if (t.mixer.mute)
        return true;
    const auto& tracks = song().tracks;
    const bool anySolo = std::any_of (tracks.begin(), tracks.end(), [] (const Track& x) { return x.mixer.solo; });
    return anySolo && ! t.mixer.solo;
}

// ---- devices ---------------------------------------------------------------------
void Session::setDeviceLibrary (DeviceLibrary lib)
{
    library = std::move (lib);
    for (const auto& [port, id] : deviceChoice)
        library.assign (port, id);
    publish();
    sendChangeMessage();
}

const DeviceProfile& Session::profileFor (const Track& t) const
{
    static const DeviceProfile gm = device_profile::generalMidi();
    if (t.output.kind == TrackOutput::Kind::midiPort)
        return library.forPort (t.output.port);
    return gm;
}

void Session::resetDevice (TrackId id)
{
    const auto* t = edit::findTrack (song(), id);
    if (t == nullptr || t->output.kind != TrackOutput::Kind::midiPort)
        return;
    for (const auto& message : profileFor (*t).reset)
        devices.sendNow (t->output.port, message);
    // Then put this song's sounds and mixer back.
    for (const auto& other : song().tracks)
        if (other.output == t->output || (other.output.kind == TrackOutput::Kind::midiPort && other.output.port == t->output.port))
            for (const auto& m : trackSetupMessages (other, profileFor (other).mixer))
                engine.sendMidi (portFor (other), m.bytes[0], m.bytes[1], m.bytes[2]);
}

// ---- selection ---------------------------------------------------------------------
void Session::selectTrack (TrackId id)
{
    if (id == selTrack)
        return;
    selTrack = id;
    updateThru();
    sendSynchronousChangeMessage();
}

void Session::selectClips (std::set<ClipId> ids)
{
    selClips = std::move (ids);
    sendSynchronousChangeMessage();
}

void Session::openInEditor (ClipId id)
{
    editClip = id;
    if (id != 0)
        if (const auto t = edit::trackOfClip (song(), id))
            selTrack = t;
    updateThru();
    sendSynchronousChangeMessage();
}

TrackId Session::recordTarget() const
{
    for (const auto& t : song().tracks)
        if (t.armed)
            return t.id;
    return selTrack;
}

void Session::setInputMonitor (bool on)
{
    monitorWanted = on;
    updateThru();
    sendChangeMessage();
}

void Session::updateThru()
{
    // The audio input goes to the output only while an audio track is the one to record on.
    {
        const auto* target = edit::findTrack (song(), recordTarget());
        engine.setInputMonitor (monitorWanted && target != nullptr && target->output.kind == TrackOutput::Kind::audio);
    }
    // Play what you hear: live keys go to the track you're about to record on.
    const auto* t = edit::findTrack (song(), recordTarget());
    // Only the track's own input and channel play it and go into the take.
    if (t != nullptr)
        devices.setPlayFilter (t->inputPort, t->inputChannel);
    else
        devices.setPlayFilter ({}, 0);
    if (t == nullptr || t->output.kind == TrackOutput::Kind::none)
        engine.setThru (0, 0);
    else
        engine.setThru (portFor (*t), t->output.channel);
}

// ---- transport -------------------------------------------------------------------------
void Session::togglePlay()
{
    if (engine.isPlaying())
        stop();
    else
        engine.play();
}

void Session::stop()
{
    if (engine.isPlaying())
        engine.stop();
    else
        engine.locate (0); // second stop: back to the start
    if (recordingOn)
    {
        recordingOn = false;
        engine.setRecording (false);
        finishTake();
    }
    sendSynchronousChangeMessage();
}

void Session::locate (Tick t)
{
    engine.locate (std::max<Tick> (0, t));
}

void Session::toggleRecord()
{
    if (recordingOn)
    {
        recordingOn = false;
        engine.setRecording (false);
        finishTake();
    }
    else
    {
        takeTrack = recordTarget();
        if (takeTrack == 0)
        {
            if (onError)
                onError ("Add a track to record on first.");
            return;
        }
        take.clear();
        const auto* target = edit::findTrack (song(), takeTrack);
        audioTake = target != nullptr && target->output.kind == TrackOutput::Kind::audio;
        if (audioTake)
        {
            const auto status = devices.audioStatus();
            if (status.inputs <= 0)
            {
                audioTake = false;
                if (onError)
                    onError ("No audio input is open. Open Audio and MIDI settings and choose an input.");
                return;
            }
            takeLeft.clear();
            takeRight.clear();
            takePeaks.clear();
            engine.capturedAudio().pop (takeLeft, takeRight); // old audio that nobody took
            takeLeft.clear();
            takeRight.clear();
            takeRate = status.sampleRate;
            takeLatency = (status.outputLatencyMs + status.inputLatencyMs) / 1000.0;
            if (const auto racks = engine.racks(); racks != nullptr && status.sampleRate > 0.0)
                takeLatency += racks->totalLatency() / status.sampleRate; // plugins make the song sound later still
            takeStereo = status.inputs > 1;
            engine.setAudioCapture (true);
        }
        recordingOn = true;
        engine.setRecording (true);
        if (! engine.isPlaying())
            engine.play();
    }
    sendSynchronousChangeMessage();
}

void Session::drainAudioTake()
{
    const auto before = takeLeft.size();
    if (engine.capturedAudio().pop (takeLeft, takeRight) == 0)
        return;
    // Peaks for the picture of the take, over whole groups of frames only.
    const auto step = static_cast<std::size_t> (kTakePeakFrames);
    for (std::size_t f = (before / step) * step; f + step <= takeLeft.size(); f += step)
    {
        float peak = 0.0f;
        for (std::size_t i = f; i < f + step; ++i)
            peak = std::max ({ peak, std::abs (takeLeft[i]), std::abs (takeRight[i]) });
        if (f / step < takePeaks.size())
            takePeaks[f / step] = peak;
        else
            takePeaks.push_back (peak);
    }
}

void Session::finishAudioTake()
{
    const double startTick = engine.captureStartTicks();
    engine.setAudioCapture (false);
    drainAudioTake();
    audioTake = false;
    const auto lost = engine.capturedAudio().takeLost();
    const auto* track = edit::findTrack (song(), takeTrack);
    const auto skip = static_cast<std::size_t> (std::max (0.0, takeLatency * takeRate));
    if (track == nullptr || startTick < 0.0 || takeLeft.size() <= skip + static_cast<std::size_t> (takeRate * 0.05))
    {
        takeLeft.clear();
        takeRight.clear();
        takePeaks.clear();
        return; // less than a moment of audio: nothing to keep
    }

    // The file goes next to the project, or into the Music folder when the project has no file yet.
    const auto folder = currentFile != juce::File()
        ? currentFile.getSiblingFile (currentFile.getFileNameWithoutExtension() + " Audio")
        : juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arranger MIDI DAW").getChildFile ("Recordings");
    const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (track->name.c_str()) + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"));
    const auto file = folder.getNonexistentChildFile (name, ".wav", false);

    // What you played against the song comes back late by the latency of the device: leave that part out.
    Sample sample;
    sample.name = file.getFileNameWithoutExtension().toStdString();
    sample.sampleRate = takeRate;
    sample.channels.emplace_back (takeLeft.begin() + static_cast<std::ptrdiff_t> (skip), takeLeft.end());
    if (takeStereo)
        sample.channels.emplace_back (takeRight.begin() + static_cast<std::ptrdiff_t> (skip), takeRight.end());
    takeLeft.clear();
    takeRight.clear();
    takePeaks.clear();
    try
    {
        if (! folder.createDirectory())
            throw wav::Error ("can't make the folder " + folder.getFullPathName().toStdString());
        wav::write (sample, file.getFullPathName().toStdString(), 24);
    }
    catch (const std::exception& e)
    {
        if (onError)
            onError ("Can't save the recording: " + juce::String::fromUTF8 (e.what()));
        return;
    }

    const double seconds = sample.seconds();
    const auto trackId = takeTrack;
    ClipId added = 0;
    edit ("Record Audio", [&] (Song& s) {
        AudioClip clip;
        clip.name = sample.name;
        clip.file = file.getFullPathName().toStdString();
        clip.start = std::max<Tick> (0, static_cast<Tick> (std::llround (startTick)));
        const double startSeconds = s.tempo.secondsAt (static_cast<double> (clip.start));
        clip.length = std::max<Tick> (1, static_cast<Tick> (std::llround (s.tempo.tickAtSeconds (startSeconds + seconds))) - clip.start);
        if (const auto* t = edit::findTrack (s, trackId))
            clip.layer = edit::layerAbove (*t, clip.start, clip.end()); // a new take goes on top
        added = edit::addAudioClip (s, trackId, std::move (clip));
    });
    selectClips ({ added });
    if (lost > 0 && onError)
        onError ("The computer was too busy and some of the recording is missing.");
}

void Session::finishTake()
{
    if (audioTake)
    {
        while (engine.popRecorded()) {} // MIDI played meanwhile does not go onto an audio track
        take.clear();
        finishAudioTake();
        return;
    }
    // Drain what the engine still holds.
    while (auto e = engine.popRecorded())
        take.push_back (*e);
    if (take.empty() || edit::findTrack (song(), takeTrack) == nullptr)
    {
        take.clear();
        return;
    }

    // Pair note-ons with note-offs.
    std::array<std::vector<std::pair<double, std::uint8_t>>, 128> open;
    std::vector<Note> notes;
    std::vector<ControlEvent> controls;
    double first = take.front().tick, last = first;
    for (const auto& e : take)
    {
        first = std::min (first, e.tick);
        last = std::max (last, e.tick);
        const int kind = e.bytes[0] & 0xf0;
        const auto d1 = e.bytes[1];
        const auto d2 = e.bytes[2];
        if (kind == 0x90 && d2 > 0)
            open[d1].push_back ({ e.tick, d2 });
        else if ((kind == 0x80 || kind == 0x90) && ! open[d1].empty())
        {
            const auto [start, vel] = open[d1].front();
            open[d1].erase (open[d1].begin());
            notes.push_back ({ static_cast<Tick> (std::llround (start)), std::max<Tick> (1, static_cast<Tick> (std::llround (e.tick - start))), d1, vel });
        }
        else if (kind == 0xb0)
            controls.push_back ({ static_cast<Tick> (std::llround (e.tick)), ControlEvent::Kind::controller, d1, d2 });
        else if (kind == 0xe0)
            controls.push_back ({ static_cast<Tick> (std::llround (e.tick)), ControlEvent::Kind::pitchBend, 0, ((d2 << 7) | d1) - 8192 });
        else if (kind == 0xd0)
            controls.push_back ({ static_cast<Tick> (std::llround (e.tick)), ControlEvent::Kind::channelPressure, 0, d1 });
    }
    // Keys still held when recording stopped end there.
    for (int p = 0; p < 128; ++p)
        for (const auto& [start, vel] : open[static_cast<std::size_t> (p)])
            notes.push_back ({ static_cast<Tick> (std::llround (start)), std::max<Tick> (1, static_cast<Tick> (std::llround (last - start))),
                               static_cast<std::uint8_t> (p), vel });
    take.clear();
    if (notes.empty() && controls.empty())
        return;

    // The track plays a Clip grid slot: add the take to that slot, wrapped to its loop.
    if (const auto status = engine.slotStatus (takeTrack); status.playing >= 0)
    {
        const auto* t = edit::findTrack (song(), takeTrack);
        const auto scene = static_cast<std::size_t> (status.playing);
        if (scene < t->slots.size() && t->slots[scene] != nullptr && t->slots[scene]->length > 0)
        {
            const auto slot = t->slots[scene];
            const auto wrap = [&] (Tick at) {
                const auto phase = std::fmod (static_cast<double> (at) - status.launchTick, static_cast<double> (slot->length));
                return static_cast<Tick> (std::llround (phase < 0.0 ? phase + static_cast<double> (slot->length) : phase)) % slot->length;
            };
            Clip part;
            part.length = slot->length;
            for (auto n : notes)
            {
                n.start = wrap (n.start);
                n.length = std::min (n.length, slot->length - n.start);
                part.notes.push_back (n);
            }
            for (auto c : controls)
            {
                c.at = wrap (c.at);
                part.controls.push_back (c);
            }
            if (quantiseOnRecord)
            {
                arranger::quantise (part, quantise);
                for (auto& n : part.notes)
                    n.start %= slot->length;
            }
            edit ("Record into Slot", [&] (Song& s) {
                edit::modifyClip (s, slot->id, [&] (Clip& c) {
                    c.notes.insert (c.notes.end(), part.notes.begin(), part.notes.end());
                    c.controls.insert (c.controls.end(), part.controls.begin(), part.controls.end());
                    edit::normalise (c);
                    edit::removeOverlaps (c);
                });
            });
            sendSynchronousChangeMessage();
            return;
        }
    }

    const Tick bar = song().timeSignature.ticksPerBar();
    const Tick clipStart = (static_cast<Tick> (std::floor (first)) / bar) * bar;
    const Tick clipEnd = ((static_cast<Tick> (std::ceil (last)) + bar - 1) / bar) * bar;

    Clip clip;
    clip.start = std::max<Tick> (0, clipStart);
    clip.length = std::max (bar, clipEnd - clip.start);
    for (auto n : notes)
    {
        n.start -= clip.start;
        clip.notes.push_back (n);
    }
    for (auto c : controls)
    {
        c.at -= clip.start;
        clip.controls.push_back (c);
    }
    edit::removeOverlaps (clip);
    if (quantiseOnRecord)
        arranger::quantise (clip, quantise);

    const auto track = takeTrack;
    ClipId added = 0;
    edit ("Record", [&] (Song& s) { added = edit::addClip (s, track, clip); });
    selClips = { added };
    sendSynchronousChangeMessage();
}

// ---- arranger style ----------------------------------------------------------------------
void Session::setChord (Chord c)
{
    if (c == currentChord)
        return;
    currentChord = c;
    if (song().style)
        publish();
    sendChangeMessage();
}

// ---- clip grid ---------------------------------------------------------------------------
void Session::launchSlot (TrackId track, int scene)
{
    engine.launchSlot (track, scene, song().launchQuantise);
}

void Session::stopSlot (TrackId track)
{
    engine.stopSlot (track, song().launchQuantise);
}

void Session::launchScene (int scene)
{
    engine.launchScene (scene, song().launchQuantise);
}

void Session::stopAllSlots()
{
    engine.stopAllSlots (song().launchQuantise);
}

void Session::setJamRecording (bool on)
{
    if (on == jamOn)
        return;
    if (! on)
    {
        finishJam();
        return;
    }
    jamOn = true;
    jamOpen.clear();
    jamDone.clear();

    // Slots that play already are part of the jam, from the start of their current loop.
    const double now = engine.positionTicks();
    for (const auto& t : song().tracks)
    {
        const auto status = engine.slotStatus (t.id);
        const auto scene = static_cast<std::size_t> (status.playing);
        if (status.playing < 0 || scene >= t.slots.size() || t.slots[scene] == nullptr || t.slots[scene]->length <= 0)
            continue;
        const auto length = static_cast<double> (t.slots[scene]->length);
        const double from = status.launchTick + std::floor ((now - status.launchTick) / length) * length;
        jamOpen[t.id] = { t.id, status.playing, std::max (0.0, from), 0.0 };
    }
    sendSynchronousChangeMessage();
}

void Session::pollSlots()
{
    bool changed = false;
    while (const auto change = engine.popSlotChange())
    {
        changed = true;
        if (! jamOn)
            continue;
        if (const auto it = jamOpen.find (change->track); it != jamOpen.end())
        {
            it->second.to = change->tick;
            jamDone.push_back (it->second);
            jamOpen.erase (it);
        }
        if (change->scene >= 0)
            jamOpen[change->track] = { change->track, change->scene, change->tick, 0.0 };
    }
    if (changed)
        sendChangeMessage();
}

void Session::finishJam()
{
    if (! jamOn)
        return;
    pollSlots();
    jamOn = false;
    const double now = engine.positionTicks();
    for (auto& [id, span] : jamOpen)
    {
        span.to = now;
        jamDone.push_back (span);
    }
    jamOpen.clear();

    // One Timeline clip for each stretch a slot played. It replaces what was there.
    std::vector<std::pair<TrackId, Clip>> made;
    std::vector<std::pair<TrackId, AudioClip>> madeAudio;
    for (const auto& span : jamDone)
    {
        const auto* t = edit::findTrack (song(), span.track);
        const auto scene = static_cast<std::size_t> (span.scene);
        if (t != nullptr && scene < t->audioSlots.size() && t->audioSlots[scene] != nullptr)
        {
            // An audio loop: one clip for each time it played, because a Timeline audio clip does not loop.
            const auto& loop = *t->audioSlots[scene];
            const auto from = static_cast<Tick> (std::llround (span.from)), to = static_cast<Tick> (std::llround (span.to));
            for (Tick at = from; to - at >= kTicksPerQuarter / 8; at += loop.length)
            {
                AudioClip c = loop;
                c.id = 0;
                c.start = at;
                c.length = std::min (loop.length, to - at);
                madeAudio.push_back ({ span.track, std::move (c) });
            }
            continue;
        }
        if (t == nullptr || scene >= t->slots.size() || t->slots[scene] == nullptr)
            continue;
        Clip c = *t->slots[scene];
        c.id = 0;
        c.start = static_cast<Tick> (std::llround (span.from));
        const auto length = static_cast<Tick> (std::llround (span.to)) - c.start;
        if (length < kTicksPerQuarter / 8)
            continue; // the song loop jumped back, or it played for a moment only
        c.loopLength = length > c.length ? c.length : 0;
        c.length = length;
        made.push_back ({ span.track, std::move (c) });
    }
    jamDone.clear();

    if (! made.empty() || ! madeAudio.empty())
    {
        edit ("Record Clip Grid", [&] (Song& s) {
            for (auto& [track, clip] : made)
            {
                edit::clearRange (s, track, clip.start, clip.end());
                edit::addClip (s, track, clip);
            }
            for (auto& [track, clip] : madeAudio)
            {
                if (const auto* t = edit::findTrack (s, track))
                    clip.layer = edit::layerAbove (*t, clip.start, clip.end());
                edit::addAudioClip (s, track, clip);
            }
        });
        // The Timeline now holds the jam: let it play.
        engine.stopAllSlots (0);
    }
    sendSynchronousChangeMessage();
}

// ---- sound -------------------------------------------------------------------------------
void Session::loadSoundFont (const juce::File& f)
{
    if (loader != nullptr)
        loader->stopThread (10000);

    fontStatus = "Loading " + f.getFileName() + "...";
    sendChangeMessage();

    const auto rate = engine.sampleRate() > 0 ? engine.sampleRate() : 48000.0;
    const auto path = f.getFullPathName().toStdString();

    struct Loader final : juce::Thread
    {
        Loader (Session& s, std::string p, double r) : juce::Thread ("SoundFont loader"), session (s), path (std::move (p)), rate (r) {}
        void run() override
        {
            std::shared_ptr<SoundFontPlayer> player;
            juce::String error;
            try
            {
                player = std::make_shared<SoundFontPlayer> (path, rate, 2048);
            }
            catch (const std::exception& e)
            {
                error = juce::String::fromUTF8 (e.what());
            }
            juce::MessageManager::callAsync ([s = &session, player, error, p = path] {
                if (player != nullptr)
                {
                    s->font = player;
                    s->engine.setInstrument (player);
                    s->fontStatus = juce::String::fromUTF8 (player->name().c_str()) + " (" + juce::String (player->presets().size()) + " sounds)";
                    if (s->song().soundFont != p)
                        s->edit ("Choose SoundFont", [p] (Song& song) { song.soundFont = p; });
                    // Re-send every SoundFont track's sound to the new player.
                    for (const auto& t : s->song().tracks)
                        if (t.output.kind == TrackOutput::Kind::soundFont)
                            for (const auto& m : trackSetupMessages (t))
                                s->engine.sendMidi (MidiEvent::kSoundFontPort, m.bytes[0], m.bytes[1], m.bytes[2]);
                }
                else
                {
                    s->fontStatus = "SoundFont failed to load";
                    if (s->onError)
                        s->onError (error);
                }
                s->sendChangeMessage();
            });
        }
        Session& session;
        std::string path;
        double rate;
    };

    loader = std::make_unique<Loader> (*this, path, rate);
    loader->startThread();
}

void Session::previewNote (TrackId id, int pitch, int velocity, int ms)
{
    const auto* t = edit::findTrack (song(), id);
    if (t == nullptr || t->output.kind == TrackOutput::Kind::none)
        return;
    const auto port = portFor (*t);
    const auto ch = static_cast<std::uint8_t> (t->output.channel - 1);
    engine.sendMidi (port, static_cast<std::uint8_t> (0x90 | ch), static_cast<std::uint8_t> (pitch & 0x7f), static_cast<std::uint8_t> (velocity & 0x7f));
    previewOffs.push_back ({ juce::Time::getMillisecondCounterHiRes() + ms, port, static_cast<std::uint8_t> (0x80 | ch), static_cast<std::uint8_t> (pitch & 0x7f) });
}

// ---- control surfaces ----------------------------------------------------------------
void Session::setControllers (std::vector<ControllerProfile> list)
{
    controllers = std::move (list);
    surfaces.clear();
    refreshControlFilters (true);
    controllerPollCountdown = 0;
}

const ControllerProfile* Session::controllerFor (const std::string& port) const
{
    if (const auto it = controllerChoice.find (port); it != controllerChoice.end())
    {
        for (const auto& c : controllers)
            if (c.id == it->second)
                return &c;
        if (it->second == "none")
            return nullptr;
    }
    for (const auto& c : controllers)
        if (c.matchesPort (port))
            return &c;
    return nullptr;
}

void Session::refreshControlFilters (bool force)
{
    // One filter per plugged-in input that is a controller, so that a page chosen by hand counts too.
    std::vector<std::string> names;
    for (const auto& in : devices.midiInputs())
        names.push_back (in.name);
    if (! force && names == filteredInputs)
        return;
    filteredInputs = names;
    std::vector<platform::ControlFilter> filters;
    for (const auto& name : names)
        if (const auto* c = controllerFor (name))
            filters.push_back ({ juce::String (name).toLowerCase().toStdString(), c->channel });
    devices.setControlFilters (std::move (filters));
}

void Session::assignDeviceProfile (const std::string& port, const std::string& profileId)
{
    if (profileId.empty())
        deviceChoice.erase (port);
    else
        deviceChoice[port] = profileId;
    library.assign (port, profileId);
    publish(); // the mixer controllers of the port may change
    if (onPortSetupChanged)
        onPortSetupChanged();
    sendChangeMessage();
}

void Session::assignControllerPage (const std::string& port, const std::string& pageId)
{
    if (pageId.empty())
        controllerChoice.erase (port);
    else
        controllerChoice[port] = pageId;
    surfaces.erase (port);
    surfaceOutputs.erase (port); // greet it again with its new page
    padsShown.erase (port);
    refreshControlFilters (true);
    controllerPollCountdown = 0;
    if (onPortSetupChanged)
        onPortSetupChanged();
    sendChangeMessage();
}

juce::StringArray Session::connectedControllers() const
{
    juce::StringArray names;
    for (const auto& [port, surface] : surfaces)
        names.addIfNotAlreadyThere (juce::String::fromUTF8 (surface.profile().name.c_str()));
    for (const auto& [port, id] : surfaceOutputs)
        for (const auto& c : controllers)
            if (c.id == id)
                names.addIfNotAlreadyThere (juce::String::fromUTF8 (c.name.c_str()));
    return names;
}

std::vector<Session::LoggedMidi> Session::recentMidi (double seconds) const
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    std::vector<LoggedMidi> out;
    for (const auto& m : midiLog)
        if (now - m.ms <= seconds * 1000.0)
            out.push_back (m);
    return out;
}

std::vector<platform::IncomingMidi> Session::takeMonitor()
{
    std::vector<platform::IncomingMidi> taken;
    taken.swap (monitor);
    return taken;
}

void Session::pollControllers()
{
    // Greet controllers that were just plugged in; forget ones that left.
    std::set<std::string> present;
    for (const auto& out : devices.midiOutputs())
    {
        present.insert (out.name);
        if (surfaceOutputs.count (out.name) != 0)
            continue;
        if (const auto* c = controllerFor (out.name))
        {
            for (const auto& m : c->connect)
                devices.sendNow (out.name, m);
            surfaceOutputs[out.name] = c->id;
            sendChangeMessage();
        }
    }
    refreshControlFilters (false);
    std::erase_if (surfaceOutputs, [&] (const auto& entry) { return present.count (entry.first) == 0; });
    std::erase_if (padsShown, [&] (const auto& entry) { return present.count (entry.first) == 0; });
}

void Session::setSurfaceScene (int scene)
{
    padScene = std::clamp (scene, 0, std::max (0, song().sceneCount() - 1));
    sendChangeMessage();
}

int Session::surfacePadRows() const
{
    int rows = 0;
    for (const auto& [port, id] : surfaceOutputs)
        for (const auto& c : controllers)
            if (c.id == id)
                rows = std::max (rows, c.padRows());
    return rows;
}

void Session::updatePadLights()
{
    for (const auto& [port, id] : surfaceOutputs)
        for (const auto& c : controllers)
        {
            if (c.id != id || c.padRows() == 0)
                continue;
            const auto lights = padLights (c, song(), bankOffset, padScene, [this] (int index) {
                return engine.slotStatus (song().tracks[static_cast<std::size_t> (index)].id);
            });
            auto& shown = padsShown[port];
            for (std::size_t i = 0; i < lights.size(); ++i)
            {
                if (i < shown.size() && shown[i] == lights[i])
                    continue;
                const auto& l = lights[i];
                // Back to a steady light first, so a pad that flashed or pulsed stops.
                if (i < shown.size() && shown[i].channel != l.channel)
                    devices.sendNow (port, { static_cast<std::uint8_t> (0x90 | (c.pads.channel - 1)), static_cast<std::uint8_t> (l.note), 0 });
                devices.sendNow (port, { static_cast<std::uint8_t> (0x90 | (l.channel - 1)), static_cast<std::uint8_t> (l.note), static_cast<std::uint8_t> (l.colour) });
            }
            shown = lights;
        }
}

void Session::pollInput()
{
    for (auto& m : devices.takeIncomingMidi())
    {
        const auto* page = controllerFor (m.port);
        const bool owned = page != nullptr && page->owns (m.bytes[0]);
        if (m.size >= 2 && owned)
        {
            auto it = surfaces.find (m.port);
            if (it == surfaces.end() || it->second.profile().id != page->id)
                it = surfaces.insert_or_assign (m.port, ControllerSurface (*page)).first;
            if (const auto event = it->second.handle (m.bytes))
                applySurface (*event, page->strips);
        }
        // While the song is a style, the keys held below the split name the chord.
        if (chordFromKeys && song().style && m.size >= 3)
        {
            const int kind = m.bytes[0] & 0xf0;
            if (! owned && (kind == 0x90 || kind == 0x80) && m.bytes[1] < chordSplit)
            {
                if (kind == 0x90 && m.bytes[2] > 0)
                {
                    chordKeys.insert (m.bytes[1]);
                    const std::vector<int> held (chordKeys.begin(), chordKeys.end());
                    if (const auto c = style_chord::detect (held)) // the chord stays when the keys go up
                        setChord (*c);
                }
                else
                    chordKeys.erase (m.bytes[1]);
            }
        }
        if (onStepNote && m.size >= 3 && ((m.bytes[0] & 0xf0) == 0x90 || (m.bytes[0] & 0xf0) == 0x80))
        {
            if (! owned)
                onStepNote (m.bytes[1], (m.bytes[0] & 0xf0) == 0x90 ? m.bytes[2] : 0);
        }
        midiLog.push_back ({ juce::Time::getMillisecondCounterHiRes(), m });
        while (midiLog.size() > 2000 || (! midiLog.empty() && midiLog.back().ms - midiLog.front().ms > 120000.0))
            midiLog.pop_front();
        if (monitor.size() < 4096)
            monitor.push_back (std::move (m));
    }
}

void Session::applySurface (const SurfaceEvent& e, int strips)
{
    const auto& tracks = song().tracks;
    const int count = static_cast<int> (tracks.size());
    const int index = bankOffset + e.strip;
    const TrackId id = index >= 0 && index < count ? tracks[static_cast<std::size_t> (index)].id : 0;
    const auto key = [&] (const char* what) { return std::string ("surface-") + what + "-" + std::to_string (id); };
    const auto onTrack = [&] (const char* name, const char* mergeKey, const std::function<void (Track&)>& change) {
        if (id == 0)
            return;
        edit (name, [&] (Song& s) {
            if (auto* t = edit::findTrack (s, id))
                change (*t);
        }, mergeKey != nullptr ? key (mergeKey) : std::string());
    };
    const int v = std::clamp (e.value, 0, 127);

    switch (e.action)
    {
        case ControlAction::volume: onTrack ("Change Volume", "volume", [v] (Track& t) { t.mixer.volume = v; }); break;
        case ControlAction::pan: onTrack ("Change Pan", "pan", [v] (Track& t) { t.mixer.pan = v; }); break;
        case ControlAction::reverb: onTrack ("Change Reverb", "reverb", [v] (Track& t) { t.mixer.reverbSend = v; }); break;
        case ControlAction::chorus: onTrack ("Change Chorus", "chorus", [v] (Track& t) { t.mixer.chorusSend = v; }); break;
        case ControlAction::panCentre: onTrack ("Centre Pan", nullptr, [] (Track& t) { t.mixer.pan = 64; }); break;
        case ControlAction::mute: onTrack ("Mute", nullptr, [] (Track& t) { t.mixer.mute = ! t.mixer.mute; }); break;
        case ControlAction::solo: onTrack ("Solo", nullptr, [] (Track& t) { t.mixer.solo = ! t.mixer.solo; }); break;
        case ControlAction::arm:
            if (id != 0)
                edit ("Record Arm", [id] (Song& s) {
                    for (auto& t : s.tracks)
                        t.armed = t.id == id ? ! t.armed : false;
                });
            break;
        case ControlAction::select:
            if (id != 0)
                selectTrack (id);
            break;
        case ControlAction::masterVolume:
        {
            // Fader top = 0 dB, so a full push never clips.
            const float db = v == 0 ? -60.0f : std::max (-60.0f, 40.0f * std::log10 (static_cast<float> (v) / 127.0f));
            edit ("Master Level", [db] (Song& s) { s.master.gainDb = std::round (db * 10.0f) / 10.0f; }, "surface-master");
            break;
        }
        case ControlAction::play: togglePlay(); break;
        case ControlAction::stop: stop(); break;
        case ControlAction::record: toggleRecord(); break;
        case ControlAction::loop: edit ("Loop", [] (Song& s) { s.loop.enabled = ! s.loop.enabled; }); break;
        case ControlAction::bankLeft:
        case ControlAction::bankRight:
        {
            const int step = e.action == ControlAction::bankLeft ? -strips : strips;
            const int last = std::max (0, ((count - 1) / strips) * strips);
            bankOffset = std::clamp (bankOffset + step, 0, last);
            sendChangeMessage();
            break;
        }
        case ControlAction::launchClip:
        {
            // A pad with a clip starts it; an empty pad stops the track.
            const int scene = padScene + e.row;
            const auto* t = id != 0 ? edit::findTrack (song(), id) : nullptr;
            if (t == nullptr)
                break;
            if (static_cast<std::size_t> (scene) < t->slots.size() && t->slots[static_cast<std::size_t> (scene)] != nullptr)
                launchSlot (id, scene);
            else
                stopSlot (id);
            break;
        }
        case ControlAction::launchScene: launchScene (padScene); break;
        case ControlAction::stopClips: stopAllSlots(); break;
        case ControlAction::sceneUp: setSurfaceScene (padScene - 1); break;
        case ControlAction::sceneDown: setSurfaceScene (padScene + 1); break;
        case ControlAction::none: break;
    }
}

void Session::timerCallback()
{
    engine.collectGarbage();
    pollInput();
    pollSlots();
    if (! padsShown.empty() || ! surfaceOutputs.empty())
        updatePadLights();
    if (--controllerPollCountdown <= 0)
    {
        controllerPollCountdown = 60; // every 2 seconds
        if (! controllers.empty())
            pollControllers();
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    for (auto it = previewOffs.begin(); it != previewOffs.end();)
    {
        if (it->dueMs <= now)
        {
            engine.sendMidi (it->port, it->status, it->pitch, 0);
            it = previewOffs.erase (it);
        }
        else
            ++it;
    }

    if (recordingOn && audioTake)
    {
        drainAudioTake();
        sendChangeMessage();
    }
    if (recordingOn)
    {
        bool got = false;
        while (auto e = engine.popRecorded())
        {
            take.push_back (*e);
            got = true;
        }
        if (got)
            sendChangeMessage();
    }

    // Transport stopped by itself (or from elsewhere) while recording: keep the take.
    const bool playing = engine.isPlaying();
    if (wasPlaying && ! playing && recordingOn)
    {
        recordingOn = false;
        engine.setRecording (false);
        finishTake();
    }
    if (wasPlaying && ! playing && jamOn)
        finishJam();
    wasPlaying = playing;
}

} // namespace arranger
