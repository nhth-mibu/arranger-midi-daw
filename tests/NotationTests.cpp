#include "arranger/core/Notation.h"
#include "arranger/core/ScoreLayout.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;
using namespace arranger::notation;

namespace
{
    constexpr Tick kQ = kTicksPerQuarter;

    Note note (Tick start, Tick length, int pitch) { return { start, length, static_cast<std::uint8_t> (pitch), 100 }; }

    const std::vector<Event>& events (const Part& p, std::size_t bar, std::size_t staff = 0) { return p.staves.at (staff).bars.at (bar).events; }
}

TEST_CASE ("Key names give key signatures", "[notation]")
{
    CHECK (keyFifths ("C major") == 0);
    CHECK (keyFifths ("G major") == 1);
    CHECK (keyFifths ("F major") == -1);
    CHECK (keyFifths ("Eb major") == -3);
    CHECK (keyFifths ("A minor") == 0);
    CHECK (keyFifths ("E minor") == 1);
    CHECK (keyFifths ("C minor") == -3);
    CHECK (keyFifths ("F# major") == 6);
    CHECK (keyFifths ("Gb major") == -6);
    CHECK (keyFifths ("Db major") == -5);
    CHECK (keyFifths ("C# minor") == 4);
}

TEST_CASE ("Pitches are spelled in the key", "[notation]")
{
    CHECK (spell (60, 0) == Spelling { 0, 0, 4 });   // C4
    CHECK (spell (61, 0) == Spelling { 0, 1, 4 });   // C#
    CHECK (spell (70, 0) == Spelling { 6, -1, 4 });  // Bb
    CHECK (spell (63, -3) == Spelling { 2, -1, 4 }); // Eb in Eb major
    CHECK (spell (66, 1) == Spelling { 3, 1, 4 });   // F# in G major
    CHECK (spell (65, 2) == Spelling { 3, 0, 4 });   // F natural in D major
    CHECK (spell (68, -1) == Spelling { 5, -1, 4 }); // Ab in F major
    CHECK (spell (68, 2) == Spelling { 4, 1, 4 });   // G# in D major
    CHECK (spell (71, -6) == Spelling { 0, -1, 5 }); // Cb in Gb major, octave of the C above
}

TEST_CASE ("Quarter notes in 4/4", "[notation]")
{
    const auto part = makePart ("Melody", { note (0, kQ, 72), note (kQ, kQ, 74), note (kQ * 2, kQ, 76), note (kQ * 3, kQ, 77) }, 0, {}, 0);
    REQUIRE (part.staves.size() == 1);
    CHECK (part.staves[0].clef == Clef::treble);
    REQUIRE (part.staves[0].bars.size() == 1);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 4);
    for (const auto& ev : e)
    {
        CHECK (ev.value == Value::quarter);
        CHECK_FALSE (ev.rest);
    }
    CHECK (e[0].heads[0].position == 5); // C5: third space
    CHECK (e[0].stemUp == false);        // above the middle line: stem down
}

TEST_CASE ("Played timing is cleaned up for display only", "[notation]")
{
    // Slightly early and late notes, as played.
    const auto part = makePart ("Melody", { note (8, kQ - 30, 67), note (kQ - 12, kQ + 20, 69) }, kQ * 4, {}, 0);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 3);
    CHECK (e[0].start == 0);
    CHECK (e[0].value == Value::quarter);
    CHECK (e[1].start == kQ);
    CHECK (e[1].value == Value::quarter);
    CHECK (e[2].rest);
    CHECK (e[2].value == Value::half);
}

TEST_CASE ("Long notes split at the bar line with a tie", "[notation]")
{
    const auto part = makePart ("Pad", { note (kQ * 3, kQ * 2, 64) }, 0, {}, 0);
    REQUIRE (part.staves[0].bars.size() == 2);
    const auto& a = events (part, 0);
    const auto& b = events (part, 1);
    REQUIRE (a.size() == 3);
    CHECK (a[0].rest);
    CHECK (a[0].value == Value::half);
    CHECK (a[0].dots == 0); // rests show beats: a half rest and a quarter rest, never a dotted half rest
    CHECK (a[1].rest);
    CHECK (a[1].value == Value::quarter);
    CHECK (a[2].value == Value::quarter);
    CHECK (a[2].heads[0].tieToNext);
    CHECK (b[0].value == Value::quarter);
    CHECK_FALSE (b[0].heads[0].tieToNext);
    CHECK (b[1].rest);
}

TEST_CASE ("Notes show the middle of a 4/4 bar", "[notation]")
{
    // A half note on beat 2 is written as two tied quarters.
    const auto part = makePart ("x", { note (0, kQ, 60), note (kQ, kQ * 2, 62), note (kQ * 3, kQ, 64) }, 0, {}, 0);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 4);
    CHECK (e[1].value == Value::quarter);
    CHECK (e[1].heads[0].tieToNext);
    CHECK (e[2].value == Value::quarter);
    // A dotted half from the start of the bar is fine.
    const auto dotted = makePart ("x", { note (0, kQ * 3, 60) }, kQ * 4, {}, 0);
    CHECK (events (dotted, 0)[0].value == Value::half);
    CHECK (events (dotted, 0)[0].dots == 1);
}

TEST_CASE ("Eighths and sixteenths are beamed by the beat", "[notation]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 4; ++i)
        notes.push_back (note (kQ / 2 * i, kQ / 2, 67 + i));
    for (int i = 0; i < 4; ++i)
        notes.push_back (note (kQ * 2 + kQ / 4 * i, kQ / 4, 72));
    const auto part = makePart ("x", notes, 0, {}, 0);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 9); // 4 eighths, 4 sixteenths, a quarter rest
    CHECK (e[0].beamGroup == 0);
    CHECK (e[1].beamGroup == 0);
    CHECK (e[2].beamGroup == 1);
    CHECK (e[3].beamGroup == 1);
    CHECK (e[4].beamGroup == 2);
    CHECK (e[7].beamGroup == 2);
    CHECK (e[4].value == Value::sixteenth);
    CHECK (e[8].rest);
    CHECK (e[8].beamGroup < 0);
}

TEST_CASE ("Swung eighths read as triplets", "[notation]")
{
    std::vector<Note> notes;
    for (int beat = 0; beat < 4; ++beat)
    {
        notes.push_back (note (kQ * beat, kQ * 2 / 3, 64));
        notes.push_back (note (kQ * beat + kQ * 2 / 3 + 5, kQ / 3, 67));
    }
    const auto part = makePart ("x", notes, 0, {}, 0);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 8);
    CHECK (e[0].triplet);
    CHECK (e[0].value == Value::quarter); // two thirds of a beat: a triplet quarter
    CHECK (e[1].value == Value::eighth);
    CHECK (e[0].tupletGroup == 0);
    CHECK (e[1].tupletGroup == 0);
    CHECK (e[2].tupletGroup == 1);

    // Straight eighths stay straight.
    const auto straight = makePart ("x", { note (0, kQ / 2, 64), note (kQ / 2, kQ / 2, 67) }, kQ * 4, {}, 0);
    CHECK_FALSE (events (straight, 0)[0].triplet);
}

TEST_CASE ("Accidentals hold for the bar", "[notation]")
{
    const auto part = makePart ("x", { note (0, kQ, 66), note (kQ, kQ, 66), note (kQ * 2, kQ, 65), note (kQ * 3, kQ, 66), note (kQ * 4, kQ, 66) }, 0, {}, 0);
    const auto& a = events (part, 0);
    CHECK (a[0].heads[0].accidental == Accidental::sharp);
    CHECK (a[1].heads[0].accidental == Accidental::none);
    CHECK (a[2].heads[0].accidental == Accidental::natural);
    CHECK (a[3].heads[0].accidental == Accidental::sharp);
    CHECK (events (part, 1)[0].heads[0].accidental == Accidental::sharp); // a new bar
    // In G major, F# needs nothing.
    const auto g = makePart ("x", { note (0, kQ, 66) }, 0, {}, keyFifths ("G major"));
    CHECK (events (g, 0)[0].heads[0].accidental == Accidental::none);
}

TEST_CASE ("Piano music goes on a grand staff", "[notation]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 8; ++i)
    {
        notes.push_back (note (kQ * i, kQ, 72));
        notes.push_back (note (kQ * i, kQ, 43));
    }
    const auto part = makePart ("Grand Piano", notes, 0, {}, 0);
    CHECK (part.grandStaff);
    REQUIRE (part.staves.size() == 2);
    CHECK (part.staves[0].clef == Clef::treble);
    CHECK (part.staves[1].clef == Clef::bass);
    CHECK (events (part, 0, 0)[0].heads[0].pitch == 72);
    CHECK (events (part, 0, 1)[0].heads[0].pitch == 43);
    CHECK (events (part, 0, 1)[0].heads[0].position == 0); // G2: the bottom line of the bass staff
}

TEST_CASE ("A bass line gets the bass clef; empty bars get a whole rest", "[notation]")
{
    const auto part = makePart ("Bass", { note (0, kQ, 36), note (kQ, kQ, 43) }, kQ * 8, {}, 0);
    CHECK (part.staves[0].clef == Clef::bass);
    REQUIRE (part.staves[0].bars.size() == 2);
    REQUIRE (events (part, 1).size() == 1);
    CHECK (events (part, 1)[0].wholeBarRest);
}

TEST_CASE ("Drums use drum notation", "[notation]")
{
    Options o;
    o.drums = true;
    const auto part = makePart ("Drums", { note (0, kQ / 2, 36), note (0, kQ / 2, 42), note (kQ / 2, kQ / 2, 42), note (kQ, kQ / 2, 38) }, kQ * 4, {}, 0, o);
    CHECK (part.staves[0].clef == Clef::percussion);
    const auto& e = events (part, 0);
    REQUIRE (e.size() >= 3);
    REQUIRE (e[0].heads.size() == 2);
    CHECK (e[0].heads[0].position == 1);  // kick
    CHECK (e[0].heads[1].position == 10); // hi-hat
    CHECK (e[0].heads[1].cross);
    CHECK (e[0].stemUp);
}

TEST_CASE ("Short drum hits hold to the next hit and beam", "[notation]")
{
    Options o;
    o.drums = true;
    std::vector<Note> hits;
    for (int i = 0; i < 8; ++i)
        hits.push_back (note (kQ / 2 * i, 30, 42)); // 8th hi-hats, played short
    const auto part = makePart ("Drums", hits, kQ * 4, {}, 0, o);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 8);
    for (const auto& ev : e)
    {
        CHECK_FALSE (ev.rest);
        CHECK (ev.value == Value::eighth);
        CHECK (ev.beamGroup >= 0);
    }
}

TEST_CASE ("Compound time beams in dotted quarters", "[notation]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 6; ++i)
        notes.push_back (note (kQ / 2 * i, kQ / 2, 67));
    const auto part = makePart ("x", notes, 0, { 6, 8 }, 0);
    const auto& e = events (part, 0);
    REQUIRE (e.size() == 6);
    CHECK (e[0].beamGroup == e[2].beamGroup);
    CHECK (e[3].beamGroup != e[2].beamGroup);
    // A note that fills a beat is a dotted quarter.
    const auto held = makePart ("x", { note (0, kQ * 3 / 2, 67) }, 0, { 6, 8 }, 0);
    CHECK (events (held, 0)[0].value == Value::quarter);
    CHECK (events (held, 0)[0].dots == 1);
}

TEST_CASE ("Timeline notes include loop repeats", "[notation]")
{
    Track t;
    Clip c;
    c.start = kQ * 4;
    c.length = kQ * 8;
    c.loopLength = kQ * 4;
    c.notes = { note (0, kQ, 60) };
    t.clips.push_back (std::make_shared<const Clip> (c));
    const auto notes = timelineNotes (t);
    REQUIRE (notes.size() == 2);
    CHECK (notes[0].start == kQ * 4);
    CHECK (notes[1].start == kQ * 8);
}

#include "arranger/core/Pdf.h"
#include "arranger/core/ScoreLayout.h"

#include <algorithm>
#include <string>

TEST_CASE ("A score lays out on pages", "[notation]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 64 * 4; ++i)
        notes.push_back (note (kQ / 2 * i, kQ / 2, 60 + (i * 5) % 19));
    const auto part = makePart ("Melody", notes, 0, {}, keyFifths ("D major"));
    score::PageSetup setup;
    setup.title = "Test";
    setup.bpm = 120;
    const auto layout = score::layout (part, setup);
    REQUIRE (layout.pages.size() >= 2);
    CHECK (layout.systems >= 8);
    const auto& first = layout.pages[0];
    const auto count = [&] (char32_t code) {
        return std::count_if (first.glyphs.begin(), first.glyphs.end(), [code] (const score::Glyph& g) { return g.code == code; });
    };
    CHECK (count (score::smufl::gClef) >= 2);                   // a clef on each system
    CHECK (count (score::smufl::accidentalSharp) >= 2);          // the key signature
    CHECK (count (score::smufl::timeSig0 + 4) == 2);             // 4/4 on the first system only
    CHECK (count (score::smufl::noteheadBlack) > 50);
    CHECK_FALSE (first.shapes.empty());                          // beams
    for (const auto& page : layout.pages)
        for (const auto& g : page.glyphs)
        {
            CHECK (g.x >= 0.0f);
            CHECK (g.x <= page.width);
            CHECK (g.y <= page.height);
        }

    // On one long page for the screen.
    score::PageSetup screen;
    screen.height = 0;
    const auto tall = score::layout (part, screen);
    REQUIRE (tall.pages.size() == 1);
    CHECK (tall.pages[0].height > 100.0f);
}

TEST_CASE ("A grand staff has a brace and shared bar lines", "[notation]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 16; ++i)
    {
        notes.push_back (note (kQ * i, kQ, 72));
        notes.push_back (note (kQ * i, kQ * 2, 48));
    }
    const auto layout = score::layout (makePart ("Piano", notes, 0, {}, 0));
    REQUIRE_FALSE (layout.pages.empty());
    const auto& g = layout.pages[0].glyphs;
    CHECK (std::any_of (g.begin(), g.end(), [] (const score::Glyph& x) { return x.code == score::smufl::brace && x.scale > 3.0f; }));
    CHECK (std::any_of (g.begin(), g.end(), [] (const score::Glyph& x) { return x.code == score::smufl::fClef; }));
}

TEST_CASE ("PDF files have the parts a reader needs", "[notation]")
{
    pdf::Document doc;
    doc.beginPage (595, 842);
    doc.setColour (0, 0, 0);
    doc.moveTo (10, 10);
    doc.lineTo (100, 10);
    doc.cubicTo (100, 50, 50, 100, 10, 100);
    doc.closePath();
    doc.fill();
    doc.beginPage (595, 842);
    const auto bytes = doc.finish ("A (test)");
    const std::string text (bytes.begin(), bytes.end());
    CHECK (text.rfind ("%PDF-1.4", 0) == 0);
    CHECK (text.find ("/Count 2") != std::string::npos);
    CHECK (text.find ("832 m") != std::string::npos); // y goes up in PDF
    CHECK (text.find ("(A \\(test\\))") != std::string::npos);
    // The xref offsets point at the objects.
    const auto xref = std::stoul (text.substr (text.find ("startxref") + 10));
    CHECK (text.compare (xref, 4, "xref") == 0);
    const auto firstOffset = std::stoul (text.substr (xref + text.substr (xref).find ("0000000000 65535 f \n") + 20, 10));
    CHECK (text.compare (firstOffset, 7, "1 0 obj") == 0);
}

TEST_CASE ("Noteheads know the note they come from", "[notation][scoreedit]")
{
    Song song;
    auto& track = edit::addTrack (song, "Lead");
    Clip clip;
    clip.start = kQ * 4;
    clip.length = kQ * 8;
    clip.notes = { note (0, kQ * 6, 60), note (kQ * 6, kQ, 64) }; // the first note is tied over a bar line
    const auto clipId = edit::addClip (song, track.id, clip);

    const auto& t = song.tracks[0];
    const auto notes = timelineNotes (t);
    const auto refs = timelineNoteRefs (t);
    REQUIRE (refs.size() == notes.size());
    CHECK (refs[0] == NoteRef { clipId, 0, kQ * 4 });
    CHECK (refs[1] == NoteRef { clipId, 1, kQ * 4 });

    const auto part = makePart ("Lead", notes, song.contentEnd(), {}, 0);
    int fromFirst = 0;
    for (const auto& bar : part.staves[0].bars)
        for (const auto& e : bar.events)
            for (const auto& h : e.heads)
            {
                CHECK (h.source == (h.pitch == 60 ? 0 : 1));
                fromFirst += h.pitch == 60 ? 1 : 0;
            }
    CHECK (fromFirst >= 2); // tied pieces all point to the one note

    // The layout says where each head is, and a click there finds the same place.
    const auto layout = score::layout (part);
    REQUIRE_FALSE (layout.notes.empty());
    REQUIRE (layout.bars.size() == part.staves[0].bars.size());
    const auto& last = layout.notes.back();
    CHECK (last.source == 1);
    const auto hit = score::hitTest (layout, last.page, last.x, last.y);
    REQUIRE (hit.valid);
    CHECK (pitchAt (hit.clef, hit.position, 0) == 64);
    CHECK (hit.tick >= kQ * 10);
    CHECK (hit.tick < kQ * 11);
    CHECK_FALSE (score::hitTest (layout, last.page, last.x, last.y + 40.0f).valid);
    CHECK (score::xOfTick (layout, last.page, hit.staff, hit.top, kQ * 10) > 0.0f);
}

TEST_CASE ("Staff positions give pitches", "[notation][scoreedit]")
{
    CHECK (pitchAt (Clef::treble, 0, 0) == 64);  // E4, bottom line
    CHECK (pitchAt (Clef::treble, -2, 0) == 60); // middle C
    CHECK (pitchAt (Clef::treble, 8, 1) == 78);  // F sharp in G major
    CHECK (pitchAt (Clef::treble, 4, -1) == 70); // B flat in F major
    CHECK (pitchAt (Clef::bass, 0, 0) == 43);    // G2
    CHECK (pitchAt (Clef::bass, 10, 0) == 60);
    CHECK (drumPitchAt (1) == 36);
    CHECK (drumPitchAt (10) == 42);
    CHECK (drumPitchAt (13) == 49); // no drum up there: the nearest one
}

TEST_CASE ("Notes go on the Timeline from the score", "[scoreedit]")
{
    Song song;
    auto& track = edit::addTrack (song, "Lead");
    const auto id = track.id;

    // No clip yet: a clip of one bar is made.
    const auto first = edit::addTimelineNote (song, id, kQ * 5, kQ, 67);
    REQUIRE (first.clip != 0);
    REQUIRE (song.tracks[0].clips.size() == 1);
    CHECK (song.tracks[0].clips[0]->start == kQ * 4);
    CHECK (song.tracks[0].clips[0]->length == kQ * 4);
    CHECK (first.note.start == kQ);

    // The same bar again: the note goes into that clip.
    const auto second = edit::addTimelineNote (song, id, kQ * 6, kQ / 2, 69);
    CHECK (second.clip == first.clip);
    CHECK (song.tracks[0].clips[0]->notes.size() == 2);

    // Move up a tone, make longer.
    auto keys = edit::changeNotes (song, { first, second }, [] (Note& n) { n.pitch = static_cast<std::uint8_t> (n.pitch + 2); n.length *= 2; });
    REQUIRE (keys.size() == 2);
    CHECK (keys[0].note.pitch == 69);
    CHECK (keys[1].note.pitch == 71);
    CHECK (song.tracks[0].clips[0]->notes[0].length == kQ * 2);

    edit::removeNotes (song, { keys[0] });
    REQUIRE (song.tracks[0].clips[0]->notes.size() == 1);
    CHECK (song.tracks[0].clips[0]->notes[0].pitch == 71);

    // A stale key changes nothing.
    CHECK (edit::changeNotes (song, { first }, [] (Note& n) { n.pitch = 1; }).empty());
    CHECK (edit::addTimelineNote (song, 9999, 0, kQ, 60).clip == 0);
}

TEST_CASE ("Marks are drawn on the score", "[notation][marks]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back (note (kQ * i, kQ, 72 + (i % 3)));
    auto part = makePart ("Lead", notes, kQ * 8, {}, 0);
    const auto plain = score::layout (part);

    part.marks = {
        { ScoreMark::Kind::chord, 0, "Cmaj7" },
        { ScoreMark::Kind::dynamic, 0, "mf" },
        { ScoreMark::Kind::lyric, 0, "Extraordinary" },
        { ScoreMark::Kind::lyric, kQ, "words" },
        { ScoreMark::Kind::articulation, kQ * 2, "staccato" },
        { ScoreMark::Kind::articulation, kQ * 5, "fermata" },
    };
    const auto marked = score::layout (part);
    const auto& page = marked.pages.at (0);
    const auto hasText = [&] (const std::string& t) {
        return std::any_of (page.texts.begin(), page.texts.end(), [&] (const score::Text& x) { return x.text == t; });
    };
    const auto glyphs = [&] (char32_t code) {
        return std::count_if (page.glyphs.begin(), page.glyphs.end(), [&] (const score::Glyph& g) { return g.code == code; });
    };
    CHECK (hasText ("Cmaj7"));
    CHECK (hasText ("Extraordinary"));
    CHECK (hasText ("words"));
    CHECK (glyphs (score::smufl::dynamicMezzo) == 1);
    CHECK (glyphs (score::smufl::dynamicForte) == 1);
    CHECK (glyphs (score::smufl::articStaccatoAbove) + glyphs (score::smufl::articStaccatoBelow) == 1);
    CHECK (glyphs (score::smufl::fermataAbove) == 1);

    // A long word makes room: the second note moves to the right.
    const auto xOfSecond = [] (const score::Layout& l) { return l.notes.at (1).x - l.notes.at (0).x; };
    CHECK (xOfSecond (marked) > xOfSecond (plain) + 1.0f);
}
