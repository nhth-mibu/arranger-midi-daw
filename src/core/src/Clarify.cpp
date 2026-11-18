#include "arranger/core/Clarify.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/SongEdit.h"
#include "arranger/core/StyleChord.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace arranger::clarify
{

namespace
{
    int mod12 (int x) noexcept { return ((x % 12) + 12) % 12; }

    std::string noteName (int pc) { return style_chord::name ({ mod12 (pc), ChordType::major, -1 }); }

    std::string chordName (const Chord& c) { return style_chord::name (c); }

    std::string timeText (double seconds)
    {
        const auto s = static_cast<int> (std::max (0.0, seconds));
        char buffer[16];
        std::snprintf (buffer, sizeof (buffer), "%d:%02d", s / 60, s % 60);
        return buffer;
    }

    std::string idFor (double start)
    {
        char buffer[32];
        std::snprintf (buffer, sizeof (buffer), "q-%.2f", start);
        return buffer;
    }

    /** The chroma frames that fall in [start, end) seconds. */
    std::pair<std::size_t, std::size_t> frameRange (const analysis::Chroma& c, double start, double end, std::size_t frames)
    {
        if (c.hopSeconds <= 0.0 || frames == 0)
            return { 0, 0 };
        const auto first = static_cast<std::size_t> (std::clamp (std::floor (start / c.hopSeconds), 0.0, static_cast<double> (frames)));
        const auto last = static_cast<std::size_t> (std::clamp (std::ceil (end / c.hopSeconds), 0.0, static_cast<double> (frames)));
        return { first, std::max (first, last) };
    }

    bool answered (const analysis::Result& r, double start, double end)
    {
        const double middle = (start + end) * 0.5;
        return std::any_of (r.answers.begin(), r.answers.end(), [&] (const analysis::Answer& a) { return middle >= a.start && middle < a.end; });
    }

    /** The pitch with pitch class `pc` nearest to `pitch`. */
    int nearestWith (int pitch, int pc)
    {
        int up = mod12 (pc - pitch);
        const int to = up <= 6 ? pitch + up : pitch + up - 12;
        return std::clamp (to, 0, 127);
    }

    Tick onBeat (Tick t, Tick beat) { return beat > 0 ? (t + beat / 2) / beat * beat : t; }

    /** The chord a track's marks say at `at` (the last chord mark at or before it). */
    std::optional<Chord> chordAt (const Track& t, Tick at)
    {
        std::optional<Chord> out;
        for (const auto& m : t.marks)
        {
            if (m.at > at)
                break;
            if (m.kind == ScoreMark::Kind::chord)
                out = style_chord::parse (m.text);
        }
        return out;
    }

    TrackId bassTrack (const Song& song, Tick from, Tick to)
    {
        for (const auto& t : song.tracks)
            if (t.output.kind != TrackOutput::Kind::audio && t.name == "Bass MIDI")
                return t.id;
        const auto roles = arranging::guessRoles (song);
        for (const auto& [id, role] : roles)
            if (role == arranging::Role::bass)
                if (const auto* t = edit::findTrack (song, id); t != nullptr && ! arranging::playedNotes (*t, from, to).empty())
                    return id;
        return 0;
    }

    void addUnique (std::vector<TrackId>& ids, TrackId id)
    {
        if (std::find (ids.begin(), ids.end(), id) == ids.end())
            ids.push_back (id);
    }

    /** The notes of a chord for a track that plays the chords (as the analysis makes them). */
    std::vector<int> chordPitches (const Chord& c, int base = 48, int bassBase = 36)
    {
        std::vector<int> out;
        for (const int tone : style_chord::tones (c.type))
            out.push_back (base + c.root + tone);
        if (c.bass >= 0)
            out.push_back (bassBase + c.bass);
        return out;
    }
} // namespace

int bassOf (const Chord& c) noexcept
{
    return c.bass >= 0 ? mod12 (c.bass) : mod12 (c.root);
}

// ---- evidence ---------------------------------------------------------------------------

std::array<float, 12> harmonyEvidence (const analysis::Chroma& c, double start, double end)
{
    std::array<float, 12> sum {};
    const auto [first, last] = frameRange (c, start, end, c.frames.size());
    for (auto i = first; i < last; ++i)
        for (std::size_t k = 0; k < 12; ++k)
            sum[k] += c.frames[i][k];
    return sum;
}

std::array<float, 12> bassEvidence (const analysis::Result& r, const analysis::Chroma& harmony, const analysis::Chroma* bassStem, double start, double end)
{
    std::array<double, 12> notes {}, low {};
    double notesSum = 0.0, lowSum = 0.0;
    if (const auto* bass = r.stem ("bass"); bass != nullptr && bass->hasNotes)
        for (const auto& n : bass->notes)
        {
            if (n.pitch >= 60)
                continue;
            const double overlap = std::min (end, n.start + n.length) - std::max (start, n.start);
            if (overlap <= 0.0)
                continue;
            const double w = overlap * n.velocity / 127.0;
            notes[static_cast<std::size_t> (mod12 (n.pitch))] += w;
            notesSum += w;
        }
    const auto& lowChroma = bassStem != nullptr && ! bassStem->bass.empty() ? *bassStem : harmony;
    const auto [first, last] = frameRange (lowChroma, start, end, lowChroma.bass.size());
    for (auto i = first; i < last; ++i)
        for (std::size_t k = 0; k < 12; ++k)
        {
            low[k] += lowChroma.bass[i][k];
            lowSum += lowChroma.bass[i][k];
        }
    // Where the bass is quiet (it does not play), its chroma is noise: it does not count.
    if (lowSum > 0.0 && last > first)
    {
        std::vector<double> levels;
        levels.reserve (lowChroma.bass.size());
        for (const auto& frame : lowChroma.bass)
        {
            double e = 0.0;
            for (const float v : frame)
                e += v;
            levels.push_back (e);
        }
        const auto at = levels.begin() + static_cast<std::ptrdiff_t> (levels.size() * 9 / 10);
        std::nth_element (levels.begin(), at, levels.end());
        const double loud = *at;
        if (lowSum / static_cast<double> (last - first) < 0.1 * loud)
            lowSum = 0.0;
    }

    std::array<double, 12> mixed {};
    int sources = 0;
    for (const auto& [part, sum] : { std::pair { &notes, notesSum }, std::pair { &low, lowSum } })
        if (sum > 0.0)
        {
            ++sources;
            for (std::size_t k = 0; k < 12; ++k)
                mixed[k] += (*part)[k] / sum;
        }
    std::array<float, 12> out {};
    if (sources == 0)
        return out;
    const double top = *std::max_element (mixed.begin(), mixed.end());
    for (std::size_t k = 0; k < 12; ++k)
        out[k] = top > 0.0 ? static_cast<float> (mixed[k] / top) : 0.0f;
    return out;
}

double score (const Chord& c, const std::array<float, 12>& harmony, const std::array<float, 12>& bassWeights, const Settings& settings)
{
    const double fit = analysis::chordFit (harmony, c);
    const double bass = bassWeights[static_cast<std::size_t> (bassOf (c))];
    const double inversion = c.bass >= 0 && mod12 (c.bass) != mod12 (c.root) ? 0.1 : 0.0;
    return fit + settings.bassWeight * bass - inversion;
}

// ---- the questions ----------------------------------------------------------------------

std::vector<Question> find (const analysis::Result& r, const analysis::Chroma& harmony, const analysis::Chroma* bassStem, const Settings& settings)
{
    struct Found
    {
        Question q;
        double rank = 0.0;
    };
    std::vector<Found> found;
    const auto types = analysis::findableChordTypes();

    for (const auto& tc : r.chords)
    {
        const double length = tc.end - tc.start;
        if (length < settings.shortest || answered (r, tc.start, tc.end))
            continue;
        const auto h = harmonyEvidence (harmony, tc.start, tc.end);
        const auto w = bassEvidence (r, harmony, bassStem, tc.start, tc.end);
        if (*std::max_element (h.begin(), h.end()) <= 0.0f)
            continue;

        // Without a bass stem the low notes of the mix are less sure (kick drum, low chord notes): they count less.
        const auto* bassNotes = r.stem ("bass");
        Settings here = settings;
        const bool mixOnly = bassStem == nullptr && (bassNotes == nullptr || bassNotes->notes.empty());
        if (mixOnly)
        {
            here.bassWeight *= 0.5;
            here.ask = std::max (here.ask, 1.0); // ask only when the other choice fits better
        }

        const Chord heard = tc.chord;
        const double heardScore = score (heard, h, w, here);

        // The best chord with another bass note or another root.
        Chord best;
        double bestScore = -1.0;
        for (const auto type : types)
            for (int root = 0; root < 12; ++root)
                for (const int tone : style_chord::tones (type))
                {
                    // A chord over another bass note: over its third, or its seventh when a bass stem says so.
                    // Not over its fifth: that is mostly a root-fifth bass line. Not at all from the mix alone.
                    if (tone != 0 && (mixOnly || tone == 6 || tone == 7 || ((tone == 10 || tone == 11) && bassStem == nullptr)))
                        continue;
                    Chord c { root, type, tone == 0 ? -1 : mod12 (root + tone) };
                    if (mod12 (c.root) == mod12 (heard.root) && bassOf (c) == bassOf (heard))
                        continue;
                    const double s = score (c, h, w, here);
                    if (s > bestScore)
                    {
                        bestScore = s;
                        best = c;
                    }
                }
        if (bestScore <= 0.0)
            continue;
        const double doubt = bestScore / std::max (heardScore, 1e-6);
        if (doubt < here.ask)
            continue;

        Question q;
        q.id = idFor (tc.start);
        q.start = tc.start;
        q.end = tc.end;
        q.doubt = doubt;
        const bool bassDiffers = bassOf (best) != bassOf (heard);
        q.kind = bassDiffers ? "bass" : "root";
        const auto label = [&] (const Chord& c) { return bassDiffers ? "Bass " + noteName (bassOf (c)) + " (" + chordName (c) + ")" : chordName (c); };
        q.choices = { { heard, label (heard), heardScore }, { best, label (best), bestScore } };
        q.text = bassDiffers ? "Bass " + noteName (bassOf (heard)) + " or " + noteName (bassOf (best)) + " here?"
                             : chordName (heard) + " or " + chordName (best) + " here?";
        const auto fitA = static_cast<int> (std::lround (analysis::chordFit (h, heard) * 100.0));
        const auto fitB = static_cast<int> (std::lround (analysis::chordFit (h, best) * 100.0));
        q.because = "At " + timeText (tc.start) + ": the notes match " + chordName (heard) + " (" + std::to_string (fitA) + ") and "
                  + chordName (best) + " (" + std::to_string (fitB) + ") about the same.";
        const auto wa = w[static_cast<std::size_t> (bassOf (heard))], wb = w[static_cast<std::size_t> (bassOf (best))];
        if (bassDiffers && (wa > 0.0f || wb > 0.0f))
            q.because += " In the bass, " + noteName (bassOf (heard)) + " is " + std::to_string (static_cast<int> (std::lround (wa * 100))) + "% and "
                       + noteName (bassOf (best)) + " is " + std::to_string (static_cast<int> (std::lround (wb * 100))) + "% of the strongest note.";
        found.push_back ({ q, std::min (doubt, 1.25) * std::sqrt (std::min (length, 8.0)) });
    }

    std::stable_sort (found.begin(), found.end(), [] (const Found& a, const Found& b) { return a.rank > b.rank; });
    if (found.size() > settings.mostQuestions)
        found.resize (settings.mostQuestions);

    // The same two choices in other places: one answer can do them all.
    const auto key = [] (const Question& q) {
        auto a = chordName (q.choices[0].chord), b = chordName (q.choices[1].chord);
        return a < b ? a + "|" + b : b + "|" + a;
    };
    std::vector<Question> out;
    for (auto& f : found)
        out.push_back (std::move (f.q));
    for (auto& q : out)
        for (const auto& other : out)
            if (&other != &q && key (other) == key (q))
                q.similar.push_back (other.id);
    return out;
}

std::vector<const Question*> similarTo (const std::vector<Question>& all, const Question& q)
{
    std::vector<const Question*> out;
    for (const auto& other : all)
        if (std::find (q.similar.begin(), q.similar.end(), other.id) != q.similar.end())
            out.push_back (&other);
    return out;
}

// ---- answers ----------------------------------------------------------------------------

Changes answer (analysis::Result& r, const Question& q, const Chord& chosen)
{
    Changes changes;
    const Chord old = q.choices.empty() ? chosen : q.choices.front().chord;
    for (auto& tc : r.chords)
    {
        const double middle = (tc.start + tc.end) * 0.5;
        if (std::abs (tc.start - q.start) < 1e-3 || (middle >= q.start && middle < q.end))
            if (! (tc.chord == chosen))
            {
                tc.chord = chosen;
                ++changes.chords;
            }
    }
    if (bassOf (old) != bassOf (chosen))
        for (auto& stem : r.stems)
            if (stem.name == "bass")
                for (auto& n : stem.notes)
                    if (n.start >= q.start - 0.05 && n.start < q.end && mod12 (n.pitch) == bassOf (old))
                    {
                        n.pitch = nearestWith (n.pitch, bassOf (chosen));
                        ++changes.bassNotes;
                    }

    r.answers.erase (std::remove_if (r.answers.begin(), r.answers.end(), [&] (const analysis::Answer& a) { return a.start < q.end && a.end > q.start; }), r.answers.end());
    r.answers.push_back ({ q.start, q.end, chosen });
    std::sort (r.answers.begin(), r.answers.end(), [] (const auto& a, const auto& b) { return a.start < b.start; });
    if (changes.chords > 0)
        changes.what.push_back ("Chord " + chordName (old) + " -> " + chordName (chosen));
    if (changes.bassNotes > 0)
        changes.what.push_back (std::to_string (changes.bassNotes) + " bass note(s) " + noteName (bassOf (old)) + " -> " + noteName (bassOf (chosen)));
    return changes;
}

std::pair<Tick, Tick> span (const Song& song, ClipId source, const Question& q)
{
    const auto clip = edit::findAudioClip (song, source);
    if (clip == nullptr)
        throw std::runtime_error ("the analysed audio clip is not in the song any more");
    const Tick beat = std::max<Tick> (1, song.timeSignature.ticksPerBeat());
    Tick from = onBeat (analysis::tickInSong (song, *clip, q.start), beat);
    Tick to = onBeat (analysis::tickInSong (song, *clip, q.end), beat);
    from = std::clamp (from, clip->start, std::max (clip->start, clip->end() - 1));
    to = std::clamp (to, from + beat, std::max (from + beat, clip->end()));
    return { from, to };
}

Changes change (Song& song, ClipId source, const Question& q, const Chord& chosen)
{
    Changes changes;
    const Chord old = q.choices.empty() ? chosen : q.choices.front().chord;
    if (old == chosen)
        return changes;
    const auto clip = edit::findAudioClip (song, source);
    if (clip == nullptr)
        throw std::runtime_error ("the analysed audio clip is not in the song any more");
    const Tick clipEnd = clip->end();
    const auto [from, to] = span (song, source, q);
    const auto oldName = chordName (old), newName = chordName (chosen);

    // Chord symbols: on each track whose chord there is the old one.
    for (auto& t : song.tracks)
    {
        if (t.output.kind == TrackOutput::Kind::audio)
            continue;
        bool hasOld = false;
        for (const auto& m : t.marks)
            if (m.kind == ScoreMark::Kind::chord && m.at >= from && m.at < to && style_chord::parse (m.text) == std::optional<Chord> (old))
                hasOld = true;
        const auto atStart = chordAt (t, from);
        if (! hasOld && ! (atStart && *atStart == old))
            continue;
        const auto atEnd = chordAt (t, to);
        auto& marks = t.marks;
        marks.erase (std::remove_if (marks.begin(), marks.end(), [&] (const ScoreMark& m) {
                         return m.kind == ScoreMark::Kind::chord && m.at >= from && m.at < to && style_chord::parse (m.text) == std::optional<Chord> (old);
                     }),
                     marks.end());
        const auto markAt = [&] (Tick at) {
            return std::any_of (marks.begin(), marks.end(), [&] (const ScoreMark& m) { return m.kind == ScoreMark::Kind::chord && m.at == at; });
        };
        if (! markAt (from))
            marks.push_back ({ ScoreMark::Kind::chord, from, newName });
        if (to < clipEnd && ! markAt (to) && atEnd && *atEnd == old)
            marks.push_back ({ ScoreMark::Kind::chord, to, oldName });
        std::stable_sort (marks.begin(), marks.end(), [] (const ScoreMark& a, const ScoreMark& b) { return a.at < b.at; });
        ++changes.chords;
        addUnique (changes.tracks, t.id);

        // The track that the analysis made to play the chords: its notes follow.
        if (t.name != "Chords")
            continue;
        const auto oldPitches = chordPitches (old), newPitches = chordPitches (chosen);
        for (auto& ptr : t.clips)
        {
            if (ptr == nullptr || ptr->end() <= from || ptr->start >= to)
                continue;
            Clip c = *ptr;
            const Tick a = from - c.start, b = to - c.start;
            std::vector<Note> notes;
            for (const auto& n : c.notes)
            {
                const bool oldNote = std::find (oldPitches.begin(), oldPitches.end(), n.pitch) != oldPitches.end();
                if (! oldNote || n.end() <= a || n.start >= b)
                {
                    notes.push_back (n);
                    continue;
                }
                if (n.start < a)
                {
                    Note head = n;
                    head.length = a - n.start;
                    notes.push_back (head);
                }
                if (n.end() > b)
                {
                    Note tail = n;
                    tail.start = b;
                    tail.length = n.end() - b;
                    notes.push_back (tail);
                }
            }
            const Tick s = std::max<Tick> (0, a), e = std::min (c.length, b);
            if (e > s)
                for (const int p : newPitches)
                    notes.push_back ({ s, e - s, static_cast<std::uint8_t> (std::clamp (p, 0, 127)), 70 });
            c.notes = std::move (notes);
            edit::normalise (c);
            ptr = std::make_shared<const Clip> (std::move (c));
        }
    }

    // Bass notes on the old bass note move to the new one.
    if (bassOf (old) != bassOf (chosen))
        if (const auto id = bassTrack (song, from, to); id != 0)
            if (auto* t = edit::findTrack (song, id))
            {
                int moved = 0;
                for (auto& ptr : t->clips)
                {
                    if (ptr == nullptr || ptr->end() <= from || ptr->start >= to || ptr->loopLength > 0)
                        continue;
                    Clip c = *ptr;
                    int here = 0;
                    for (auto& n : c.notes)
                    {
                        const Tick at = c.start + n.start;
                        if (at >= from && at < to && mod12 (n.pitch) == bassOf (old))
                        {
                            n.pitch = static_cast<std::uint8_t> (nearestWith (n.pitch, bassOf (chosen)));
                            ++here;
                        }
                    }
                    if (here > 0)
                    {
                        edit::normalise (c);
                        ptr = std::make_shared<const Clip> (std::move (c));
                        moved += here;
                    }
                }
                if (moved > 0)
                {
                    changes.bassNotes = moved;
                    addUnique (changes.tracks, id);
                }
            }

    if (changes.chords > 0)
        changes.what.push_back ("Chord " + oldName + " -> " + newName);
    if (changes.bassNotes > 0)
        changes.what.push_back (std::to_string (changes.bassNotes) + " bass note(s) " + noteName (bassOf (old)) + " -> " + noteName (bassOf (chosen)));
    return changes;
}

// ---- A/B --------------------------------------------------------------------------------

arranging::Proposal proposal (const Song& song, ClipId source, const Question& q)
{
    arranging::Proposal p;
    p.id = q.id;
    p.kind = "transcription";
    p.title = q.text;
    p.because = q.because;
    const auto [from, to] = span (song, source, q);
    p.from = from;
    p.to = to;
    p.weight = q.doubt;

    Song changed = song;
    const auto changes = q.choices.size() > 1 ? change (changed, source, q, q.choices[1].chord) : Changes {};
    p.tracks = changes.tracks;
    for (const auto id : changes.tracks)
        if (const auto* t = edit::findTrack (song, id))
            p.before.push_back (*t);

    arranging::Option keep;
    keep.label = q.choices.empty() ? std::string ("Keep") : q.choices[0].label;
    keep.summary = "Keep what the analysis heard.";
    keep.tracks = p.before;
    p.options.push_back (keep);
    if (q.choices.size() > 1)
    {
        arranging::Option other;
        other.label = q.choices[1].label;
        std::string summary;
        for (const auto& w : changes.what)
            summary += (summary.empty() ? "" : "; ") + w;
        other.summary = summary.empty() ? "Changes the chord in the analysis." : summary + ".";
        for (const auto id : changes.tracks)
            if (const auto* t = edit::findTrack (changed, id))
                other.tracks.push_back (*t);
        other.notesChanged = changes.bassNotes;
        p.options.push_back (other);
    }
    return p;
}

Song listen (const Song& song, ClipId source, const Question& q, int choice)
{
    Song out = song;
    const auto [from, to] = span (song, source, q);
    std::set<int> used;
    for (auto& t : out.tracks)
        if (t.output.kind != TrackOutput::Kind::audio)
        {
            t.mixer.mute = true;
            used.insert (t.output.channel);
        }
    if (choice < 0 || choice >= static_cast<int> (q.choices.size()))
        return out;

    const auto& chord = q.choices[static_cast<std::size_t> (choice)].chord;
    auto& guide = edit::addTrack (out, "Guide: " + chordName (chord));
    int channel = 1;
    while (channel < 16 && (channel == 10 || used.count (channel) > 0))
        ++channel;
    guide.output.channel = channel;
    guide.sound.program = 0;
    guide.sound.name = kGeneralMidiNames[0];
    const auto id = guide.id;
    Clip c;
    c.name = "Guide";
    c.start = from;
    c.length = to - from;
    const Tick beat = std::max<Tick> (1, out.timeSignature.ticksPerBeat());
    // The chord on each beat (so it is heard again when the loop starts), the bass note held.
    for (Tick at = 0; at < c.length; at += beat)
        for (const int tone : style_chord::tones (chord.type))
            c.notes.push_back ({ at, std::min (beat, c.length - at), static_cast<std::uint8_t> (60 + mod12 (chord.root) + tone - (mod12 (chord.root) > 6 ? 12 : 0)), 72 });
    c.notes.push_back ({ 0, c.length, static_cast<std::uint8_t> (36 + bassOf (chord)), 100 });
    edit::normalise (c);
    edit::addClip (out, id, c);
    return out;
}

} // namespace arranger::clarify
