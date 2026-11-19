#pragma once

// A song that uses every part of the song model, for round-trip and undo tests.
// When the model gets a new field, set it here too: the round-trip test then
// checks that project files keep it.

#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"

#include <filesystem>
#include <string>

namespace arranger::test
{

inline std::filesystem::path examplesFolder()
{
    return std::filesystem::path (ARRANGER_EXAMPLES_DIR);
}

inline Song kitchenSinkSong()
{
    // Start from a style, so the song has style data and a Clip grid.
    Song s = style_convert::toSong (style_file::load (examplesFolder() / "demo-style.sty"));
    s.title = "Kitchen Sink \xc3\xa9\xe2\x99\xab";
    s.tempo.setPoints ({ { 0, 97.5 }, { 3840 * 4, 140.0 }, { 3840 * 9, 61.25 } });
    s.timeSignature = { 7, 8 };
    s.key = "F# minor";
    s.guitarChords = true;
    s.metronome = { false, 0.35f, false };
    s.loop = { true, 960, 3840 * 3 };
    s.launchQuantise = 960;
    s.soundFont = "/Users/me/Sound Fonts/GeneralUser GS.sf2";
    s.scenes = { "Intro", "", "Verse \xe2\x80\x93 A" };

    // A SoundFont track with notes, controls, a loop, marks and automation.
    auto& keys = edit::addTrack (s, "Sink Keys");
    const auto keysId = keys.id;
    keys.colour = "violet";
    keys.sound = { "Piano > Grand", 0, 112, 1 };
    keys.mixer = { 87, 30, 12, 64, true, false };
    keys.inputPort = "Launchkey Mini MK3 MIDI Port";
    keys.inputChannel = 3;
    Clip c;
    c.name = "Riff";
    c.start = 3840;
    c.length = 3840 * 4;
    c.loopLength = 3840;
    c.notes = { { 0, 480, 60, 100 }, { 0, 480, 64, 90 }, { 480, 1, 127, 1 }, { 960, 2880, 0, 127 } };
    c.controls = { { 0, ControlEvent::Kind::controller, 64, 127 }, { 10, ControlEvent::Kind::pitchBend, 0, -8192 },
                   { 20, ControlEvent::Kind::pitchBend, 0, 8191 }, { 30, ControlEvent::Kind::program, 0, 5 },
                   { 40, ControlEvent::Kind::channelPressure, 0, 77 } };
    edit::addClip (s, keysId, c);
    Clip second;
    second.start = 3840 * 6;
    second.length = 3840;
    second.notes = { { 0, 960, 72, 64 } };
    edit::addClip (s, keysId, second);
    edit::setMark (s, keysId, { ScoreMark::Kind::chord, 3840, "F#m7b5/C" });
    edit::setMark (s, keysId, { ScoreMark::Kind::lyric, 3840, "Twin-" });
    edit::setMark (s, keysId, { ScoreMark::Kind::dynamic, 4320, "mf" });
    edit::setMark (s, keysId, { ScoreMark::Kind::articulation, 4320, "staccato" });
    edit::setAutomationPoint (s, keysId, 0, { 0, 100.0f });
    edit::setAutomationPoint (s, keysId, 0, { 3840, 20.5f });
    edit::setAutomationPoint (s, keysId, 1, { 960, 0.0f });
    edit::setAutomationPoint (s, keysId, 2, { 960, 127.0f });
    edit::setAutomationPoint (s, keysId, 3, { 1920, 64.0f });

    // A MIDI port track, armed.
    auto& psr = edit::addTrack (s, "PSR Strings");
    psr.output = { TrackOutput::Kind::midiPort, "Digital Keyboard-1", 16 };
    psr.sound = { "Strings > Orchestra", 0, 117, 48 };
    psr.armed = true;

    // A sampler track with a kit.
    auto& drums = edit::addTrack (s, "Kit");
    const auto drumsId = drums.id;
    drums.output = { TrackOutput::Kind::sampler, "", 10 };
    drums.kit.name = "My Kit";
    drums.kit.pads = { { "Kick", 36, "/samples/kick.wav", 0, 0, -1.5f, 0.0f, 0.0f, 0, true },
                       { "Hat", 42, "/samples/loop.wav", 1000, 5000, 0.0f, -0.5f, 2.0f, 1, false },
                       { "Open", 46, "/samples/loop.wav", 5000, 0, 0.0f, 0.5f, -12.0f, 1, true } };
    edit::addTimelineNote (s, drumsId, 0, 120, 36, 127);

    // A plugin track with an instrument, effects, automation of a control and sends.
    auto& synth = edit::addTrack (s, "Sink Synth");
    const auto synthId = synth.id;
    synth.output = { TrackOutput::Kind::plugin, "", 1 };
    synth.instrument = { "VST3", "ABCDEF0123456789", "Test Tone", "c3RhdGU=", false, {} };
    synth.effects = { { "VST3", "1111", "Test Gain", "", true, {} }, { "Internal", "reverb", "Reverb", "AAEC", false, {} } };
    const auto lane = edit::addControlLine (s, synthId, 0, 3, "Gain");
    edit::setAutomationPoint (s, synthId, lane, { 0, 10.0f });
    edit::setAutomationPoint (s, synthId, lane, { 1920, 110.0f });
    edit::addControlLine (s, synthId, -1, 0, "Pitch");

    // An audio track with takes, fades, a stretched loop and an audio slot.
    auto& audio = edit::addAudioTrack (s, "Sink Vocals");
    const auto audioId = audio.id;
    audio.audioInput = 4;
    audio.audioInputChannels = 2;
    for (int take = 0; take < 2; ++take)
    {
        AudioClip a;
        a.name = "Take " + std::to_string (take + 1);
        a.file = "/recordings/vocal " + std::to_string (take + 1) + ".wav";
        a.start = 3840 + take * 960;
        a.length = 3840 * 2;
        a.offset = 0.25 * take;
        a.gainDb = -2.5f;
        a.fadeIn = 120;
        a.fadeOut = 240;
        a.layer = take;
        edit::addAudioClip (s, audioId, a);
    }
    AudioClip loop;
    loop.name = "Loop";
    loop.file = "/loops/drums 120.wav";
    loop.length = 3840;
    loop.bpm = 120.0;
    edit::setAudioSlot (s, audioId, 2, loop);

    // Buses: a group and a return with an effect.
    const auto group = edit::addBus (s, "Group").id;
    auto& ret = edit::addBus (s, "Reverb Return");
    ret.colour = "amber";
    ret.volume = 90;
    ret.pan = 20;
    ret.mute = true;
    ret.effects = { { "Internal", "reverb", "Reverb", "", false, {} } };
    const auto retId = ret.id;
    edit::setTrackBus (s, synthId, group);
    edit::setSend (s, synthId, retId, 77);
    edit::setSend (s, keysId, retId, 30);

    // The master.
    s.master.gainDb = -4.5f;
    s.master.effects = { { "VST3", "2222", "Limiter", "bGltaXQ=", false, {} } };

    // A slot clip on the keys track.
    Clip slot;
    slot.length = 3840 * 2;
    slot.notes = { { 0, 960, 67, 80 } };
    edit::setSlot (s, keysId, 1, slot);
    return s;
}

} // namespace arranger::test
