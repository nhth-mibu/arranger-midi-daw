#include "arranger/core/Clarify.h"
#include "arranger/core/Document.h"
#include "arranger/core/McpServer.h"
#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace arranger;

namespace
{
    constexpr double kHop = 0.1;

    /** Chroma frames: `seconds` of `notes` (pitch classes) with `low` in the bass. */
    void addFrames (analysis::Chroma& c, double seconds, std::initializer_list<int> notes, std::initializer_list<std::pair<int, float>> low)
    {
        c.hopSeconds = kHop;
        const auto n = static_cast<int> (std::lround (seconds / kHop));
        for (int i = 0; i < n; ++i)
        {
            std::array<float, 12> all {}, bass {};
            for (const int p : notes)
                all[static_cast<std::size_t> (p)] += 1.0f;
            for (const auto& [p, v] : low)
            {
                bass[static_cast<std::size_t> (p)] += v;
                all[static_cast<std::size_t> (p)] += v * 0.3f;
            }
            c.frames.push_back (all);
            c.bass.push_back (bass);
        }
    }

    Chord chord (const std::string& name) { return *style_chord::parse (name); }

    /**
        0-2 s: a clear C with C in the bass. 2-4 s and 4-6 s: C, E, G and A
        (C6 or Am7) with A and C about as strong in the bass: a real question.
    */
    struct Case
    {
        analysis::Result result;
        analysis::Chroma chroma;

        Case()
        {
            addFrames (chroma, 2.0, { 0, 4, 7 }, { { 0, 1.0f } });
            addFrames (chroma, 2.0, { 0, 4, 7, 9 }, { { 9, 1.0f }, { 0, 0.95f } });
            addFrames (chroma, 2.0, { 0, 4, 7, 9 }, { { 9, 1.0f }, { 0, 0.95f } });
            result.source = "/music/song.wav";
            result.chords = { { 0.0, 2.0, chord ("C") }, { 2.0, 4.0, chord ("C") }, { 4.0, 6.0, chord ("C") } };
            analysis::Stem bass { "bass", "/tmp/bass.wav", true, {}, {} };
            bass.notes = { { 0.0, 2.0, 36, 100 }, { 2.0, 1.0, 33, 100 }, { 3.0, 1.0, 36, 100 }, { 4.0, 1.0, 33, 100 }, { 5.0, 1.0, 36, 100 } };
            result.stems.push_back (bass);
        }
    };

    int markCount (const Track& t, const std::string& text)
    {
        int n = 0;
        for (const auto& m : t.marks)
            n += m.kind == ScoreMark::Kind::chord && m.text == text;
        return n;
    }
} // namespace

TEST_CASE ("Clear chords give no question; near ties do", "[clarify]")
{
    Case c;
    const auto questions = clarify::find (c.result, c.chroma);
    REQUIRE (questions.size() == 2);
    const auto& q = questions.front();
    CHECK (q.kind == "bass");
    CHECK (q.start >= 2.0);
    CHECK (q.choices.size() == 2);
    CHECK (q.choices[0].chord == chord ("C"));
    CHECK (clarify::bassOf (q.choices[1].chord) == 9); // A in the bass
    CHECK (q.text == "Bass C or A here?");
    CHECK (q.doubt >= 0.9);
    CHECK_FALSE (q.because.empty());
    // The two places have the same two choices: one answer can do both.
    REQUIRE (q.similar.size() == 1);
    CHECK (q.similar[0] == questions[1].id);
    CHECK (clarify::similarTo (questions, q).size() == 1);
}

TEST_CASE ("A strong bass removes the doubt", "[clarify]")
{
    Case c;
    c.chroma = {};
    addFrames (c.chroma, 6.0, { 0, 4, 7 }, { { 0, 1.0f } });
    c.result.stems.clear();
    CHECK (clarify::find (c.result, c.chroma).empty());
}

TEST_CASE ("Clean audio of clear chords gives no question", "[clarify]")
{
    Sample s;
    s.sampleRate = 22050.0;
    s.channels.assign (1, {});
    for (const auto& pitches : std::vector<std::vector<int>> { { 36, 48, 60, 64, 67 }, { 33, 45, 60, 64, 69 }, { 29, 41, 60, 65, 69 } })
        for (int i = 0; i < 2 * 22050; ++i)
        {
            const double t = i / 22050.0;
            double v = 0.0;
            for (const int p : pitches)
                v += std::sin (2.0 * std::numbers::pi * 440.0 * std::pow (2.0, (p - 69) / 12.0) * t);
            s.channels[0].push_back (static_cast<float> (0.1 * v));
        }
    analysis::Result r;
    r.chords = analysis::findChords (s);
    REQUIRE (r.chords.size() == 3);
    CHECK (clarify::find (r, analysis::chroma (s)).empty());
}

TEST_CASE ("An answer corrects the chord and the bass notes, and locks the place", "[clarify]")
{
    Case c;
    const auto questions = clarify::find (c.result, c.chroma);
    REQUIRE_FALSE (questions.empty());
    const auto q = questions.front();
    const auto chosen = q.choices[1].chord;
    const auto changes = clarify::answer (c.result, q, chosen);
    CHECK (changes.chords == 1);
    CHECK (changes.bassNotes == 1); // the C at 3 s; the A was there already
    CHECK (c.result.chords[1].chord == chosen);
    CHECK (c.result.stems[0].notes[2].pitch == 33);  // C2 -> A1 (the nearest A)
    CHECK (c.result.stems[0].notes[0].pitch == 36);  // outside the place: as it was
    REQUIRE (c.result.answers.size() == 1);

    // Asked again: the answered place is not asked about.
    const auto again = clarify::find (c.result, c.chroma);
    REQUIRE (again.size() == 1);
    CHECK (again[0].id != q.id);

    // The answer is kept in the saved analysis.
    const auto read = analysis::parse (analysis::toJson (c.result));
    CHECK (read.answers == c.result.answers);

    // Keeping what is there changes nothing but the lock.
    const auto keep = clarify::answer (c.result, again[0], again[0].choices[0].chord);
    CHECK (keep.chords == 0);
    CHECK (keep.bassNotes == 0);
    CHECK (c.result.answers.size() == 2);
    CHECK (clarify::find (c.result, c.chroma).empty());
}

TEST_CASE ("An answer changes the chord symbols and the bass track in the song", "[clarify]")
{
    Case c;
    Song song;
    song.tempo = TempoMap (120.0);
    auto& audioTrack = edit::addAudioTrack (song, "Song");
    AudioClip clip;
    clip.name = "Take";
    clip.file = c.result.source;
    clip.length = kTicksPerQuarter * 16; // 8 s at 120
    const auto clipId = edit::addAudioClip (song, audioTrack.id, clip);
    analysis::ApplyOptions options;
    options.stemTracks = false;
    analysis::apply (song, clipId, c.result, options);

    const auto* bassTrack = [&]() -> const Track* {
        for (const auto& t : song.tracks)
            if (t.name == "Bass MIDI")
                return &t;
        return nullptr;
    }();
    REQUIRE (bassTrack != nullptr);
    REQUIRE (markCount (*bassTrack, "C") == 1); // C, C, C: one symbol

    const auto questions = clarify::find (c.result, c.chroma);
    REQUIRE_FALSE (questions.empty());
    const auto& q = questions.front();
    const auto [from, to] = clarify::span (song, clipId, q);
    CHECK (from == kTicksPerQuarter * 4); // 2 s at 120 BPM: beat 5
    CHECK (to == kTicksPerQuarter * 8);

    // The proposal for the A/B cards: A keeps, B changes the bass track.
    const auto p = clarify::proposal (song, clipId, q);
    CHECK (p.id == q.id);
    REQUIRE (p.options.size() == 2);
    CHECK_FALSE (p.tracks.empty());
    CHECK (arranging::isCurrent (song, p));

    const auto newName = style_chord::name (q.choices[1].chord);
    const auto changes = clarify::change (song, clipId, q, q.choices[1].chord);
    CHECK (changes.chords == 1);
    CHECK (changes.bassNotes == 1);
    const auto* t = edit::findTrack (song, bassTrack->id);
    CHECK (markCount (*t, newName) == 1);
    CHECK (markCount (*t, "C") == 2); // before and after the place
    bool movedA = false;
    for (const auto& n : t->clips.front()->notes)
        if (n.start == kTicksPerQuarter * 6)
            movedA = n.pitch == 33;
    CHECK (movedA);
}

TEST_CASE ("The Chords track that the analysis made follows the answer", "[clarify]")
{
    Case c;
    c.result.stems.clear();
    Song song;
    song.tempo = TempoMap (120.0);
    auto& audioTrack = edit::addAudioTrack (song, "Song");
    AudioClip clip;
    clip.file = c.result.source;
    clip.length = kTicksPerQuarter * 12;
    const auto clipId = edit::addAudioClip (song, audioTrack.id, clip);
    const auto done = analysis::apply (song, clipId, c.result);
    const auto* chords = edit::findTrack (song, done.chordTrack);
    REQUIRE (chords != nullptr);
    REQUIRE (chords->name == "Chords");

    clarify::Question q;
    q.start = 2.0;
    q.end = 4.0;
    q.choices = { { chord ("C"), "C", 1.0 }, { chord ("Am7"), "Am7", 1.0 } };
    clarify::change (song, clipId, q, chord ("Am7"));
    chords = edit::findTrack (song, done.chordTrack);
    bool hasA = false, cAfter = false;
    for (const auto& n : chords->clips.front()->notes)
    {
        if (n.start == kTicksPerQuarter * 4 && n.pitch == 48 + 9)
            hasA = true;
        if (n.start == kTicksPerQuarter * 8 && n.pitch == 48)
            cAfter = true;
    }
    CHECK (hasA);
    CHECK (cAfter);
}

TEST_CASE ("Listening plays the recording with a guide for a choice", "[clarify]")
{
    Case c;
    Song song;
    auto& audioTrack = edit::addAudioTrack (song, "Song");
    AudioClip clip;
    clip.file = c.result.source;
    clip.length = kTicksPerQuarter * 16;
    const auto clipId = edit::addAudioClip (song, audioTrack.id, clip);
    analysis::apply (song, clipId, c.result);
    const auto q = clarify::find (c.result, c.chroma).front();

    const auto original = clarify::listen (song, clipId, q, -1);
    CHECK (original.tracks.size() == song.tracks.size());
    for (const auto& t : original.tracks)
        if (t.output.kind != TrackOutput::Kind::audio)
            CHECK (t.mixer.mute);

    const auto b = clarify::listen (song, clipId, q, 1);
    REQUIRE (b.tracks.size() == song.tracks.size() + 1);
    const auto& guide = b.tracks.back();
    CHECK_FALSE (guide.mixer.mute);
    REQUIRE (guide.clips.size() == 1);
    bool bassA = false;
    for (const auto& n : guide.clips.front()->notes)
        bassA = bassA || n.pitch == 36 + 9;
    CHECK (bassA);
}

TEST_CASE ("MCP: the AI asks the questions, plays the choices and answers", "[clarify][mcp]")
{
    using json = nlohmann::json;
    struct Host final : mcp::Host
    {
        Document doc;
        Case data;
        ClipId clip = 0;
        int played = -9;
        const Song& song() const override { return doc.song(); }
        std::uint64_t songRevision() const override { return doc.revision(); }
        void edit (const std::string& d, const std::function<void (Song&)>& fn) override { doc.edit (d, fn); }
        TranscriptionQuestions transcriptionQuestions (ClipId) override { return { clip, clarify::find (data.result, data.chroma), "nothing to ask" }; }
        clarify::Changes answerTranscriptionQuestion (ClipId c, const clarify::Question& q, int choice, bool) override
        {
            const auto chosen = q.choices[static_cast<std::size_t> (choice)].chord;
            clarify::Changes out;
            doc.edit ("Answer", [&] (Song& s) { out = clarify::change (s, c, q, chosen); });
            clarify::answer (data.result, q, chosen);
            return out;
        }
        void playTranscriptionChoice (ClipId, const clarify::Question&, int choice) override { played = choice; }
    } host;
    host.edit ("setup", [&] (Song& s) {
        const auto t = edit::addAudioTrack (s, "Song").id;
        AudioClip a;
        a.file = host.data.result.source;
        a.length = kTicksPerQuarter * 16;
        host.clip = edit::addAudioClip (s, t, a);
        analysis::apply (s, host.clip, host.data.result);
    });
    mcp::Server server (host);
    const auto call = [&] (const std::string& tool, const json& args) -> json {
        const auto reply = json::parse (server.handle (json ({ { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "tools/call" },
                                                               { "params", { { "name", tool }, { "arguments", args } } } }).dump()));
        const auto& result = reply.at ("result");
        const auto text = result.at ("content").at (0).at ("text").get<std::string>();
        if (result.at ("isError").get<bool>())
            return { { "error", text } };
        return json::parse (text);
    };

    const auto list = call ("transcription_questions", json::object());
    REQUIRE (list.at ("questions").size() == 2);
    const auto& first = list.at ("questions")[0];
    CHECK (first.at ("text") == "Bass C or A here?");
    CHECK (first.at ("choices").size() == 2);
    CHECK (first.at ("at").at ("bar") == 2); // 2 s at 120 BPM
    const auto id = first.at ("question").get<std::string>();

    call ("play_transcription_choice", { { "question", id }, { "listen", "2" } });
    CHECK (host.played == 1);
    call ("play_transcription_choice", { { "question", id }, { "listen", "original" } });
    CHECK (host.played == -1);

    const auto done = call ("answer_transcription_question", { { "question", id }, { "choice", 2 } });
    CHECK (done.at ("bass_notes") == 1);
    CHECK (done.at ("questions_left") == 1);
    CHECK (call ("answer_transcription_question", { { "question", id }, { "choice", 2 } }).contains ("error"));
}
