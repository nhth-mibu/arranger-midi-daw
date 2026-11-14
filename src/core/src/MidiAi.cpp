#include "arranger/core/MidiAi.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/Notation.h"
#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace arranger::ai
{

using nlohmann::json;

namespace
{
    constexpr double kQuarter = static_cast<double> (kTicksPerQuarter);
    constexpr int kDrumProgram = 128; // the Anticipatory Music Transformer's number for drums
    constexpr std::size_t kMostNotes = 3000; // to a helper; more notes make it slow and don't help

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    // ---- arguments ------------------------------------------------------------------------
    const Track* findTrack (const Song& s, const json& ref)
    {
        if (ref.is_number_integer())
            return edit::findTrack (s, ref.get<TrackId>());
        if (! ref.is_string())
            return nullptr;
        const auto name = lower (ref.get<std::string>());
        for (const auto& t : s.tracks)
            if (lower (t.name) == name)
                return &t;
        for (const auto& t : s.tracks)
            if (lower (t.name).find (name) != std::string::npos)
                return &t;
        return nullptr;
    }

    const Track& trackArg (const Song& s, const json& a, const char* key = "track")
    {
        if (! a.contains (key))
            throw std::runtime_error (std::string ("give \"") + key + "\": a track name or id (see get_song)");
        const auto* t = findTrack (s, a[key]);
        if (t == nullptr)
            throw std::runtime_error ("no such track: " + a[key].dump() + ". Call get_song to see the tracks.");
        if (t->output.kind == TrackOutput::Kind::audio)
            throw std::runtime_error (t->name + " is an audio track; the AI tools work on MIDI tracks");
        return *t;
    }

    /** Bars from "from_bar" to "to_bar" (both inclusive). `defaultBars`: the length when to_bar is missing. */
    std::pair<Tick, Tick> barRange (const Song& s, const json& a, int defaultBars)
    {
        const Tick bar = s.timeSignature.ticksPerBar();
        if (! a.contains ("from_bar"))
            throw std::runtime_error ("give \"from_bar\" (the first bar, from 1)");
        const int from = a["from_bar"].get<int>();
        const int to = a.value ("to_bar", from + defaultBars - 1);
        if (from < 1 || to < from)
            throw std::runtime_error ("from_bar must be 1 or more, and to_bar must not be before from_bar");
        if (to - from + 1 > 64)
            throw std::runtime_error ("at most 64 bars at a time");
        return { static_cast<Tick> (from - 1) * bar, static_cast<Tick> (to) * bar };
    }

    int programFrom (const json& v)
    {
        if (v.is_number_integer())
        {
            const int p = v.get<int>();
            if (p < 1 || p > 128)
                throw std::runtime_error ("a General MIDI sound is 1 to 128");
            return p - 1;
        }
        if (! v.is_string())
            throw std::runtime_error ("a sound is a General MIDI number (1-128), a name like \"Fingered Bass\", or \"drums\"");
        const auto name = lower (v.get<std::string>());
        if (name == "drums" || name == "drum kit" || name == "drum")
            return kDrumProgram;
        for (int i = 0; i < 128; ++i)
            if (lower (kGeneralMidiNames[i]) == name)
                return i;
        for (int i = 0; i < 128; ++i)
            if (lower (kGeneralMidiNames[i]).find (name) != std::string::npos)
                return i;
        throw std::runtime_error ("no General MIDI sound \"" + v.get<std::string>() + "\"");
    }

    double temperatureArg (const json& a, double fallback)
    {
        const double t = a.value ("temperature", fallback);
        if (t <= 0.0 || t > 2.0)
            throw std::runtime_error ("temperature is from 0.1 (safe) to 2 (wild)");
        return t;
    }

    Tick quantiseArg (const json& a, const char* fallback)
    {
        const auto q = a.value ("quantise", std::string (fallback));
        static const std::map<std::string, Tick> steps { { "none", 0 },
                                                         { "1/8", kTicksPerQuarter / 2 },
                                                         { "1/8T", kTicksPerQuarter / 3 },
                                                         { "1/16", kTicksPerQuarter / 4 },
                                                         { "1/16T", kTicksPerQuarter / 6 },
                                                         { "1/32", kTicksPerQuarter / 8 } };
        const auto it = steps.find (q);
        if (it == steps.end())
            throw std::runtime_error ("quantise is none, 1/8, 1/8T, 1/16, 1/16T or 1/32");
        return it->second;
    }

    // ---- notes ----------------------------------------------------------------------------
    bool isDrums (const Track& t) { return notation::isDrumTrack (t); }

    int programOf (const Track& t)
    {
        if (isDrums (t))
            return kDrumProgram;
        return t.sound.program >= 0 && t.sound.program < 128 ? t.sound.program : 0;
    }

    /** A track's notes that start in [from, to), as helper notes relative to `origin`. */
    json notesJson (const Track& t, Tick from, Tick to, Tick origin)
    {
        json out = json::array();
        const int program = programOf (t);
        for (const auto& n : notation::timelineNotes (t))
        {
            if (n.start < from || n.start >= to)
                continue;
            if (out.size() >= kMostNotes)
                break;
            out.push_back ({ { "start", static_cast<double> (n.start - origin) / kQuarter },
                             { "length", static_cast<double> (n.length) / kQuarter },
                             { "pitch", n.pitch },
                             { "velocity", n.velocity },
                             { "program", program == kDrumProgram ? 0 : program },
                             { "drum", program == kDrumProgram } });
        }
        return out;
    }

    void append (json& to, const json& from)
    {
        for (const auto& n : from)
            to.push_back (n);
    }

    /** The MIDI tracks that take part (not muted, with notes). */
    std::vector<const Track*> playingTracks (const Song& s)
    {
        std::vector<const Track*> out;
        const bool anySolo = std::any_of (s.tracks.begin(), s.tracks.end(), [] (const Track& t) { return t.mixer.solo; });
        for (const auto& t : s.tracks)
        {
            if (t.output.kind == TrackOutput::Kind::audio || t.mixer.mute || (anySolo && ! t.mixer.solo))
                continue;
            out.push_back (&t);
        }
        return out;
    }

    /** Helper notes (relative to `origin`) to song notes in [from, to). */
    std::vector<Note> notesFrom (const json& list, Tick origin, Tick from, Tick to)
    {
        std::vector<Note> out;
        if (! list.is_array())
            throw std::runtime_error ("the AI helper gave no notes");
        for (const auto& n : list)
        {
            Note note;
            note.start = origin + static_cast<Tick> (std::llround (n.at ("start").get<double>() * kQuarter));
            note.length = std::max<Tick> (1, static_cast<Tick> (std::llround (n.value ("length", 0.25) * kQuarter)));
            note.pitch = static_cast<std::uint8_t> (std::clamp (n.at ("pitch").get<int>(), 0, 127));
            note.velocity = static_cast<std::uint8_t> (std::clamp (n.value ("velocity", 90), 1, 127));
            if (note.start < from || note.start >= to)
                continue;
            note.length = std::min (note.length, to - note.start);
            out.push_back (note);
        }
        std::sort (out.begin(), out.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });
        return out;
    }

    /** Puts notes (song positions) on a track in place of what is there from `from` to `to`. Returns the count. */
    std::size_t replaceRange (Song& s, TrackId track, Tick from, Tick to, std::vector<Note> notes, const std::string& clipName)
    {
        edit::clearRange (s, track, from, to);
        if (notes.empty())
            return 0;
        Clip clip;
        clip.name = clipName;
        clip.start = from;
        clip.length = to - from;
        for (auto n : notes)
        {
            n.start -= from;
            clip.notes.push_back (n);
        }
        edit::addClip (s, track, std::move (clip));
        return notes.size();
    }

    /** A new track for an AI part, after the last track. */
    TrackId newTrack (Song& s, const std::string& name, int program)
    {
        auto& t = edit::addTrack (s, name);
        t.output.kind = TrackOutput::Kind::soundFont;
        if (program == kDrumProgram)
        {
            t.output.channel = 10;
            t.sound = { "Drums", -1, -1, 0 };
        }
        else
        {
            if (t.output.channel == 10)
                t.output.channel = 11;
            t.sound = { kGeneralMidiNames[program], -1, -1, program };
        }
        return t.id;
    }

    json positionJson (const Song& s, Tick t)
    {
        return t / s.timeSignature.ticksPerBar() + 1;
    }

    std::string resultText (const Song& s, const std::string& track, std::size_t notes, Tick from, Tick to, bool newTrack, const std::string& engine)
    {
        json out = { { "track", track }, { "notes", notes }, { "from_bar", positionJson (s, from) }, { "to_bar", positionJson (s, to - 1) }, { "engine", engine } };
        if (newTrack)
            out["new_track"] = true;
        if (notes == 0)
            out["note"] = "The AI wrote no notes this time. Try again, or with a higher temperature.";
        return out.dump();
    }

    json parseAnswer (const std::string& answer)
    {
        const auto r = json::parse (answer.empty() ? std::string ("{}") : answer);
        if (r.contains ("error"))
            throw std::runtime_error (r["error"].get<std::string>());
        return r;
    }

    double quartersPerBar (const Song& s)
    {
        return static_cast<double> (s.timeSignature.ticksPerBar()) / kQuarter;
    }

    void needFourFour (const Song& s, const std::string& what)
    {
        if (s.timeSignature.numerator != 4 || s.timeSignature.denominator != 4)
            throw std::runtime_error (what + " works in 4/4 only. Use engine \"simple\", or the Anticipatory Music Transformer (ai_continue with engine \"anticipation\").");
    }

    // ---- the tools ------------------------------------------------------------------------
    Job humanizeJob (const Song& s, const json& a)
    {
        const auto& t = trackArg (s, a);
        const auto [from, to] = barRange (s, a, 4);
        const bool drums = isDrums (t);
        const auto engine = a.value ("engine", std::string (drums ? "magenta" : "simple"));
        const double amount = std::clamp (a.value ("amount", 0.5), 0.0, 1.0);
        const auto id = t.id;
        const auto name = t.name;
        Job job;
        job.tool = "ai_humanize";
        job.title = "AI: Humanize";
        if (engine == "simple")
        {
            if (notesJson (t, from, to, from).empty())
                throw std::runtime_error (name + " has no notes in those bars");
            const auto seed = a.value ("seed", 1u);
            job.apply = [=] (Song& song, const std::string&) {
                const auto* track = edit::findTrack (song, id);
                std::vector<Note> notes;
                for (const auto& n : notation::timelineNotes (*track))
                    if (n.start >= from && n.start < to)
                        notes.push_back (n);
                notes = humanize (notes, song.timeSignature, { amount, drums, seed });
                for (auto& n : notes)
                    n.start = std::clamp (n.start, from, to - 1);
                const auto count = replaceRange (song, id, from, to, notes, "Humanized");
                return resultText (song, name, count, from, to, false, "simple");
            };
            return job;
        }
        if (engine != "magenta")
            throw std::runtime_error ("engine is magenta (GrooVAE, for drums) or simple");
        if (! drums)
            throw std::runtime_error ("Magenta Groove is for drum tracks. Use engine \"simple\" for " + name + ".");
        needFourFour (s, "Magenta Groove");
        const auto notes = notesJson (t, from, to, from);
        if (notes.empty())
            throw std::runtime_error (name + " has no notes in those bars");
        job.helper = kMagenta;
        job.request = json ({ { "op", "groove" },
                              { "quarters", static_cast<double> (to - from) / kQuarter },
                              { "notes", notes },
                              { "temperature", temperatureArg (a, 0.5) },
                              { "amount", amount } })
                          .dump();
        job.apply = [=] (Song& song, const std::string& answer) {
            const auto r = parseAnswer (answer);
            const auto count = replaceRange (song, id, from, to, notesFrom (r.at ("notes"), from, from, to), "Groove");
            return resultText (song, name, count, from, to, false, "magenta groovae");
        };
        return job;
    }

    Job drumifyJob (const Song& s, const json& a)
    {
        const auto& source = trackArg (s, a);
        const auto [from, to] = barRange (s, a, 4);
        needFourFour (s, "Drumify");
        const auto notes = notesJson (source, from, to, from);
        if (notes.empty())
            throw std::runtime_error (source.name + " has no notes in those bars to take the rhythm from");
        TrackId target = 0;
        std::string targetName = a.value ("name", std::string ("AI Drums"));
        if (a.contains ("to_track"))
        {
            const auto& t = trackArg (s, a, "to_track");
            if (! isDrums (t))
                throw std::runtime_error (t.name + " is not a drum track");
            target = t.id;
            targetName = t.name;
        }
        Job job;
        job.tool = "ai_drumify";
        job.title = "AI: Drumify";
        job.helper = kMagenta;
        job.request = json ({ { "op", "drumify" },
                              { "quarters", static_cast<double> (to - from) / kQuarter },
                              { "notes", notes },
                              { "temperature", temperatureArg (a, 0.6) } })
                          .dump();
        job.apply = [=] (Song& song, const std::string& answer) {
            const auto r = parseAnswer (answer);
            const bool made = target == 0;
            const auto id = made ? newTrack (song, targetName, kDrumProgram) : target;
            const auto count = replaceRange (song, id, from, to, notesFrom (r.at ("notes"), from, from, to), "Drumify");
            return resultText (song, edit::findTrack (song, id)->name, count, from, to, made, "magenta groovae tap2drum");
        };
        return job;
    }

    /** Where each instrument of the answer goes. */
    struct Targets
    {
        std::map<int, TrackId> byInstrument; ///< existing tracks
        int newInstrument = -1;              ///< >= 0: make a new track for this one
        std::string newName;
    };

    Job anticipationJob (const Song& s, const json& a, const std::string& tool, Tick from, Tick to, Targets targets, const std::set<TrackId>& generated)
    {
        // Context: what all tracks play before the range (history) and after it (so the AI joins up with it).
        const Tick contextBars = std::clamp (a.value ("context_bars", 8), 0, 32);
        const Tick context = contextBars * s.timeSignature.ticksPerBar();
        json history = json::array();
        json controls = json::array();
        for (const auto* t : playingTracks (s))
        {
            append (history, notesJson (*t, std::max<Tick> (0, from - context), from, from));
            if (generated.count (t->id) != 0)
                append (history, notesJson (*t, to, to + context, from)); // its own future: the AI fills the gap up to it
            else
                append (controls, notesJson (*t, from, to + context, from)); // the other parts: the AI plays along
        }
        std::vector<int> instruments;
        for (const auto& [instrument, track] : targets.byInstrument)
            instruments.push_back (instrument);
        if (targets.newInstrument >= 0)
            instruments.push_back (targets.newInstrument);
        if (instruments.empty())
            throw std::runtime_error ("no part to write");

        Job job;
        job.tool = tool;
        job.title = tool == "ai_write_part" ? "AI: Write Part" : "AI: Continue";
        job.helper = kAnticipation;
        job.request = json ({ { "op", "generate" },
                              { "tempo", s.tempo.tempoAt (static_cast<double> (from)) },
                              { "quarters", static_cast<double> (to - from) / kQuarter },
                              { "inputs", history },
                              { "controls", controls },
                              { "instruments", instruments },
                              { "top_p", std::clamp (a.value ("top_p", 0.95), 0.5, 1.0) },
                              { "model", a.value ("model", std::string ("small")) } })
                          .dump();
        const auto step = quantiseArg (a, "1/16");
        job.apply = [=] (Song& song, const std::string& answer) {
            const auto r = parseAnswer (answer);
            std::map<int, json> byInstrument;
            for (const auto& n : r.at ("notes"))
            {
                const int instrument = n.value ("drum", false) ? kDrumProgram : n.value ("program", 0);
                byInstrument[instrument].push_back (n);
            }
            json tracks = json::array();
            std::size_t total = 0;
            auto put = [&] (TrackId id, const json& list, bool made) {
                auto notes = snap (notesFrom (list.is_null() ? json::array() : list, from, from, to), step);
                const auto count = replaceRange (song, id, from, to, notes, "AI");
                total += count;
                json one = { { "track", edit::findTrack (song, id)->name }, { "notes", count } };
                if (made)
                    one["new_track"] = true;
                tracks.push_back (one);
            };
            for (const auto& [instrument, id] : targets.byInstrument)
                put (id, byInstrument[instrument], false);
            if (targets.newInstrument >= 0)
                put (newTrack (song, targets.newName, targets.newInstrument), byInstrument[targets.newInstrument], true);
            json out = { { "tracks", tracks }, { "from_bar", positionJson (song, from) }, { "to_bar", positionJson (song, to - 1) }, { "engine", "anticipatory music transformer" } };
            if (total == 0)
                out["note"] = "The AI wrote no notes this time. Try again, or with a higher top_p.";
            return out.dump();
        };
        return job;
    }

    Job continueJob (const Song& s, const json& a)
    {
        const auto [from, to] = barRange (s, a, 4);
        const auto engine = a.value ("engine", std::string (a.contains ("track") ? "magenta" : "anticipation"));
        if (engine == "anticipation")
        {
            Targets targets;
            std::set<TrackId> generated;
            if (a.contains ("track"))
            {
                const auto& t = trackArg (s, a);
                targets.byInstrument[programOf (t)] = t.id;
                generated.insert (t.id);
            }
            else
            {
                for (const auto* t : playingTracks (s))
                {
                    if (notation::timelineNotes (*t).empty())
                        continue;
                    // One track per instrument: the first one gets the notes.
                    if (targets.byInstrument.emplace (programOf (*t), t->id).second)
                        generated.insert (t->id);
                }
                if (targets.byInstrument.empty())
                    throw std::runtime_error ("there are no notes to continue from");
            }
            return anticipationJob (s, a, "ai_continue", from, to, targets, generated);
        }
        if (engine != "magenta")
            throw std::runtime_error ("engine is magenta (one track: MelodyRNN or DrumsRNN) or anticipation (one or all tracks)");
        const auto& t = trackArg (s, a);
        needFourFour (s, "Magenta continue");
        const Tick primerLength = std::clamp (a.value ("context_bars", 4), 1, 16) * s.timeSignature.ticksPerBar();
        const auto primer = notesJson (t, std::max<Tick> (0, from - primerLength), from, from);
        if (primer.empty())
            throw std::runtime_error (t.name + " has no notes in the bars before bar " + std::to_string (from / s.timeSignature.ticksPerBar() + 1) + " to continue from");
        const bool drums = isDrums (t);
        const auto id = t.id;
        const auto name = t.name;
        Job job;
        job.tool = "ai_continue";
        job.title = "AI: Continue";
        job.helper = kMagenta;
        job.request = json ({ { "op", "continue" },
                              { "kind", drums ? "drums" : "melody" },
                              { "primer", primer },
                              { "quarters", static_cast<double> (to - from) / kQuarter },
                              { "temperature", temperatureArg (a, 1.0) } })
                          .dump();
        job.apply = [=] (Song& song, const std::string& answer) {
            const auto r = parseAnswer (answer);
            const auto count = replaceRange (song, id, from, to, notesFrom (r.at ("notes"), from, from, to), "AI");
            return resultText (song, name, count, from, to, false, drums ? "magenta drum_kit_rnn" : "magenta melody_rnn");
        };
        return job;
    }

    Job writePartJob (const Song& s, const json& a)
    {
        const auto [from, to] = barRange (s, a, 8);
        Targets targets;
        std::set<TrackId> generated;
        if (a.contains ("track"))
        {
            const auto& t = trackArg (s, a);
            targets.byInstrument[programOf (t)] = t.id;
            generated.insert (t.id);
        }
        else
        {
            if (! a.contains ("sound"))
                throw std::runtime_error ("give \"sound\" (a General MIDI name like \"Fingered Bass\", or \"drums\") or an existing \"track\"");
            targets.newInstrument = programFrom (a["sound"]);
            targets.newName = a.value ("name", targets.newInstrument == kDrumProgram ? std::string ("AI Drums") : std::string ("AI ") + kGeneralMidiNames[targets.newInstrument]);
        }
        bool anyOther = false;
        for (const auto* t : playingTracks (s))
            if (generated.count (t->id) == 0)
                for (const auto& n : notation::timelineNotes (*t))
                    if (n.start >= from && n.start < to)
                    {
                        anyOther = true;
                        break;
                    }
        if (! anyOther)
            throw std::runtime_error ("the other tracks play nothing in those bars, so there is nothing to write a part under. Use ai_continue to make new music.");
        return anticipationJob (s, a, "ai_write_part", from, to, targets, generated);
    }

    Job melodyJob (const Song& s, const json& a)
    {
        const auto [from, to] = barRange (s, a, 8);
        needFourFour (s, "Magenta melody over chords");
        // The chord symbols of all tracks; the last one before the range starts it.
        std::map<Tick, std::string> chords;
        for (const auto& t : s.tracks)
            for (const auto& m : t.marks)
                if (m.kind == ScoreMark::Kind::chord && ! m.text.empty() && m.at < to)
                    chords[m.at] = m.text;
        json list = json::array();
        std::string before;
        for (const auto& [at, text] : chords)
        {
            if (at <= from)
                before = text;
            else
                list.push_back ({ { "start", static_cast<double> (at - from) / kQuarter }, { "chord", text } });
        }
        if (! before.empty())
            list.insert (list.begin(), json ({ { "start", 0.0 }, { "chord", before } }));
        if (list.empty())
            throw std::runtime_error ("there are no chord symbols in those bars. Write them with set_chords, or find them with find_chords.");
        if (list.front()["start"].get<double>() > 0.0)
            list.insert (list.begin(), json ({ { "start", 0.0 }, { "chord", "N.C." } }));

        TrackId target = 0;
        std::string targetName = a.value ("name", std::string ("AI Melody"));
        int program = 73; // Flute
        if (a.contains ("track"))
        {
            const auto& t = trackArg (s, a);
            target = t.id;
            targetName = t.name;
        }
        else if (a.contains ("sound"))
        {
            program = programFrom (a["sound"]);
            if (program == kDrumProgram)
                throw std::runtime_error ("a melody needs a sound with pitches, not drums");
        }
        Job job;
        job.tool = "ai_melody";
        job.title = "AI: Melody over Chords";
        job.helper = kMagenta;
        job.request = json ({ { "op", "improvise" },
                              { "chords", list },
                              { "quarters", static_cast<double> (to - from) / kQuarter },
                              { "quarters_per_bar", quartersPerBar (s) },
                              { "temperature", temperatureArg (a, 1.0) } })
                          .dump();
        job.apply = [=] (Song& song, const std::string& answer) {
            const auto r = parseAnswer (answer);
            const bool made = target == 0;
            const auto id = made ? newTrack (song, targetName, program) : target;
            const auto count = replaceRange (song, id, from, to, notesFrom (r.at ("notes"), from, from, to), "AI Melody");
            return resultText (song, edit::findTrack (song, id)->name, count, from, to, made, "magenta improv_rnn");
        };
        return job;
    }

    const json& definitions()
    {
        static const json kTrack = { { "description", "The track: its name or its id (see get_song)" } };
        static const json kBar = { { "type", "integer" }, { "minimum", 1 } };
        static const json kTemperature = { { "type", "number" }, { "minimum", 0.1 }, { "maximum", 2 }, { "description", "Low = safe and plain, high = wild (default 1)" } };
        static const json kSound = { { "description", "General MIDI sound number 1-128 or name, e.g. \"Fingered Bass\", or \"drums\"" } };
        auto schema = [] (json properties, json required) {
            json s = { { "type", "object" }, { "properties", std::move (properties) } };
            if (! required.empty())
                s["required"] = std::move (required);
            return s;
        };
        static const json list = json::array ({
            { { "name", "ai_humanize" },
              { "description", "Make a part sound played by a person: small changes in timing and loudness. engine magenta (Magenta GrooVAE, drum tracks in 4/4, learned from real drummers) or simple (any track, quick, no AI). Replaces the notes in the bars." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "engine", { { "type", "string" }, { "enum", { "magenta", "simple" } } } },
                                         { "amount", { { "type", "number" }, { "minimum", 0 }, { "maximum", 1 }, { "description", "simple: how much (default 0.5)" } } },
                                         { "temperature", kTemperature } },
                                       { "track", "from_bar" }) } },
            { { "name", "ai_drumify" },
              { "description", "Magenta Drumify: make a drum part from the rhythm of another track (for example a bass line or tapped notes). Goes to a new drum track, or to_track. 4/4 only." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "to_track", { { "description", "An existing drum track for the result" } } },
                                         { "name", { { "type", "string" }, { "description", "Name of the new drum track" } } },
                                         { "temperature", kTemperature } },
                                       { "track", "from_bar" }) } },
            { { "name", "ai_continue" },
              { "description", "Let the AI continue the music from from_bar to to_bar, from what comes before. engine magenta: one track (Magenta MelodyRNN for a melody, DrumsRNN for drums; one note at a time). engine anticipation (Anticipatory Music Transformer): one track that plays along with the others, or with no track all tracks; when there are notes after to_bar, it fills the gap to join them. Replaces the notes in the bars." },
              { "inputSchema", schema ({ { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "engine", { { "type", "string" }, { "enum", { "magenta", "anticipation" } } } },
                                         { "context_bars", { { "type", "integer" }, { "minimum", 1 }, { "maximum", 32 }, { "description", "How many bars before (and after) it listens to (default magenta 4, anticipation 8)" } } },
                                         { "temperature", kTemperature },
                                         { "quantise", { { "type", "string" }, { "enum", { "none", "1/8", "1/8T", "1/16", "1/16T", "1/32" } }, { "description", "anticipation: snap notes to this grid (default 1/16)" } } } },
                                       { "from_bar" }) } },
            { { "name", "ai_write_part" },
              { "description", "Anticipatory Music Transformer: write a new part that fits the other tracks in the bars, for example \"write a bass under this\" (sound \"Fingered Bass\") or drums (sound \"drums\"). Goes to a new track with that sound, or into an existing track (its notes in the bars are replaced)." },
              { "inputSchema", schema ({ { "sound", kSound }, { "track", kTrack }, { "from_bar", kBar }, { "to_bar", kBar },
                                         { "name", { { "type", "string" }, { "description", "Name of the new track" } } },
                                         { "top_p", { { "type", "number" }, { "minimum", 0.5 }, { "maximum", 1 }, { "description", "Low = safe, 1 = most varied (default 0.95)" } } },
                                         { "model", { { "type", "string" }, { "enum", { "small", "medium" } }, { "description", "medium: better, slower and a bigger download (default small)" } } },
                                         { "quantise", { { "type", "string" }, { "enum", { "none", "1/8", "1/8T", "1/16", "1/16T", "1/32" } } } } },
                                       { "from_bar" }) } },
            { { "name", "ai_melody" },
              { "description", "Magenta ImprovRNN: write a melody over the chord symbols of the song (set_chords or find_chords first). Goes to a new track (sound, default Flute) or an existing track. 4/4 only." },
              { "inputSchema", schema ({ { "from_bar", kBar }, { "to_bar", kBar }, { "track", kTrack }, { "sound", kSound },
                                         { "name", { { "type", "string" } } }, { "temperature", kTemperature } },
                                       { "from_bar" }) } },
        });
        return list;
    }
} // namespace

bool isTool (const std::string& name)
{
    return name == "ai_humanize" || name == "ai_drumify" || name == "ai_continue" || name == "ai_write_part" || name == "ai_melody";
}

std::string toolDefinitions()
{
    return definitions().dump();
}

Job prepare (const Song& s, const std::string& tool, const std::string& arguments)
{
    json a;
    try
    {
        a = arguments.empty() ? json::object() : json::parse (arguments);
    }
    catch (const json::exception&)
    {
        throw std::runtime_error ("the arguments are not JSON");
    }
    try
    {
        if (tool == "ai_humanize") return humanizeJob (s, a);
        if (tool == "ai_drumify") return drumifyJob (s, a);
        if (tool == "ai_continue") return continueJob (s, a);
        if (tool == "ai_write_part") return writePartJob (s, a);
        if (tool == "ai_melody") return melodyJob (s, a);
    }
    catch (const json::exception& e)
    {
        throw std::runtime_error (std::string ("wrong arguments: ") + e.what());
    }
    throw std::runtime_error ("no AI tool " + tool);
}

// ---- the simple humanize --------------------------------------------------------------------

std::vector<Note> humanize (std::vector<Note> notes, const TimeSignature& sig, const HumanizeOptions& o)
{
    // std::mt19937 gives the same numbers everywhere; the standard distributions don't, so they aren't used.
    std::mt19937 random (o.seed);
    auto uniform = [&] { return static_cast<double> (random()) / 4294967295.0 * 2.0 - 1.0; };        // -1..1
    auto bell = [&] { return (uniform() + uniform() + uniform()) / 3.0; };                            // -1..1, most near 0
    const double amount = std::clamp (o.amount, 0.0, 1.0);
    const Tick beat = sig.ticksPerBeat();
    const Tick bar = sig.ticksPerBar();
    for (auto& n : notes)
    {
        const bool snare = o.drums && (n.pitch == 38 || n.pitch == 40 || n.pitch == 39);
        const bool kick = o.drums && (n.pitch == 35 || n.pitch == 36);
        const bool hat = o.drums && (n.pitch == 42 || n.pitch == 44 || n.pitch == 46);

        // Loudness: the first beat of the bar loudest, off-beats softer.
        const Tick inBeat = n.start % beat;
        double accent = 0.0;
        if (inBeat == 0)
            accent = n.start % bar == 0 ? 0.10 : 0.04;
        else if (inBeat * 2 == beat)
            accent = -0.06;
        else
            accent = -0.13;
        const double spread = hat ? 0.12 : 0.07;
        const double factor = 1.0 + amount * (accent + spread * bell());
        n.velocity = static_cast<std::uint8_t> (std::clamp (static_cast<int> (std::lround (n.velocity * factor)), 1, 127));

        // Time: up to about 1/64 note of spread; the snare a bit behind the beat, the kick tight.
        const double maxShift = static_cast<double> (kTicksPerQuarter) / 16.0 * amount;
        double shift = maxShift * bell() * (kick ? 0.4 : 1.0);
        if (snare)
            shift += maxShift * 0.5;
        const Tick moved = std::max<Tick> (0, n.start + static_cast<Tick> (std::lround (shift)));
        n.length = std::max<Tick> (1, n.length - (moved - n.start));
        n.start = moved;
    }
    std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });
    return notes;
}

std::vector<Note> snap (std::vector<Note> notes, Tick step)
{
    if (step <= 0)
        return notes;
    for (auto& n : notes)
    {
        const Tick end = n.end();
        n.start = (n.start + step / 2) / step * step;
        n.length = std::max (step, (end + step / 2) / step * step - n.start);
    }
    // Two notes of one pitch on one step: keep the first.
    std::sort (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });
    notes.erase (std::unique (notes.begin(), notes.end(), [] (const Note& x, const Note& y) { return x.start == y.start && x.pitch == y.pitch; }), notes.end());
    return notes;
}

} // namespace arranger::ai
