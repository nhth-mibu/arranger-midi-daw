#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/ProjectFile.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/SongForm.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace arranger;

namespace
{
    constexpr Tick kBeat = kTicksPerQuarter;
    constexpr Tick kBar = kBeat * 4;

    struct Part
    {
        const char* name;
        int bars;
        bool melody, busy, pad;
        int root; ///< bass note
    };

    /** A little pop song: intro, verse, chorus, verse, chorus, outro. Each section plays differently. */
    Song popSong()
    {
        const Part parts[] = {
            { "Intro", 4, false, false, false, 45 },
            { "Verse", 8, true, false, false, 45 },
            { "Chorus", 8, true, true, true, 41 },
            { "Verse", 8, true, false, false, 45 },
            { "Chorus", 8, true, true, true, 41 },
            { "Outro", 4, false, false, true, 45 },
        };
        Song s;
        s.key = "A minor";
        auto add = [&] (const std::string& name, int channel) {
            auto& t = edit::addTrack (s, name);
            t.output = { TrackOutput::Kind::soundFont, {}, channel };
            return t.id;
        };
        const auto drums = add ("Drums", 10), bass = add ("Bass", 2), piano = add ("Piano", 3), melody = add ("Melody", 4), pad = add ("Strings", 5);
        Clip d, b, p, m, st;
        Tick at = 0;
        for (const auto& part : parts)
        {
            for (int bar = 0; bar < part.bars; ++bar)
            {
                const Tick t0 = at + bar * kBar;
                // Drums: hats in 8ths (16ths in the chorus), kick and snare.
                const Tick hat = part.busy ? kBeat / 4 : kBeat / 2;
                if (part.name != std::string ("Intro") || bar >= 2)
                    for (Tick t = 0; t < kBar; t += hat)
                        d.notes.push_back ({ t0 + t, 60, 42, static_cast<std::uint8_t> (part.busy ? 90 : 70) });
                for (Tick t = 0; t < kBar; t += kBeat * 2)
                {
                    d.notes.push_back ({ t0 + t, 60, 36, 100 });
                    d.notes.push_back ({ t0 + t + kBeat, 60, 38, 96 });
                }
                // Bass on the beats (8ths in the chorus).
                for (Tick t = 0; t < kBar; t += part.busy ? kBeat / 2 : kBeat)
                    b.notes.push_back ({ t0 + t, (part.busy ? kBeat / 2 : kBeat) - 20, static_cast<std::uint8_t> (part.root), 92 });
                // Piano chords.
                for (Tick t = 0; t < kBar; t += kBeat * 2)
                    for (int iv : { 12, 15, 19 })
                        p.notes.push_back ({ t0 + t, kBeat * 2 - 20, static_cast<std::uint8_t> (part.root + 12 + iv), 80 });
                if (part.melody)
                {
                    const int verse[] = { 69, 72, 71, 69 }, chorus[] = { 76, 74, 72, 74 };
                    for (int beat = 0; beat < 4; ++beat)
                        m.notes.push_back ({ t0 + beat * kBeat, kBeat - 30, static_cast<std::uint8_t> ((part.busy ? chorus : verse)[(beat + bar) % 4]), 100 });
                }
                if (part.pad)
                    st.notes.push_back ({ t0, kBar - 10, static_cast<std::uint8_t> (part.root + 24), 70 });
            }
            at += part.bars * kBar;
        }
        for (auto [id, clip] : { std::pair { drums, &d }, { bass, &b }, { piano, &p }, { melody, &m }, { pad, &st } })
        {
            clip->start = 0;
            clip->length = at;
            edit::normalise (*clip);
            edit::addClip (s, id, *clip);
        }
        return s;
    }

    TrackId idOf (const Song& s, const std::string& name)
    {
        for (const auto& t : s.tracks)
            if (t.name == name)
                return t.id;
        return 0;
    }

    std::vector<Note> notesOf (const Song& s, const std::string& track, Tick from, Tick to)
    {
        std::vector<Note> out;
        if (const auto* t = edit::findTrack (s, idOf (s, track)))
            for (const auto& c : t->clips)
                for (const auto& n : c->notes)
                    if (c->start + n.start >= from && c->start + n.start < to)
                        out.push_back ({ c->start + n.start, n.length, n.pitch, n.velocity });
        return out;
    }

    std::vector<std::string> names (const std::vector<SongSection>& secs)
    {
        std::vector<std::string> out;
        for (const auto& s : secs)
            out.push_back (s.name);
        return out;
    }
}

TEST_CASE ("Song forms find the sections of a song", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::findSections (s);
    CHECK (names (secs) == std::vector<std::string> { "Intro", "Verse 1", "Chorus 1", "Verse 2", "Chorus 2", "Outro" });
    REQUIRE (secs.size() == 6);
    CHECK (secs[0].start == 0);
    CHECK (secs[1].start == 4 * kBar);
    CHECK (secs[2].start == 12 * kBar);
    CHECK (secs[5].end == 40 * kBar);
    CHECK (form::kindOf ("Chorus 2") == "chorus");
    CHECK (form::kindOf ("Pre-Chorus") == "prechorus");
    CHECK (form::problemWith (secs).empty());
}

TEST_CASE ("Song forms find sections in recordings by their loudness", "[songform]")
{
    // Two recorded tracks: pads all through, drums only from bar 9. Each bar is two seconds (120 BPM).
    Song s;
    const double seconds = s.tempo.secondsAt (static_cast<double> (16 * kBar));
    std::map<std::string, std::shared_ptr<const Sample>> files;
    const auto record = [&] (const std::string& name, int fromBar) {
        Sample sm;
        sm.sampleRate = 8000.0;
        sm.channels.assign (1, std::vector<float> (static_cast<std::size_t> (seconds * sm.sampleRate), 0.0f));
        const auto start = static_cast<std::size_t> (s.tempo.secondsAt (static_cast<double> (fromBar * kBar)) * sm.sampleRate);
        for (auto f = start; f < sm.frames(); ++f)
            sm.channels[0][f] = 0.3f * static_cast<float> (std::sin (static_cast<double> (f) * 0.2));
        files[name + ".wav"] = std::make_shared<const Sample> (std::move (sm));
        auto& t = edit::addTrack (s, name);
        t.output.kind = TrackOutput::Kind::audio;
        AudioClip c;
        c.file = name + ".wav";
        c.length = 16 * kBar;
        edit::addAudioClip (s, t.id, c);
    };
    record ("Pads", 0);
    record ("Drums", 8);
    const auto lookup = [&] (const std::string& f) -> std::shared_ptr<const Sample> {
        const auto it = files.find (f);
        return it == files.end() ? nullptr : it->second;
    };

    const auto heard = form::findSections (s, nullptr, lookup);
    REQUIRE (heard.size() == 2);
    CHECK (heard[1].start == 8 * kBar);
    // Without the audio, only the clips count: the clips play all through.
    CHECK (form::findSections (s).size() != 2);
}

TEST_CASE ("Song forms read and write sections as text", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::parseSections (s, "Intro 1-4\nVerse 1: 5-12\nChorus 1 = 13 - 20\nTag 21");
    REQUIRE (secs.size() == 4);
    CHECK (secs[1].name == "Verse 1");
    CHECK (secs[2].start == 12 * kBar);
    CHECK (secs[2].end == 20 * kBar);
    CHECK (secs[3].end - secs[3].start == kBar);
    CHECK (form::sectionsText (s, secs) == "Intro: 1-4\nVerse 1: 5-12\nChorus 1: 13-20\nTag: 21-21\n");
    CHECK_THROWS (form::parseSections (s, "Intro 1-4\nIntro 5-8"));
    CHECK_THROWS (form::parseSections (s, "Intro 1-8\nVerse 5-12"));
    CHECK_THROWS (form::parseSections (s, "Intro"));
}

TEST_CASE ("Song form presets", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::findSections (s);

    const auto shortForm = form::makePreset (s, secs, form::Preset::shortVersion);
    CHECK (form::describe (shortForm) == "Intro \xE2\x86\x92 Verse 1 \xE2\x86\x92 Chorus 1 \xE2\x86\x92 Chorus 2 \xE2\x86\x92 Outro");

    const auto gig = form::makePreset (s, secs, form::Preset::gig);
    CHECK (form::describe (gig) == "Intro (vamp) \xE2\x86\x92 Intro \xE2\x86\x92 Verse 1 \xE2\x86\x92 Chorus 1 \xE2\x86\x92 Verse 2 \xE2\x86\x92 Chorus 2 \xE2\x86\x92 Chorus 2 (again) \xE2\x86\x92 Outro");
    CHECK (gig.ending == Transition::hold);

    const auto build = form::makePreset (s, secs, form::Preset::longIntro);
    REQUIRE (build.parts.size() == 7);
    CHECK (build.parts[0].label == "Intro (build)");
    // The build keeps drums and bass only.
    const std::set<TrackId> muted (build.parts[0].muted.begin(), build.parts[0].muted.end());
    CHECK (muted.count (idOf (s, "Piano")) == 1);
    CHECK (muted.count (idOf (s, "Drums")) == 0);
    CHECK (muted.count (idOf (s, "Bass")) == 0);

    const auto solo = form::makePreset (s, secs, form::Preset::instrumentalBreak);
    CHECK (form::describe (solo) == "Intro \xE2\x86\x92 Verse 1 \xE2\x86\x92 Chorus 1 \xE2\x86\x92 Verse 2 \xE2\x86\x92 Solo \xE2\x86\x92 Chorus 2 \xE2\x86\x92 Outro");
    const auto& part = solo.parts[4];
    CHECK (part.section == "Verse 1");
    CHECK (part.muted == std::vector<TrackId> { idOf (s, "Melody") });

    CHECK (form::parsePreset ("gig") == form::Preset::gig);
    CHECK (form::parsePreset ("Long intro") == form::Preset::longIntro);
    CHECK_THROWS (form::makePreset (s, {}, form::Preset::gig));
}

TEST_CASE ("Song forms from text", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::findSections (s);
    const auto f = form::parseForm (s, secs, "Mine", "Intro -> Chorus x2, Verse 1 (no melody), Outro");
    REQUIRE (f.parts.size() == 5);
    CHECK (f.parts[1].section == "Chorus 1"); // "Chorus" finds the first chorus
    CHECK (f.parts[2].section == "Chorus 1");
    CHECK (f.parts[3].muted == std::vector<TrackId> { idOf (s, "Melody") });
    CHECK (form::describe (f) == "Intro \xE2\x86\x92 Chorus 1 \xC3\x97" "2 \xE2\x86\x92 Verse 1 \xE2\x86\x92 Outro");
    CHECK_THROWS_WITH (form::parseForm (s, secs, "x", "Intro, Bridge"), "No section \"Bridge\". The sections are: Intro, Verse 1, Chorus 1, Verse 2, Chorus 2, Outro.");
    CHECK_THROWS (form::parseForm (s, secs, "x", "Intro (no kazoo)"));
}

TEST_CASE ("Song forms render the new order with transitions", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::findSections (s);
    const auto f = form::makePreset (s, secs, form::Preset::shortVersion);
    const auto r = form::render (s, f, secs);

    // Intro 4 + Verse 8 + Chorus 8 + Chorus 8 + Outro 4 = 32 bars, the outro ends it (no final chord).
    CHECK (r.length == 32 * kBar);
    CHECK (r.ending == Transition::none);
    REQUIRE (r.boundaries.size() == 4);
    CHECK (r.boundaries[0].natural);  // intro -> verse 1, as in the original
    CHECK (r.boundaries[0].chosen == Transition::none);
    CHECK (r.boundaries[1].natural);
    // Chorus 1 -> Chorus 2: a new join.
    const auto& join = r.boundaries[2];
    CHECK_FALSE (join.natural);
    CHECK (join.at == 20 * kBar);
    CHECK (join.chosen != Transition::none);
    CHECK_FALSE (join.why.empty());
    CHECK (std::find (join.options.begin(), join.options.end(), Transition::fill) != join.options.end());

    // The sections follow the parts.
    REQUIRE (r.song.sections.size() == 5);
    CHECK (r.song.sections[3].name == "Chorus 2");
    CHECK (r.song.sections[3].start == 20 * kBar);
    // Every MIDI track has one clip for the form.
    for (const auto& t : r.song.tracks)
    {
        REQUIRE (t.clips.size() == 1);
        CHECK (t.clips[0]->length == r.length);
    }
    // The melody of chorus 2 is the chorus tune.
    const auto melody = notesOf (r.song, "Melody", 20 * kBar, 21 * kBar);
    REQUIRE_FALSE (melody.empty());
    CHECK (melody.front().pitch == 76);
    // The original is unchanged.
    CHECK (s.contentEnd() == 40 * kBar);
}

TEST_CASE ("Song form transitions change the bar before the join", "[songform]")
{
    const auto s = popSong();
    const auto secs = form::findSections (s);
    auto f = form::parseForm (s, secs, "Test", "Verse 1, Chorus 1, Chorus 1, Verse 2");
    const Tick join = 16 * kBar; // Chorus 1 -> Chorus 1

    SECTION ("A drum fill with toms and a crash")
    {
        f.parts[2].into = Transition::fill;
        const auto r = form::render (s, f, secs);
        const auto drums = notesOf (r.song, "Drums", join - kBar, join + kBeat);
        const auto toms = std::count_if (drums.begin(), drums.end(), [] (const Note& n) { return n.pitch >= 41 && n.pitch <= 50 && n.pitch != 42 && n.pitch != 44 && n.pitch != 46; });
        CHECK (toms >= 3);
        CHECK (std::any_of (drums.begin(), drums.end(), [&] (const Note& n) { return n.start == join && n.pitch == 49; }));
        // No hi-hats in the fill's beat.
        CHECK (std::none_of (drums.begin(), drums.end(), [&] (const Note& n) { return n.pitch == 42 && n.start >= join - kBeat && n.start < join; }));
        // The fill gets louder.
        std::vector<Note> fill;
        for (const auto& n : drums)
            if (n.start >= join - kBeat && n.start < join && n.pitch != 36)
                fill.push_back (n);
        REQUIRE (fill.size() >= 3);
        CHECK (fill.back().velocity > fill.front().velocity);
    }
    SECTION ("A hold rings the chord to the join")
    {
        f.parts[2].into = Transition::hold;
        const auto r = form::render (s, f, secs);
        const auto piano = notesOf (r.song, "Piano", join - kBeat * 2 + kBeat / 4, join);
        CHECK (piano.empty()); // nothing new starts after the hold
        const auto held = notesOf (r.song, "Piano", join - kBeat * 3, join - kBeat * 2 + 1);
        REQUIRE_FALSE (held.empty());
        CHECK (held.front().start + held.front().length >= join - 20);
        const auto hats = notesOf (r.song, "Drums", join - kBeat * 2 + 1, join);
        CHECK (hats.empty());
    }
    SECTION ("A break is one beat of silence")
    {
        f.parts[2].into = Transition::stop;
        const auto r = form::render (s, f, secs);
        for (const auto* t : { "Drums", "Bass", "Piano", "Melody" })
            CHECK (notesOf (r.song, t, join - kBeat, join).empty());
        for (const auto& n : notesOf (r.song, "Melody", join - kBar, join))
            CHECK (n.start + n.length <= join - kBeat);
    }
    SECTION ("A lead-in brings what led into the section in the original")
    {
        f.parts[2].into = Transition::pickup;
        const auto r = form::render (s, f, secs);
        // In the original, verse 1 led into chorus 1: its last two beats (bass on A) now end the first chorus.
        const auto bass = notesOf (r.song, "Bass", join - kBeat * 2, join);
        REQUIRE_FALSE (bass.empty());
        CHECK (bass.front().pitch == 45);
        CHECK (bass.size() == 2); // the verse plays quarters
    }
    SECTION ("A straight cut changes nothing")
    {
        f.parts[2].into = Transition::none;
        const auto r = form::render (s, f, secs);
        const auto drums = notesOf (r.song, "Drums", join - kBeat, join);
        CHECK (std::any_of (drums.begin(), drums.end(), [] (const Note& n) { return n.pitch == 42; }));
    }
}

TEST_CASE ("Song forms: a gig version ends on a final chord in the key", "[songform]")
{
    auto s = popSong();
    const auto secs = form::findSections (s);
    auto f = form::parseForm (s, secs, "Gig", "Verse 1, Chorus 1, Verse 2");
    f.ending = Transition::hold;
    const auto r = form::render (s, f, secs);
    CHECK (r.length == 25 * kBar);
    CHECK (r.song.sections.back().name == "Ending");
    const auto bass = notesOf (r.song, "Bass", 24 * kBar, 25 * kBar);
    REQUIRE (bass.size() == 1);
    CHECK (bass[0].pitch % 12 == 9); // A, the tonic of A minor
    const auto piano = notesOf (r.song, "Piano", 24 * kBar, 25 * kBar);
    std::set<int> classes;
    for (const auto& n : piano)
        classes.insert (n.pitch % 12);
    CHECK (classes == std::set<int> { 9, 0, 4 }); // A minor: A C E
    const auto drums = notesOf (r.song, "Drums", 24 * kBar, 25 * kBar);
    CHECK (std::any_of (drums.begin(), drums.end(), [] (const Note& n) { return n.pitch == 49; }));
}

TEST_CASE ("Song forms: pushes go with the section they lead into", "[songform]")
{
    auto s = popSong();
    // A melody note a sixteenth before chorus 1 that rings into it.
    const auto melody = idOf (s, "Melody");
    edit::modifyClip (s, s.tracks[3].clips[0]->id, [] (Clip& c) {
        c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [] (const Note& n) { return n.start >= 12 * kBar - kBeat && n.start < 12 * kBar + kBeat; }), c.notes.end());
        c.notes.push_back ({ 12 * kBar - kBeat / 4, kBeat, 81, 110 });
        edit::normalise (c);
    });
    REQUIRE (melody != 0);
    const auto secs = form::findSections (s);
    const auto f = form::parseForm (s, secs, "Push", "Intro, Chorus 1");
    const auto r = form::render (s, f, secs);
    // The push is now a sixteenth before the chorus in the form (bar 5).
    const auto pushed = notesOf (r.song, "Melody", 4 * kBar - kBeat, 4 * kBar);
    REQUIRE (pushed.size() == 1);
    CHECK (pushed[0].pitch == 81);
    CHECK (pushed[0].start == 4 * kBar - kBeat / 4);
}

TEST_CASE ("Song forms: using a form is one undo step and is saved", "[songform]")
{
    Document doc (popSong());
    const auto secs = form::findSections (doc.song());
    doc.edit ("Song Form", [&] (Song& s) {
        s.sections = secs;
        form::putForm (s, form::makePreset (s, secs, form::Preset::shortVersion));
        auto gig = form::makePreset (s, secs, form::Preset::gig);
        gig.parts[1].into = Transition::stop;
        form::putForm (s, gig);
    });
    REQUIRE (doc.song().forms.size() == 2);

    // Saved and read again.
    const auto again = project_file::fromJson (project_file::toJson (doc.song()));
    CHECK (again.sections == doc.song().sections);
    CHECK (again.forms == doc.song().forms);

    const auto before = doc.song();
    doc.edit ("Use Form", [&] (Song& s) { s = form::use (s, *form::findForm (s, "short version"), s.sections); });
    CHECK (doc.song().contentEnd() == 32 * kBar);
    CHECK (doc.song().forms.empty());
    CHECK (doc.song().sections.size() == 5);
    REQUIRE (doc.undo());
    CHECK (doc.song() == before);
}

TEST_CASE ("Song forms: a style's Fill In is the fill", "[songform]")
{
    auto s = popSong();
    s.style = StyleData {};
    s.scenes = { "Main A", "Fill In AA" };
    auto& rhythm = edit::addTrack (s, "Rhythm 1");
    rhythm.styleChannel = 9;
    Clip fill;
    fill.length = kBar;
    for (Tick t = 0; t < kBar; t += kBeat / 2)
        fill.notes.push_back ({ t, 100, 39, 100 }); // hand claps: easy to find
    const auto rid = rhythm.id;
    edit::setSlot (s, rid, 1, fill);
    const auto secs = form::findSections (s);
    auto f = form::parseForm (s, secs, "Style", "Verse 1, Chorus 1, Chorus 1");
    f.parts[2].into = Transition::fill;
    const auto r = form::render (s, f, secs);
    const auto drums = notesOf (r.song, "Drums", 15 * kBar, 16 * kBar);
    CHECK (std::count_if (drums.begin(), drums.end(), [] (const Note& n) { return n.pitch == 39; }) == 8);
    CHECK (std::any_of (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("Fill In AA") != std::string::npos; }));
}

TEST_CASE ("Song forms from Clip grid scenes", "[songform]")
{
    Song s;
    auto& drums = edit::addTrack (s, "Drums");
    drums.output = { TrackOutput::Kind::soundFont, {}, 10 };
    const auto id = drums.id;
    s.scenes = { "Groove", "Break" };
    Clip a;
    a.length = kBar;
    a.notes = { { 0, 100, 36, 100 }, { kBeat, 100, 38, 100 } };
    edit::setSlot (s, id, 0, a);
    Clip b;
    b.length = kBar * 2;
    b.notes = { { 0, 100, 49, 100 } };
    edit::setSlot (s, id, 1, b);

    const auto secs = form::findSections (s);
    REQUIRE (names (secs) == std::vector<std::string> { "Groove", "Break" });
    CHECK (secs[0].scene == 0);
    CHECK (secs[0].length() == 4 * kBar); // short loops play for four bars
    const auto f = form::parseForm (s, secs, "Jam", "Groove, Break, Groove");
    const auto r = form::render (s, f, secs);
    CHECK (r.length == 13 * kBar); // with a last hit at the end
    const auto kicks = notesOf (r.song, "Drums", 0, 3 * kBar);
    CHECK (std::count_if (kicks.begin(), kicks.end(), [] (const Note& n) { return n.pitch == 36; }) == 3);
    // The second groove is the same loop again.
    const auto again = notesOf (r.song, "Drums", 8 * kBar, 11 * kBar);
    CHECK (std::count_if (again.begin(), again.end(), [] (const Note& n) { return n.pitch == 38; }) == 3);
}

TEST_CASE ("Song forms keep tempo changes and chords with their sections", "[songform]")
{
    auto s = popSong();
    s.tempo.set (12 * kBar, 128.0); // the chorus is faster
    s.tempo.set (20 * kBar, 120.0);
    s.tracks[2].marks.push_back ({ ScoreMark::Kind::chord, 12 * kBar, "F" });
    s.tracks[2].marks.push_back ({ ScoreMark::Kind::chord, 20 * kBar, "Am" });
    const auto secs = form::findSections (s);
    const auto f = form::parseForm (s, secs, "T", "Chorus 1, Verse 2");
    const auto r = form::render (s, f, secs);
    CHECK (r.song.tempo.tempoAt (0.0) == 128.0);
    CHECK (r.song.tempo.tempoAt (static_cast<double> (9 * kBar)) == 120.0);
    const auto& marks = r.song.tracks[2].marks;
    REQUIRE (marks.size() == 2);
    CHECK (marks[0].text == "F");
    CHECK (marks[1].text == "Am");
    CHECK (marks[1].at == 8 * kBar);
}

// ---- the AI tools ----------------------------------------------------------------
namespace
{
    struct FormHost final : mcp::Host
    {
        Document doc;
        std::optional<Song> playing;
        Tick from = 0, to = 0;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        bool undo() override { return doc.undo(); }
        void audition (const Song* s, Tick a, Tick b) override
        {
            playing = s != nullptr ? std::optional<Song> (*s) : std::nullopt;
            from = a;
            to = b;
        }
    };
}

TEST_CASE ("The AI tools make, preview and use song forms", "[songform][mcp]")
{
    FormHost host;
    host.doc.reset (popSong());
    mcp::Server server (host);
    using nlohmann::json;

    const auto sections = json::parse (server.call ("song_sections", "{}"));
    CHECK_FALSE (sections["saved"].get<bool>());
    CHECK (sections["sections"][2]["name"] == "Chorus 1");
    CHECK (sections["sections"][2]["from_bar"] == 13);

    const auto made = json::parse (server.call ("make_song_form", R"({"preset": "short"})"));
    CHECK (made["name"] == "Short version");
    CHECK (made["bars"] == 32);
    CHECK (host.doc.song().sections.size() == 6); // saved with the form
    CHECK (host.doc.song().contentEnd() == 40 * kBar); // the Timeline is unchanged
    const auto& joins = made["joins"];
    REQUIRE (joins.size() == 4);
    CHECK (joins[0]["natural"].get<bool>());
    CHECK_FALSE (joins[2]["natural"].get<bool>());

    server.call ("make_song_form", R"({"name": "Mine", "order": "Intro, Chorus 1 x2, Outro"})");
    const auto set = json::parse (server.call ("set_form_transition", R"({"form": "mine", "join": 2, "transition": "stop", "ending": "hold"})"));
    CHECK (set["joins"][1]["transition"] == "stop");
    CHECK (set["joins"][1]["chosen_by"] == "user");
    CHECK (set["ending"] == "hold");
    CHECK (json::parse (server.call ("song_forms", "{}"))["forms"].size() == 2);

    server.call ("preview_song_form", R"({"form": "Mine", "join": 2})");
    REQUIRE (host.playing);
    CHECK (host.from == 10 * kBar);
    CHECK (host.to == 14 * kBar);
    server.call ("preview_song_form", R"({"stop": true})");
    CHECK_FALSE (host.playing);

    const auto used = json::parse (server.call ("use_song_form", R"({"form": "Mine"})"));
    CHECK (used["bars"] == 25); // 4 + 8 + 8 + 4 and the final chord
    CHECK (host.doc.song().forms.empty());
    REQUIRE (host.doc.undo());
    CHECK (host.doc.song().forms.size() == 2);
    CHECK_THROWS (server.call ("use_song_form", R"({"form": "Nope"})"));
    server.call ("remove_song_form", R"({"form": "Mine"})");
    CHECK (host.doc.song().forms.size() == 1);

    const auto saved = json::parse (server.call ("set_song_sections", R"({"sections": [{"name": "A", "from_bar": 1, "to_bar": 20}, {"name": "B", "from_bar": 21, "to_bar": 40}]})"));
    CHECK (saved["saved"].get<bool>());
    CHECK_THROWS (server.call ("set_song_sections", R"({"sections": [{"name": "A", "from_bar": 1, "to_bar": 20}, {"name": "A", "from_bar": 21, "to_bar": 40}]})"));
    // The short version names sections that are gone now.
    CHECK (json::parse (server.call ("song_forms", "{}"))["forms"][0].contains ("problem"));
}
