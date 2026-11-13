#include "arranger/core/AudioToMidi.h"

#include "arranger/core/SongEdit.h"
#include "arranger/core/Transients.h"

#include <algorithm>
#include <cmath>

namespace arranger::audio_to_midi
{

namespace
{
    constexpr int kUnvoiced = -1;

    /** Averages groups of `factor` frames: a cheap low-pass and a lower sample rate. */
    std::vector<float> decimate (const std::vector<float>& in, int factor)
    {
        if (factor <= 1)
            return in;
        std::vector<float> out (in.size() / static_cast<std::size_t> (factor));
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < factor; ++k)
                sum += in[i * static_cast<std::size_t> (factor) + static_cast<std::size_t> (k)];
            out[i] = sum / static_cast<float> (factor);
        }
        return out;
    }

    int velocityOf (double rms, double loudest)
    {
        if (loudest <= 0.0)
            return 100;
        // 40 dB of level over the velocity range 30..127.
        const double db = 20.0 * std::log10 (std::max (1.0e-6, rms / loudest));
        return std::clamp (static_cast<int> (std::lround (127.0 + db * (97.0 / 40.0))), 30, 127);
    }
}

std::vector<Found> melody (const Sample& sample, const MelodySettings& settings)
{
    std::vector<Found> found;
    if (sample.frames() == 0 || sample.sampleRate <= 0.0)
        return found;

    // About 16 kHz is enough for the pitch of a melody and makes the search fast.
    const int factor = std::max (1, static_cast<int> (std::floor (sample.sampleRate / 16000.0)));
    const auto x = decimate (sample.mono(), factor);
    const double rate = sample.sampleRate / factor;
    const double lowHz = 440.0 * std::pow (2.0, (settings.lowestPitch - 69) / 12.0);
    const double highHz = 440.0 * std::pow (2.0, (settings.highestPitch - 69) / 12.0);
    const auto tauMax = static_cast<std::size_t> (rate / lowHz) + 2;
    const auto tauMin = std::max<std::size_t> (2, static_cast<std::size_t> (rate / highHz));
    const std::size_t window = tauMax * 2;
    const auto hop = std::max<std::size_t> (1, static_cast<std::size_t> (rate * 0.01));
    if (x.size() < window + tauMax + 1)
        return found;

    // One pitch (or "no note") and one level every 10 ms.
    std::vector<int> pitches;
    std::vector<double> levels;
    std::vector<double> d (tauMax + 1);
    for (std::size_t at = 0; at + window + tauMax < x.size(); at += hop)
    {
        double energy = 0.0;
        for (std::size_t i = 0; i < window; ++i)
            energy += static_cast<double> (x[at + i]) * x[at + i];
        const double rms = std::sqrt (energy / static_cast<double> (window));
        levels.push_back (rms);
        int pitch = kUnvoiced;
        if (rms > 0.003)
        {
            // YIN: the difference of the wave with itself moved by tau, then normalised by its mean so far.
            double running = 0.0;
            d[0] = 1.0;
            std::size_t pick = 0;
            for (std::size_t tau = 1; tau <= tauMax; ++tau)
            {
                double sum = 0.0;
                for (std::size_t i = 0; i < window; ++i)
                {
                    const double diff = static_cast<double> (x[at + i]) - x[at + i + tau];
                    sum += diff * diff;
                }
                running += sum;
                d[tau] = running > 0.0 ? sum * static_cast<double> (tau) / running : 1.0;
            }
            for (std::size_t tau = tauMin; tau < tauMax; ++tau)
                if (d[tau] < settings.clarity)
                {
                    while (tau + 1 < tauMax && d[tau + 1] < d[tau])
                        ++tau; // down to the bottom of this dip
                    pick = tau;
                    break;
                }
            if (pick > 0)
            {
                // A parabola through the three points around the dip gives a part of a sample.
                const double a = d[pick - 1], b = d[pick], c = d[pick + 1];
                const double denominator = a - 2.0 * b + c;
                const double shift = std::abs (denominator) > 1.0e-12 ? 0.5 * (a - c) / denominator : 0.0;
                const double hz = rate / (static_cast<double> (pick) + std::clamp (shift, -1.0, 1.0));
                const int midi = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
                if (midi >= settings.lowestPitch && midi <= settings.highestPitch)
                    pitch = midi;
            }
        }
        pitches.push_back (pitch);
    }

    // A median of five frames removes single wrong frames.
    auto smooth = pitches;
    for (std::size_t i = 2; i + 2 < pitches.size(); ++i)
    {
        int w[5] = { pitches[i - 2], pitches[i - 1], pitches[i], pitches[i + 1], pitches[i + 2] };
        std::sort (std::begin (w), std::end (w));
        smooth[i] = w[2];
    }

    const double frameSeconds = static_cast<double> (hop) / rate;
    const double loudest = levels.empty() ? 0.0 : *std::max_element (levels.begin(), levels.end());
    for (std::size_t i = 0; i < smooth.size();)
    {
        std::size_t j = i;
        double level = 0.0;
        while (j < smooth.size() && smooth[j] == smooth[i])
            level = std::max (level, levels[j++]);
        // Each run of one pitch is one note. A very quiet run is the tail of a room, not a note.
        if (smooth[i] != kUnvoiced && static_cast<double> (j - i) * frameSeconds >= settings.minNoteSeconds && level > loudest * 0.03)
        {
            Found n;
            n.start = static_cast<double> (i) * frameSeconds;
            n.length = static_cast<double> (j - i) * frameSeconds;
            n.pitch = smooth[i];
            n.velocity = velocityOf (level, loudest);
            // The same pitch again after a very short hole is the same note.
            if (! found.empty() && found.back().pitch == n.pitch && n.start - (found.back().start + found.back().length) < 0.04)
                found.back().length = n.start + n.length - found.back().start;
            else
                found.push_back (n);
        }
        i = j;
    }
    return found;
}

std::vector<Found> drums (const Sample& sample, const DrumSettings& settings)
{
    std::vector<Found> found;
    if (sample.frames() == 0 || sample.sampleRate <= 0.0)
        return found;
    const auto mono = sample.mono();
    TransientSettings ts;
    ts.sensitivity = settings.sensitivity;
    const auto hits = findTransients (mono, sample.sampleRate, ts);
    const auto span = static_cast<std::size_t> (sample.sampleRate * 0.05);
    for (std::size_t h = 0; h < hits.size(); ++h)
    {
        const auto from = hits[h].frame;
        const auto to = std::min ({ mono.size(), from + span, h + 1 < hits.size() ? hits[h + 1].frame : mono.size() });
        if (to <= from + 8)
            continue;
        // How bright the hit is: the level of the wave's changes against the level of the wave.
        // For a sine this gives its frequency; for a mix, a kind of centre.
        double energy = 1.0e-12, change = 0.0;
        for (std::size_t i = from + 1; i < to; ++i)
        {
            energy += static_cast<double> (mono[i]) * mono[i];
            const double diff = static_cast<double> (mono[i]) - mono[i - 1];
            change += diff * diff;
        }
        const double ratio = std::min (1.0, 0.5 * std::sqrt (change / energy));
        const double hz = std::asin (ratio) * sample.sampleRate / 3.141592653589793;

        Found n;
        n.start = static_cast<double> (from) / sample.sampleRate;
        n.length = 0.1;
        n.pitch = hz < 400.0 ? settings.kick : (hz < 3500.0 ? settings.snare : settings.hat);
        n.velocity = std::clamp (static_cast<int> (std::lround (40.0 + 87.0 * std::sqrt (static_cast<double> (hits[h].strength)))), 1, 127);
        found.push_back (n);
    }
    return found;
}

Clip toClip (const std::vector<Found>& notes, const Song& song, const AudioClip& audio)
{
    Clip clip;
    clip.name = audio.name;
    clip.start = audio.start;
    clip.length = audio.length;
    const double speed = edit::audioSpeed (song, audio);
    const double startSeconds = song.tempo.secondsAt (static_cast<double> (audio.start));
    const auto tickOf = [&] (double fileSeconds) {
        return static_cast<Tick> (std::llround (song.tempo.tickAtSeconds (startSeconds + (fileSeconds - audio.offset) / speed))) - audio.start;
    };
    for (const auto& n : notes)
    {
        const Tick start = tickOf (n.start);
        const Tick end = std::min (tickOf (n.start + n.length), audio.length);
        if (start < 0 || start >= audio.length || end <= start)
            continue; // outside the part of the file that the clip plays
        Note note;
        note.start = start;
        note.length = std::max<Tick> (1, end - start);
        note.pitch = static_cast<std::uint8_t> (std::clamp (n.pitch, 0, 127));
        note.velocity = static_cast<std::uint8_t> (std::clamp (n.velocity, 1, 127));
        clip.notes.push_back (note);
    }
    return clip;
}

} // namespace arranger::audio_to_midi
