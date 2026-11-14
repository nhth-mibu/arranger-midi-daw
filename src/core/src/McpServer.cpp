#include "arranger/core/McpServer.h"

#include "arranger/core/ChordFinder.h"
#include "arranger/core/Clipboard.h"
#include "arranger/core/GeneralMidi.h"
#include "arranger/core/Notation.h"
#include "arranger/core/Quantise.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/Style.h"
#include "arranger/core/StyleChord.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>

namespace arranger::mcp
{

using nlohmann::json;

namespace
{
    constexpr const char* kProtocolVersion = "2025-06-18";
    constexpr std::size_t kMostNotes = 4000;

    struct ToolError : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    // ---- time --------------------------------------------------------------------
    Tick beatTicks (const Song& s) { return s.timeSignature.ticksPerBeat(); }
    Tick barTicks (const Song& s) { return s.timeSignature.ticksPerBar(); }

    Tick tickAt (const Song& s, double bar, double beat)
    {
        if (bar < 1.0 || beat < 1.0)
            throw ToolError ("bar and beat start at 1");
        return static_cast<Tick> (std::llround ((bar - 1.0) * static_cast<double> (barTicks (s)) + (beat - 1.0) * static_cast<double> (beatTicks (s))));
    }

    Tick lengthTicks (const Song& s, double beats)
    {
        if (beats <= 0.0)
            throw ToolError ("a length must be more than 0 beats");
        return std::max<Tick> (1, static_cast<Tick> (std::llround (beats * static_cast<double> (beatTicks (s)))));
    }

    double round3 (double v) { return std::round (v * 1000.0) / 1000.0; }

    /** {"bar": 3, "beat": 2.5} for a song position. */
    json position (const Song& s, Tick t)
    {
        const Tick bar = barTicks (s);
        return { { "bar", t / bar + 1 }, { "beat", round3 (1.0 + static_cast<double> (t % bar) / static_cast<double> (beatTicks (s))) } };
    }

    double beats (const Song& s, Tick t) { return round3 (static_cast<double> (t) / static_cast<double> (beatTicks (s))); }

    /** A range of whole bars from "from_bar" and "to_bar" (both inclusive); the whole song when they are missing. */
    std::pair<Tick, Tick> barRange (const Song& s, const json& a)
    {
        const Tick bar = barTicks (s);
        const auto from = a.value ("from_bar", 1);
        const Tick end = std::max (s.contentEnd(), bar);
        const auto to = a.value ("to_bar", static_cast<int> ((end + bar - 1) / bar));
        if (from < 1 || to < from)
            throw ToolError ("from_bar must be 1 or more, and to_bar must not be before from_bar");
        return { static_cast<Tick> (from - 1) * bar, static_cast<Tick> (to) * bar };
    }

    // ---- pitches -----------------------------------------------------------------
    std::string pitchName (int pitch)
    {
        return std::string (pitchClassName (pitch)) + std::to_string (pitch / 12 - 1);
    }

    int parsePitch (const json& v)
    {
        if (v.is_number())
        {
            const int p = v.get<int>();
            if (p < 0 || p > 127)
                throw ToolError ("a pitch must be 0 to 127");
            return p;
        }
        if (! v.is_string())
            throw ToolError ("a pitch is a MIDI number or a name like \"C4\", \"F#3\" or \"Bb2\"");
        const auto text = v.get<std::string>();
        static constexpr int kLetters[] = { 9, 11, 0, 2, 4, 5, 7 };
        if (text.empty() || std::toupper (static_cast<unsigned char> (text[0])) < 'A' || std::toupper (static_cast<unsigned char> (text[0])) > 'G')
            throw ToolError ("not a pitch: " + text);
        int pc = kLetters[std::toupper (static_cast<unsigned char> (text[0])) - 'A'];
        std::size_t i = 1;
        while (i < text.size() && (text[i] == '#' || text[i] == 'b'))
            pc += text[i++] == '#' ? 1 : -1;
        try
        {
            const int octave = std::stoi (text.substr (i));
            const int p = (octave + 1) * 12 + pc;
            if (p < 0 || p > 127)
                throw ToolError ("pitch out of range: " + text);
            return p;
        }
        catch (const std::logic_error&)
        {
            throw ToolError ("not a pitch (it needs an octave, like C4): " + text);
        }
    }

    // ---- tracks ------------------------------------------------------------------
    std::string trackType (const Track& t)
    {
        switch (t.output.kind)
        {
            case TrackOutput::Kind::soundFont: return t.output.channel == 10 ? "drums" : "instrument";
            case TrackOutput::Kind::midiPort: return "midi_port";
            case TrackOutput::Kind::sampler: return "drum_sampler";
            case TrackOutput::Kind::audio: return "audio";
            case TrackOutput::Kind::plugin: return "plugin";
            case TrackOutput::Kind::none: return "silent";
        }
        return "instrument";
    }

    /** A track by id (a number) or by name (any case). */
    const Track& findTrack (const Song& s, const json& ref)
    {
        if (ref.is_number())
        {
            if (const auto* t = edit::findTrack (s, ref.get<TrackId>()))
                return *t;
        }
        else if (ref.is_string())
        {
            const auto name = lower (ref.get<std::string>());
            for (const auto& t : s.tracks)
                if (lower (t.name) == name)
                    return t;
            for (const auto& t : s.tracks)
                if (lower (t.name).find (name) != std::string::npos)
                    return t;
        }
        throw ToolError ("no such track: " + ref.dump() + ". Call get_song to see the tracks.");
    }

    const Track& trackArg (const Song& s, const json& a)
    {
        if (! a.contains ("track"))
            throw ToolError ("give \"track\": its name or id");
        return findTrack (s, a["track"]);
    }

    int programFrom (const json& v)
    {
        if (v.is_number())
        {
            const int p = v.get<int>();
            if (p < 1 || p > 128)
                throw ToolError ("a General MIDI program is 1 to 128");
            return p - 1;
        }
        const auto want = lower (v.get<std::string>());
        for (int i = 0; i < 128; ++i)
            if (lower (kGeneralMidiNames[i]) == want)
                return i;
        for (int i = 0; i < 128; ++i)
            if (lower (kGeneralMidiNames[i]).find (want) != std::string::npos)
                return i;
        // Word by word, in any order: "fingered bass" finds "Electric Bass (finger)".
        const auto words = [] (const std::string& text) {
            std::vector<std::string> out;
            std::string w;
            for (const char c : text + " ")
                if (std::isalnum (static_cast<unsigned char> (c)))
                    w += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
                else if (! w.empty())
                {
                    out.push_back (w);
                    w.clear();
                }
            return out;
        };
        const auto wanted = words (want);
        for (int i = 0; i < 128 && ! wanted.empty(); ++i)
        {
            const auto have = words (kGeneralMidiNames[i]);
            const bool all = std::all_of (wanted.begin(), wanted.end(), [&] (const std::string& q) {
                return std::any_of (have.begin(), have.end(), [&] (const std::string& h) {
                    const auto n = std::min (q.size(), h.size());
                    return n >= 3 && q.compare (0, n, h, 0, n) == 0;
                });
            });
            if (all)
                return i;
        }
        throw ToolError ("no General MIDI sound named \"" + v.get<std::string>() + "\"");
    }

    int midi127 (const json& v, const char* what)
    {
        const int n = v.get<int>();
        if (n < 0 || n > 127)
            throw ToolError (std::string (what) + " must be 0 to 127");
        return n;
    }

    json trackJson (const Song& s, const Track& t)
    {
        json clips = json::array();
        for (const auto& c : t.clips)
            clips.push_back ({ { "id", c->id }, { "name", c->name }, { "start", position (s, c->start) }, { "length_beats", beats (s, c->length) },
                               { "notes", c->notes.size() }, { "loops", c->loopLength > 0 && c->loopLength < c->length } });
        for (const auto& a : t.audio)
            clips.push_back ({ { "id", a->id }, { "name", a->name }, { "start", position (s, a->start) }, { "length_beats", beats (s, a->length) },
                               { "audio_file", a->file } });
        json chords = json::array();
        for (const auto& m : t.marks)
            if (m.kind == ScoreMark::Kind::chord)
                chords.push_back ({ { "at", position (s, m.at) }, { "chord", m.text } });
        json out = { { "id", t.id }, { "name", t.name }, { "type", trackType (t) }, { "channel", t.output.channel },
                     { "volume", t.mixer.volume }, { "pan", t.mixer.pan }, { "reverb", t.mixer.reverbSend }, { "chorus", t.mixer.chorusSend },
                     { "mute", t.mixer.mute }, { "solo", t.mixer.solo }, { "clips", clips } };
        if (! t.sound.name.empty() || t.sound.program >= 0)
            out["sound"] = { { "name", t.sound.name }, { "program", t.sound.program + 1 } };
        if (t.output.kind == TrackOutput::Kind::midiPort)
            out["port"] = t.output.port;
        if (! chords.empty())
            out["chords"] = chords;
        if (t.styleChannel > 0)
            out["style_channel"] = t.styleChannel;
        return out;
    }

    // ---- the tools -----------------------------------------------------------------
    json schema (json properties, std::vector<std::string> required = {})
    {
        json s = { { "type", "object" }, { "properties", std::move (properties) } };
        if (! required.empty())
            s["required"] = required;
        return s;
    }

    const json kTrack = { { "description", "The track: its name or its id (see get_song)" } };
    const json kBar = { { "type", "integer" }, { "minimum", 1 } };

    json toolDefinitions()
    {
        const json noteItem = { { "type", "object" },
                                { "properties",
                                  { { "bar", kBar },
                                    { "beat", { { "type", "number" }, { "minimum", 1 }, { "description", "1 = the first beat; 2.5 = halfway through beat 2" } } },
                                    { "length", { { "type", "number" }, { "description", "In beats" } } },
                                    { "pitch", { { "description", "MIDI number or a name like C4 (middle C), F#3, Bb2" } } },
                                    { "velocity", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 127 } } } } },
                                { "required", { "bar", "beat", "length", "pitch" } } };
        return json::array ({
            { { "name", "get_song" },
              { "description", "Read the song: title, tempo, time signature, key, length, loop, and every track with its id, type, sound, mixer and clips. Call this first." },
              { "inputSchema", schema (json::object()) } },
            { { "name", "get_notes" },
              { "description", "Read the notes a track plays on the Timeline (loops written out), in a range of bars." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar } }, { "track" }) } },
            { { "name", "add_track" },
              { "description", "Add a track. type: instrument (built-in General MIDI sounds), drums (General MIDI drums on channel 10), midi_port (a hardware synth), audio." },
              { "inputSchema", schema ({ { "name", { { "type", "string" } } },
                                         { "type", { { "type", "string" }, { "enum", { "instrument", "drums", "midi_port", "audio" } } } },
                                         { "sound", { { "description", "General MIDI program 1-128 or its name, e.g. \"Acoustic Grand Piano\", \"Fingered Bass\"" } } },
                                         { "port", { { "type", "string" }, { "description", "midi_port only: the MIDI output name" } } },
                                         { "channel", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 16 } } } },
                                       { "name" }) } },
            { { "name", "set_track" },
              { "description", "Change a track: name, General MIDI sound, MIDI channel, mixer (volume, pan 0-127 with 64 = centre, reverb, chorus), mute, solo." },
              { "inputSchema", schema ({ { "track", kTrack }, { "name", { { "type", "string" } } }, { "sound", { { "description", "General MIDI program 1-128 or its name" } } },
                                         { "channel", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 16 } } },
                                         { "volume", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 127 } } },
                                         { "pan", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 127 } } },
                                         { "reverb", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 127 } } },
                                         { "chorus", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 127 } } },
                                         { "mute", { { "type", "boolean" } } }, { "solo", { { "type", "boolean" } } } },
                                       { "track" }) } },
            { { "name", "remove_track" },
              { "description", "Delete a track and its clips." },
              { "inputSchema", schema ({ { "track", kTrack } }, { "track" }) } },
            { { "name", "add_notes" },
              { "description", "Write notes on a track. They go into the clip at that place; where there is no clip, a new clip is made. A note at the same time and pitch is replaced." },
              { "inputSchema", schema ({ { "track", kTrack }, { "notes", { { "type", "array" }, { "items", noteItem } } } }, { "track", "notes" }) } },
            { { "name", "remove_notes" },
              { "description", "Delete the notes of a track that start in a range of bars; optionally only some pitches." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "pitches", { { "type", "array" }, { "description", "Only these pitches (numbers or names)" } } } },
                                       { "track", "from_bar", "to_bar" }) } },
            { { "name", "change_notes" },
              { "description", "Change the notes of a track in a range of bars (all bars when no range): transpose, set or scale velocity, quantise, set length." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "transpose", { { "type", "integer" }, { "description", "Semitones, e.g. 12 = an octave up" } } },
                                         { "velocity", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 127 } } },
                                         { "velocity_scale", { { "type", "number" }, { "description", "e.g. 0.8 = 20% softer" } } },
                                         { "quantise", { { "type", "string" }, { "enum", { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" } } } },
                                         { "length", { { "type", "number" }, { "description", "Set every note to this length in beats" } } } },
                                       { "track" }) } },
            { { "name", "edit_clips" },
              { "description", "Move, copy, delete or split clips by id (see get_song). move/copy: to a bar and optionally to another track; split: at a bar." },
              { "inputSchema", schema ({ { "action", { { "type", "string" }, { "enum", { "move", "copy", "delete", "split" } } } },
                                         { "clips", { { "type", "array" }, { "items", { { "type", "integer" } } } } },
                                         { "to_bar", kBar }, { "to_track", kTrack }, { "at_bar", kBar } },
                                       { "action", "clips" }) } },
            { { "name", "set_song" },
              { "description", "Change the title, tempo (BPM), time signature (\"3/4\") or key (\"D minor\") of the song." },
              { "inputSchema", schema ({ { "title", { { "type", "string" } } }, { "tempo", { { "type", "number" } } },
                                         { "time_signature", { { "type", "string" } } }, { "key", { { "type", "string" } } } }) } },
            { { "name", "find_chords" },
              { "description", "Find the chords in the notes (drums left out) and write chord symbols above the top track, in place of the ones there. step: how often a chord can change." },
              { "inputSchema", schema ({ { "step", { { "type", "string" }, { "enum", { "bar", "half_bar", "beat" } } } }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "track", { { "description", "Where the chord symbols go; the first track when missing" } } } }) } },
            { { "name", "set_chords" },
              { "description", "Write chord symbols (for the score) on a track, e.g. Cm7, F#7b9, Bb/D. They replace the chord symbols in the bars they cover." },
              { "inputSchema", schema ({ { "track", kTrack },
                                         { "chords", { { "type", "array" },
                                                       { "items", { { "type", "object" },
                                                                    { "properties", { { "bar", kBar }, { "beat", { { "type", "number" } } }, { "chord", { { "type", "string" } } } } },
                                                                    { "required", { "bar", "chord" } } } } } } },
                                       { "track", "chords" }) } },
            { { "name", "add_score_marks" },
              { "description", "Write marks on a track's score: dynamic (pp p mp mf f ff), articulation (staccato accent tenuto marcato fermata) or chord. Give an empty text to remove that kind of mark at that place." },
              { "inputSchema", schema ({ { "track", kTrack },
                                         { "marks", { { "type", "array" },
                                                      { "items", { { "type", "object" },
                                                                   { "properties", { { "bar", kBar }, { "beat", { { "type", "number" } } },
                                                                                     { "type", { { "type", "string" }, { "enum", { "dynamic", "articulation", "chord" } } } },
                                                                                     { "text", { { "type", "string" } } } } },
                                                                   { "required", { "bar", "type", "text" } } } } } } },
                                       { "track", "marks" }) } },
            { { "name", "set_lyrics" },
              { "description", "Write lyrics under the notes of a track: one word per note from the given place on. Use a hyphen to divide a word over notes: \"Twin-kle twin-kle\"." },
              { "inputSchema", schema ({ { "track", kTrack }, { "bar", kBar }, { "beat", { { "type", "number" } } }, { "text", { { "type", "string" } } } }, { "track", "bar", "text" }) } },
            { { "name", "remove_score_marks" },
              { "description", "Delete score marks of a track in a range of bars: all kinds, or one kind (dynamic, articulation, chord, lyric)." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar }, { "type", { { "type", "string" }, { "enum", { "dynamic", "articulation", "chord", "lyric" } } } } },
                                       { "track", "from_bar", "to_bar" }) } },
            { { "name", "get_beat" },
              { "description", "Read the drum pattern of a track as step rows, like the Beat view: one character per step, x = normal, X = accent, o = soft, . = empty." },
              { "inputSchema", schema ({ { "track", kTrack }, { "steps_per_beat", { { "type", "integer" }, { "description", "4 = 16th notes in 4/4 (default)" } } } }, { "track" }) } },
            { { "name", "set_beat" },
              { "description", "Write a drum pattern (a clip that repeats) on a track, like the Beat view. Each row is a drum and a step string: x = normal, X = accent, o = soft, . = empty. A short string repeats to fill the pattern. The rows given replace those drums; other drums stay unless clear is true. Makes the pattern when the track has none." },
              { "inputSchema", schema ({ { "track", kTrack },
                                         { "rows", { { "type", "array" },
                                                     { "items", { { "type", "object" },
                                                                  { "properties", { { "drum", { { "description", "GM drum note number or a name: kick, snare, clap, rim, closed hihat, pedal hihat, open hihat, crash, ride, low tom, mid tom, high tom, tambourine, cowbell, shaker" } } },
                                                                                    { "steps", { { "type", "string" } } } } },
                                                                  { "required", { "drum", "steps" } } } } } },
                                         { "bars", { { "type", "integer" }, { "enum", { 1, 2, 4, 8 } }, { "description", "Pattern length (default: the pattern's own, or 1)" } } },
                                         { "steps_per_beat", { { "type", "integer" }, { "description", "4 = 16th notes in 4/4 (default)" } } },
                                         { "bar", { { "type", "integer" }, { "minimum", 1 }, { "description", "Where a new pattern starts" } } },
                                         { "play_bars", { { "type", "integer" }, { "minimum", 1 }, { "description", "How many bars the new pattern plays on the Timeline (default 4)" } } },
                                         { "clear", { { "type", "boolean" } } } },
                                       { "track", "rows" }) } },
            { { "name", "get_clip_grid" },
              { "description", "Read the Clip grid: the scenes (rows) and each track's slot clips." },
              { "inputSchema", schema (json::object()) } },
            { { "name", "set_slot" },
              { "description", "Put a MIDI clip in a Clip grid slot (replacing what is there): with notes (bar 1 = the start of the clip), or a copy of a Timeline clip. The clip loops over its length." },
              { "inputSchema", schema ({ { "track", kTrack }, { "scene", { { "type", "integer" }, { "minimum", 1 } } }, { "name", { { "type", "string" } } },
                                         { "length_bars", { { "type", "integer" }, { "minimum", 1 } } },
                                         { "notes", { { "type", "array" }, { "items", noteItem } } },
                                         { "copy_clip", { { "type", "integer" }, { "description", "Id of a Timeline clip to copy" } } } },
                                       { "track", "scene" }) } },
            { { "name", "clear_slot" },
              { "description", "Empty a Clip grid slot." },
              { "inputSchema", schema ({ { "track", kTrack }, { "scene", { { "type", "integer" }, { "minimum", 1 } } } }, { "track", "scene" }) } },
            { { "name", "name_scene" },
              { "description", "Name a Clip grid scene (row), e.g. Verse or Chorus." },
              { "inputSchema", schema ({ { "scene", { { "type", "integer" }, { "minimum", 1 } } }, { "name", { { "type", "string" } } } }, { "scene", "name" }) } },
            { { "name", "launch" },
              { "description", "Start a Clip grid scene, or one slot (track and scene), or stop all slots." },
              { "inputSchema", schema ({ { "scene", { { "type", "integer" }, { "minimum", 1 } } }, { "track", kTrack }, { "stop", { { "type", "boolean" } } } }) } },
            { { "name", "transport" },
              { "description", "Play, stop, or move the playhead to a bar." },
              { "inputSchema", schema ({ { "action", { { "type", "string" }, { "enum", { "play", "stop", "go_to" } } } }, { "bar", kBar } }, { "action" }) } },
            { { "name", "undo" },
              { "description", "Undo the last changes (also the user's own), or redo them." },
              { "inputSchema", schema ({ { "steps", { { "type", "integer" }, { "minimum", 1 } } }, { "redo", { { "type", "boolean" } } } }) } },
            { { "name", "find_styles" },
              { "description", "Find arranger style files (Yamaha .sty, .prs and others) in the user's style folders by name. Spaces and order of words don't matter: \"Beat16\" also finds \"16 Beat Ballad\". When several match, choose the closest or ask the user." },
              { "inputSchema", schema ({ { "name", { { "type", "string" } } }, { "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 100 } } } }, { "name" }) } },
            { { "name", "get_style" },
              { "description", "Read a style file: its name, tempo, time signature and sections (Intro A, Main A, Fill In AA, Ending A...) with their length and parts." },
              { "inputSchema", schema ({ { "file", { { "type", "string" }, { "description", "The full path from find_styles" } } } }, { "file" }) } },
            { { "name", "fill_from_style" },
              { "description", "Write a section of a style onto new tracks of the Timeline (drums, bass, chords, pads...), for a number of bars, following one chord per bar. Use it to start a song in the feel of a style, then change the tracks as needed." },
              { "inputSchema", schema ({ { "file", { { "type", "string" }, { "description", "The full path from find_styles" } } },
                                         { "section", { { "type", "string" }, { "description", "For example \"Main A\" (the default)" } } },
                                         { "chords", { { "type", "string" }, { "description", "One chord for each bar, e.g. \"D Bm G A\"; the list starts again when there are more bars" } } },
                                         { "bars", kBar }, { "start_bar", kBar },
                                         { "use_style_tempo", { { "type", "boolean" }, { "description", "Set the song's tempo to the style's tempo" } } } },
                                       { "file", "chords", "bars" }) } },
            { { "name", "list_devices" },
              { "description", "The MIDI outputs plugged in now (hardware synths and keyboards) and the device profiles the app has for them. Use the output name with set_track's output." },
              { "inputSchema", schema (json::object()) } },
            { { "name", "find_sounds" },
              { "description", "Search the sound list of a device (e.g. \"D-05\", \"PSR-S970\", \"XioSynth\", or \"General MIDI\") by words such as \"pad\", \"analog bass\" or \"strings\"." },
              { "inputSchema", schema ({ { "device", { { "type", "string" } } }, { "search", { { "type", "string" } } },
                                         { "limit", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 100 } } } },
                                       { "device" }) } },
            { { "name", "set_track_output" },
              { "description", "Send a track to a hardware synth (a MIDI output or a device name like \"D-05\"), or back to the built-in sounds (\"built-in\"). Optionally choose the synth's sound by name (see find_sounds) and the MIDI channel." },
              { "inputSchema", schema ({ { "track", kTrack }, { "output", { { "type", "string" } } },
                                         { "sound", { { "type", "string" }, { "description", "A sound name of that device, e.g. \"Fat Warm Pad\"" } } },
                                         { "channel", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 16 } } } },
                                       { "track", "output" }) } },
            { { "name", "get_midi_setup" },
              { "description", "The MIDI setup: inputs (keyboards and controllers) with on/off and the controller page each one uses, outputs with their device profile, and each track's input, input channel and output." },
              { "inputSchema", schema (json::object()) } },
            { { "name", "set_midi_input" },
              { "description", "Turn a MIDI input on or off. An input that is off sends nothing to the app." },
              { "inputSchema", schema ({ { "input", { { "type", "string" }, { "description", "Input name or part of it, e.g. \"X-TOUCH\"" } } }, { "on", { { "type", "boolean" } } } }, { "input", "on" }) } },
            { { "name", "set_track_input" },
              { "description", "Choose which MIDI input a track plays and records from: an input name (or part of it, e.g. \"Launchkey\") or \"all\". Optionally only one MIDI channel. Only the selected or armed track plays live, so this decides which keyboard you hear." },
              { "inputSchema", schema ({ { "track", kTrack }, { "input", { { "type", "string" } } },
                                         { "channel", { { "type", "integer" }, { "minimum", 0 }, { "maximum", 16 }, { "description", "Record only this MIDI channel; 0 = all channels" } } } },
                                       { "track", "input" }) } },
            { { "name", "set_port_device" },
              { "description", "Choose by hand what a MIDI port is. For an output: a device profile (its sounds and mixer controls), e.g. \"Roland D-05\". For an input: a controller page (what its knobs, faders and pads do), e.g. \"X-Touch Mini\", or \"none\" to use it as a plain keyboard. \"auto\" goes back to choosing by the port name. Use it when a synth or controller has an unusual port name (for example a USB MIDI cable)." },
              { "inputSchema", schema ({ { "port", { { "type", "string" }, { "description", "Port name or part of it" } } },
                                         { "kind", { { "type", "string" }, { "enum", json::array ({ "output", "input" }) } } },
                                         { "device", { { "type", "string" }, { "description", "Device profile or controller page name or id, \"none\" or \"auto\"" } } } },
                                       { "port", "kind", "device" }) } },
            { { "name", "recent_midi" },
              { "description", "The MIDI messages that came in during the last seconds (default 15), with port, channel and controller or note numbers. Ask the user to press a key or move a knob first, then call this to learn which message it sends." },
              { "inputSchema", schema ({ { "seconds", { { "type", "number" }, { "minimum", 1 }, { "maximum", 120 } } } }) } },
            { { "name", "list_device_files" },
              { "description", "The device profiles (a synth's sounds, banks and mixer controllers) and controller pages (mixer layouts for knobs, faders and pads) the app has, built-in and the user's own." },
              { "inputSchema", schema (json::object()) } },
            { { "name", "read_device_file" },
              { "description", "Read a device profile or controller page (YAML). Read one before you write a new one: use it as the pattern." },
              { "inputSchema", schema ({ { "name", { { "type", "string" } } } }, { "name" }) } },
            { { "name", "write_device_file" },
              { "description", "Save a device profile or a controller page (YAML) in the user's device folder; the app checks it and loads it at once. A file with the id of a built-in one replaces it. Use it to add or rename a synth's sounds, map a controller's knobs and faders, or add a new synth from its MIDI chart." },
              { "inputSchema", schema ({ { "name", { { "type", "string" }, { "description", "File name, e.g. \"novation-xiosynth.yaml\"" } } }, { "text", { { "type", "string" } } } }, { "name", "text" }) } },
            { { "name", "get_sound_editor" },
              { "description", "The sound settings of a hardware synth (filter, envelopes, oscillators...) from its device profile. Without a page: the pages and their settings. With a page: each setting's range or choices and its value now (when known)." },
              { "inputSchema", schema ({ { "device", { { "type", "string" } } }, { "page", { { "type", "string" } } } }, { "device" }) } },
            { { "name", "set_sound" },
              { "description", "Change the sound of a hardware synth now: send settings to it. Each setting is \"Page/Name\" (or just the name when it is on one page only) and a value: a number in the range get_sound_editor shows, or a choice's name. Use it to shape a sound the user describes (\"brighter\", \"slow attack\")." },
              { "inputSchema", schema ({ { "device", { { "type", "string" } } },
                                         { "settings", { { "type", "array" }, { "items", { { "type", "object" }, { "properties", { { "setting", { { "type", "string" } } }, { "value", { { "type", { "number", "string" } } } } } }, { "required", { "setting", "value" } } } } } } },
                                       { "device", "settings" }) } },
            { { "name", "read_sound_names" },
              { "description", "Read the names of the sounds stored in a synth (its own and the user's patches) over MIDI and add them to the synth's sound list, so find_sounds finds them. Works for synths whose profile says how (the Roland D-05). It takes about half a minute; call find_sounds after." },
              { "inputSchema", schema ({ { "device", { { "type", "string" } } } }, { "device" }) } },
            { { "name", "name_sounds" },
              { "description", "Add sounds to a synth's sound list or rename them, for example when the user made a new sound or the list only has numbers (XioSynth \"A005\"). Saved in the user's own sound list for the device. A sound with the program (and bank) of a listed one renames it." },
              { "inputSchema", schema ({ { "device", { { "type", "string" } } },
                                         { "group", { { "type", "string" }, { "description", "The group, e.g. \"Bank A\" or \"Internal 1\"; a new name makes a new group" } } },
                                         { "sounds", { { "type", "array" }, { "items", { { "type", "object" }, { "properties", { { "name", { { "type", "string" } } }, { "program", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 128 } } }, { "bank_msb", { { "type", "integer" } } }, { "bank_lsb", { { "type", "integer" } } } } }, { "required", { "name", "program" } } } } } } },
                                       { "device", "group", "sounds" }) } },
        });
    }

    // ---- tool bodies --------------------------------------------------------------------
    json getSong (Host& host)
    {
        const auto& s = host.song();
        json tracks = json::array();
        for (const auto& t : s.tracks)
            tracks.push_back (trackJson (s, t));
        const Tick bar = barTicks (s);
        json out = { { "title", s.title },
                     { "tempo", s.baseTempo() },
                     { "time_signature", std::to_string (s.timeSignature.numerator) + "/" + std::to_string (s.timeSignature.denominator) },
                     { "key", s.key },
                     { "bars", (std::max (s.contentEnd(), bar) + bar - 1) / bar },
                     { "playing", host.isPlaying() },
                     { "playhead", position (s, static_cast<Tick> (host.position())) },
                     { "tracks", tracks } };
        if (s.tempo.getPoints().size() > 1)
            out["tempo_changes"] = s.tempo.getPoints().size() - 1;
        if (s.loop.enabled && s.loop.isValid())
            out["loop"] = { { "from", position (s, s.loop.start) }, { "to", position (s, s.loop.end) } };
        if (s.style)
            out["style"] = true; // the Clip grid is an arranger style (scenes are its sections)
        return out;
    }

    json getNotes (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const auto [from, to] = barRange (s, a);
        json notes = json::array();
        std::size_t count = 0;
        for (const auto& n : notation::timelineNotes (t))
        {
            if (n.start < from || n.start >= to)
                continue;
            if (++count > kMostNotes)
                break;
            auto at = position (s, n.start);
            notes.push_back ({ { "bar", at["bar"] }, { "beat", at["beat"] }, { "length", beats (s, n.length) }, { "pitch", n.pitch },
                               { "name", pitchName (n.pitch) }, { "velocity", n.velocity } });
        }
        json out = { { "track", t.name }, { "notes", notes } };
        if (count > kMostNotes)
            out["note"] = "Only the first " + std::to_string (kMostNotes) + " notes; ask for fewer bars to see the rest.";
        return out;
    }

    json addTrack (Host& host, const json& a)
    {
        const auto name = a.value ("name", std::string ("Track"));
        const auto type = a.value ("type", std::string ("instrument"));
        if (type != "instrument" && type != "drums" && type != "midi_port" && type != "audio")
            throw ToolError ("type must be instrument, drums, midi_port or audio");
        const int program = a.contains ("sound") ? programFrom (a["sound"]) : (type == "drums" ? 0 : -1);
        TrackId id = 0;
        host.edit ("AI: Add Track", [&] (Song& s) {
            if (type == "audio")
            {
                id = edit::addAudioTrack (s, name).id;
                return;
            }
            auto& t = edit::addTrack (s, name);
            id = t.id;
            t.output.kind = type == "midi_port" ? TrackOutput::Kind::midiPort : TrackOutput::Kind::soundFont;
            if (type == "midi_port")
                t.output.port = a.value ("port", std::string());
            if (type == "drums")
                t.output.channel = 10;
            if (a.contains ("channel"))
                t.output.channel = std::clamp (a["channel"].get<int>(), 1, 16);
            if (program >= 0)
                t.sound = { type == "drums" ? std::string ("Drums") : std::string (kGeneralMidiNames[program]), -1, -1, program };
        });
        return { { "added", trackJson (host.song(), *edit::findTrack (host.song(), id)) } };
    }

    json setTrack (Host& host, const json& a)
    {
        const auto id = trackArg (host.song(), a).id;
        const int program = a.contains ("sound") ? programFrom (a["sound"]) : -1;
        for (const char* key : { "volume", "pan", "reverb", "chorus" })
            if (a.contains (key))
                midi127 (a[key], key);
        host.edit ("AI: Change Track", [&] (Song& s) {
            auto* t = edit::findTrack (s, id);
            if (a.contains ("name"))
                t->name = a["name"].get<std::string>();
            if (program >= 0)
                t->sound = { kGeneralMidiNames[program], -1, -1, program };
            if (a.contains ("channel"))
                t->output.channel = std::clamp (a["channel"].get<int>(), 1, 16);
            if (a.contains ("volume")) t->mixer.volume = a["volume"].get<int>();
            if (a.contains ("pan")) t->mixer.pan = a["pan"].get<int>();
            if (a.contains ("reverb")) t->mixer.reverbSend = a["reverb"].get<int>();
            if (a.contains ("chorus")) t->mixer.chorusSend = a["chorus"].get<int>();
            if (a.contains ("mute")) t->mixer.mute = a["mute"].get<bool>();
            if (a.contains ("solo")) t->mixer.solo = a["solo"].get<bool>();
        });
        return { { "track", trackJson (host.song(), *edit::findTrack (host.song(), id)) } };
    }

    json removeTrack (Host& host, const json& a)
    {
        const Track t = trackArg (host.song(), a); // a copy: an edit replaces the song
        const auto id = t.id;
        const auto name = t.name;
        host.edit ("AI: Delete Track", [&] (Song& s) { edit::removeTrack (s, id); });
        return { { "removed", name } };
    }

    json addNotes (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        if (t.output.kind == TrackOutput::Kind::audio)
            throw ToolError ("an audio track has no notes");
        if (! a.contains ("notes") || ! a["notes"].is_array() || a["notes"].empty())
            throw ToolError ("give \"notes\": a list of {bar, beat, length, pitch, velocity}");
        std::vector<Note> absolute;
        for (const auto& n : a["notes"])
        {
            Note note;
            note.start = tickAt (s, n.at ("bar").get<double>(), n.value ("beat", 1.0));
            note.length = lengthTicks (s, n.at ("length").get<double>());
            note.pitch = static_cast<std::uint8_t> (parsePitch (n.at ("pitch")));
            note.velocity = static_cast<std::uint8_t> (std::clamp (n.value ("velocity", 90), 1, 127));
            absolute.push_back (note);
        }
        // The clipboard paste does the work: notes go into the clip there, or a new one.
        std::sort (absolute.begin(), absolute.end(), [] (const Note& x, const Note& y) { return x.start < y.start; });
        clipboard::Content content;
        content.kind = clipboard::Content::Kind::notes;
        const Tick first = absolute.front().start;
        for (auto n : absolute)
        {
            n.start -= first;
            content.span = std::max (content.span, n.end());
            content.notes.push_back (n);
        }
        // Notes far apart go in separate pastes, so a gap of bars does not make one long clip.
        const auto id = t.id;
        std::size_t added = 0;
        host.edit ("AI: Add Notes", [&] (Song& song) {
            const Tick bar = song.timeSignature.ticksPerBar();
            std::size_t i = 0;
            while (i < absolute.size())
            {
                clipboard::Content part = content;
                part.notes.clear();
                part.span = 0;
                const Tick start = absolute[i].start;
                Tick reach = absolute[i].end();
                std::size_t j = i;
                while (j < absolute.size() && absolute[j].start < ((reach + bar - 1) / bar) * bar + bar)
                {
                    auto n = absolute[j];
                    reach = std::max (reach, n.end());
                    n.start -= start;
                    part.span = std::max (part.span, n.end());
                    part.notes.push_back (n);
                    ++j;
                }
                added += clipboard::pasteNotes (song, part, id, start).size();
                i = j;
            }
        });
        return { { "track", t.name }, { "added", added } };
    }

    /** Changes each Timeline clip of a track that has notes in [from, to): `fn` gets the clip and the range relative to it. */
    void eachClip (Song& s, TrackId id, Tick from, Tick to, const std::function<void (Clip&, Tick, Tick)>& fn)
    {
        auto* t = edit::findTrack (s, id);
        std::vector<ClipId> ids;
        for (const auto& c : t->clips)
            if (c->start < to && c->end() > from)
                ids.push_back (c->id);
        for (const auto cid : ids)
            edit::modifyClip (s, cid, [&] (Clip& c) { fn (c, from - c.start, to - c.start); });
    }

    json removeNotes (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const auto [from, to] = barRange (s, a);
        std::vector<int> pitches;
        if (a.contains ("pitches"))
            for (const auto& p : a["pitches"])
                pitches.push_back (parsePitch (p));
        std::size_t removed = 0;
        host.edit ("AI: Delete Notes", [&] (Song& song) {
            eachClip (song, t.id, from, to, [&] (Clip& c, Tick a0, Tick a1) {
                removed += std::erase_if (c.notes, [&] (const Note& n) {
                    return n.start >= a0 && n.start < a1 && (pitches.empty() || std::find (pitches.begin(), pitches.end(), n.pitch) != pitches.end());
                });
            });
        });
        return { { "removed", removed } };
    }

    json changeNotes (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const auto [from, to] = barRange (s, a);
        const int transpose = a.value ("transpose", 0);
        const int velocity = a.contains ("velocity") ? std::clamp (a["velocity"].get<int>(), 1, 127) : -1;
        const double scale = a.value ("velocity_scale", 1.0);
        const Tick length = a.contains ("length") ? lengthTicks (s, a["length"].get<double>()) : 0;
        QuantiseSettings q;
        const bool quantise = a.contains ("quantise");
        if (quantise)
        {
            q.grid = QuantiseSettings::gridFromName (a["quantise"].get<std::string>());
            if (q.grid <= 0)
                throw ToolError ("quantise is one of 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32");
        }
        std::size_t changed = 0;
        host.edit ("AI: Change Notes", [&] (Song& song) {
            eachClip (song, t.id, from, to, [&] (Clip& c, Tick a0, Tick a1) {
                Clip inside = c;
                inside.notes.clear();
                std::vector<Note> outside;
                for (const auto& n : c.notes)
                    (n.start >= a0 && n.start < a1 ? inside.notes : outside).push_back (n);
                for (auto& n : inside.notes)
                {
                    n.pitch = static_cast<std::uint8_t> (std::clamp (n.pitch + transpose, 0, 127));
                    if (velocity > 0)
                        n.velocity = static_cast<std::uint8_t> (velocity);
                    n.velocity = static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::lround (n.velocity * scale)), 1, 127));
                    if (length > 0)
                        n.length = length;
                }
                if (quantise)
                    arranger::quantise (inside, q);
                changed += inside.notes.size();
                c.notes = outside;
                c.notes.insert (c.notes.end(), inside.notes.begin(), inside.notes.end());
                edit::normalise (c);
            });
        });
        return { { "changed", changed } };
    }

    json editClips (Host& host, const json& a)
    {
        const auto& s = host.song();
        const auto action = a.at ("action").get<std::string>();
        std::vector<ClipId> ids;
        for (const auto& v : a.at ("clips"))
        {
            const auto id = v.get<ClipId>();
            if (edit::trackOfClip (s, id) == 0)
                throw ToolError ("no clip with id " + std::to_string (id) + " on the Timeline. Call get_song to see the clips.");
            ids.push_back (id);
        }
        if (ids.empty())
            throw ToolError ("give the ids of the clips");
        json made = json::array();
        if (action == "delete")
            host.edit ("AI: Delete Clips", [&] (Song& song) {
                for (auto id : ids)
                    edit::removeClip (song, id);
            });
        else if (action == "split")
        {
            const Tick at = tickAt (s, a.at ("at_bar").get<double>(), 1.0);
            host.edit ("AI: Split Clips", [&] (Song& song) {
                for (auto id : ids)
                    if (const auto right = edit::splitClip (song, id, at); right != 0)
                        made.push_back (right);
            });
        }
        else if (action == "move" || action == "copy")
        {
            if (! a.contains ("to_bar") && ! a.contains ("to_track"))
                throw ToolError ("give to_bar, to_track or both");
            // The earliest clip goes to the bar; the others keep their distance to it.
            auto content = clipboard::copyClips (s, ids);
            Tick earliest = std::numeric_limits<Tick>::max();
            for (auto id : ids)
            {
                if (auto c = edit::findClipPtr (s, id)) earliest = std::min (earliest, c->start);
                if (auto c = edit::findAudioClip (s, id)) earliest = std::min (earliest, c->start);
            }
            const Tick at = a.contains ("to_bar") ? tickAt (s, a["to_bar"].get<double>(), 1.0) : earliest;
            TrackId firstTrack = 0;
            int firstIndex = 1 << 30;
            for (auto id : ids)
            {
                const auto tid = edit::trackOfClip (s, id);
                if (edit::trackIndex (s, tid) < firstIndex)
                {
                    firstIndex = edit::trackIndex (s, tid);
                    firstTrack = tid;
                }
            }
            const TrackId target = a.contains ("to_track") ? findTrack (s, a["to_track"]).id : firstTrack;
            host.edit (action == "move" ? "AI: Move Clips" : "AI: Copy Clips", [&] (Song& song) {
                if (action == "move")
                    for (auto id : ids)
                        edit::removeClip (song, id);
                for (auto id : clipboard::pasteClips (song, content, target, at))
                    made.push_back (id);
            });
        }
        else
            throw ToolError ("action is move, copy, delete or split");
        return { { "done", action }, { "new_clips", made } };
    }

    json setSong (Host& host, const json& a)
    {
        TimeSignature ts = host.song().timeSignature;
        if (a.contains ("time_signature"))
        {
            const auto text = a["time_signature"].get<std::string>();
            const auto slash = text.find ('/');
            try
            {
                if (slash == std::string::npos)
                    throw std::invalid_argument ("no slash");
                ts.numerator = std::stoi (text.substr (0, slash));
                ts.denominator = std::stoi (text.substr (slash + 1));
            }
            catch (const std::exception&)
            {
                throw ToolError ("a time signature looks like 3/4 or 6/8");
            }
            if (! ts.isValid())
                throw ToolError ("not a valid time signature: " + text);
        }
        if (a.contains ("tempo") && (a["tempo"].get<double>() < 20.0 || a["tempo"].get<double>() > 400.0))
            throw ToolError ("the tempo must be 20 to 400 BPM");
        host.edit ("AI: Change Song", [&] (Song& s) {
            if (a.contains ("title")) s.title = a["title"].get<std::string>();
            if (a.contains ("key")) s.key = a["key"].get<std::string>();
            if (a.contains ("tempo")) s.tempo.set (0, a["tempo"].get<double>());
            s.timeSignature = ts;
        });
        const auto& s = host.song();
        return { { "title", s.title }, { "tempo", s.baseTempo() }, { "time_signature", std::to_string (ts.numerator) + "/" + std::to_string (ts.denominator) }, { "key", s.key } };
    }

    json findChords (Host& host, const json& a)
    {
        const auto& s = host.song();
        const auto [from, to] = barRange (s, a);
        const auto step = a.value ("step", std::string ("half_bar"));
        const Tick ticks = step == "bar" ? barTicks (s) : (step == "beat" ? beatTicks (s) : barTicks (s) / 2);
        std::vector<const Track*> tracks;
        for (const auto& t : s.tracks)
            tracks.push_back (&t);
        if (tracks.empty())
            throw ToolError ("the song has no tracks");
        const TrackId target = a.contains ("track") ? findTrack (s, a["track"]).id : s.tracks.front().id;
        const auto marks = chords::find (tracks, s.timeSignature, from, to, ticks);
        host.edit ("AI: Find Chords", [&] (Song& song) { chords::replaceChords (*edit::findTrack (song, target), from, to, marks); });
        json out = json::array();
        for (const auto& m : marks)
            out.push_back ({ { "at", position (host.song(), m.at) }, { "chord", m.text } });
        return { { "chords", out } };
    }

    json setChords (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        std::vector<ScoreMark> marks;
        for (const auto& c : a.at ("chords"))
        {
            const auto text = c.at ("chord").get<std::string>();
            if (! style_chord::parse (text))
                throw ToolError ("not a chord: " + text);
            marks.push_back ({ ScoreMark::Kind::chord, tickAt (s, c.at ("bar").get<double>(), c.value ("beat", 1.0)), text });
        }
        if (marks.empty())
            throw ToolError ("give at least one chord");
        Tick from = marks.front().at, to = 0;
        for (const auto& m : marks)
        {
            from = std::min (from, m.at);
            to = std::max (to, m.at);
        }
        const Tick bar = barTicks (s);
        from = (from / bar) * bar;
        to = (to / bar + 1) * bar;
        const auto id = t.id;
        host.edit ("AI: Set Chords", [&] (Song& song) { chords::replaceChords (*edit::findTrack (song, id), from, to, marks); });
        return { { "track", t.name }, { "chords", marks.size() } };
    }

    // ---- score ---------------------------------------------------------------------------
    ScoreMark::Kind markKind (const std::string& type)
    {
        if (type == "dynamic") return ScoreMark::Kind::dynamic;
        if (type == "articulation") return ScoreMark::Kind::articulation;
        if (type == "chord") return ScoreMark::Kind::chord;
        if (type == "lyric") return ScoreMark::Kind::lyric;
        throw ToolError ("a mark type is dynamic, articulation, chord or lyric");
    }

    json addScoreMarks (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        static const std::vector<std::string> kDynamics { "pp", "p", "mp", "mf", "f", "ff" };
        static const std::vector<std::string> kArticulations { "staccato", "accent", "tenuto", "marcato", "fermata" };
        std::vector<ScoreMark> marks;
        for (const auto& m : a.at ("marks"))
        {
            ScoreMark mark { markKind (m.at ("type").get<std::string>()), tickAt (s, m.at ("bar").get<double>(), m.value ("beat", 1.0)), m.at ("text").get<std::string>() };
            if (! mark.text.empty())
            {
                if (mark.kind == ScoreMark::Kind::dynamic && std::find (kDynamics.begin(), kDynamics.end(), mark.text) == kDynamics.end())
                    throw ToolError ("a dynamic is pp, p, mp, mf, f or ff");
                if (mark.kind == ScoreMark::Kind::articulation && std::find (kArticulations.begin(), kArticulations.end(), mark.text) == kArticulations.end())
                    throw ToolError ("an articulation is staccato, accent, tenuto, marcato or fermata");
                if (mark.kind == ScoreMark::Kind::chord && ! style_chord::parse (mark.text))
                    throw ToolError ("not a chord: " + mark.text);
            }
            marks.push_back (mark);
        }
        const auto id = t.id;
        host.edit ("AI: Add Score Marks", [&] (Song& song) {
            for (const auto& m : marks)
                edit::setMark (song, id, m);
        });
        return { { "track", t.name }, { "marks", marks.size() } };
    }

    json setLyrics (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const Tick from = tickAt (s, a.at ("bar").get<double>(), a.value ("beat", 1.0));
        std::vector<Tick> starts;
        for (const auto& n : notation::timelineNotes (t))
            if (starts.empty() || starts.back() != n.start)
                starts.push_back (n.start);
        const auto id = t.id;
        const auto text = a.at ("text").get<std::string>();
        int placed = 0;
        host.edit ("AI: Add Lyrics", [&] (Song& song) { placed = edit::setLyrics (song, id, from, text, starts); });
        return { { "track", t.name }, { "words_placed", placed } };
    }

    json removeScoreMarks (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const auto [from, to] = barRange (s, a);
        const bool one = a.contains ("type");
        const auto kind = one ? markKind (a["type"].get<std::string>()) : ScoreMark::Kind::chord;
        std::size_t removed = 0;
        const auto id = t.id;
        host.edit ("AI: Remove Score Marks", [&] (Song& song) {
            auto* track = edit::findTrack (song, id);
            removed = std::erase_if (track->marks, [&] (const ScoreMark& m) { return m.at >= from && m.at < to && (! one || m.kind == kind); });
        });
        return { { "removed", removed } };
    }

    // ---- beat ----------------------------------------------------------------------------
    int drumNote (const json& v)
    {
        if (v.is_number())
            return std::clamp (v.get<int>(), 0, 127);
        auto name = lower (v.get<std::string>());
        std::erase_if (name, [] (char c) { return c == '-' || c == '_'; });
        static const std::pair<const char*, int> kNames[] = {
            { "kick", 36 }, { "bass drum", 36 }, { "bassdrum", 36 }, { "snare", 38 }, { "clap", 39 }, { "hand clap", 39 }, { "rim", 37 }, { "side stick", 37 },
            { "closed hihat", 42 }, { "closed hi hat", 42 }, { "hihat", 42 }, { "hi hat", 42 }, { "pedal hihat", 44 }, { "pedal hi hat", 44 },
            { "open hihat", 46 }, { "open hi hat", 46 }, { "crash", 49 }, { "ride", 51 }, { "low tom", 45 }, { "mid tom", 47 }, { "high tom", 50 },
            { "floor tom", 41 }, { "tambourine", 54 }, { "cowbell", 56 }, { "shaker", 70 }, { "maracas", 70 }, { "splash", 55 }, { "china", 52 },
        };
        for (const auto& [n, note] : kNames)
            if (name == n)
                return note;
        for (int note = 35; note <= 81; ++note)
            if (lower (gmDrumName (note)) == name)
                return note;
        for (int note = 35; note <= 81; ++note)
            if (lower (gmDrumName (note)).find (name) != std::string::npos)
                return note;
        throw ToolError ("no drum named \"" + v.get<std::string>() + "\"");
    }

    /** The pattern clip of a track: the first clip that repeats, else the first clip. */
    ClipPtr patternOf (const Track& t)
    {
        for (const auto& c : t.clips)
            if (c->loopLength > 0)
                return c;
        return t.clips.empty() ? nullptr : t.clips.front();
    }

    Tick patternLength (const Clip& c) { return c.loopLength > 0 && c.loopLength < c.length ? c.loopLength : c.length; }

    json getBeat (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        const auto clip = patternOf (t);
        if (clip == nullptr)
            return { { "track", t.name }, { "pattern", nullptr }, { "note", "This track has no pattern yet. set_beat makes one." } };
        const int perBeat = std::clamp (a.value ("steps_per_beat", 4), 1, 8);
        const Tick step = std::max<Tick> (1, beatTicks (s) / perBeat);
        const Tick length = patternLength (*clip);
        const auto count = static_cast<std::size_t> ((length + step - 1) / step);
        std::map<int, std::string> rows;
        for (const auto& n : clip->notes)
        {
            if (n.start >= length)
                continue;
            auto& row = rows[n.pitch];
            row.resize (count, '.');
            const auto i = static_cast<std::size_t> (n.start / step);
            if (i < count)
                row[i] = n.velocity >= 120 ? 'X' : (n.velocity < 80 ? 'o' : 'x');
        }
        json out = json::array();
        for (const auto& [pitch, steps] : rows)
            out.push_back ({ { "drum", pitch }, { "name", gmDrumName (pitch) }, { "steps", steps } });
        return { { "track", t.name }, { "clip", clip->id }, { "start", position (s, clip->start) }, { "bars", length / barTicks (s) },
                 { "steps_per_beat", perBeat }, { "rows", out } };
    }

    json setBeat (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        if (t.output.kind == TrackOutput::Kind::audio)
            throw ToolError ("an audio track has no drum pattern");
        const int perBeat = std::clamp (a.value ("steps_per_beat", 4), 1, 8);
        const Tick bar = barTicks (s);
        const Tick step = std::max<Tick> (1, beatTicks (s) / perBeat);
        struct Row { int note; std::string steps; };
        std::vector<Row> rows;
        for (const auto& r : a.at ("rows"))
        {
            auto steps = r.at ("steps").get<std::string>();
            std::erase_if (steps, [] (char c) { return c == ' ' || c == '|'; });
            if (steps.empty())
                throw ToolError ("a row needs steps, like x...x...x...x...");
            rows.push_back ({ drumNote (r.at ("drum")), steps });
        }
        auto clip = patternOf (t);
        const int bars = a.value ("bars", clip != nullptr ? static_cast<int> (std::max<Tick> (1, patternLength (*clip) / bar)) : 1);
        if (bars < 1 || bars > 64)
            throw ToolError ("bars must be 1 to 64");
        const Tick length = bar * bars;
        const bool clearOthers = a.value ("clear", false);
        const auto id = t.id;
        ClipId clipId = clip != nullptr ? clip->id : 0;
        host.edit ("AI: Set Beat", [&] (Song& song) {
            if (clipId == 0)
            {
                Tick start = a.contains ("bar") ? (a["bar"].get<Tick>() - 1) * bar : 0;
                for (const auto& c : edit::findTrack (song, id)->clips)
                    if (start < c->end() && start + bar > c->start)
                        start = ((c->end() + bar - 1) / bar) * bar;
                Clip c;
                c.name = "Pattern";
                c.start = start;
                c.length = std::max<Tick> (length, bar * a.value ("play_bars", 4));
                c.loopLength = length;
                clipId = edit::addClip (song, id, c);
            }
            edit::modifyClip (song, clipId, [&] (Clip& c) {
                c.loopLength = length;
                c.length = std::max (c.length, length);
                std::erase_if (c.notes, [&] (const Note& n) {
                    if (n.start >= length)
                        return false;
                    return clearOthers || std::any_of (rows.begin(), rows.end(), [&] (const Row& r) { return r.note == n.pitch; });
                });
                const auto count = static_cast<std::size_t> (length / step);
                for (const auto& r : rows)
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const char ch = r.steps[i % r.steps.size()];
                        const int velocity = ch == 'X' ? 127 : (ch == 'x' ? 100 : (ch == 'o' ? 60 : 0));
                        if (velocity > 0)
                            c.notes.push_back ({ static_cast<Tick> (i) * step, std::max<Tick> (1, step / 2), static_cast<std::uint8_t> (r.note), static_cast<std::uint8_t> (velocity) });
                    }
                edit::normalise (c);
            });
        });
        auto read = getBeat (host, { { "track", id }, { "steps_per_beat", perBeat } });
        return read;
    }

    // ---- clip grid --------------------------------------------------------------------------
    int sceneArg (const json& a)
    {
        const int scene = a.at ("scene").get<int>();
        if (scene < 1 || scene > 512)
            throw ToolError ("scenes count from 1");
        return scene - 1;
    }

    json getClipGrid (Host& host)
    {
        const auto& s = host.song();
        json scenes = json::array();
        for (int i = 0; i < s.sceneCount(); ++i)
            scenes.push_back ({ { "scene", i + 1 }, { "name", static_cast<std::size_t> (i) < s.scenes.size() ? s.scenes[static_cast<std::size_t> (i)] : std::string() } });
        json tracks = json::array();
        for (const auto& t : s.tracks)
        {
            json slots = json::array();
            for (std::size_t i = 0; i < t.slots.size(); ++i)
                if (t.slots[i] != nullptr)
                    slots.push_back ({ { "scene", i + 1 }, { "clip", t.slots[i]->id }, { "name", t.slots[i]->name }, { "length_beats", beats (s, t.slots[i]->length) },
                                       { "notes", t.slots[i]->notes.size() } });
            for (std::size_t i = 0; i < t.audioSlots.size(); ++i)
                if (t.audioSlots[i] != nullptr)
                    slots.push_back ({ { "scene", i + 1 }, { "clip", t.audioSlots[i]->id }, { "name", t.audioSlots[i]->name },
                                       { "length_beats", beats (s, t.audioSlots[i]->length) }, { "audio_file", t.audioSlots[i]->file } });
            tracks.push_back ({ { "id", t.id }, { "name", t.name }, { "slots", slots } });
        }
        json out = { { "scenes", scenes }, { "tracks", tracks } };
        if (s.style)
            out["style"] = true; // the Clip grid is an arranger style (scenes are its sections)
        return out;
    }

    json setSlot (Host& host, const json& a)
    {
        const auto& s = host.song();
        const Track t = trackArg (s, a); // a copy: an edit replaces the song
        if (t.output.kind == TrackOutput::Kind::audio)
            throw ToolError ("set_slot puts MIDI clips; this is an audio track");
        const int scene = sceneArg (a);
        const Tick bar = barTicks (s);
        Clip clip;
        clip.name = a.value ("name", t.name);
        if (a.contains ("copy_clip"))
        {
            const auto source = edit::findClipPtr (s, a["copy_clip"].get<ClipId>());
            if (source == nullptr)
                throw ToolError ("no MIDI clip with that id");
            clip = *source;
            clip.id = 0;
            if (clip.loopLength > 0 && clip.loopLength < clip.length)
                clip.length = clip.loopLength; // a looping pattern goes in as one round
            clip.loopLength = 0;
            if (a.contains ("name"))
                clip.name = a["name"].get<std::string>();
        }
        if (a.contains ("notes"))
            for (const auto& n : a["notes"])
                clip.notes.push_back ({ tickAt (s, n.at ("bar").get<double>(), n.value ("beat", 1.0)), lengthTicks (s, n.at ("length").get<double>()),
                                        static_cast<std::uint8_t> (parsePitch (n.at ("pitch"))), static_cast<std::uint8_t> (std::clamp (n.value ("velocity", 90), 1, 127)) });
        Tick end = 0;
        for (const auto& n : clip.notes)
            end = std::max (end, n.end());
        clip.length = a.contains ("length_bars") ? bar * std::max (1, a["length_bars"].get<int>())
                                                 : std::max (a.contains ("copy_clip") ? clip.length : bar, ((end + bar - 1) / bar) * bar);
        const auto id = t.id;
        ClipId made = 0;
        host.edit ("AI: Set Slot", [&] (Song& song) { made = edit::setSlot (song, id, scene, clip); });
        return { { "track", t.name }, { "scene", scene + 1 }, { "clip", made }, { "length_beats", beats (s, clip.length) }, { "notes", clip.notes.size() } };
    }

    json clearSlot (Host& host, const json& a)
    {
        const Track t = trackArg (host.song(), a); // a copy: an edit replaces the song
        const int scene = sceneArg (a);
        const auto id = t.id;
        host.edit ("AI: Clear Slot", [&] (Song& song) { edit::clearSlot (song, id, scene); });
        return { { "cleared", { { "track", t.name }, { "scene", scene + 1 } } } };
    }

    json nameScene (Host& host, const json& a)
    {
        const int scene = sceneArg (a);
        const auto name = a.at ("name").get<std::string>();
        host.edit ("AI: Name Scene", [&] (Song& song) {
            if (song.scenes.size() <= static_cast<std::size_t> (scene))
                song.scenes.resize (static_cast<std::size_t> (scene) + 1);
            song.scenes[static_cast<std::size_t> (scene)] = name;
        });
        return { { "scene", scene + 1 }, { "name", name } };
    }

    json launch (Host& host, const json& a)
    {
        if (a.value ("stop", false))
        {
            host.stopSlots();
            return { { "stopped", true } };
        }
        const int scene = sceneArg (a);
        if (a.contains ("track"))
            host.launchSlot (findTrack (host.song(), a["track"]).id, scene);
        else
            host.launchScene (scene);
        return { { "launched", scene + 1 } };
    }

    json transport (Host& host, const json& a)
    {
        const auto action = a.at ("action").get<std::string>();
        if (action == "play")
            host.play();
        else if (action == "stop")
            host.stop();
        else if (action == "go_to")
            host.locate (tickAt (host.song(), a.at ("bar").get<double>(), 1.0));
        else
            throw ToolError ("action is play, stop or go_to");
        return { { "playing", host.isPlaying() }, { "playhead", position (host.song(), static_cast<Tick> (host.position())) } };
    }

    json undo (Host& host, const json& a)
    {
        const int steps = std::max (1, a.value ("steps", 1));
        const bool redo = a.value ("redo", false);
        int done = 0;
        for (int i = 0; i < steps; ++i)
            if (redo ? host.redo() : host.undo())
                ++done;
        return { { redo ? "redone" : "undone", done } };
    }

    // ---- styles and devices ------------------------------------------------------
    /** Lower case letters and digits only. */
    std::string squash (const std::string& s)
    {
        std::string out;
        for (char c : s)
            if (std::isalnum (static_cast<unsigned char> (c)))
                out += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    /** Words split at spaces and between letters and digits: "Beat16" -> beat, 16. */
    std::vector<std::string> words (const std::string& s)
    {
        std::vector<std::string> out;
        std::string w;
        int kind = 0; // 1 letter, 2 digit
        for (char raw : s)
        {
            const auto c = static_cast<unsigned char> (raw);
            const int k = std::isdigit (c) ? 2 : (std::isalpha (c) ? 1 : 0);
            if (k == 0 || (kind != 0 && k != kind))
            {
                if (! w.empty())
                    out.push_back (w);
                w.clear();
            }
            if (k != 0)
                w += static_cast<char> (std::tolower (c));
            kind = k;
        }
        if (! w.empty())
            out.push_back (w);
        return out;
    }

    /** How well a name matches a search: 0 = not at all. */
    int matchScore (const std::string& name, const std::string& search)
    {
        const auto n = squash (name), q = squash (search);
        if (q.empty())
            return 0;
        if (n == q)
            return 100;
        if (n.rfind (q, 0) == 0)
            return 80;
        if (n.find (q) != std::string::npos)
            return 60;
        const auto ws = words (search);
        if (ws.empty() || ! std::all_of (ws.begin(), ws.end(), [&n] (const std::string& w) { return n.find (w) != std::string::npos; }))
            return 0;
        return 40;
    }

    std::string stem (const std::string& path)
    {
        return std::filesystem::path (path).stem().string();
    }

    json findStyles (Host& host, const json& a)
    {
        const auto search = a.at ("name").get<std::string>();
        const auto limit = static_cast<std::size_t> (a.value ("limit", 20));
        const auto files = host.styleFiles();
        if (files.empty())
            throw ToolError ("no style folder is set: ask the user to choose one with Style > Style Folders for AI");
        std::vector<std::pair<int, std::string>> found;
        for (const auto& f : files)
            if (const int score = matchScore (stem (f), search); score > 0)
                found.push_back ({ score, f });
        std::stable_sort (found.begin(), found.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
        json list = json::array();
        for (std::size_t i = 0; i < found.size() && i < limit; ++i)
            list.push_back ({ { "name", stem (found[i].second) }, { "file", found[i].second } });
        return { { "styles", list }, { "matches", found.size() }, { "files_searched", files.size() } };
    }

    Style loadStyle (Host& host, const std::string& file)
    {
        const auto files = host.styleFiles();
        if (std::find (files.begin(), files.end(), file) == files.end())
            throw ToolError ("use a file from find_styles");
        try
        {
            return style_file::load (file);
        }
        catch (const std::exception& e)
        {
            throw ToolError ("this style file can't be read (only Yamaha styles can): " + std::string (e.what()));
        }
    }

    json getStyle (Host& host, const json& a)
    {
        const auto style = loadStyle (host, a.at ("file").get<std::string>());
        const auto barLength = style.timeSignature.ticksPerBar();
        json sections = json::array();
        for (const auto& sec : style.sections)
        {
            json parts = json::array();
            for (const auto& [channel, clip] : sec.parts)
                if (! clip.notes.empty())
                    parts.push_back ({ { "channel", channel }, { "notes", clip.notes.size() } });
            if (! parts.empty())
                sections.push_back ({ { "name", sec.name }, { "bars", static_cast<double> (sec.length) / static_cast<double> (std::max<Tick> (1, barLength)) }, { "parts", parts } });
        }
        return { { "name", style.name }, { "tempo", round3 (style.tempo) },
                 { "time_signature", std::to_string (style.timeSignature.numerator) + "/" + std::to_string (style.timeSignature.denominator) },
                 { "sections", sections } };
    }

    json fillFromStyle (Host& host, const json& a)
    {
        const auto style = loadStyle (host, a.at ("file").get<std::string>());
        style_convert::Fill fill;
        fill.section = a.value ("section", std::string ("Main A"));
        if (style.section (fill.section) == nullptr)
        {
            std::string names;
            for (const auto& sec : style.sections)
                names += (names.empty() ? "" : ", ") + sec.name;
            throw ToolError ("the style has no section \"" + fill.section + "\"; it has: " + names);
        }
        const auto chordText = a.at ("chords").get<std::string>();
        fill.chords = style_chord::parseList (chordText);
        if (fill.chords.empty())
            throw ToolError ("give chords like \"D Bm G A\"");
        fill.bars = std::clamp (a.at ("bars").get<int>(), 1, 999);
        fill.at = barTicks (host.song()) * (std::max (1, a.value ("start_bar", 1)) - 1);
        const bool useTempo = a.value ("use_style_tempo", false);
        std::vector<TrackId> made;
        host.edit ("AI: Fill Tracks from a Style", [&] (Song& s) {
            made = style_convert::fillTimeline (s, style, fill);
            if (! made.empty() && useTempo)
                s.tempo.set (0, std::clamp (style.tempo, 20.0, 400.0));
        });
        if (made.empty())
            throw ToolError ("the section has no notes");
        json tracks = json::array();
        for (const auto id : made)
            if (const auto* t = edit::findTrack (host.song(), id))
                tracks.push_back ({ { "id", t->id }, { "name", t->name }, { "sound", t->sound.name }, { "channel", t->output.channel } });
        return { { "added_tracks", tracks }, { "style", style.name }, { "section", fill.section }, { "bars", fill.bars } };
    }

    const DeviceProfile* profileNamed (Host& host, const std::string& search)
    {
        const auto* lib = host.devices();
        if (lib == nullptr)
            return nullptr;
        if (const auto* p = lib->byId (search))
            return p;
        const DeviceProfile* best = nullptr;
        int bestScore = 0;
        for (const auto& p : lib->all())
        {
            const int score = std::max (matchScore (p.name, search), matchScore (p.id, search));
            if (score > bestScore)
            {
                best = &p;
                bestScore = score;
            }
        }
        return best;
    }

    json listDevices (Host& host, const json&)
    {
        const auto* lib = host.devices();
        json outputs = json::array();
        for (const auto& port : host.midiOutputs())
        {
            json o = { { "output", port } };
            if (lib != nullptr)
                o["device"] = lib->forPort (port).name;
            outputs.push_back (o);
        }
        json profiles = json::array();
        if (lib != nullptr)
            for (const auto& p : lib->all())
                profiles.push_back ({ { "device", p.name }, { "sounds", p.soundCount() } });
        return { { "outputs", outputs }, { "profiles", profiles } };
    }

    json findSounds (Host& host, const json& a)
    {
        const auto device = a.at ("device").get<std::string>();
        const auto* profile = profileNamed (host, device);
        if (profile == nullptr && host.devices() != nullptr && matchScore ("General MIDI", device) > 0)
            profile = &host.devices()->generalMidi();
        if (profile == nullptr)
            throw ToolError ("no device profile matches \"" + device + "\" (see list_devices)");
        const auto search = a.value ("search", std::string());
        const auto limit = static_cast<std::size_t> (a.value ("limit", 30));
        std::vector<std::pair<int, json>> found;
        std::size_t total = 0;
        for (const auto& g : profile->groups)
            for (const auto& snd : g.sounds)
            {
                ++total;
                const int score = search.empty() ? 1 : std::max (matchScore (snd.name, search), matchScore (g.name + " " + snd.name, search) / 2);
                if (score > 0)
                    found.push_back ({ score, { { "name", snd.name }, { "group", g.name }, { "program", snd.program + 1 }, { "bank_msb", snd.bankMsb }, { "bank_lsb", snd.bankLsb } } });
            }
        std::stable_sort (found.begin(), found.end(), [] (const auto& x, const auto& y) { return x.first > y.first; });
        json list = json::array();
        for (std::size_t i = 0; i < found.size() && i < limit; ++i)
            list.push_back (found[i].second);
        json out = { { "device", profile->name }, { "sounds", list }, { "matches", found.size() } };
        if (found.empty() && total > 0)
            out["note"] = "no sound name matches; the sound names may be numbers only (" + profile->groups.front().sounds.front().name + "...)";
        return out;
    }

    json setTrackOutput (Host& host, const json& a)
    {
        const auto id = trackArg (host.song(), a).id;
        const auto output = a.at ("output").get<std::string>();
        const auto* lib = host.devices();
        const bool builtIn = matchScore ("built-in", output) >= 60 || matchScore ("soundfont", output) >= 60 || lower (output) == "internal";

        std::string port;
        const DeviceProfile* profile = nullptr;
        if (! builtIn)
        {
            // A plugged-in output by name, then by its device's name.
            int best = 0;
            for (const auto& p : host.midiOutputs())
            {
                int score = matchScore (p, output);
                if (lib != nullptr)
                    score = std::max (score, matchScore (lib->forPort (p).name, output));
                if (score > best)
                {
                    best = score;
                    port = p;
                }
            }
            if (! port.empty() && lib != nullptr)
                profile = &lib->forPort (port);
            else if (const auto* named = profileNamed (host, output); named != nullptr && ! named->ports.empty())
            {
                profile = named; // not plugged in now: use a name its port will match
                port = named->ports.front();
            }
            if (port.empty())
                throw ToolError ("no MIDI output or device matches \"" + output + "\" (see list_devices)");
        }

        TrackSound sound;
        bool hasSound = false;
        if (a.contains ("sound"))
        {
            const auto wanted = a["sound"].get<std::string>();
            if (builtIn)
            {
                const int program = programFrom (a["sound"]);
                sound = { kGeneralMidiNames[program], -1, -1, program };
                hasSound = true;
            }
            else if (profile != nullptr)
            {
                int best = 0;
                for (const auto& g : profile->groups)
                    for (const auto& snd : g.sounds)
                        if (const int score = matchScore (snd.name, wanted); score > best)
                        {
                            best = score;
                            sound = profile->trackSound (g, snd);
                            hasSound = true;
                        }
                if (! hasSound)
                    throw ToolError ("the " + profile->name + " has no sound named like \"" + wanted + "\" (see find_sounds)");
            }
        }

        host.edit ("AI: Change Track Output", [&] (Song& s) {
            auto* t = edit::findTrack (s, id);
            if (t->output.kind == TrackOutput::Kind::audio)
                return;
            t->output.kind = builtIn ? TrackOutput::Kind::soundFont : TrackOutput::Kind::midiPort;
            t->output.port = builtIn ? std::string() : port;
            if (a.contains ("channel"))
                t->output.channel = std::clamp (a["channel"].get<int>(), 1, 16);
            if (hasSound)
                t->sound = sound;
        });
        const auto& t = *edit::findTrack (host.song(), id);
        if (t.output.kind == TrackOutput::Kind::audio)
            throw ToolError ("an audio track has no MIDI output");
        json out = { { "track", trackJson (host.song(), t) } };
        if (! builtIn)
        {
            const auto plugged = host.midiOutputs();
            if (std::find (plugged.begin(), plugged.end(), port) == plugged.end())
                out["note"] = "this device is not plugged in now; the track plays on it once it is";
        }
        return out;
    }

    // ---- MIDI setup ------------------------------------------------------------------
    std::string trackInputJson (const Track& t)
    {
        return t.inputPort.empty() ? std::string ("all") : t.inputPort;
    }

    json getMidiSetup (Host& host, const json&)
    {
        const auto* lib = host.devices();
        const auto pages = host.controllers();
        json inputs = json::array();
        for (const auto& in : host.midiInputs())
        {
            json i = { { "input", in.name }, { "enabled", in.enabled } };
            const auto pageId = host.controllerPageFor (in.name);
            for (const auto& c : pages)
                if (c.id == pageId)
                    i["controller_page"] = c.name;
            if (! i.contains ("controller_page"))
                i["controller_page"] = "none (a keyboard: its notes play the selected track)";
            inputs.push_back (i);
        }
        json outputs = json::array();
        for (const auto& port : host.midiOutputs())
        {
            json o = { { "output", port } };
            if (lib != nullptr)
                o["device"] = lib->forPort (port).name;
            outputs.push_back (o);
        }
        json tracks = json::array();
        for (const auto& t : host.song().tracks)
        {
            if (t.output.kind == TrackOutput::Kind::audio)
                continue;
            json j = { { "id", t.id }, { "name", t.name }, { "input", trackInputJson (t) },
                       { "input_channel", t.inputChannel == 0 ? json ("all") : json (t.inputChannel) }, { "channel", t.output.channel } };
            j["output"] = t.output.kind == TrackOutput::Kind::midiPort ? t.output.port : std::string ("built-in");
            tracks.push_back (j);
        }
        json pageNames = json::array();
        for (const auto& c : pages)
            pageNames.push_back (c.name);
        json profiles = json::array();
        if (lib != nullptr)
            for (const auto& d : lib->all())
                profiles.push_back (d.name);
        return { { "inputs", inputs }, { "outputs", outputs }, { "tracks", tracks }, { "controller_pages", pageNames }, { "device_profiles", profiles } };
    }

    /** The plugged-in port that best matches a name; empty when none does. */
    std::string bestPort (const std::vector<std::string>& ports, const std::string& wanted)
    {
        std::string port;
        int best = 0;
        for (const auto& p : ports)
            if (const int score = p == wanted ? 1000000 : matchScore (p, wanted); score > best)
            {
                best = score;
                port = p;
            }
        return port;
    }

    std::vector<std::string> inputNames (const Host& host)
    {
        std::vector<std::string> names;
        for (const auto& in : host.midiInputs())
            names.push_back (in.name);
        return names;
    }

    json setMidiInput (Host& host, const json& a)
    {
        const auto port = bestPort (inputNames (host), a.at ("input").get<std::string>());
        if (port.empty())
            throw ToolError ("no MIDI input like that is plugged in; see get_midi_setup");
        const bool on = a.at ("on").get<bool>();
        if (! host.setMidiInputEnabled (port, on))
            throw ToolError ("the input " + port + " can't be changed");
        return { { "input", port }, { "enabled", on } };
    }

    json setPortDevice (Host& host, const json& a)
    {
        const auto kind = a.at ("kind").get<std::string>();
        const auto wanted = a.at ("port").get<std::string>();
        const auto device = a.at ("device").get<std::string>();
        const bool automatic = lower (device) == "auto" || device.empty();
        if (kind == "output")
        {
            auto port = bestPort (host.midiOutputs(), wanted);
            if (port.empty())
                throw ToolError ("no MIDI output like that is plugged in; see get_midi_setup");
            const auto* lib = host.devices();
            if (lib == nullptr)
                throw ToolError ("there are no device profiles");
            std::string id;
            if (! automatic)
            {
                int best = 0;
                for (const auto& d : lib->all())
                    if (const int score = std::max (d.id == device ? 1000000 : 0, std::max (matchScore (d.name, device), matchScore (d.id, device))); score > best)
                    {
                        best = score;
                        id = d.id;
                    }
                if (lower (device) == "general midi" || lower (device) == "gm")
                    id = lib->generalMidi().id;
                if (id.empty())
                    throw ToolError ("no device profile like \"" + device + "\"; see device_profiles in get_midi_setup, or write one with write_device_file");
            }
            host.assignDeviceProfile (port, id);
            return { { "output", port }, { "device", lib->forPort (port).name }, { "chosen_by", automatic ? "port name" : "hand" } };
        }
        if (kind != "input")
            throw ToolError ("kind is \"output\" or \"input\"");
        auto port = bestPort (inputNames (host), wanted);
        if (port.empty())
            throw ToolError ("no MIDI input like that is plugged in; see get_midi_setup");
        std::string id = automatic ? std::string() : std::string ("none");
        if (! automatic && lower (device) != "none")
        {
            id.clear();
            int best = 0;
            for (const auto& c : host.controllers())
                if (const int score = std::max (c.id == device ? 1000000 : 0, std::max (matchScore (c.name, device), matchScore (c.id, device))); score > best)
                {
                    best = score;
                    id = c.id;
                }
            if (id.empty())
                throw ToolError ("no controller page like \"" + device + "\"; see controller_pages in get_midi_setup, or write one with write_device_file");
        }
        host.assignControllerPage (port, id);
        std::string page = "none";
        const auto now = host.controllerPageFor (port);
        for (const auto& c : host.controllers())
            if (c.id == now)
                page = c.name;
        return { { "input", port }, { "controller_page", page }, { "chosen_by", automatic ? "port name" : "hand" } };
    }

    json setTrackInput (Host& host, const json& a)
    {
        const auto id = trackArg (host.song(), a).id;
        const auto wanted = a.at ("input").get<std::string>();
        std::string port;
        if (lower (wanted) != "all" && ! wanted.empty())
        {
            int best = 0;
            for (const auto& in : host.midiInputs())
                if (const int score = matchScore (in.name, wanted); score > best)
                {
                    best = score;
                    port = in.name;
                }
            if (port.empty())
                port = wanted; // not plugged in now: the name still matches it later
        }
        int channel = edit::findTrack (host.song(), id)->inputChannel;
        if (a.contains ("channel") && ! a["channel"].is_null())
        {
            const auto& c = a["channel"];
            if (c.is_string() && lower (c.get<std::string>()) == "all")
                channel = 0;
            else if (c.is_number_integer() && c.get<int>() >= 0 && c.get<int>() <= 16)
                channel = c.get<int>();
            else
                throw ToolError ("channel is 1 to 16, or 0 for all channels");
        }
        host.edit ("AI: Change Track Input", [&] (Song& s) {
            auto* t = edit::findTrack (s, id);
            t->inputPort = port;
            t->inputChannel = channel;
        });
        return { { "track", { { "id", id }, { "input", port.empty() ? std::string ("all") : port },
                              { "input_channel", channel == 0 ? json ("all") : json (channel) } } } };
    }

    json recentMidi (Host& host, const json& a)
    {
        const double seconds = std::clamp (a.value ("seconds", 15.0), 1.0, 120.0);
        json list = json::array();
        for (const auto& m : host.recentMidi (seconds))
        {
            if (m.bytes.empty())
                continue;
            const int status = m.bytes[0];
            const int kind = status & 0xf0;
            const auto byte = [&m] (std::size_t i) { return i < m.bytes.size() ? static_cast<int> (m.bytes[i]) : 0; };
            json j = { { "seconds_ago", round3 (m.secondsAgo) }, { "port", m.port } };
            if (status >= 0xf0)
                j["type"] = status == 0xf0 ? "sysex" : "system";
            else
            {
                j["channel"] = (status & 0x0f) + 1;
                switch (kind)
                {
                    case 0x90: j["type"] = byte (2) > 0 ? "note_on" : "note_off"; j["note"] = byte (1); j["velocity"] = byte (2); break;
                    case 0x80: j["type"] = "note_off"; j["note"] = byte (1); break;
                    case 0xb0: j["type"] = "control_change"; j["controller"] = byte (1); j["value"] = byte (2); break;
                    case 0xc0: j["type"] = "program_change"; j["program"] = byte (1) + 1; break;
                    case 0xe0: j["type"] = "pitch_bend"; j["value"] = (byte (2) << 7 | byte (1)) - 8192; break;
                    case 0xd0: j["type"] = "aftertouch"; j["value"] = byte (1); break;
                    default: j["type"] = "other"; break;
                }
            }
            list.push_back (j);
        }
        // Many messages from one knob: keep the newest 200.
        if (list.size() > 200)
            list.erase (list.begin(), list.end() - 200);
        return { { "messages", list } };
    }

    bool isControllerPage (const std::string& text)
    {
        return text.rfind ("controller:", 0) == 0 || text.find ("\ncontroller:") != std::string::npos;
    }

    json listDeviceFiles (Host& host, const json&)
    {
        json list = json::array();
        for (const auto& f : host.deviceFiles())
            list.push_back ({ { "name", f.name }, { "kind", device_profile::isSoundAddOn (f.text) ? "sound_list" : isControllerPage (f.text) ? "controller_page" : "device_profile" }, { "built_in", f.builtIn } });
        return { { "files", list } };
    }

    json readDeviceFile (Host& host, const json& a)
    {
        const auto name = a.at ("name").get<std::string>();
        const auto files = host.deviceFiles();
        // The user's own file wins over a built-in one with the same name.
        const Host::DeviceFile* best = nullptr;
        int bestScore = 0;
        for (const auto& f : files)
        {
            const int score = matchScore (f.name, name) * 2 + (f.builtIn ? 0 : 1);
            if (score > bestScore)
            {
                bestScore = score;
                best = &f;
            }
        }
        if (best == nullptr || bestScore < 2)
            throw ToolError ("no device file named like \"" + name + "\" (see list_device_files)");
        return { { "name", best->name }, { "built_in", best->builtIn }, { "text", best->text } };
    }

    json writeDeviceFile (Host& host, const json& a)
    {
        auto name = a.at ("name").get<std::string>();
        const auto text = a.at ("text").get<std::string>();
        std::string safe;
        for (char c : std::filesystem::path (name).filename().string())
            if (std::isalnum (static_cast<unsigned char> (c)) || c == '-' || c == '_' || c == '.')
                safe += c;
        if (safe.empty() || safe.front() == '.')
            throw ToolError ("give a plain file name like \"my-synth.yaml\"");
        if (const auto dot = safe.rfind ('.'); dot == std::string::npos || (lower (safe.substr (dot)) != ".yaml" && lower (safe.substr (dot)) != ".yml"))
            safe += ".yaml";

        json summary;
        try
        {
            if (device_profile::isSoundAddOn (text))
            {
                const auto addOn = device_profile::parseAddOn (text);
                if (host.devices() != nullptr && host.devices()->byId (addOn.profileId) == nullptr)
                    throw std::runtime_error ("soundsFor: there is no device profile \"" + addOn.profileId + "\"");
                summary = { { "kind", "sound_list" }, { "for", addOn.profileId }, { "groups", addOn.groups.size() } };
            }
            else if (isControllerPage (text))
            {
                const auto c = controller_profile::parse (text);
                summary = { { "kind", "controller_page" }, { "id", c.id }, { "name", c.name } };
            }
            else
            {
                const auto d = device_profile::parse (text);
                summary = { { "kind", "device_profile" }, { "id", d.id }, { "name", d.name }, { "sounds", d.soundCount() }, { "sound_settings", d.editor.parameterCount() } };
            }
        }
        catch (const std::exception& e)
        {
            throw ToolError (std::string ("the file has a mistake, nothing was saved: ") + e.what());
        }
        try
        {
            host.writeDeviceFile (safe, text);
        }
        catch (const std::exception& e)
        {
            throw ToolError (e.what());
        }
        summary["saved_as"] = safe;
        return summary;
    }


    // ---- synth sound editors -----------------------------------------------------------------
    const DeviceProfile& editableProfile (Host& host, const std::string& device)
    {
        const auto* p = profileNamed (host, device);
        if (p == nullptr)
            throw ToolError ("no device profile matches \"" + device + "\" (see list_devices)");
        if (p->editor.empty())
            throw ToolError ("the " + p->name + " has no sound editor in its profile; add an editor: part (see read_device_file roland-d-05.yaml for the pattern)");
        return *p;
    }

    json shownValue (const SynthParameter& p, int value)
    {
        if (p.isChoice())
            return p.display (value);
        if (p.noteFrom >= 0)
            return p.display (value);
        return value + p.offset;
    }

    json getSoundEditor (Host& host, const json& a)
    {
        const auto device = a.at ("device").get<std::string>();
        const auto& profile = editableProfile (host, device);
        const auto& editor = profile.editor;
        const auto pageName = a.value ("page", std::string());
        if (pageName.empty())
        {
            json pages = json::array();
            for (const auto& pg : editor.pages)
            {
                json names = json::array();
                for (const auto& p : pg.params)
                    names.push_back (p.name);
                pages.push_back ({ { "page", pg.name }, { "settings", names } });
            }
            return { { "device", profile.name }, { "pages", pages }, { "can_read_names", editor.names.has_value() },
                     { "note", "call again with a page for ranges and values" } };
        }
        const SynthPage* page = nullptr;
        int best = 0;
        for (const auto& pg : editor.pages)
            if (const int score = matchScore (pg.name, pageName); score > best)
            {
                best = score;
                page = &pg;
            }
        if (page == nullptr)
            throw ToolError ("the " + profile.name + " editor has no page \"" + pageName + "\"");
        json list = json::array();
        for (const auto& p : page->params)
        {
            json item = { { "name", p.name } };
            if (p.isChoice())
                item["choices"] = p.choices;
            else if (p.noteFrom >= 0)
                item["range"] = p.display (p.min) + " to " + p.display (p.max);
            else
            {
                item["min"] = p.min + p.offset;
                item["max"] = p.max + p.offset;
                if (! p.unit.empty())
                    item["unit"] = p.unit;
            }
            const int now = host.synthParameter (profile.id, page->name, p.name);
            if (now >= 0)
                item["value"] = shownValue (p, now);
            list.push_back (item);
        }
        return { { "device", profile.name }, { "page", page->name }, { "settings", list } };
    }

    /** A value from the AI (shown scale, choice name or note name) as the MIDI value. */
    int midiValue (const SynthParameter& p, const json& v)
    {
        if (v.is_string())
        {
            const auto text = v.get<std::string>();
            for (std::size_t i = 0; i < p.choices.size(); ++i)
                if (lower (p.choices[i]) == lower (text))
                    return p.choiceValue (i);
            if (p.noteFrom >= 0)
                for (int x = p.min; x <= p.max; ++x)
                    if (lower (p.display (x)) == lower (text))
                        return x;
            for (std::size_t i = 0; i < p.choices.size(); ++i)
                if (matchScore (p.choices[i], text) >= 60)
                    return p.choiceValue (i);
            try
            {
                return p.clamp (std::stoi (text) - p.offset);
            }
            catch (const std::exception&)
            {
                throw ToolError ("\"" + text + "\" is not a value of " + p.name);
            }
        }
        if (! v.is_number())
            throw ToolError ("the value of " + p.name + " is a number or a name");
        const int n = static_cast<int> (std::lround (v.get<double>()));
        if (p.isChoice() && p.values.empty() && n >= 0 && n < static_cast<int> (p.choices.size()) && p.min == 0)
            return n;
        return p.clamp (n - (p.noteFrom >= 0 ? 0 : p.offset));
    }

    json setSound (Host& host, const json& a)
    {
        const auto device = a.at ("device").get<std::string>();
        const auto& profile = editableProfile (host, device);
        const auto& editor = profile.editor;
        json done = json::array(), problems = json::array();
        bool pluggedIn = true;
        for (const auto& s : a.at ("settings"))
        {
            const auto name = s.at ("setting").get<std::string>();
            const SynthPage* page = nullptr;
            const SynthParameter* param = editor.find (name, &page);
            if (param == nullptr)
            {
                // "Filter Frequency" or a near name: the best match over "Page Name".
                int best = 0;
                for (const auto& pg : editor.pages)
                    for (const auto& p : pg.params)
                    {
                        const int score = std::max (matchScore (pg.name + " " + p.name, name), matchScore (p.name, name) - 1);
                        if (score > best)
                        {
                            best = score;
                            param = &p;
                            page = &pg;
                        }
                    }
                if (best < 40)
                    param = nullptr;
            }
            if (param == nullptr || page == nullptr)
            {
                problems.push_back ("no setting \"" + name + "\" (see get_sound_editor)");
                continue;
            }
            try
            {
                const int value = midiValue (*param, s.at ("value"));
                pluggedIn = host.sendSynthParameter (profile.id, page->name, param->name, value) && pluggedIn;
                done.push_back ({ { "setting", page->name + "/" + param->name }, { "value", shownValue (*param, value) } });
            }
            catch (const ToolError& e)
            {
                problems.push_back (e.what());
            }
        }
        json out = { { "device", profile.name }, { "set", done } };
        if (! problems.empty())
            out["problems"] = problems;
        if (! pluggedIn)
            out["note"] = "the " + profile.name + " is not plugged in: the values are kept in the app's sound editor, but the synth did not get them";
        return out;
    }

    json readSoundNamesTool (Host& host, const json& a)
    {
        const auto* p = profileNamed (host, a.at ("device").get<std::string>());
        if (p == nullptr)
            throw ToolError ("no device profile matches \"" + a.at ("device").get<std::string>() + "\"");
        if (! p->editor.names)
            throw ToolError ("the " + p->name + " profile doesn't say how to read its sound names; use name_sounds to name them by hand");
        const auto problem = host.readSoundNames (p->id);
        if (! problem.empty())
            throw ToolError (problem);
        return { { "device", p->name }, { "started", true }, { "note", "reading takes about half a minute; the names then show in find_sounds" } };
    }

    json nameSounds (Host& host, const json& a)
    {
        const auto* p = profileNamed (host, a.at ("device").get<std::string>());
        if (p == nullptr)
            throw ToolError ("no device profile matches \"" + a.at ("device").get<std::string>() + "\"");
        SoundGroup group;
        group.name = a.at ("group").get<std::string>();
        const SoundGroup* existing = nullptr;
        for (const auto& g : p->groups)
            if (lower (g.name) == lower (group.name) || (existing == nullptr && matchScore (g.name, group.name) >= 80))
                existing = &g;
        if (existing != nullptr)
            group.name = existing->name;
        for (const auto& s : a.at ("sounds"))
        {
            ProfileSound snd;
            snd.name = s.at ("name").get<std::string>();
            snd.program = std::clamp (s.at ("program").get<int>(), 1, 128) - 1;
            snd.bankMsb = s.value ("bank_msb", -2);
            snd.bankLsb = s.value ("bank_lsb", -2);
            if (snd.bankMsb == -2 || snd.bankLsb == -2)
            {
                // The bank of the sound with that program in the group, else of the group's first sound.
                const ProfileSound* like = nullptr;
                if (existing != nullptr)
                {
                    for (const auto& x : existing->sounds)
                        if (x.program == snd.program && like == nullptr)
                            like = &x;
                    if (like == nullptr && ! existing->sounds.empty())
                        like = &existing->sounds.front();
                }
                if (snd.bankMsb == -2)
                    snd.bankMsb = like != nullptr ? like->bankMsb : -1;
                if (snd.bankLsb == -2)
                    snd.bankLsb = like != nullptr ? like->bankLsb : -1;
            }
            if (snd.name.empty())
                throw ToolError ("a sound needs a name");
            group.sounds.push_back (snd);
        }

        // Join the user's own sound list file for this device.
        const auto fileName = p->id + ".sounds.yaml";
        SoundAddOn mine;
        mine.profileId = p->id;
        for (const auto& f : host.deviceFiles())
            if (! f.builtIn && f.name == fileName)
                mine = device_profile::parseAddOn (f.text);
        mine.merge ({ group });
        try
        {
            host.writeDeviceFile (fileName, device_profile::writeAddOn (mine));
        }
        catch (const std::exception& e)
        {
            throw ToolError (e.what());
        }
        return { { "device", p->name }, { "group", group.name }, { "saved", group.sounds.size() }, { "file", fileName } };
    }

    json runTool (Host& host, const std::string& name, const json& a)
    {
        if (name == "get_song") return getSong (host);
        if (name == "get_notes") return getNotes (host, a);
        if (name == "add_track") return addTrack (host, a);
        if (name == "set_track") return setTrack (host, a);
        if (name == "remove_track") return removeTrack (host, a);
        if (name == "add_notes") return addNotes (host, a);
        if (name == "remove_notes") return removeNotes (host, a);
        if (name == "change_notes") return changeNotes (host, a);
        if (name == "edit_clips") return editClips (host, a);
        if (name == "set_song") return setSong (host, a);
        if (name == "find_chords") return findChords (host, a);
        if (name == "set_chords") return setChords (host, a);
        if (name == "add_score_marks") return addScoreMarks (host, a);
        if (name == "set_lyrics") return setLyrics (host, a);
        if (name == "remove_score_marks") return removeScoreMarks (host, a);
        if (name == "get_beat") return getBeat (host, a);
        if (name == "set_beat") return setBeat (host, a);
        if (name == "get_clip_grid") return getClipGrid (host);
        if (name == "set_slot") return setSlot (host, a);
        if (name == "clear_slot") return clearSlot (host, a);
        if (name == "name_scene") return nameScene (host, a);
        if (name == "launch") return launch (host, a);
        if (name == "transport") return transport (host, a);
        if (name == "undo") return undo (host, a);
        if (name == "find_styles") return findStyles (host, a);
        if (name == "get_style") return getStyle (host, a);
        if (name == "fill_from_style") return fillFromStyle (host, a);
        if (name == "list_devices") return listDevices (host, a);
        if (name == "find_sounds") return findSounds (host, a);
        if (name == "set_track_output") return setTrackOutput (host, a);
        if (name == "get_midi_setup") return getMidiSetup (host, a);
        if (name == "set_track_input") return setTrackInput (host, a);
        if (name == "set_midi_input") return setMidiInput (host, a);
        if (name == "set_port_device") return setPortDevice (host, a);
        if (name == "recent_midi") return recentMidi (host, a);
        if (name == "list_device_files") return listDeviceFiles (host, a);
        if (name == "read_device_file") return readDeviceFile (host, a);
        if (name == "write_device_file") return writeDeviceFile (host, a);
        if (name == "get_sound_editor") return getSoundEditor (host, a);
        if (name == "set_sound") return setSound (host, a);
        if (name == "read_sound_names") return readSoundNamesTool (host, a);
        if (name == "name_sounds") return nameSounds (host, a);
        throw ToolError ("no tool named " + name);
    }

    constexpr const char* kInstructions =
        "This server edits the song open in Arranger MIDI DAW, a MIDI sequencer. Call get_song first to see the tracks and their ids. "
        "Positions are a bar (from 1) and a beat in the bar (from 1; 2.5 is halfway through beat 2). Lengths are in beats of the time signature "
        "(in 4/4 a beat is a quarter note). Pitches are MIDI numbers or names: C4 = 60 = middle C. Each change is one undo step in the app. "
        "Use General MIDI sound names for instruments; drums use General MIDI drum notes (36 kick, 38 snare, 42 closed hi-hat, 46 open hi-hat, 49 crash). "
        "To start from an arranger style, use find_styles, get_style and fill_from_style. "
        "To play a track on a hardware synth (for example a Roland D-05 or a Yamaha PSR-S970), use list_devices, find_sounds and set_track_output. "
        "For the MIDI setup (inputs, controllers, mappings, new synths, sound names), use get_midi_setup, set_midi_input, set_track_input, set_port_device, recent_midi and the device file tools. "
        "To map a knob or button (MIDI Learn), ask the user to move it, read it with recent_midi, then add a binding to the controller page with read_device_file and write_device_file. "
        "To shape a hardware synth's sound, use get_sound_editor and set_sound; to learn or name its stored sounds, use read_sound_names and name_sounds. "
        "To add a sound editor for a new synth, write the editor: part of its device profile from its MIDI chart (read roland-d-05.yaml or novation-xiosynth.yaml for the pattern).";
}

Server::Server (Host& h) : host (h) {}

std::string Server::toolList() const
{
    return toolDefinitions().dump();
}

std::string Server::call (const std::string& tool, const std::string& arguments)
{
    const auto a = arguments.empty() ? json::object() : json::parse (arguments);
    return runTool (host, tool, a).dump();
}

std::string Server::handle (const std::string& message)
{
    json request;
    try
    {
        request = json::parse (message);
    }
    catch (const json::exception&)
    {
        return json ({ { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32700 }, { "message", "Parse error" } } } }).dump();
    }
    if (! request.is_object() || ! request.contains ("method"))
        return {}; // a reply from the client, or nothing we know
    const auto method = request["method"].get<std::string>();
    if (! request.contains ("id"))
        return {}; // a notification, for example notifications/initialized
    const auto id = request["id"];
    const auto params = request.value ("params", json::object());
    const auto reply = [&] (json result) { return json ({ { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (result) } }).dump(); };
    const auto error = [&] (int code, const std::string& text) {
        return json ({ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", text } } } }).dump();
    };

    if (method == "initialize")
        return reply ({ { "protocolVersion", params.value ("protocolVersion", std::string (kProtocolVersion)) },
                        { "capabilities", { { "tools", { { "listChanged", false } } } } },
                        { "serverInfo", { { "name", "arranger-midi-daw" }, { "title", "Arranger MIDI DAW" }, { "version", "1.0" } } },
                        { "instructions", kInstructions } });
    if (method == "ping")
        return reply (json::object());
    if (method == "tools/list")
        return reply ({ { "tools", toolDefinitions() } });
    if (method == "tools/call")
    {
        const auto name = params.value ("name", std::string());
        const auto arguments = params.value ("arguments", json::object());
        try
        {
            const auto result = runTool (host, name, arguments);
            return reply ({ { "content", { { { "type", "text" }, { "text", result.dump() } } } }, { "isError", false } });
        }
        catch (const std::exception& e)
        {
            // Wrong arguments go back to the model as a tool error, so it can try again.
            return reply ({ { "content", { { { "type", "text" }, { "text", std::string ("Error: ") + e.what() } } } }, { "isError", true } });
        }
    }
    return error (-32601, "Method not found: " + method);
}

} // namespace arranger::mcp
