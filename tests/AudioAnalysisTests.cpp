#include "arranger/core/AudioAnalysis.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace arranger;

namespace
{
    /** Chords played as tones with a few overtones, `seconds` each. */
    Sample chordAudio (const std::vector<std::vector<int>>& chords, double seconds, double rate = 44100.0)
    {
        Sample s;
        s.sampleRate = rate;
        s.channels.assign (2, {});
        const auto per = static_cast<std::size_t> (seconds * rate);
        for (const auto& pitches : chords)
            for (std::size_t i = 0; i < per; ++i)
            {
                const double t = static_cast<double> (i) / rate;
                double v = 0.0;
                for (const int p : pitches)
                {
                    const double f = 440.0 * std::pow (2.0, (p - 69) / 12.0);
                    for (int h = 1; h <= 4; ++h)
                        v += std::sin (2.0 * std::numbers::pi * f * h * t) / (h * h);
                }
                const auto x = static_cast<float> (0.1 * v);
                s.channels[0].push_back (x);
                s.channels[1].push_back (x);
            }
        return s;
    }

    std::vector<std::string> names (const std::vector<analysis::TimedChord>& chords)
    {
        std::vector<std::string> out;
        for (const auto& c : chords)
            out.push_back (style_chord::name (c.chord));
        return out;
    }
} // namespace

TEST_CASE ("Chords are found in audio", "[analysis]")
{
    // C, Am, F, G7: two seconds each.
    const auto audio = chordAudio ({ { 48, 60, 64, 67 }, { 45, 60, 64, 69 }, { 41, 60, 65, 69 }, { 43, 59, 62, 65, 67 } }, 2.0);
    const auto found = analysis::findChords (audio);
    REQUIRE (names (found) == std::vector<std::string> { "C", "Am", "F", "G7" });
    CHECK (found[0].start == Catch::Approx (0.0).margin (0.3));
    CHECK (found[1].start == Catch::Approx (2.0).margin (0.3));
    CHECK (found[3].start == Catch::Approx (6.0).margin (0.3));
}

TEST_CASE ("A chord over another bass note gets a slash", "[analysis]")
{
    // C major with E in the bass, loud.
    const auto audio = chordAudio ({ { 40, 40, 60, 64, 67 } }, 2.0);
    const auto found = analysis::findChords (audio);
    REQUIRE (found.size() == 1);
    CHECK (style_chord::name (found[0].chord) == "C/E");
}

TEST_CASE ("Silence has no chords", "[analysis]")
{
    Sample s;
    s.sampleRate = 44100.0;
    s.channels.assign (1, std::vector<float> (44100 * 2, 0.0f));
    CHECK (analysis::findChords (s).empty());
}

TEST_CASE ("Resampling keeps a tone and its length", "[analysis]")
{
    const auto audio = chordAudio ({ { 69 } }, 1.0, 48000.0);
    const auto out = analysis::resampleMono (audio, 16000.0);
    CHECK (out.size() == 16000);
    double energy = 0.0;
    for (const float v : out)
        energy += v * v;
    CHECK (energy / static_cast<double> (out.size()) > 0.001);
}

TEST_CASE ("The helper's JSON is read and written", "[analysis]")
{
    const std::string text = R"({
        "source": "/music/song.wav", "language": "en",
        "stems": [
            { "name": "vocals", "file": "/tmp/vocals.wav", "notes": [ { "start": 1.0, "end": 1.5, "pitch": 67, "velocity": 90 } ],
              "tags": [ { "label": "Music", "score": 0.9 }, { "label": "Female singing", "score": 0.6 } ] },
            { "name": "drums", "file": "/tmp/drums.wav" }
        ],
        "chords": [ { "start": 0.0, "end": 2.0, "chord": "Am7" } ],
        "words": [ { "start": 1.0, "end": 1.4, "text": "Hello", "probability": 0.8 } ],
        "warnings": [ "PANNs is not installed" ]
    })";
    const auto r = analysis::parse (text);
    REQUIRE (r.stems.size() == 2);
    CHECK (r.stems[0].hasNotes);
    CHECK (r.stems[0].notes.size() == 1);
    CHECK (r.stems[0].notes[0].length == Catch::Approx (0.5));
    CHECK (r.stems[0].tags[0].label == "Music");
    CHECK_FALSE (r.stems[1].hasNotes);
    CHECK (style_chord::name (r.chords.at (0).chord) == "Am7");
    CHECK (r.words.at (0).text == "Hello");
    CHECK (r.warnings.size() == 1);

    const auto again = analysis::parse (analysis::toJson (r));
    CHECK (again.stems.size() == 2);
    CHECK (again.stems[0].notes == r.stems[0].notes);
    CHECK (again.words == r.words);
    CHECK (again.chords == r.chords);

    CHECK_THROWS (analysis::parse ("not json"));
}

TEST_CASE ("Stems get a fitting General MIDI sound", "[analysis]")
{
    analysis::Stem other { "other", "", false, {}, { { "Music", 0.9f }, { "Female singing", 0.5f }, { "Electric guitar", 0.3f } } };
    CHECK (analysis::instrumentFor (other).program == 27); // the voice belongs to the vocal stem
    analysis::Stem bass { "bass", "", false, {}, { { "Piano", 0.4f }, { "Double bass", 0.2f } } };
    CHECK (analysis::instrumentFor (bass).program == 32);
    analysis::Stem bare { "bass", "", false, {}, {} };
    CHECK (analysis::instrumentFor (bare).program == 33);
    CHECK (analysis::instrumentFor ({ "drums", "", false, {}, {} }).drums);
    CHECK (analysis::programForLabel ("Saxophone") == 65);
    CHECK (analysis::programForLabel ("Speech") == -1);
}

TEST_CASE ("Words go on the notes near them", "[analysis]")
{
    Clip clip;
    clip.length = kTicksPerQuarter * 8;
    clip.notes = { { 0, 240, 60, 90 }, { 480, 240, 62, 90 }, { 960, 240, 64, 90 } };
    const auto w = [] (const char* text) { return analysis::Word { 0, 0, text, 1.0f }; };
    const auto marks = analysis::placeWords (clip, { { 10, w ("Hel-") }, { 470, w ("lo") }, { 2000, w ("world") } }, 120);
    REQUIRE (marks.size() == 3);
    CHECK (marks[0].at == 0);
    CHECK (marks[1].at == 480);
    CHECK (marks[1].text == "lo");
    CHECK (marks[2].at == 2000); // no note near it: it got one
    CHECK (clip.notes.size() == 4);
}

TEST_CASE ("What was found becomes tracks", "[analysis]")
{
    Song song;
    auto& audioTrack = edit::addAudioTrack (song, "Song");
    AudioClip clip;
    clip.name = "Take";
    clip.file = "/music/song.wav";
    clip.start = kTicksPerQuarter * 4; // bar 2
    clip.length = kTicksPerQuarter * 16;
    const auto clipId = edit::addAudioClip (song, audioTrack.id, clip);
    const auto audioTrackId = audioTrack.id;

    analysis::Result r;
    r.stems.push_back ({ "drums", "/tmp/drums.wav", true, { { 0.0, 0.1, 36, 100 } }, {} });
    r.stems.push_back ({ "vocals", "/tmp/vocals.wav", true, { { 0.5, 0.4, 67, 90 }, { 1.0, 0.4, 69, 90 } }, {} });
    r.stems.push_back ({ "bass", "/tmp/bass.wav", true, { { 0.0, 1.0, 36, 90 } }, { { "Bass guitar", 0.5f } } });
    r.words = { { 0.5, 0.9, "Hel-", 1.0f }, { 1.0, 1.4, "lo", 1.0f } };
    r.chords = { { 0.0, 2.0, { 0, ChordType::major, -1 } }, { 2.0, 4.0, { 9, ChordType::minor, -1 } } };

    const auto done = analysis::apply (song, clipId, r);
    CHECK (done.audioTracks.size() == 3);
    CHECK (done.midiTracks.size() == 3);
    CHECK (done.words == 2);
    CHECK (done.chords == 2);
    CHECK (edit::findTrack (song, audioTrackId)->mixer.mute);

    // Vocals first, drums last; the stems keep the clip's place.
    CHECK (song.tracks[1].name == "Take Vocals");
    CHECK (song.tracks[1].audio.at (0)->file == "/tmp/vocals.wav");
    CHECK (song.tracks[1].audio.at (0)->start == clip.start);
    const auto* vocals = edit::findTrack (song, done.lyricsTrack);
    REQUIRE (vocals != nullptr);
    CHECK (vocals->name == "Vocals MIDI");
    CHECK (done.chordTrack == done.lyricsTrack);
    int lyrics = 0, chords = 0;
    for (const auto& m : vocals->marks)
    {
        lyrics += m.kind == ScoreMark::Kind::lyric;
        chords += m.kind == ScoreMark::Kind::chord;
    }
    CHECK (lyrics == 2);
    CHECK (chords == 2);
    CHECK (vocals->marks.front().at >= clip.start);
    const auto* drums = edit::findTrack (song, done.midiTracks.back());
    CHECK (drums->output.channel == 10);
    const auto* bass = edit::findTrack (song, done.midiTracks[1]);
    CHECK (bass->sound.program == 33);
}

TEST_CASE ("Chords alone make a Chords track that plays them", "[analysis]")
{
    Song song;
    auto& audioTrack = edit::addAudioTrack (song, "Song");
    AudioClip clip;
    clip.file = "/music/song.wav";
    clip.length = kTicksPerQuarter * 16;
    const auto clipId = edit::addAudioClip (song, audioTrack.id, clip);
    const auto audioTrackId = audioTrack.id;
    analysis::Result r;
    r.chords = { { 0.0, 2.0, { 7, ChordType::seventh, -1 } } };
    const auto done = analysis::apply (song, clipId, r);
    const auto* t = edit::findTrack (song, done.chordTrack);
    REQUIRE (t != nullptr);
    CHECK (t->name == "Chords");
    CHECK (t->clips.at (0)->notes.size() == 4);
    CHECK_FALSE (edit::findTrack (song, audioTrackId)->mixer.mute); // no stems: the original still plays
}

TEST_CASE ("Words make lines at pauses", "[analysis]")
{
    const std::vector<analysis::Word> words { { 0.0, 0.3, "one", 1 }, { 0.4, 0.6, "two", 1 }, { 3.0, 3.2, "three", 1 } };
    CHECK (analysis::lines (words) == std::vector<std::string> { "one two", "three" });
}
