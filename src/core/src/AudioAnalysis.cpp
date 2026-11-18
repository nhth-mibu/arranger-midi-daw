#include "arranger/core/AudioAnalysis.h"

#include "arranger/core/ChordFinder.h"
#include "arranger/core/GeneralMidi.h"
#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace arranger::analysis
{

using json = nlohmann::json;

const Stem* Result::stem (const std::string& name) const noexcept
{
    for (const auto& s : stems)
        if (s.name == name)
            return &s;
    return nullptr;
}

// ---- JSON ---------------------------------------------------------------------------

Result parse (const std::string& text)
{
    json j;
    try
    {
        j = json::parse (text);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error (std::string ("the analysis result is not JSON: ") + e.what());
    }
    if (! j.is_object())
        throw std::runtime_error ("the analysis result is not a JSON object");

    Result r;
    r.source = j.value ("source", std::string());
    r.language = j.value ("language", std::string());
    if (j.contains ("warnings"))
        for (const auto& w : j["warnings"])
            if (w.is_string())
                r.warnings.push_back (w.get<std::string>());

    if (j.contains ("stems"))
        for (const auto& s : j["stems"])
        {
            Stem stem;
            stem.name = s.value ("name", std::string ("mix"));
            stem.file = s.value ("file", std::string());
            if (s.contains ("notes") && s["notes"].is_array())
            {
                stem.hasNotes = true;
                for (const auto& n : s["notes"])
                {
                    audio_to_midi::Found f;
                    f.start = n.value ("start", 0.0);
                    f.length = std::max (0.0, n.value ("end", f.start) - f.start);
                    f.pitch = std::clamp (n.value ("pitch", 60), 0, 127);
                    f.velocity = std::clamp (n.value ("velocity", 100), 1, 127);
                    if (f.length > 0.0)
                        stem.notes.push_back (f);
                }
                std::sort (stem.notes.begin(), stem.notes.end(), [] (const auto& a, const auto& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); });
            }
            if (s.contains ("tags"))
                for (const auto& t : s["tags"])
                    stem.tags.push_back ({ t.value ("label", std::string()), t.value ("score", 0.0f) });
            std::stable_sort (stem.tags.begin(), stem.tags.end(), [] (const Tag& a, const Tag& b) { return a.score > b.score; });
            r.stems.push_back (std::move (stem));
        }

    if (j.contains ("chords"))
        for (const auto& c : j["chords"])
            if (const auto chord = style_chord::parse (c.value ("chord", std::string())))
                r.chords.push_back ({ c.value ("start", 0.0), c.value ("end", 0.0), *chord });

    if (j.contains ("answers"))
        for (const auto& a : j["answers"])
            if (const auto chord = style_chord::parse (a.value ("chord", std::string())))
                r.answers.push_back ({ a.value ("start", 0.0), a.value ("end", 0.0), *chord });

    if (j.contains ("words"))
        for (const auto& w : j["words"])
        {
            Word word { w.value ("start", 0.0), w.value ("end", 0.0), w.value ("text", std::string()), w.value ("probability", 1.0f) };
            if (! word.text.empty())
                r.words.push_back (std::move (word));
        }
    return r;
}

std::string toJson (const Result& r)
{
    json stems = json::array();
    for (const auto& s : r.stems)
    {
        json stem = { { "name", s.name } };
        if (! s.file.empty())
            stem["file"] = s.file;
        if (s.hasNotes)
        {
            json notes = json::array();
            for (const auto& n : s.notes)
                notes.push_back ({ { "start", n.start }, { "end", n.start + n.length }, { "pitch", n.pitch }, { "velocity", n.velocity } });
            stem["notes"] = notes;
        }
        json tags = json::array();
        for (const auto& t : s.tags)
            tags.push_back ({ { "label", t.label }, { "score", t.score } });
        stem["tags"] = tags;
        stems.push_back (stem);
    }
    json chords = json::array();
    for (const auto& c : r.chords)
        chords.push_back ({ { "start", c.start }, { "end", c.end }, { "chord", style_chord::name (c.chord) } });
    json words = json::array();
    for (const auto& w : r.words)
        words.push_back ({ { "start", w.start }, { "end", w.end }, { "text", w.text }, { "probability", w.probability } });
    json out = { { "source", r.source }, { "stems", stems }, { "chords", chords }, { "words", words }, { "language", r.language } };
    if (! r.warnings.empty())
        out["warnings"] = r.warnings;
    if (! r.answers.empty())
    {
        json answers = json::array();
        for (const auto& a : r.answers)
            answers.push_back ({ { "start", a.start }, { "end", a.end }, { "chord", style_chord::name (a.chord) } });
        out["answers"] = answers;
    }
    return out.dump (1);
}

// ---- resampling -----------------------------------------------------------------------

std::vector<float> resampleMono (const Sample& sample, double rate)
{
    const auto in = sample.mono();
    if (in.empty() || sample.sampleRate <= 0.0 || rate <= 0.0)
        return {};
    if (std::abs (sample.sampleRate - rate) < 0.5)
        return in;

    // A windowed-sinc filter that also cuts what is above the new Nyquist frequency.
    const double ratio = rate / sample.sampleRate;
    const double cutoff = std::min (1.0, ratio) * 0.95;
    constexpr int halfTaps = 16;
    const double width = halfTaps / cutoff;
    const auto outFrames = static_cast<std::size_t> (std::floor (static_cast<double> (in.size()) * ratio));
    std::vector<float> out (outFrames);
    const auto n = static_cast<std::int64_t> (in.size());
    for (std::size_t i = 0; i < outFrames; ++i)
    {
        const double centre = static_cast<double> (i) / ratio;
        const auto first = static_cast<std::int64_t> (std::ceil (centre - width));
        const auto last = static_cast<std::int64_t> (std::floor (centre + width));
        double sum = 0.0, weights = 0.0;
        for (auto k = std::max<std::int64_t> (0, first); k <= std::min (n - 1, last); ++k)
        {
            const double x = (static_cast<double> (k) - centre) * cutoff;
            const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double t = (static_cast<double> (k) - centre) / width; // -1..1
            const double window = 0.42 + 0.5 * std::cos (std::numbers::pi * t) + 0.08 * std::cos (2.0 * std::numbers::pi * t);
            const double w = sinc * window;
            sum += w * in[static_cast<std::size_t> (k)];
            weights += w;
        }
        out[i] = static_cast<float> (weights != 0.0 ? sum / weights : 0.0);
    }
    return out;
}

// ---- chroma and chords ----------------------------------------------------------------

namespace
{
    void fft (std::vector<std::complex<double>>& a)
    {
        const auto n = a.size();
        for (std::size_t i = 1, j = 0; i < n; ++i)
        {
            auto bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (a[i], a[j]);
        }
        for (std::size_t len = 2; len <= n; len <<= 1)
        {
            const double angle = -2.0 * std::numbers::pi / static_cast<double> (len);
            const std::complex<double> step (std::cos (angle), std::sin (angle));
            for (std::size_t i = 0; i < n; i += len)
            {
                std::complex<double> w (1.0, 0.0);
                for (std::size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k];
                    const auto v = a[i + k + len / 2] * w;
                    a[i + k] = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= step;
                }
            }
        }
    }

    constexpr double kChromaRate = 11025.0;
    constexpr std::size_t kFrame = 4096;
    constexpr std::size_t kHop = 1102; // about 0.1 s

    struct Template
    {
        ChordType type;
        std::array<float, 12> weights {};
        float bias = 1.0f;
    };

    std::vector<Template> templates()
    {
        const auto make = [] (ChordType type, std::initializer_list<int> tones, float bias) {
            Template t { type, {}, bias };
            float length = 0.0f;
            for (const int x : tones)
            {
                const float w = x == 0 ? 1.0f : (x == 7 ? 0.8f : (x >= 10 ? 0.7f : 1.0f));
                t.weights[static_cast<std::size_t> (x)] = w;
                length += w * w;
            }
            for (auto& w : t.weights)
                w /= std::sqrt (length);
            return t;
        };
        return { make (ChordType::major, { 0, 4, 7 }, 1.0f),
                 make (ChordType::minor, { 0, 3, 7 }, 1.0f),
                 make (ChordType::seventh, { 0, 4, 7, 10 }, 0.96f),
                 make (ChordType::major7, { 0, 4, 7, 11 }, 0.95f),
                 make (ChordType::minor7, { 0, 3, 7, 10 }, 0.96f),
                 make (ChordType::sus4, { 0, 5, 7 }, 0.93f),
                 make (ChordType::diminished, { 0, 3, 6 }, 0.9f) };
    }
} // namespace

float chordFit (const std::array<float, 12>& frame, const Chord& chord)
{
    static const auto shapes = templates();
    float length = 0.0f;
    for (const float v : frame)
        length += v * v;
    if (length <= 1e-12f)
        return 0.0f;
    length = std::sqrt (length);
    for (const auto& t : shapes)
        if (t.type == chord.type)
        {
            float dot = 0.0f;
            for (int x = 0; x < 12; ++x)
                dot += t.weights[static_cast<std::size_t> (x)] * frame[static_cast<std::size_t> (((chord.root + x) % 12 + 12) % 12)];
            return dot / length * t.bias;
        }
    return 0.0f;
}

std::vector<ChordType> findableChordTypes()
{
    std::vector<ChordType> out;
    for (const auto& t : templates())
        out.push_back (t.type);
    return out;
}

Chroma chroma (const Sample& sample)
{
    Chroma c;
    c.hopSeconds = static_cast<double> (kHop) / kChromaRate;
    const auto audio = resampleMono (sample, kChromaRate);
    if (audio.size() < kFrame / 2)
        return c;

    // For each FFT bin: its pitch class and how close it is to the middle of a semitone.
    struct Bin
    {
        std::size_t index;
        int pitchClass;
        float weight;
        bool low;
    };
    std::vector<Bin> bins;
    for (std::size_t k = 1; k < kFrame / 2; ++k)
    {
        const double f = static_cast<double> (k) * kChromaRate / static_cast<double> (kFrame);
        if (f < 40.0 || f > 2100.0)
            continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        const double off = midi - nearest; // -0.5..0.5
        const auto weight = static_cast<float> (std::pow (std::cos (std::numbers::pi * off), 2.0));
        bins.push_back ({ k, ((static_cast<int> (nearest) % 12) + 12) % 12, weight, f < 165.0 }); // low: up to E3
    }

    std::vector<double> window (kFrame);
    for (std::size_t i = 0; i < kFrame; ++i)
        window[i] = 0.5 - 0.5 * std::cos (2.0 * std::numbers::pi * static_cast<double> (i) / static_cast<double> (kFrame));

    std::vector<std::complex<double>> buffer (kFrame);
    for (std::size_t start = 0; start + kFrame / 2 < audio.size(); start += kHop)
    {
        // The frame is centred on its time.
        for (std::size_t i = 0; i < kFrame; ++i)
        {
            const auto at = static_cast<std::int64_t> (start + i) - static_cast<std::int64_t> (kFrame / 2);
            const float x = at >= 0 && static_cast<std::size_t> (at) < audio.size() ? audio[static_cast<std::size_t> (at)] : 0.0f;
            buffer[i] = { x * window[i], 0.0 };
        }
        fft (buffer);
        std::array<float, 12> all {}, low {};
        for (const auto& b : bins)
        {
            const auto m = static_cast<float> (std::abs (buffer[b.index])) * b.weight;
            all[static_cast<std::size_t> (b.pitchClass)] += m;
            if (b.low)
                low[static_cast<std::size_t> (b.pitchClass)] += m;
        }
        c.frames.push_back (all);
        c.bass.push_back (low);
    }
    return c;
}

std::vector<TimedChord> findChords (const Sample& sample, double minSeconds)
{
    const auto c = chroma (sample);
    const auto frames = c.frames.size();
    if (frames == 0)
        return {};

    // Quiet frames are "no chord".
    std::vector<float> energy (frames);
    float loudest = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
    {
        for (const float v : c.frames[i])
            energy[i] += v;
        loudest = std::max (loudest, energy[i]);
    }
    if (loudest <= 0.0f)
        return {};

    const auto shapes = templates();
    const std::size_t states = shapes.size() * 12 + 1; // the last: no chord
    const std::size_t none = states - 1;
    constexpr float kChange = 0.45f; // the cost of changing the chord: a new chord must win for a while

    // Viterbi: the best chord path through the frames.
    std::vector<float> score (states, 0.0f), next (states);
    std::vector<std::vector<std::uint16_t>> from (frames, std::vector<std::uint16_t> (states));
    std::vector<float> emit (states);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const bool quiet = energy[i] < loudest * 0.02f;
        float length = 0.0f;
        for (const float v : c.frames[i])
            length += v * v;
        length = std::sqrt (std::max (length, 1e-12f));
        for (std::size_t t = 0; t < shapes.size(); ++t)
            for (int root = 0; root < 12; ++root)
            {
                float dot = 0.0f;
                for (int x = 0; x < 12; ++x)
                    dot += shapes[t].weights[static_cast<std::size_t> (x)] * c.frames[i][static_cast<std::size_t> ((root + x) % 12)];
                emit[t * 12 + static_cast<std::size_t> (root)] = quiet ? 0.0f : dot / length * shapes[t].bias;
            }
        emit[none] = quiet ? 1.0f : 0.3f;

        if (i == 0)
        {
            score = emit;
            continue;
        }
        std::size_t best = 0;
        for (std::size_t s = 1; s < states; ++s)
            if (score[s] > score[best])
                best = s;
        for (std::size_t s = 0; s < states; ++s)
        {
            const float stay = score[s];
            const float change = score[best] - kChange;
            if (stay >= change)
            {
                next[s] = stay + emit[s];
                from[i][s] = static_cast<std::uint16_t> (s);
            }
            else
            {
                next[s] = change + emit[s];
                from[i][s] = static_cast<std::uint16_t> (best);
            }
        }
        std::swap (score, next);
    }
    std::vector<std::size_t> path (frames);
    path.back() = static_cast<std::size_t> (std::max_element (score.begin(), score.end()) - score.begin());
    for (std::size_t i = frames - 1; i > 0; --i)
        path[i - 1] = from[i][path[i]];

    // Runs of the same chord.
    struct Run
    {
        std::size_t state, first, last; // frames first..last-1
    };
    std::vector<Run> runs;
    for (std::size_t i = 0; i < frames; ++i)
        if (runs.empty() || runs.back().state != path[i])
            runs.push_back ({ path[i], i, i + 1 });
        else
            runs.back().last = i + 1;

    // Too short: give it to the run before (or after, at the start).
    const auto minFrames = static_cast<std::size_t> (std::max (1.0, minSeconds / c.hopSeconds));
    for (bool changed = true; changed && runs.size() > 1;)
    {
        changed = false;
        for (std::size_t i = 0; i < runs.size(); ++i)
            if (runs[i].last - runs[i].first < minFrames)
            {
                if (i > 0)
                    runs[i - 1].last = runs[i].last;
                else
                    runs[1].first = runs[0].first;
                runs.erase (runs.begin() + static_cast<std::ptrdiff_t> (i));
                // Neighbours with the same chord become one.
                for (std::size_t k = 1; k < runs.size();)
                    if (runs[k].state == runs[k - 1].state)
                    {
                        runs[k - 1].last = runs[k].last;
                        runs.erase (runs.begin() + static_cast<std::ptrdiff_t> (k));
                    }
                    else
                        ++k;
                changed = true;
                break;
            }
    }

    std::vector<TimedChord> out;
    for (const auto& r : runs)
    {
        if (r.state == none)
            continue;
        Chord chord;
        chord.root = static_cast<int> (r.state % 12);
        chord.type = shapes[r.state / 12].type;

        // A bass note that is a chord note but not the root, and clearly the strongest low note: C/E.
        std::array<float, 12> low {};
        for (std::size_t i = r.first; i < r.last; ++i)
            for (std::size_t k = 0; k < 12; ++k)
                low[k] += c.bass[i][k];
        const auto strongest = static_cast<int> (std::max_element (low.begin(), low.end()) - low.begin());
        if (strongest != chord.root && low[static_cast<std::size_t> (strongest)] > 1.5f * low[static_cast<std::size_t> (chord.root)])
        {
            const auto tones = style_chord::tones (chord.type);
            if (std::find (tones.begin(), tones.end(), ((strongest - chord.root) % 12 + 12) % 12) != tones.end())
                chord.bass = strongest;
        }
        out.push_back ({ static_cast<double> (r.first) * c.hopSeconds, static_cast<double> (r.last) * c.hopSeconds, chord });
    }
    return out;
}

Sample harmonicMix (const std::vector<const Sample*>& stems)
{
    Sample mix;
    for (const auto* s : stems)
    {
        if (s == nullptr || s->frames() == 0)
            continue;
        const auto mono = s->mono();
        if (mix.channels.empty())
        {
            mix.sampleRate = s->sampleRate;
            mix.channels.push_back (mono);
            continue;
        }
        auto& out = mix.channels.front();
        if (std::abs (s->sampleRate - mix.sampleRate) > 0.5)
            continue; // the helper writes all stems at one rate
        if (out.size() < mono.size())
            out.resize (mono.size(), 0.0f);
        for (std::size_t i = 0; i < mono.size(); ++i)
            out[i] += mono[i];
    }
    return mix;
}

// ---- words ----------------------------------------------------------------------------

std::vector<std::string> lines (const std::vector<Word>& words, double gapSeconds)
{
    std::vector<std::string> out;
    double lastEnd = -1e9;
    for (const auto& w : words)
    {
        if (out.empty() || w.start - lastEnd >= gapSeconds)
            out.emplace_back();
        auto& line = out.back();
        if (! line.empty())
            line += ' ';
        line += w.text;
        lastEnd = w.end;
    }
    return out;
}

// ---- instruments ------------------------------------------------------------------------

namespace
{
    std::string lower (std::string s)
    {
        for (auto& ch : s)
            ch = static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
        return s;
    }

    struct LabelProgram
    {
        const char* label;
        int program;
    };

    // AudioSet labels (as PANNs and YAMNet name them) to General MIDI programs (0-based).
    constexpr LabelProgram kLabels[] = {
        { "piano", 0 }, { "electric piano", 4 }, { "keyboard (musical)", 0 }, { "harpsichord", 6 },
        { "organ", 16 }, { "electronic organ", 16 }, { "hammond organ", 16 }, { "accordion", 21 }, { "harmonica", 22 },
        { "synthesizer", 81 }, { "sampler", 81 },
        { "acoustic guitar", 25 }, { "steel guitar, slide guitar", 25 }, { "electric guitar", 27 }, { "guitar", 25 },
        { "plucked string instrument", 25 }, { "banjo", 105 }, { "mandolin", 25 }, { "sitar", 104 }, { "ukulele", 24 },
        { "bass guitar", 33 }, { "double bass", 32 }, { "electronic bass", 38 },
        { "violin, fiddle", 40 }, { "pizzicato", 45 }, { "cello", 42 }, { "string section", 48 },
        { "bowed string instrument", 48 }, { "orchestra", 48 }, { "harp", 46 },
        { "trumpet", 56 }, { "trombone", 57 }, { "french horn", 60 }, { "brass instrument", 61 },
        { "saxophone", 65 }, { "alto saxophone", 65 }, { "tenor saxophone", 66 }, { "clarinet", 71 },
        { "flute", 73 }, { "wind instrument, woodwind instrument", 73 }, { "bagpipes", 109 },
        { "marimba, xylophone", 12 }, { "vibraphone", 11 }, { "glockenspiel", 9 }, { "steelpan", 114 }, { "bell", 14 },
        { "choir", 52 }, { "singing", 53 }, { "male singing", 53 }, { "female singing", 53 }, { "child singing", 53 },
        { "a capella", 52 }, { "vocal music", 53 }, { "humming", 53 }, { "whistling", 78 },
    };

    bool isVoice (int program) { return program == 52 || program == 53; }
    bool isBass (int program) { return program >= 32 && program <= 39; }
} // namespace

int programForLabel (const std::string& label)
{
    const auto l = lower (label);
    for (const auto& lp : kLabels)
        if (l == lp.label)
            return lp.program;
    return -1;
}

Instrument instrumentFor (const Stem& stem)
{
    const auto name = lower (stem.name);
    if (name == "drums")
        return { 0, true, "Drums" };

    // The best tag that names an instrument that fits the stem.
    for (const auto& t : stem.tags)
    {
        if (t.score < 0.03f)
            break;
        const int p = programForLabel (t.label);
        if (p < 0)
            continue;
        if (name == "vocals" && ! isVoice (p))
            continue;
        if (name == "bass" && ! isBass (p))
            continue;
        if ((name == "other" || name == "guitar" || name == "piano") && (isVoice (p) || isBass (p)))
            continue;
        return { p, false, t.label };
    }
    if (name == "vocals")
        return { 53, false, "Vocals" };
    if (name == "bass")
        return { 33, false, "Bass" };
    if (name == "guitar")
        return { 27, false, "Guitar" };
    if (name == "piano")
        return { 0, false, "Piano" };
    if (name == "other")
        return { 0, false, "Other" };
    return { 0, false, stem.name.empty() ? std::string ("Music") : stem.name };
}

// ---- into the song --------------------------------------------------------------------

Tick tickInSong (const Song& song, const AudioClip& audio, double fileSeconds)
{
    const double speed = edit::audioSpeed (song, audio);
    const double startSeconds = song.tempo.secondsAt (static_cast<double> (audio.start));
    return static_cast<Tick> (std::llround (song.tempo.tickAtSeconds (startSeconds + (fileSeconds - audio.offset) / speed)));
}

std::vector<ScoreMark> placeWords (Clip& clip, const std::vector<std::pair<Tick, Word>>& words, Tick slack, int fallbackPitch)
{
    std::vector<ScoreMark> marks;
    std::vector<bool> taken (clip.notes.size(), false);
    std::vector<Note> added;
    std::size_t first = 0; // notes before this are behind us
    for (const auto& [at, word] : words)
    {
        if (at < 0 || at >= clip.length)
            continue;
        std::optional<std::size_t> found;
        for (std::size_t i = first; i < clip.notes.size(); ++i)
        {
            const auto& n = clip.notes[i];
            if (n.start > at + slack)
                break;
            if (! taken[i] && n.start >= at - slack)
            {
                found = i;
                break;
            }
        }
        Tick place = at;
        if (found)
        {
            taken[*found] = true;
            place = clip.notes[*found].start;
            first = *found + 1;
            // Chords in a vocal line: other notes at the same start can't take a word.
            while (first < clip.notes.size() && clip.notes[first].start == place)
                taken[first++] = true;
        }
        else
        {
            Note n;
            n.start = at;
            n.length = std::max<Tick> (kTicksPerQuarter / 4, std::min<Tick> (kTicksPerQuarter, clip.length - at));
            n.pitch = static_cast<std::uint8_t> (std::clamp (fallbackPitch, 0, 127));
            n.velocity = 80;
            added.push_back (n);
        }
        if (! marks.empty() && marks.back().at == place)
            marks.back().text += " " + word.text; // two words on one note
        else
            marks.push_back ({ ScoreMark::Kind::lyric, place, word.text });
    }
    if (! added.empty())
    {
        clip.notes.insert (clip.notes.end(), added.begin(), added.end());
        edit::normalise (clip);
    }
    return marks;
}

namespace
{
    std::string capitalised (std::string s)
    {
        if (! s.empty())
            s[0] = static_cast<char> (std::toupper (static_cast<unsigned char> (s[0])));
        return s;
    }

    /** The order the tracks come in: the voice first, as in a score. */
    int stemOrder (const std::string& name)
    {
        static const char* order[] = { "vocals", "guitar", "piano", "other", "bass", "drums" };
        for (int i = 0; i < 6; ++i)
            if (name == order[i])
                return i;
        return 6;
    }
} // namespace

Applied apply (Song& song, ClipId sourceId, const Result& result, const ApplyOptions& options)
{
    const auto source = edit::findAudioClip (song, sourceId);
    const auto sourceTrack = edit::trackOfClip (song, sourceId);
    if (source == nullptr || sourceTrack == 0)
        throw std::runtime_error ("the audio clip is not in the song any more");
    const AudioClip clip = *source;

    Applied applied;
    int after = edit::trackIndex (song, sourceTrack);
    const auto clipName = clip.name.empty() ? std::string ("Audio") : clip.name;

    std::vector<const Stem*> stems;
    for (const auto& s : result.stems)
        stems.push_back (&s);
    std::stable_sort (stems.begin(), stems.end(), [] (const Stem* a, const Stem* b) { return stemOrder (a->name) < stemOrder (b->name); });

    // Audio tracks: one per stem.
    if (options.stemTracks)
        for (const auto* s : stems)
        {
            if (s->file.empty() || s->name == "mix")
                continue;
            auto& t = edit::addAudioTrack (song, clipName + " " + capitalised (s->name), after++);
            AudioClip part = clip;
            part.id = 0;
            part.file = s->file;
            part.name = clipName + " " + capitalised (s->name);
            part.layer = 0;
            edit::addAudioClip (song, t.id, part);
            applied.audioTracks.push_back (t.id);
        }
    if (options.muteSource && ! applied.audioTracks.empty())
        if (auto* t = edit::findTrack (song, sourceTrack))
            t->mixer.mute = true;

    // MIDI tracks: one per stem with notes.
    TrackId vocalTrack = 0;
    if (options.noteTracks)
        for (const auto* s : stems)
        {
            if (! s->hasNotes || s->notes.empty())
                continue;
            const auto instrument = instrumentFor (*s);
            auto c = audio_to_midi::toClip (s->notes, song, clip);
            if (c.notes.empty())
                continue;
            const auto name = s->name == "mix" ? clipName + " MIDI" : capitalised (s->name) + " MIDI";
            auto& t = edit::addTrack (song, name, after++);
            if (instrument.drums)
            {
                t.output.channel = 10;
                t.sound = { "Drums", -1, -1, 0 };
            }
            else
            {
                t.sound.program = instrument.program;
                t.sound.name = kGeneralMidiNames[instrument.program];
            }
            const auto id = t.id;
            c.name = name;
            edit::addClip (song, id, c);
            applied.midiTracks.push_back (id);
            if (s->name == "vocals")
                vocalTrack = id;
        }

    // Lyrics: on the vocal MIDI track (a new one when there is none).
    if (options.lyrics && ! result.words.empty())
    {
        if (vocalTrack == 0)
        {
            auto& t = edit::addTrack (song, "Vocals MIDI", after++);
            t.sound.program = 53;
            t.sound.name = kGeneralMidiNames[53];
            vocalTrack = t.id;
            Clip empty;
            empty.name = "Vocals";
            empty.start = clip.start;
            empty.length = clip.length;
            edit::addClip (song, vocalTrack, empty);
            applied.midiTracks.push_back (vocalTrack);
        }
        auto* t = edit::findTrack (song, vocalTrack);
        if (t != nullptr && ! t->clips.empty())
        {
            Clip c = *t->clips.front();
            std::vector<std::pair<Tick, Word>> placed;
            for (const auto& w : result.words)
                placed.push_back ({ tickInSong (song, clip, w.start) - c.start, w });
            const auto marks = placeWords (c, placed, kTicksPerQuarter / 4);
            edit::modifyClip (song, c.id, [&] (Clip& target) { target = c; });
            for (auto m : marks)
            {
                m.at += c.start;
                edit::setMark (song, vocalTrack, m);
            }
            applied.words = static_cast<int> (marks.size());
            applied.lyricsTrack = vocalTrack;
        }
    }

    // Chord symbols: on the first new MIDI track that is not drums; a "Chords" track that plays them when there is none.
    if (options.chords && ! result.chords.empty())
    {
        TrackId target = 0;
        for (const auto id : applied.midiTracks)
            if (const auto* t = edit::findTrack (song, id); t != nullptr && t->output.channel != 10)
            {
                target = id;
                break;
            }
        const auto beat = song.timeSignature.ticksPerBeat();
        std::vector<ScoreMark> marks;
        std::vector<std::pair<Tick, Chord>> spans;
        for (const auto& c : result.chords)
        {
            Tick at = tickInSong (song, clip, c.start);
            at = (at + beat / 2) / beat * beat; // on the nearest beat
            if (at < clip.start || at >= clip.end())
                continue;
            const auto name = style_chord::name (c.chord);
            if (! marks.empty() && marks.back().at == at)
                marks.pop_back(); // two chords on one beat: the later one stays
            if (! marks.empty() && marks.back().text == name)
                continue;
            marks.push_back ({ ScoreMark::Kind::chord, at, name });
        }
        if (! marks.empty())
        {
            if (target == 0)
            {
                auto& t = edit::addTrack (song, "Chords", after++);
                t.sound.program = 48;
                t.sound.name = kGeneralMidiNames[48];
                target = t.id;
                Clip c;
                c.name = "Chords";
                c.start = clip.start;
                c.length = clip.length;
                for (std::size_t i = 0; i < marks.size(); ++i)
                {
                    const auto chord = style_chord::parse (marks[i].text);
                    if (! chord)
                        continue;
                    const Tick from = marks[i].at - clip.start;
                    const Tick to = (i + 1 < marks.size() ? marks[i + 1].at : clip.end()) - clip.start;
                    for (const int tone : style_chord::tones (chord->type))
                        c.notes.push_back ({ from, std::max<Tick> (1, to - from), static_cast<std::uint8_t> (48 + chord->root + tone), 70 });
                    if (chord->bass >= 0)
                        c.notes.push_back ({ from, std::max<Tick> (1, to - from), static_cast<std::uint8_t> (36 + chord->bass), 70 });
                }
                edit::normalise (c);
                edit::addClip (song, target, c);
                applied.midiTracks.push_back (target);
            }
            if (auto* t = edit::findTrack (song, target))
                chords::replaceChords (*t, clip.start, clip.end(), marks);
            applied.chordTrack = target;
            applied.chords = static_cast<int> (marks.size());
        }
    }
    return applied;
}

} // namespace arranger::analysis
