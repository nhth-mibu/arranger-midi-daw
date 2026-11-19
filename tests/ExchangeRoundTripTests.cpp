// The demo songs and the kitchen-sink song go out as MIDI and MusicXML and come
// back with the same music: what a user sends to another program or a keyboard
// must not lose notes.

#include "KitchenSink.h"

#include "arranger/core/MidiFile.h"
#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <tuple>

using namespace arranger;
namespace fs = std::filesystem;

namespace
{
    /** (song tick, pitch, length) of every note a track plays on the Timeline, loops played out. */
    using NoteSet = std::multiset<std::tuple<Tick, int, Tick>>;

    NoteSet playedNotes (const Track& t)
    {
        NoteSet out;
        for (const auto& c : t.clips)
        {
            const Tick loop = c->loopLength > 0 ? c->loopLength : c->length;
            for (Tick base = 0; base < c->length; base += loop)
                for (const auto& n : c->notes)
                {
                    const Tick at = base + n.start;
                    if (at >= c->length || (c->loopLength > 0 && n.start >= loop))
                        continue;
                    out.insert ({ c->start + at, n.pitch, std::min (n.length, c->length - at) });
                }
        }
        return out;
    }

    /** Notes per MIDI channel, for tracks that play MIDI (not audio). */
    std::map<int, NoteSet> notesByChannel (const Song& s)
    {
        std::map<int, NoteSet> out;
        for (const auto& t : s.tracks)
        {
            if (t.output.kind == TrackOutput::Kind::audio)
                continue;
            for (const auto& n : playedNotes (t))
                out[t.output.channel].insert (n);
        }
        return out;
    }

    std::vector<fs::path> exampleProjects()
    {
        std::vector<fs::path> found;
        for (const auto& e : fs::recursive_directory_iterator (test::examplesFolder()))
            if (e.path().extension() == project_file::kExtension)
                found.push_back (e.path());
        std::sort (found.begin(), found.end());
        return found;
    }
}

TEST_CASE ("Every example song keeps its notes through a MIDI file", "[exchange][midi]")
{
    for (const auto& file : exampleProjects())
    {
        INFO (file.filename().string());
        const auto song = project_file::load (file);
        const auto back = midi_file::read (midi_file::write (song));
        const auto before = notesByChannel (song);
        const auto after = notesByChannel (back);

        for (const auto& [channel, notes] : before)
        {
            INFO ("channel " << channel);
            REQUIRE (after.count (channel) == 1);
            // Overlapping notes of one pitch on one channel can't both survive MIDI; count them as one.
            std::set<std::pair<Tick, int>> starts, startsBack;
            for (const auto& [at, pitch, length] : notes)
                starts.insert ({ at, pitch });
            for (const auto& [at, pitch, length] : after.at (channel))
                startsBack.insert ({ at, pitch });
            CHECK (startsBack == starts);
        }
        CHECK (back.tempo.getPoints().size() == song.tempo.getPoints().size());
        CHECK (back.timeSignature == song.timeSignature);
    }
}

TEST_CASE ("The kitchen-sink song keeps its notes through MIDI, also as format 0 with lyrics and chords", "[exchange][midi]")
{
    const auto song = test::kitchenSinkSong();
    for (const bool single : { false, true })
    {
        INFO ("format " << (single ? 0 : 1));
        midi_file::WriteOptions o;
        o.singleTrack = single;
        o.lyrics = true;
        o.xfChords = true;
        o.automation = true;
        const auto back = midi_file::read (midi_file::write (song, o));
        const auto before = notesByChannel (song);
        const auto after = notesByChannel (back);
        // Several tracks on one channel merge in MIDI: compare the starts per channel.
        for (const auto& [channel, notes] : before)
        {
            std::set<std::pair<Tick, int>> a, b;
            for (const auto& [at, pitch, length] : notes)
                a.insert ({ at, pitch });
            if (after.count (channel) != 0)
                for (const auto& [at, pitch, length] : after.at (channel))
                    b.insert ({ at, pitch });
            INFO ("channel " << channel);
            CHECK (b == a);
        }
    }
}

TEST_CASE ("Every example song keeps its notes through MusicXML", "[exchange][musicxml]")
{
    for (const auto& file : exampleProjects())
    {
        INFO (file.filename().string());
        const auto song = project_file::load (file);
        bool hasNotes = false;
        for (const auto& t : song.tracks)
            hasNotes = hasNotes || (t.output.kind != TrackOutput::Kind::audio && ! t.clips.empty());
        if (! hasNotes)
            continue; // an audio-only song has no score
        const auto back = musicxml::read (musicxml::write (song));

        // The score shows each MIDI track as a part. Compare the note starts and pitches
        // of each part in order (drums keep their drum notes). The score may round
        // lengths to note values, so lengths are not compared here.
        std::vector<std::set<std::pair<Tick, int>>> before, after;
        for (const auto& t : song.tracks)
            if (t.output.kind != TrackOutput::Kind::audio && ! t.clips.empty())
            {
                std::set<std::pair<Tick, int>> s;
                for (const auto& [at, pitch, length] : playedNotes (t))
                    s.insert ({ at, pitch });
                before.push_back (s);
            }
        for (const auto& t : back.tracks)
        {
            std::set<std::pair<Tick, int>> s;
            for (const auto& [at, pitch, length] : playedNotes (t))
                s.insert ({ at, pitch });
            after.push_back (s);
        }
        REQUIRE (after.size() == before.size());
        for (std::size_t i = 0; i < before.size(); ++i)
        {
            INFO ("part " << i + 1);
            // The score puts notes on a grid of note values: a strummed chord (notes a few ticks
            // apart) becomes one chord. Allow half a 16th note (120 ticks).
            std::size_t matched = 0;
            for (const auto& [at, pitch] : before[i])
            {
                bool found = false;
                for (Tick d = -120; d <= 120 && ! found; ++d)
                    found = after[i].count ({ at + d, pitch }) != 0;
                matched += found ? 1 : 0;
            }
            CHECK (matched == before[i].size());
        }
    }
}
