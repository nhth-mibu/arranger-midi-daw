#include "KitchenSink.h"

#include "arranger/core/Document.h"
#include "arranger/core/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace arranger;

namespace
{
    TrackId trackNamed (const Song& s, const std::string& name)
    {
        for (const auto& t : s.tracks)
            if (t.name == name)
                return t.id;
        return 0;
    }

    ClipId firstClip (const Song& s, TrackId track)
    {
        const auto* t = edit::findTrack (s, track);
        return t != nullptr && ! t->clips.empty() ? t->clips.front()->id : 0;
    }

    ClipId firstAudioClip (const Song& s, TrackId track)
    {
        const auto* t = edit::findTrack (s, track);
        return t != nullptr && ! t->audio.empty() ? t->audio.front()->id : 0;
    }

    struct NamedEdit
    {
        std::string name;
        std::function<void (Song&, std::mt19937&)> change;
    };

    int pick (std::mt19937& rng, int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }

    /** Every kind of edit the app makes, each with random values. */
    std::vector<NamedEdit> allEdits()
    {
        return {
            { "Add Track", [] (Song& s, std::mt19937& r) { edit::addTrack (s, "New " + std::to_string (pick (r, 0, 99))); } },
            { "Add Audio Track", [] (Song& s, std::mt19937&) { edit::addAudioTrack (s, "Audio"); } },
            { "Remove Track", [] (Song& s, std::mt19937& r) {
                 if (s.tracks.size() > 1)
                     edit::removeTrack (s, s.tracks[static_cast<std::size_t> (pick (r, 0, static_cast<int> (s.tracks.size()) - 1))].id);
             } },
            { "Move Track", [] (Song& s, std::mt19937& r) {
                 if (! s.tracks.empty())
                     edit::moveTrack (s, s.tracks.front().id, pick (r, 0, static_cast<int> (s.tracks.size()) - 1));
             } },
            { "Add Clip", [] (Song& s, std::mt19937& r) {
                 Clip c;
                 c.start = pick (r, 0, 40) * 480;
                 c.length = pick (r, 1, 8) * 480;
                 c.notes = { { 0, 240, static_cast<std::uint8_t> (pick (r, 0, 127)), 90 } };
                 edit::addClip (s, trackNamed (s, "Sink Keys"), c);
             } },
            { "Remove Clip", [] (Song& s, std::mt19937&) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")))
                     edit::removeClip (s, c);
             } },
            { "Move Clip", [] (Song& s, std::mt19937& r) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")))
                     edit::moveClip (s, c, pick (r, 0, 40) * 240, trackNamed (s, "Sink Keys"));
             } },
            { "Split Clip", [] (Song& s, std::mt19937& r) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")))
                 {
                     const auto p = edit::findClipPtr (s, c);
                     if (p->length > 2)
                         edit::splitClip (s, c, p->start + pick (r, 1, static_cast<int> (p->length) - 1));
                 }
             } },
            { "Join Clips", [] (Song& s, std::mt19937&) {
                 const auto* t = edit::findTrack (s, trackNamed (s, "Sink Keys"));
                 if (t != nullptr && t->clips.size() >= 2)
                     edit::joinClips (s, { t->clips[0]->id, t->clips[1]->id });
             } },
            { "Change Clip", [] (Song& s, std::mt19937& r) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")))
                     edit::modifyClip (s, c, [&r] (Clip& clip) { clip.name = "Changed " + std::to_string (pick (r, 0, 99)); });
             } },
            { "Add Note", [] (Song& s, std::mt19937& r) { edit::addTimelineNote (s, trackNamed (s, "Sink Keys"), pick (r, 0, 64) * 120, 240, pick (r, 21, 108)); } },
            { "Transpose Notes", [] (Song& s, std::mt19937& r) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")); c != 0 && ! edit::findClipPtr (s, c)->notes.empty())
                 {
                     const int by = pick (r, -5, 5);
                     edit::changeNotes (s, { { c, edit::findClipPtr (s, c)->notes.front() } }, [by] (Note& n) {
                         n.pitch = static_cast<std::uint8_t> (std::clamp (n.pitch + by, 0, 127));
                     });
                 }
             } },
            { "Delete Notes", [] (Song& s, std::mt19937&) {
                 if (const auto c = firstClip (s, trackNamed (s, "Sink Keys")); c != 0 && ! edit::findClipPtr (s, c)->notes.empty())
                     edit::removeNotes (s, { { c, edit::findClipPtr (s, c)->notes.front() } });
             } },
            { "Clear Range", [] (Song& s, std::mt19937& r) {
                 const Tick from = pick (r, 0, 20) * 480;
                 edit::clearRange (s, trackNamed (s, "Sink Keys"), from, from + pick (r, 1, 20) * 480);
             } },
            { "Set Slot", [] (Song& s, std::mt19937& r) {
                 Clip c;
                 c.length = 3840;
                 c.notes = { { 0, 480, static_cast<std::uint8_t> (pick (r, 40, 80)), 100 } };
                 edit::setSlot (s, trackNamed (s, "Sink Keys"), pick (r, 0, 7), c);
             } },
            { "Clear Slot", [] (Song& s, std::mt19937& r) { edit::clearSlot (s, trackNamed (s, "Sink Keys"), pick (r, 0, 2)); } },
            { "Slots from Timeline", [] (Song& s, std::mt19937&) { edit::slotsFromTimeline (s, 3840 * 2); } },
            { "Change Audio Clip", [] (Song& s, std::mt19937& r) {
                 if (const auto c = firstAudioClip (s, trackNamed (s, "Sink Vocals")))
                     edit::modifyAudioClip (s, c, [&r] (AudioClip& a) { a.gainDb = static_cast<float> (pick (r, -24, 6)); });
             } },
            { "Bring Take to Front", [] (Song& s, std::mt19937&) {
                 if (const auto c = firstAudioClip (s, trackNamed (s, "Sink Vocals")))
                     edit::bringToFront (s, c);
             } },
            { "Add Bus", [] (Song& s, std::mt19937&) { edit::addBus (s, "Bus"); } },
            { "Remove Bus", [] (Song& s, std::mt19937&) {
                 if (! s.buses.empty())
                     edit::removeBus (s, s.buses.front().id);
             } },
            { "Route to Bus", [] (Song& s, std::mt19937&) {
                 if (! s.buses.empty())
                     edit::setTrackBus (s, trackNamed (s, "Sink Keys"), s.buses.back().id);
             } },
            { "Set Send", [] (Song& s, std::mt19937& r) {
                 if (! s.buses.empty())
                     edit::setSend (s, trackNamed (s, "Sink Keys"), s.buses.front().id, pick (r, 0, 127));
             } },
            { "Automation Point", [] (Song& s, std::mt19937& r) {
                 edit::setAutomationPoint (s, trackNamed (s, "Sink Keys"), pick (r, 0, 3), { pick (r, 0, 40) * 120, static_cast<float> (pick (r, 0, 127)) });
             } },
            { "Remove Automation Point", [] (Song& s, std::mt19937&) {
                 const auto* t = edit::findTrack (s, trackNamed (s, "Sink Keys"));
                 if (t != nullptr && ! t->volumeAutomation.empty())
                     edit::removeAutomationPoint (s, t->id, 0, 0);
             } },
            { "Clear Automation", [] (Song& s, std::mt19937& r) { edit::clearAutomation (s, trackNamed (s, "Sink Keys"), pick (r, 0, 3)); } },
            { "Plugin Control Line", [] (Song& s, std::mt19937& r) { edit::addControlLine (s, trackNamed (s, "Sink Synth"), 0, pick (r, 0, 20), "Control"); } },
            { "Score Mark", [] (Song& s, std::mt19937& r) {
                 static const char* chords[] = { "C", "Am7", "F#m7b5", "Bb/D", "" };
                 edit::setMark (s, trackNamed (s, "Sink Keys"), { ScoreMark::Kind::chord, pick (r, 0, 16) * 960, chords[pick (r, 0, 4)] });
             } },
            { "Clear Marks", [] (Song& s, std::mt19937&) { edit::clearMarks (s, trackNamed (s, "Sink Keys"), 0, 3840 * 4); } },
            { "Lyrics", [] (Song& s, std::mt19937&) { edit::setLyrics (s, trackNamed (s, "Sink Keys"), 0, "Twin-kle twin-kle", { 0, 480, 960, 1440 }); } },
            { "Transpose Song", [] (Song& s, std::mt19937& r) {
                 const int by = pick (r, -2, 2);
                 edit::forEachClip (s, [by] (Clip& c) {
                     for (auto& n : c.notes)
                         n.pitch = static_cast<std::uint8_t> (std::clamp (n.pitch + by, 0, 127));
                 });
             } },
            { "Mixer", [] (Song& s, std::mt19937& r) {
                 if (auto* t = edit::findTrack (s, trackNamed (s, "Sink Keys")))
                     t->mixer.volume = pick (r, 0, 127);
             } },
            { "Tempo", [] (Song& s, std::mt19937& r) { s.tempo.setPoints ({ { 0, static_cast<double> (pick (r, 40, 240)) } }); } },
            { "Song Settings", [] (Song& s, std::mt19937& r) {
                 s.key = pick (r, 0, 1) != 0 ? "D major" : "B minor";
                 s.loop = { pick (r, 0, 1) != 0, 0, 3840 * pick (r, 1, 8) };
             } },
        };
    }
}

TEST_CASE ("Each edit can be undone and redone exactly", "[undo]")
{
    std::mt19937 rng (1234);
    for (const auto& e : allEdits())
    {
        INFO (e.name);
        Document doc (test::kitchenSinkSong());
        const auto before = doc.song();
        const auto revisionBefore = doc.revision();
        doc.edit (e.name, [&] (Song& s) { e.change (s, rng); });
        if (doc.song() == before)
            continue; // this edit had nothing to change in this song; the random run covers it
        const auto after = doc.song();
        const auto revisionAfter = doc.revision();

        CHECK (doc.undoDescription() == e.name);
        REQUIRE (doc.undo());
        CHECK (doc.song() == before);
        CHECK (doc.revision() == revisionBefore);
        REQUIRE (doc.redo());
        CHECK (doc.song() == after);
        CHECK (doc.revision() == revisionAfter);

        // The undone version is still a valid project.
        doc.undo();
        CHECK (project_file::fromJson (project_file::toJson (doc.song())) == before);
    }
}

TEST_CASE ("Every edit changes the kitchen-sink song", "[undo]")
{
    // Guards the test above: an edit that never does anything tests nothing.
    std::mt19937 rng (99);
    for (const auto& e : allEdits())
    {
        INFO (e.name);
        bool changed = false;
        for (int attempt = 0; attempt < 20 && ! changed; ++attempt)
        {
            Song s = test::kitchenSinkSong();
            const auto before = s;
            e.change (s, rng);
            changed = ! (s == before);
        }
        CHECK (changed);
    }
}

TEST_CASE ("1,000 random edits, then undo all and redo all", "[undo][stress]")
{
    const auto edits = allEdits();
    for (const unsigned seed : { 1u, 2u, 3u })
    {
        INFO ("seed " << seed);
        std::mt19937 rng (seed);
        Document doc (test::kitchenSinkSong(), 2000);
        std::vector<Song> versions { doc.song() };
        for (int i = 0; i < 1000; ++i)
        {
            const auto& e = edits[static_cast<std::size_t> (pick (rng, 0, static_cast<int> (edits.size()) - 1))];
            doc.edit (e.name, [&] (Song& s) { e.change (s, rng); });
            if (! (doc.song() == versions.back()))
                versions.push_back (doc.song());
        }
        INFO (versions.size() - 1 << " steps");

        for (std::size_t i = versions.size() - 1; i > 0; --i)
        {
            REQUIRE (doc.undo());
            REQUIRE (doc.song() == versions[i - 1]);
        }
        CHECK_FALSE (doc.canUndo());
        CHECK_FALSE (doc.isModified());

        for (std::size_t i = 1; i < versions.size(); ++i)
        {
            REQUIRE (doc.redo());
            REQUIRE (doc.song() == versions[i]);
        }
        CHECK_FALSE (doc.canRedo());

        // The last version saves and loads.
        CHECK (project_file::fromJson (project_file::toJson (doc.song())) == doc.song());
    }
}

TEST_CASE ("Undo history is capped and the oldest step goes first", "[undo]")
{
    Document doc (test::kitchenSinkSong(), 10);
    for (int i = 0; i < 25; ++i)
        doc.edit ("Rename", [i] (Song& s) { s.title = "Version " + std::to_string (i); });
    int steps = 0;
    while (doc.undo())
        ++steps;
    CHECK (steps == 10);
    CHECK (doc.song().title == "Version 14");
}

TEST_CASE ("A new edit after undo clears redo", "[undo]")
{
    Document doc (test::kitchenSinkSong());
    doc.edit ("A", [] (Song& s) { s.title = "A"; });
    doc.edit ("B", [] (Song& s) { s.title = "B"; });
    doc.undo();
    CHECK (doc.canRedo());
    doc.edit ("C", [] (Song& s) { s.title = "C"; });
    CHECK_FALSE (doc.canRedo());
    doc.undo();
    CHECK (doc.song().title == "A");
}

TEST_CASE ("Modified follows undo back to the saved version", "[undo]")
{
    Document doc (test::kitchenSinkSong());
    doc.edit ("A", [] (Song& s) { s.title = "A"; });
    doc.markSaved();
    doc.edit ("B", [] (Song& s) { s.title = "B"; });
    CHECK (doc.isModified());
    doc.undo();
    CHECK_FALSE (doc.isModified());
    doc.undo();
    CHECK (doc.isModified());
    doc.redo();
    CHECK_FALSE (doc.isModified());
}
