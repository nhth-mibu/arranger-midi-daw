#include "arranger/core/MusicXml.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>

using namespace arranger;

namespace
{
    constexpr Tick kQ = kTicksPerQuarter;

    Song songWith (std::vector<Note> notes, const std::string& name = "Piano", int channel = 1)
    {
        Song song;
        song.title = "Test & <Song>";
        song.key = "D major";
        song.tempo.set (0, 96.0);
        auto& track = edit::addTrack (song, name);
        track.output.channel = channel;
        track.sound.program = 4;
        Clip clip;
        clip.length = kQ * 8;
        clip.notes = std::move (notes);
        edit::addClip (song, track.id, std::move (clip));
        return song;
    }

    std::vector<Note> notesOf (const Song& song, std::size_t track = 0) { return song.tracks.at (track).clips.at (0)->notes; }

    std::size_t count (const std::string& text, const std::string& what)
    {
        std::size_t n = 0;
        for (auto at = text.find (what); at != std::string::npos; at = text.find (what, at + 1))
            ++n;
        return n;
    }
}

TEST_CASE ("MusicXML: a song goes out and comes back with the same notes", "[musicxml]")
{
    const std::vector<Note> notes = {
        { 0, kQ, 62, 80 }, { kQ, kQ / 2, 66, 80 }, { kQ * 3 / 2, kQ / 2, 69, 80 },
        { kQ * 2, kQ * 3, 61, 80 },                      // C sharp, tied over the bar line
        { kQ * 5, kQ, 50, 80 }, { kQ * 5, kQ, 57, 80 },  // a chord
    };
    const auto song = songWith (notes);
    const auto xml = musicxml::write (song);
    CHECK (xml.find ("<score-partwise") != std::string::npos);
    CHECK (xml.find ("Test &amp; &lt;Song&gt;") != std::string::npos);
    CHECK (xml.find ("<fifths>2</fifths>") != std::string::npos);
    CHECK (xml.find ("<tie type=\"start\"/>") != std::string::npos);
    CHECK (xml.find ("<chord/>") != std::string::npos);
    CHECK (count (xml, "<measure ") == 2);

    const auto back = musicxml::read (xml);
    CHECK (back.title == song.title);
    CHECK (back.key == "D major");
    CHECK (back.baseTempo() == 96.0);
    REQUIRE (back.tracks.size() == 1);
    CHECK (back.tracks[0].name == "Piano");
    CHECK (back.tracks[0].sound.program == 4);
    CHECK (notesOf (back) == notes);
}

TEST_CASE ("MusicXML: triplets and a grand staff survive", "[musicxml]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 3; ++i)
        notes.push_back ({ kQ / 3 * i, kQ / 3, static_cast<std::uint8_t> (72 + i), 80 });
    notes.push_back ({ 0, kQ * 4, 36, 80 });
    notes.push_back ({ kQ, kQ, 76, 80 });
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    const auto xml = musicxml::write (songWith (notes));
    CHECK (xml.find ("<staves>2</staves>") != std::string::npos);
    CHECK (xml.find ("<backup>") != std::string::npos);
    CHECK (count (xml, "<actual-notes>3</actual-notes>") == 3);
    CHECK (notesOf (musicxml::read (xml)) == notes);
}

TEST_CASE ("MusicXML: drums keep their drum notes", "[musicxml]")
{
    const std::vector<Note> notes = { { 0, kQ / 2, 36, 80 }, { 0, kQ / 2, 42, 80 }, { kQ / 2, kQ / 2, 42, 80 }, { kQ, kQ, 38, 80 } };
    const auto xml = musicxml::write (songWith (notes, "Drums", 10));
    CHECK (xml.find ("<unpitched>") != std::string::npos);
    CHECK (xml.find ("<sign>percussion</sign>") != std::string::npos);
    const auto back = musicxml::read (xml);
    REQUIRE (back.tracks.size() == 1);
    CHECK (back.tracks[0].output.channel == 10);
    CHECK (back.key == "D major"); // a drum part does not hide the key of the song
    const auto got = notesOf (back);
    REQUIRE (got.size() == notes.size());
    for (std::size_t i = 0; i < got.size(); ++i)
    {
        CHECK (got[i].start == notes[i].start);
        CHECK (got[i].pitch == notes[i].pitch);
    }
}

TEST_CASE ("MusicXML: reads a file from another program", "[musicxml]")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">
<!-- a comment -->
<score-partwise version="3.1">
  <movement-title>Caf&#233; &amp; Bar</movement-title>
  <part-list>
    <score-part id="P1"><part-name>Clarinet in B&#x266d;</part-name>
      <midi-instrument id="P1-I1"><midi-channel>1</midi-channel><midi-program>72</midi-program></midi-instrument></score-part>
    <score-part id="P2"><part-name>Bass</part-name>
      <midi-instrument id="P2-I1"><midi-channel>1</midi-channel><midi-program>33</midi-program></midi-instrument></score-part>
  </part-list>
  <part id="P1">
    <measure number="0" implicit="yes">
      <attributes><divisions>2</divisions><key><fifths>-1</fifths><mode>minor</mode></key>
        <time><beats>3</beats><beat-type>4</beat-type></time>
        <transpose><diatonic>-1</diatonic><chromatic>-2</chromatic></transpose></attributes>
      <direction><direction-type><dynamics><f/></dynamics></direction-type><sound tempo="132"/></direction>
      <note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><type>quarter</type></note>
    </measure>
    <measure number="1">
      <note><grace/><pitch><step>G</step><octave>5</octave></pitch><type>eighth</type></note>
      <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>6</duration><tie type="start"/></note>
    </measure>
    <measure number="2">
      <note><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>2</duration><tie type="stop"/></note>
      <note><pitch><step>A</step><octave>4</octave></pitch><duration>2</duration></note>
      <note><chord/><pitch><step>C</step><octave>5</octave></pitch><duration>2</duration></note>
      <backup><duration>2</duration></backup>
      <note><pitch><step>E</step><octave>3</octave></pitch><duration>4</duration><voice>2</voice></note>
    </measure>
  </part>
  <part id="P2">
    <measure number="0" implicit="yes">
      <attributes><divisions>1</divisions></attributes>
      <note><rest/><duration>1</duration></note>
    </measure>
    <measure number="1"><note><pitch><step>D</step><octave>2</octave></pitch><duration>3</duration></note></measure>
    <measure number="2"><note><rest measure="yes"/><duration>3</duration></note></measure>
  </part>
</score-partwise>)";
    const auto song = musicxml::read (xml);
    CHECK (song.title == "Caf\xc3\xa9 & Bar");
    CHECK (song.key == "D minor");
    CHECK (song.timeSignature == TimeSignature { 3, 4 });
    CHECK (song.baseTempo() == 132.0);
    REQUIRE (song.tracks.size() == 2);
    CHECK (song.tracks[0].name == "Clarinet in B\xe2\x99\xad");
    CHECK (song.tracks[0].sound.program == 71);
    CHECK (song.tracks[0].output.channel == 1);
    CHECK (song.tracks[1].output.channel == 2); // two parts on one channel get a channel each

    // The pickup quarter lands on beat 3 of bar 1; the clarinet sounds a tone lower than written.
    const std::vector<Note> clarinet = {
        { kQ * 2, kQ, 60, 96 },
        { kQ * 3, kQ * 4, 64, 96 },   // F sharp, tied into the next measure
        { kQ * 7, kQ, 67, 96 }, { kQ * 7, kQ, 70, 96 },
        { kQ * 7, kQ * 2, 50, 96 },
    };
    auto got = notesOf (song, 0);
    auto want = clarinet;
    const auto order = [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; };
    std::sort (want.begin(), want.end(), order);
    CHECK (got == want);
    const auto bass = notesOf (song, 1);
    REQUIRE (bass.size() == 1);
    CHECK (bass[0].start == kQ * 3);
    CHECK (bass[0].length == kQ * 3);
    CHECK (bass[0].pitch == 38);
    CHECK (song.tracks[0].clips[0]->length == kQ * 9);
}

TEST_CASE ("MusicXML: bad files give clear errors", "[musicxml]")
{
    CHECK_THROWS_AS (musicxml::read ("PK\x03\x04zip"), musicxml::Error);
    CHECK_THROWS_AS (musicxml::read ("<html><body/></html>"), musicxml::Error);
    CHECK_THROWS_AS (musicxml::read ("<score-partwise><part-list/></score-partwise>"), musicxml::Error);
    CHECK_THROWS_AS (musicxml::read ("<score-partwise><part"), musicxml::Error);
    CHECK_THROWS_AS (musicxml::read ("</a>"), musicxml::Error);
    CHECK_THROWS_AS (musicxml::read (""), musicxml::Error);
}

TEST_CASE ("MusicXML: chord symbols, lyrics, dynamics and articulations survive", "[musicxml][marks]")
{
    auto song = songWith ({ { 0, kQ, 62, 80 }, { kQ, kQ, 64, 80 }, { kQ * 2, kQ * 2, 66, 80 }, { kQ * 4, kQ * 4, 69, 80 } }, "Lead");
    const auto id = song.tracks[0].id;
    edit::setMark (song, id, { ScoreMark::Kind::chord, 0, "D" });
    edit::setMark (song, id, { ScoreMark::Kind::chord, kQ * 2, "F#m7/A" });
    edit::setMark (song, id, { ScoreMark::Kind::chord, kQ * 4, "Bbmaj7" });
    edit::setMark (song, id, { ScoreMark::Kind::dynamic, 0, "mf" });
    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ, "staccato" });
    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ, "accent" });
    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ * 4, "fermata" });
    CHECK (edit::setLyrics (song, id, 0, "Hel-lo big world and more", { 0, kQ, kQ * 2, kQ * 4 }) == 4);
    REQUIRE (song.tracks[0].marks.size() == 11);

    const auto xml = musicxml::write (song);
    CHECK (xml.find ("<kind text=\"m7\">minor-seventh</kind>") != std::string::npos);
    CHECK (xml.find ("<bass-step>A</bass-step>") != std::string::npos);
    CHECK (xml.find ("<root-alter>-1</root-alter>") != std::string::npos);
    CHECK (xml.find ("<syllabic>begin</syllabic><text>Hel</text>") != std::string::npos);
    CHECK (xml.find ("<dynamics><mf/></dynamics>") != std::string::npos);
    CHECK (xml.find ("<articulations><staccato/><accent/></articulations>") != std::string::npos);
    CHECK (xml.find ("<fermata/>") != std::string::npos);

    const auto back = musicxml::read (xml);
    CHECK (back.tracks.at (0).marks == song.tracks[0].marks);

    // The project file keeps them too.
    const auto again = project_file::fromJson (project_file::toJson (song));
    CHECK (again.tracks.at (0).marks == song.tracks[0].marks);
}

TEST_CASE ("Score marks: set, replace, toggle and clear", "[marks]")
{
    Song song;
    const auto id = edit::addTrack (song, "Lead").id;
    edit::setMark (song, id, { ScoreMark::Kind::chord, kQ, "C" });
    edit::setMark (song, id, { ScoreMark::Kind::chord, kQ, "Am" }); // replaces
    edit::setMark (song, id, { ScoreMark::Kind::chord, 0, "F" });
    REQUIRE (song.tracks[0].marks.size() == 2);
    CHECK (song.tracks[0].marks[0].text == "F"); // sorted by time
    CHECK (song.tracks[0].marks[1].text == "Am");

    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ, "staccato" });
    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ, "tenuto" });
    CHECK (song.tracks[0].marks.size() == 4);
    edit::setMark (song, id, { ScoreMark::Kind::articulation, kQ, "staccato" }); // the same again: off
    CHECK (song.tracks[0].marks.size() == 3);

    edit::setMark (song, id, { ScoreMark::Kind::chord, 0, "" }); // empty text removes
    CHECK (song.tracks[0].marks.size() == 2);
    edit::clearMarks (song, id, kQ, kQ + 1);
    CHECK (song.tracks[0].marks.empty());
}
