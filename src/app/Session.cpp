#include "Session.h"

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
        if (old != nullptr && old->mixer == t.mixer && old->sound == t.sound && old->output == t.output)
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
    auto snap = std::make_shared<const PlaybackSnapshot> (
        buildSnapshot (document.song(), [this] (const std::string& port) { return library.mixerFor (port); }));
    devices.setOutputPorts (snap->ports);
    engine.setSnapshot (snap);
    snapshot = std::move (snap);
}

std::uint16_t Session::portFor (const Track& t) const
{
    if (t.output.kind == TrackOutput::Kind::soundFont)
        return MidiEvent::kSoundFontPort;
    if (t.output.kind == TrackOutput::Kind::none || snapshot == nullptr)
        return MidiEvent::kNoPort;
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

void Session::updateThru()
{
    // Play what you hear: live keys go to the track you're about to record on.
    const auto* t = edit::findTrack (song(), recordTarget());
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
        recordingOn = true;
        engine.setRecording (true);
        if (! engine.isPlaying())
            engine.play();
    }
    sendSynchronousChangeMessage();
}

void Session::finishTake()
{
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

void Session::timerCallback()
{
    engine.collectGarbage();

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
    wasPlaying = playing;
}

} // namespace arranger
