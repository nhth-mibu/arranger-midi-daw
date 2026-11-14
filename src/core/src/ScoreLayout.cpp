#include "arranger/core/ScoreLayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace arranger::score
{

using namespace notation;

namespace
{
    // Bravura's engraving defaults, in spaces.
    constexpr float kStaffLine = 0.13f, kStem = 0.12f, kLedger = 0.16f, kLedgerExtension = 0.4f;
    constexpr float kThinBar = 0.16f, kThickBar = 0.5f, kBeam = 0.5f, kBeamStep = 0.75f;
    constexpr float kHead = 1.18f, kWholeHead = 1.69f, kStemAnchor = 0.168f, kStemLength = 3.5f;
    constexpr float kBarLead = 1.2f, kStaffGap = 7.0f, kSystemGap = 9.0f, kWholeBarWidth = 7.0f;

    float staffY (float top, int position) { return top + static_cast<float> (8 - position) * 0.5f; }

    float headWidth (const Event& e) { return e.value == Value::whole ? kWholeHead : kHead; }

    char32_t headGlyph (const Event& e, const Head& h)
    {
        if (h.cross)
            return smufl::noteheadXBlack;
        if (e.value == Value::whole)
            return smufl::noteheadWhole;
        if (e.value == Value::half)
            return smufl::noteheadHalf;
        return smufl::noteheadBlack;
    }

    char32_t accidentalGlyph (Accidental a)
    {
        switch (a)
        {
            case Accidental::flat: return smufl::accidentalFlat;
            case Accidental::natural: return smufl::accidentalNatural;
            case Accidental::sharp: return smufl::accidentalSharp;
            case Accidental::doubleSharp: return smufl::accidentalDoubleSharp;
            case Accidental::doubleFlat: return smufl::accidentalDoubleFlat;
            default: return 0;
        }
    }

    float accidentalWidth (Accidental a)
    {
        switch (a)
        {
            case Accidental::flat: return 0.9f;
            case Accidental::natural: return 0.7f;
            case Accidental::doubleFlat: return 1.6f;
            default: return 1.0f;
        }
    }

    char32_t restGlyph (Value v)
    {
        switch (v)
        {
            case Value::whole: return smufl::restWhole;
            case Value::half: return smufl::restHalf;
            case Value::quarter: return smufl::restQuarter;
            case Value::eighth: return smufl::rest8th;
            case Value::sixteenth: return smufl::rest16th;
            default: return smufl::rest32nd;
        }
    }

    char32_t flagGlyph (int beams, bool up)
    {
        if (beams >= 3) return up ? smufl::flag32ndUp : smufl::flag32ndDown;
        if (beams == 2) return up ? smufl::flag16thUp : smufl::flag16thDown;
        return up ? smufl::flag8thUp : smufl::flag8thDown;
    }

    /** How one bar is spaced: its columns (moments where something starts) and their widths. */
    struct BarPlan
    {
        std::vector<Tick> columns;
        std::vector<float> fixed; ///< room before the column: accidentals, dots of the column before
        std::vector<float> flex;  ///< ideal room after the column, stretched to fill a system
        bool empty = true;        ///< only whole-bar rests

        float width (float stretch) const
        {
            if (empty)
                return kWholeBarWidth * std::max (1.0f, stretch);
            float w = kBarLead;
            for (std::size_t i = 0; i < columns.size(); ++i)
                w += fixed[i] + flex[i] * stretch;
            return w;
        }
        float fixedTotal() const
        {
            float w = kBarLead;
            for (const float f : fixed)
                w += f;
            return w;
        }
        float flexTotal() const
        {
            float w = 0;
            for (const float f : flex)
                w += f;
            return w;
        }
    };

    using MarkLists = std::vector<const std::vector<ScoreMark>*>;

    BarPlan planBar (const Part& part, const MarkLists& markLists, std::size_t bar)
    {
        BarPlan plan;
        std::set<Tick> starts;
        Tick length = 0;
        for (const auto& staff : part.staves)
        {
            const auto& b = staff.bars[bar];
            length = b.length;
            for (const auto& e : b.events)
                if (! e.wholeBarRest)
                {
                    starts.insert (e.start);
                    plan.empty = false;
                }
        }
        if (plan.empty)
            return plan;
        plan.columns.assign (starts.begin(), starts.end());
        plan.fixed.assign (plan.columns.size(), 0.0f);
        plan.flex.assign (plan.columns.size(), 0.0f);
        for (std::size_t i = 0; i < plan.columns.size(); ++i)
        {
            const Tick next = i + 1 < plan.columns.size() ? plan.columns[i + 1] : length;
            const auto d = static_cast<float> (next - plan.columns[i]);
            plan.flex[i] = 2.1f * std::pow (std::max (d, 60.0f) / 240.0f, 0.4f);
            for (const auto& staff : part.staves)
                for (const auto& e : staff.bars[bar].events)
                {
                    if (e.start != plan.columns[i] || e.rest)
                        continue;
                    int accidentals = 0;
                    bool second = false;
                    for (std::size_t h = 0; h < e.heads.size(); ++h)
                    {
                        accidentals += e.heads[h].accidental != Accidental::none ? 1 : 0;
                        if (h > 0 && e.heads[h].position - e.heads[h - 1].position == 1)
                            second = true;
                    }
                    if (accidentals > 0)
                        plan.fixed[i] = std::max (plan.fixed[i], 0.4f + 1.15f * static_cast<float> (std::min (accidentals, 3)));
                    float after = static_cast<float> (e.dots) * 0.6f + (second ? kHead : 0.0f) + (e.value == Value::whole ? 0.5f : 0.0f);
                    if (beamCount (e.value) > 0 && e.beamGroup < 0)
                        after += 0.6f; // a flag
                    plan.flex[i] = std::max (plan.flex[i], 2.1f * std::pow (std::max (d, 60.0f) / 240.0f, 0.4f) + after);
                }
        }

        // Room for the words under the notes, so that lyrics do not run into each other.
        const Tick barStart = part.staves.front().bars[bar].start;
        std::vector<float> words (plan.columns.size(), 0.0f);
        for (const auto* marks : markLists)
        for (const auto& m : *marks)
        {
            if (m.kind != ScoreMark::Kind::lyric || m.at < barStart || m.at >= barStart + length)
                continue;
            std::size_t column = 0;
            for (std::size_t i = 0; i < plan.columns.size(); ++i)
                if (plan.columns[i] <= m.at - barStart)
                    column = i;
            std::size_t letters = 0;
            for (const char c : m.text)
                letters += (static_cast<unsigned char> (c) & 0xc0) != 0x80 ? 1 : 0; // UTF-8: count characters, not bytes
            words[column] = std::max (words[column], 0.88f * static_cast<float> (letters));
        }
        for (std::size_t i = 0; i < plan.columns.size(); ++i)
        {
            if (words[i] <= 0.0f && (i + 1 >= words.size() || words[i + 1] <= 0.0f))
                continue;
            if (i + 1 < plan.columns.size())
            {
                const float need = (words[i] + words[i + 1]) / 2.0f + 0.8f;
                if (plan.flex[i] + plan.fixed[i + 1] < need)
                    plan.fixed[i + 1] += need - plan.flex[i] - plan.fixed[i + 1];
            }
            else
                plan.flex[i] = std::max (plan.flex[i], words[i] / 2.0f + 0.9f);
        }
        return plan;
    }

    struct PendingTie
    {
        float x = 0, y = 0;
        int dir = 1;
        std::size_t page = 0;
        int system = 0;
        float systemEnd = 0;
    };

    /** A tie or slur shape: a crescent from (x1, y1) to (x2, y2), bulging in `dir` (1 = down). */
    Shape tieShape (float x1, float y1, float x2, float y2, int dir)
    {
        Shape s;
        const float length = std::max (0.5f, x2 - x1);
        const float h = std::min (0.35f + 0.08f * length, 1.3f) * static_cast<float> (dir);
        const float inner = h - 0.2f * static_cast<float> (dir);
        constexpr int steps = 14;
        const auto quad = [&] (float bulge, float t) {
            const float mx = (x1 + x2) * 0.5f, my = (y1 + y2) * 0.5f + bulge * 2.0f;
            const float u = 1.0f - t;
            return std::pair<float, float> { u * u * x1 + 2 * u * t * mx + t * t * x2, u * u * y1 + 2 * u * t * my + t * t * y2 };
        };
        for (int i = 0; i <= steps; ++i)
            s.points.push_back (quad (h, static_cast<float> (i) / steps));
        for (int i = steps; i >= 0; --i)
            s.points.push_back (quad (inner, static_cast<float> (i) / steps));
        return s;
    }

    class Engraver
    {
    public:
        Engraver (const Part& p, const PageSetup& s) : part (p), setup (s)
        {
            // One track is one group. A full score has a group for each track.
            if (part.groups.empty())
                tracks.push_back ({ part.name, 0, part.staves.size(), &part.marks });
            else
                for (const auto& g : part.groups)
                    tracks.push_back ({ g.name, g.first, g.count, &g.marks });
            fullScore = tracks.size() > 1;
            staffGroup.assign (part.staves.size(), 0);
            offsets.assign (part.staves.size(), 0.0f);
            float y = 0.0f;
            for (std::size_t g = 0; g < tracks.size(); ++g)
            {
                markLists.push_back (tracks[g].marks);
                for (std::size_t k = 0; k < tracks[g].count && tracks[g].first + k < part.staves.size(); ++k)
                {
                    const std::size_t staff = tracks[g].first + k;
                    staffGroup[staff] = g;
                    offsets[staff] = y;
                    systemHeight = y + 4.0f;
                    y += 4.0f + kStaffGap;
                }
                y += below (tracks[g]) + (fullScore ? 1.0f : 0.0f); // a little more air between tracks
            }
        }

        Layout run()
        {
            if (part.staves.empty() || part.staves[0].bars.empty())
                return {};
            const std::size_t barCount = part.staves[0].bars.size();
            std::vector<BarPlan> plans;
            for (std::size_t b = 0; b < barCount; ++b)
                plans.push_back (planBar (part, markLists, b));

            // 1. Systems: as many bars as fit, then stretch to the full width.
            const bool named = std::any_of (tracks.begin(), tracks.end(), [] (const Group& g) { return ! g.name.empty(); });
            const float braceRoom = std::any_of (tracks.begin(), tracks.end(), [] (const Group& g) { return g.count > 1; }) ? 1.6f : 0.0f;
            struct System
            {
                std::size_t first, last; // [first, last)
                float stretch = 1.0f;
                float indent = 0.0f;
            };
            std::vector<System> systems;
            for (std::size_t b = 0; b < barCount;)
            {
                System sys;
                sys.first = b;
                sys.indent = braceRoom + (named && (systems.empty() || fullScore) ? (fullScore ? 8.0f : 6.0f) : 0.0f);
                const float avail = setup.width - 2.0f * setup.margin - sys.indent - headerWidth (systems.empty());
                float used = 0.0f;
                std::size_t e = b;
                while (e < barCount)
                {
                    const float w = plans[e].width (1.0f);
                    if (e > b && used + w > avail)
                        break;
                    used += w;
                    ++e;
                }
                sys.last = e;
                float fixedSum = 0.0f, flexSum = 0.0f, emptyCount = 0.0f;
                for (std::size_t i = b; i < e; ++i)
                {
                    if (plans[i].empty)
                        emptyCount += 1.0f;
                    else
                    {
                        fixedSum += plans[i].fixedTotal();
                        flexSum += plans[i].flexTotal();
                    }
                }
                const float flexAll = flexSum + emptyCount * kWholeBarWidth;
                sys.stretch = flexAll > 0.0f ? std::max (1.0f, (avail - fixedSum) / flexAll) : 1.0f;
                if (e == barCount && used < avail * 0.7f)
                    sys.stretch = 1.0f; // a short last line stays natural
                systems.push_back (sys);
                b = e;
            }

            // 2. Pages.
            const bool onePage = setup.height <= 0.0f;
            const float titleRoom = setup.title.empty() ? 2.0f : (setup.subtitle.empty() ? 12.0f : 15.0f);
            newPage();
            float cursor = setup.margin + titleRoom;
            drawTitle();
            for (std::size_t i = 0; i < systems.size(); ++i)
            {
                const float need = 5.0f + systemHeight + 4.0f + marksBelow();
                if (! onePage && cursor + need > setup.height - setup.margin && i > 0)
                {
                    newPage();
                    cursor = setup.margin;
                }
                const auto& sys = systems[i];
                drawSystem (static_cast<int> (i), sys.first, sys.last, setup.margin + sys.indent, cursor + 5.0f, sys.stretch, plans,
                            i == 0, i + 1 == systems.size());
                cursor += 5.0f + systemHeight + kSystemGap + marksBelow();
            }
            if (onePage)
                pages.back().height = cursor + setup.margin;
            for (std::size_t p = 1; p < pages.size(); ++p)
                pages[p].texts.push_back ({ std::to_string (p + 1), pages[p].width / 2.0f, pages[p].height - setup.margin * 0.5f, 1.6f,
                                            Text::Style::plain, Text::Align::centre, true });

            Layout out;
            out.pages = std::move (pages);
            out.systems = static_cast<int> (systems.size());
            out.notes = std::move (noteBoxes);
            out.bars = std::move (barBoxes);
            return out;
        }

    private:
        struct Group
        {
            std::string name;
            std::size_t first = 0, count = 1;
            const std::vector<ScoreMark>* marks = nullptr;
        };

        const Part& part;
        const PageSetup& setup;
        std::vector<Group> tracks;
        MarkLists markLists;
        std::vector<std::size_t> staffGroup; ///< the group of each staff
        std::vector<float> offsets;          ///< the top of each staff, from the top of the system
        float systemHeight = 4.0f;
        bool fullScore = false;
        std::vector<Page> pages;
        std::vector<NoteBox> noteBoxes;
        std::vector<BarBox> barBoxes;
        std::set<std::pair<std::size_t, std::size_t>> articulationsDrawn; ///< group, mark
        std::vector<std::map<int, PendingTie>> pending;

        Page& page() { return pages.back(); }

        void newPage()
        {
            Page p;
            p.width = setup.width;
            p.height = setup.height > 0.0f ? setup.height : 0.0f;
            pages.push_back (std::move (p));
        }

        static bool hasMarks (const Group& g, ScoreMark::Kind kind)
        {
            return std::any_of (g.marks->begin(), g.marks->end(), [&] (const ScoreMark& m) { return m.kind == kind; });
        }
        /** More room under a track for dynamics and lyrics (one staff only; a grand staff has the gap between its staves). */
        static float below (const Group& g)
        {
            if (g.count > 1)
                return 0.0f;
            return (hasMarks (g, ScoreMark::Kind::dynamic) ? 2.5f : 0.0f) + (hasMarks (g, ScoreMark::Kind::lyric) ? 2.5f : 0.0f);
        }
        float marksBelow() const { return below (tracks.back()); }

        /** Chord symbols, dynamics and lyrics of the bars that were just drawn, on the top staff of a track. */
        void drawMarks (const Group& group, Tick from, Tick to, float top)
        {
            const int staff = static_cast<int> (group.first);
            auto& p = page();
            const int pageIndex = static_cast<int> (pages.size()) - 1;
            const auto xAt = [&] (Tick tick) {
                for (const auto& bar : barBoxes)
                {
                    if (bar.page != pageIndex || bar.staff != staff || bar.top != top || tick < bar.start || tick >= bar.start + bar.length)
                        continue;
                    const Tick local = tick - bar.start;
                    float x = bar.x1 + kBarLead;
                    Tick before = -1;
                    for (const auto& [t, cx] : bar.columns)
                        if (t <= local && t > before)
                        {
                            before = t;
                            x = cx;
                        }
                    // Between two notes: a share of the way to the next one (or to the bar line).
                    Tick after = bar.length;
                    float xAfter = bar.x2;
                    for (const auto& [t, cx] : bar.columns)
                        if (t > local && t < after)
                        {
                            after = t;
                            xAfter = cx;
                        }
                    if (before < 0)
                        before = 0;
                    if (local > before && after > before)
                        x += (xAfter - x) * static_cast<float> (local - before) / static_cast<float> (after - before);
                    return x;
                }
                return -1.0f;
            };
            const bool dynamics = hasMarks (group, ScoreMark::Kind::dynamic);
            for (const auto& m : *group.marks)
            {
                if (m.at < from || m.at >= to)
                    continue;
                const float x = xAt (m.at);
                if (x < 0.0f)
                    continue;
                switch (m.kind)
                {
                    case ScoreMark::Kind::chord:
                        p.texts.push_back ({ m.text, x, top - 2.6f, 2.1f, Text::Style::bold, Text::Align::left, true });
                        break;
                    case ScoreMark::Kind::lyric:
                        p.texts.push_back ({ m.text, x + kHead / 2.0f, top + 4.0f + (dynamics ? 6.4f : 4.2f), 1.9f, Text::Style::plain, Text::Align::centre, true });
                        break;
                    case ScoreMark::Kind::dynamic:
                    {
                        float dx = x - 0.3f;
                        for (const char c : m.text)
                        {
                            char32_t glyph = 0;
                            float advance = 1.0f;
                            switch (c)
                            {
                                case 'p': glyph = smufl::dynamicPiano; advance = 1.25f; break;
                                case 'm': glyph = smufl::dynamicMezzo; advance = 1.75f; break;
                                case 'f': glyph = smufl::dynamicForte; advance = 1.0f; break;
                                case 'r': glyph = smufl::dynamicRinforzando; advance = 0.95f; break;
                                case 's': glyph = smufl::dynamicSforzando; advance = 0.85f; break;
                                case 'z': glyph = smufl::dynamicZ; advance = 1.0f; break;
                                default: break;
                            }
                            if (glyph != 0)
                                p.glyphs.push_back ({ glyph, dx, top + 4.0f + 3.4f });
                            dx += advance;
                        }
                        break;
                    }
                    case ScoreMark::Kind::articulation: break; // drawn with the note
                }
            }
        }

        /** The articulations on a note or chord: on the notehead side; a fermata or marcato above everything. */
        void drawArticulations (std::size_t groupIndex, const Bar& bar, const Event& e, float top, float headCentre)
        {
            const auto& marks = *tracks[groupIndex].marks;
            auto& p = page();
            const bool above = ! e.stemUp; // the notehead side
            const float lowY = staffY (top, e.heads.front().position), highY = staffY (top, e.heads.back().position);
            float offset = 1.15f;                                                 // from the centre of the head
            float ceiling = std::min (top - 0.9f, highY - (e.stemUp ? 4.2f : 1.3f)); // the lowest place free above the note
            std::vector<std::size_t> mine;
            for (std::size_t i = 0; i < marks.size(); ++i)
            {
                const auto& m = marks[i];
                if (m.kind == ScoreMark::Kind::articulation && std::abs (m.at - (bar.start + e.start)) <= 60 && articulationsDrawn.count ({ groupIndex, i }) == 0)
                    mine.push_back (i);
            }
            // Near the head first: staccato, tenuto, accent.
            for (const char* name : { "staccato", "tenuto", "accent" })
                for (const auto i : mine)
                {
                    if (marks[i].text != name)
                        continue;
                    articulationsDrawn.insert ({ groupIndex, i });
                    const bool dot = marks[i].text == "staccato";
                    const char32_t glyph = dot ? (above ? smufl::articStaccatoAbove : smufl::articStaccatoBelow)
                                         : marks[i].text == "tenuto" ? (above ? smufl::articTenutoAbove : smufl::articTenutoBelow)
                                                                           : (above ? smufl::articAccentAbove : smufl::articAccentBelow);
                    const float width = dot ? 0.34f : 1.3f;
                    const float height = dot ? 0.34f : (marks[i].text == "tenuto" ? 0.2f : 0.98f);
                    const float y = above ? highY - offset : lowY + offset;
                    p.glyphs.push_back ({ glyph, headCentre - width / 2.0f, y });
                    offset += height + 0.45f;
                    if (above)
                        ceiling = std::min (ceiling, y - height - 0.4f);
                }
            for (const char* name : { "marcato", "fermata" })
                for (const auto i : mine)
                {
                    if (marks[i].text != name)
                        continue;
                    articulationsDrawn.insert ({ groupIndex, i });
                    const bool fermata = marks[i].text == "fermata";
                    p.glyphs.push_back ({ fermata ? smufl::fermataAbove : smufl::articMarcatoAbove, headCentre - (fermata ? 1.25f : 0.6f), ceiling });
                    ceiling -= fermata ? 1.8f : 1.5f;
                }
            for (const auto i : mine)
                articulationsDrawn.insert ({ groupIndex, i }); // an unknown name: skip it
        }

        float keyWidth() const { return part.keyFifths == 0 ? 0.0f : static_cast<float> (std::abs (part.keyFifths)) * 1.15f + 1.0f; }
        float timeWidth() const { return part.time.numerator >= 10 || part.time.denominator >= 10 ? 3.8f : 2.6f; }
        float headerWidth (bool first) const { return 1.0f + 3.8f + keyWidth() + (first ? timeWidth() : 0.0f) + 0.6f; }

        void drawTitle()
        {
            if (setup.title.empty())
                return;
            auto& p = page();
            p.texts.push_back ({ setup.title, setup.width / 2.0f, setup.margin + 4.0f, 3.6f, Text::Style::bold, Text::Align::centre, true });
            if (! setup.subtitle.empty())
                p.texts.push_back ({ setup.subtitle, setup.width / 2.0f, setup.margin + 7.4f, 1.9f, Text::Style::italic, Text::Align::centre, true });
            if (setup.bpm > 0.0)
            {
                const float y = setup.margin + (setup.subtitle.empty() ? 10.0f : 13.0f);
                p.glyphs.push_back ({ smufl::metNoteQuarterUp, setup.margin + 1.0f, y, 0.85f });
                p.texts.push_back ({ "= " + std::to_string (static_cast<int> (std::lround (setup.bpm))), setup.margin + 2.4f, y, 1.9f,
                                     Text::Style::bold, Text::Align::left, true });
            }
        }

        void drawClefKeyTime (float x, float top, Clef clef, bool withTime)
        {
            auto& p = page();
            x += 1.0f;
            if (clef == Clef::treble)
                p.glyphs.push_back ({ smufl::gClef, x, staffY (top, 2) });
            else if (clef == Clef::bass)
                p.glyphs.push_back ({ smufl::fClef, x, staffY (top, 6) });
            else
                p.glyphs.push_back ({ smufl::percussionClef, x + 0.4f, staffY (top, 4) });
            x += 3.8f;
            if (clef != Clef::percussion && part.keyFifths != 0)
            {
                static constexpr int kSharps[7] = { 8, 5, 9, 6, 3, 7, 4 };
                static constexpr int kFlats[7] = { 4, 7, 3, 6, 2, 5, 1 };
                const int shift = clef == Clef::bass ? -2 : 0;
                const bool sharps = part.keyFifths > 0;
                for (int i = 0; i < std::min (std::abs (part.keyFifths), 7); ++i)
                    p.glyphs.push_back ({ sharps ? smufl::accidentalSharp : smufl::accidentalFlat, x + static_cast<float> (i) * 1.15f,
                                          staffY (top, (sharps ? kSharps[i] : kFlats[i]) + shift) });
                x += keyWidth();
            }
            else if (part.keyFifths != 0)
                x += keyWidth();
            if (withTime)
            {
                const auto digits = [&] (int value, int position) {
                    const auto text = std::to_string (value);
                    const float w = 1.75f * static_cast<float> (text.size());
                    float dx = x + (timeWidth() - 0.6f - w) / 2.0f;
                    for (const char c : text)
                    {
                        p.glyphs.push_back ({ static_cast<char32_t> (smufl::timeSig0 + static_cast<char32_t> (c - '0')), dx, staffY (top, position) });
                        dx += 1.75f;
                    }
                };
                digits (part.time.numerator, 6);
                digits (part.time.denominator, 2);
            }
        }

        void drawSystem (int index, std::size_t first, std::size_t last, float x0, float top, float stretch, const std::vector<BarPlan>& plans,
                         bool isFirst, bool isLast)
        {
            auto& p = page();
            const std::size_t staffCount = part.staves.size();
            if (pending.size() != staffCount)
                pending.assign (staffCount, {});
            std::vector<float> tops;
            for (std::size_t s = 0; s < staffCount; ++s)
                tops.push_back (top + offsets[s]);
            const float bottom = tops.back() + 4.0f;

            // Width of the system.
            float x = x0 + headerWidth (isFirst);
            const float headerEnd = x;
            std::vector<float> barX;
            for (std::size_t b = first; b < last; ++b)
            {
                barX.push_back (x);
                x += plans[b].width (stretch);
            }
            const float x1 = x;

            // Staff lines, the start line, the brace and the name.
            for (const float t : tops)
                for (int l = 0; l < 5; ++l)
                    p.lines.push_back ({ x0, t + static_cast<float> (l), x1, t + static_cast<float> (l), kStaffLine });
            if (staffCount > 1)
                p.lines.push_back ({ x0, tops.front(), x0, bottom, kThinBar });
            const bool anyBrace = std::any_of (tracks.begin(), tracks.end(), [] (const Group& g) { return g.count > 1; });
            for (const auto& g : tracks)
            {
                const float groupTop = tops[g.first], groupBottom = tops[g.first + g.count - 1] + 4.0f;
                if (g.count > 1)
                    p.glyphs.push_back ({ smufl::brace, x0 - 1.4f, groupBottom, (groupBottom - groupTop) / 4.0f });
                // The name: on the first system, and on each system of a full score.
                if ((isFirst || fullScore) && ! g.name.empty())
                    p.texts.push_back ({ g.name, x0 - (anyBrace ? 1.8f : 0.6f), (groupTop + groupBottom) / 2.0f + 0.6f, fullScore ? 1.7f : 1.9f,
                                         Text::Style::plain, Text::Align::right, true });
            }
            if (! isFirst)
                p.texts.push_back ({ std::to_string (first + 1), x0, tops.front() - 1.6f, 1.5f, Text::Style::italic, Text::Align::left, true });
            for (std::size_t s = 0; s < staffCount; ++s)
                drawClefKeyTime (x0, tops[s], part.staves[s].clef, isFirst);

            // Bars.
            for (std::size_t b = first; b < last; ++b)
            {
                const float bx = barX[b - first];
                const float bw = plans[b].width (stretch);
                const auto& plan = plans[b];
                std::map<Tick, float> columnX;
                float cx = bx + kBarLead;
                for (std::size_t c = 0; c < plan.columns.size(); ++c)
                {
                    cx += plan.fixed[c];
                    columnX[plan.columns[c]] = cx;
                    cx += plan.flex[c] * stretch;
                }
                for (std::size_t s = 0; s < staffCount; ++s)
                {
                    drawBar (part.staves[s].bars[b], s, tops[s], bx, bw, columnX, index, x1, headerEnd);
                    BarBox box;
                    box.page = static_cast<int> (pages.size()) - 1;
                    box.x1 = bx;
                    box.x2 = bx + bw;
                    box.top = tops[s];
                    box.staff = static_cast<int> (s);
                    box.clef = part.staves[s].clef;
                    box.start = part.staves[s].bars[b].start;
                    box.length = part.staves[s].bars[b].length;
                    box.columns.assign (columnX.begin(), columnX.end());
                    barBoxes.push_back (std::move (box));
                }

                // The bar line: across both staves of a grand staff, but not from one track to the next.
                const float lx = bx + bw;
                const bool final = isLast && b + 1 == last;
                for (const auto& g : tracks)
                {
                    const float groupTop = tops[g.first], groupBottom = tops[g.first + g.count - 1] + 4.0f;
                    if (final)
                    {
                        p.lines.push_back ({ lx - 0.85f, groupTop, lx - 0.85f, groupBottom, kThinBar });
                        p.lines.push_back ({ lx - kThickBar / 2.0f, groupTop, lx - kThickBar / 2.0f, groupBottom, kThickBar });
                    }
                    else
                        p.lines.push_back ({ lx, groupTop, lx, groupBottom, kThinBar });
                }
            }
            if (last > first)
            {
                const auto& bars = part.staves[0].bars;
                for (const auto& g : tracks)
                    if (! g.marks->empty())
                        drawMarks (g, bars[first].start, bars[last - 1].start + bars[last - 1].length, tops[g.first]);
            }
        }

        struct Stem
        {
            const Event* event = nullptr;
            float x = 0;      ///< stem centre
            float base = 0;   ///< where it leaves the notehead
            float tip = 0;
            float ideal = 0;
            bool up = true;
            int beams = 0;
        };

        void drawBar (const Bar& bar, std::size_t staffIndex, float top, float bx, float bw, const std::map<Tick, float>& columnX, int system,
                      float systemEnd, float headerEnd)
        {
            auto& p = page();
            auto& ties = pending[staffIndex];
            std::map<int, std::vector<Stem>> groups;
            std::map<int, std::vector<std::pair<float, float>>> tuplets; // x range of each tuplet's events
            std::map<int, bool> tupletBeamed;
            std::map<int, bool> tupletUp;

            for (const auto& e : bar.events)
            {
                if (e.wholeBarRest)
                {
                    p.glyphs.push_back ({ smufl::restWhole, bx + bw / 2.0f - 0.56f, staffY (top, 6) });
                    ties.clear();
                    continue;
                }
                const auto it = columnX.find (e.start);
                const float x = it != columnX.end() ? it->second : bx + kBarLead;
                if (e.tupletGroup >= 0)
                {
                    tuplets[e.tupletGroup].push_back ({ x, x + headWidth (e) });
                    tupletBeamed.try_emplace (e.tupletGroup, true);
                    if (e.beamGroup < 0)
                        tupletBeamed[e.tupletGroup] = false;
                    tupletUp[e.tupletGroup] = e.rest ? true : e.stemUp;
                }
                if (e.rest)
                {
                    const int position = e.value == Value::whole ? 6 : 4;
                    p.glyphs.push_back ({ restGlyph (e.value), x, staffY (top, position) });
                    if (e.dots > 0)
                        p.glyphs.push_back ({ smufl::augmentationDot, x + 1.6f, staffY (top, 5) });
                    ties.clear();
                    continue;
                }

                // Noteheads, seconds on the other side of the stem.
                const float w = headWidth (e);
                const bool up = e.stemUp;
                std::vector<float> hx (e.heads.size(), x);
                bool leftShift = false;
                if (up)
                {
                    for (std::size_t i = 1; i < e.heads.size(); ++i)
                        if (e.heads[i].position - e.heads[i - 1].position == 1 && hx[i - 1] == x)
                            hx[i] = x + w - kStem;
                }
                else
                {
                    for (std::size_t k = e.heads.size(); k-- > 1;)
                        if (e.heads[k].position - e.heads[k - 1].position == 1 && hx[k] == x)
                        {
                            hx[k - 1] = x - w + kStem;
                            leftShift = true;
                        }
                }
                std::map<int, bool> ledgersDone;
                for (std::size_t i = 0; i < e.heads.size(); ++i)
                {
                    const auto& h = e.heads[i];
                    const float y = staffY (top, h.position);
                    p.glyphs.push_back ({ headGlyph (e, h), hx[i], y });
                    noteBoxes.push_back ({ static_cast<int> (pages.size()) - 1, hx[i] + w / 2.0f, y, h.source, static_cast<int> (staffIndex) });
                    // Ledger lines.
                    for (int l = -2; l >= h.position; l -= 2)
                        p.lines.push_back ({ hx[i] - kLedgerExtension, staffY (top, l), hx[i] + w + kLedgerExtension, staffY (top, l), kLedger });
                    for (int l = 10; l <= h.position; l += 2)
                        p.lines.push_back ({ hx[i] - kLedgerExtension, staffY (top, l), hx[i] + w + kLedgerExtension, staffY (top, l), kLedger });
                    // Dots, in the space above a line note.
                    for (int d = 0; d < e.dots; ++d)
                        p.glyphs.push_back ({ smufl::augmentationDot, x + w + 0.35f + static_cast<float> (d) * 0.5f + (hx[i] > x ? w : 0.0f),
                                              staffY (top, h.position % 2 == 0 ? h.position + 1 : h.position) });
                }

                if (! tracks[staffGroup[staffIndex]].marks->empty())
                    drawArticulations (staffGroup[staffIndex], bar, e, top, x + w / 2.0f);

                // Accidentals: from the top down, in columns that don't collide.
                std::vector<std::pair<int, int>> placed; // position, column
                for (std::size_t k = e.heads.size(); k-- > 0;)
                {
                    const auto& h = e.heads[k];
                    if (h.accidental == Accidental::none)
                        continue;
                    int column = 0;
                    while (std::any_of (placed.begin(), placed.end(), [&] (const auto& pc) { return pc.second == column && std::abs (pc.first - h.position) < 6; }))
                        ++column;
                    placed.push_back ({ h.position, column });
                    const float ax = x - (leftShift ? w : 0.0f) - 0.25f - accidentalWidth (h.accidental) - static_cast<float> (column) * 1.15f;
                    p.glyphs.push_back ({ accidentalGlyph (h.accidental), ax, staffY (top, h.position) });
                }

                // Ties into this event, then the ties that leave it.
                for (std::size_t i = 0; i < e.heads.size(); ++i)
                {
                    const auto& h = e.heads[i];
                    const auto t = ties.find (h.pitch);
                    if (t == ties.end())
                        continue;
                    const float y = staffY (top, h.position) + 0.55f * static_cast<float> (t->second.dir);
                    if (t->second.system == system && t->second.page == pages.size() - 1)
                        p.shapes.push_back (tieShape (t->second.x, t->second.y, hx[i] - 0.15f, y, t->second.dir));
                    else
                    {
                        pages[t->second.page].shapes.push_back (tieShape (t->second.x, t->second.y, t->second.systemEnd + 0.4f, t->second.y, t->second.dir));
                        p.shapes.push_back (tieShape (headerEnd - 0.6f, y, hx[i] - 0.15f, y, t->second.dir));
                    }
                }
                ties.clear();
                for (std::size_t i = 0; i < e.heads.size(); ++i)
                {
                    const auto& h = e.heads[i];
                    if (! h.tieToNext)
                        continue;
                    int dir = up ? 1 : -1;
                    if (e.heads.size() > 1)
                        dir = i * 2 + 1 >= e.heads.size() ? -1 : 1; // upper half of a chord curves up
                    if (e.value == Value::whole)
                        dir = h.position >= 4 ? -1 : 1;
                    ties[h.pitch] = { hx[i] + w + 0.15f, staffY (top, h.position) + 0.55f * static_cast<float> (dir), dir, pages.size() - 1, system, systemEnd };
                }

                if (e.value == Value::whole)
                    continue;

                // Stem.
                Stem stem;
                stem.event = &e;
                stem.up = up;
                stem.beams = beamCount (e.value);
                const float yHigh = staffY (top, e.heads.back().position), yLow = staffY (top, e.heads.front().position);
                const float extra = stem.beams > 1 ? static_cast<float> (stem.beams - 1) * 0.5f : 0.0f;
                if (up)
                {
                    stem.x = x + w - kStem / 2.0f;
                    stem.base = yLow - kStemAnchor;
                    stem.ideal = std::min (yHigh - kStemLength - extra, staffY (top, 4));
                }
                else
                {
                    stem.x = x + kStem / 2.0f;
                    stem.base = yHigh + kStemAnchor;
                    stem.ideal = std::max (yLow + kStemLength + extra, staffY (top, 4));
                }
                stem.tip = stem.ideal;
                if (e.beamGroup >= 0)
                    groups[e.beamGroup].push_back (stem);
                else
                {
                    p.lines.push_back ({ stem.x, stem.base, stem.x, stem.tip, kStem });
                    if (stem.beams > 0)
                        p.glyphs.push_back ({ flagGlyph (stem.beams, up), stem.x - kStem / 2.0f, stem.tip });
                }
            }

            // Beams.
            std::map<int, std::pair<float, float>> beamLine; // group -> y at first stem, slope
            for (auto& [g, stems] : groups)
            {
                const bool up = stems.front().up;
                const float xa = stems.front().x, xb = stems.back().x;
                int maxBeams = 1;
                for (const auto& s : stems)
                    maxBeams = std::max (maxBeams, s.beams);
                const float extra = static_cast<float> (maxBeams - 1) * kBeamStep;
                for (auto& s : stems)
                    s.ideal += up ? -extra + static_cast<float> (s.beams - 1) * 0.5f : extra - static_cast<float> (s.beams - 1) * 0.5f;
                float rise = stems.back().ideal - stems.front().ideal;
                rise = std::clamp (rise, -1.0f, 1.0f);
                const float slope = xb > xa ? rise / (xb - xa) : 0.0f;
                float y0 = stems.front().ideal;
                float shift = 0.0f;
                for (const auto& s : stems)
                {
                    const float yb = y0 + slope * (s.x - xa);
                    shift = up ? std::max (shift, yb - s.ideal) : std::max (shift, s.ideal - yb);
                }
                y0 += up ? -shift : shift;
                beamLine[g] = { y0, slope };
                const auto at = [&] (float xx) { return y0 + slope * (xx - xa); };
                for (auto& s : stems)
                {
                    s.tip = at (s.x);
                    p.lines.push_back ({ s.x, s.base, s.x, s.tip, kStem });
                }
                const auto beam = [&] (float from, float to, int level) {
                    const float off = static_cast<float> (level) * kBeamStep * (up ? 1.0f : -1.0f);
                    const float t1 = at (from) + off, t2 = at (to) + off;
                    const float d = up ? kBeam : -kBeam;
                    p.shapes.push_back ({ { { from, t1 }, { to, t2 }, { to, t2 + d }, { from, t1 + d } } });
                };
                beam (xa - kStem / 2.0f, xb + kStem / 2.0f, 0);
                for (int level = 1; level < maxBeams; ++level)
                {
                    for (std::size_t i = 0; i < stems.size();)
                    {
                        if (stems[i].beams <= level)
                        {
                            ++i;
                            continue;
                        }
                        std::size_t j = i;
                        while (j + 1 < stems.size() && stems[j + 1].beams > level)
                            ++j;
                        if (j > i)
                            beam (stems[i].x - kStem / 2.0f, stems[j].x + kStem / 2.0f, level);
                        else
                        {
                            // A single short note: a stub towards its neighbour.
                            const bool right = i == 0 || (i + 1 < stems.size() && stems[i - 1].beams < stems[i + 1].beams);
                            const float sx = stems[i].x;
                            if (right)
                                beam (sx - kStem / 2.0f, sx + 1.1f, level);
                            else
                                beam (sx - 1.1f, sx + kStem / 2.0f, level);
                        }
                        i = j + 1;
                    }
                }
            }

            // Triplet numbers (and a bracket when the notes have no beam).
            for (const auto& [g, spans] : tuplets)
            {
                const float xa = spans.front().first, xb = spans.back().second;
                const float mid = (xa + xb) / 2.0f;
                const bool up = tupletUp[g];
                float y = up ? top - 1.4f : top + 4.0f + 2.6f;
                int beamGroup = -1;
                for (const auto& e : bar.events)
                    if (e.tupletGroup == g && e.beamGroup >= 0)
                        beamGroup = e.beamGroup;
                if (tupletBeamed[g] && beamGroup >= 0 && beamLine.count (beamGroup) != 0)
                {
                    const auto& stems = groups[beamGroup];
                    const auto [y0, slope] = beamLine[beamGroup];
                    const float yb = y0 + slope * (mid - stems.front().x);
                    y = up ? std::min (yb - 0.7f, top - 0.6f) : std::max (yb + 1.9f, top + 5.6f);
                    p.glyphs.push_back ({ smufl::tuplet3, mid - 0.45f, y, 0.85f });
                    continue;
                }
                // Unbeamed: a bracket with the number in a gap.
                const float by = up ? std::min (y, top - 1.8f) : std::max (y, top + 6.0f);
                const float hook = up ? 0.6f : -0.6f;
                p.lines.push_back ({ xa, by, mid - 0.9f, by, kThinBar });
                p.lines.push_back ({ mid + 0.9f, by, xb, by, kThinBar });
                p.lines.push_back ({ xa, by, xa, by + hook, kThinBar });
                p.lines.push_back ({ xb, by, xb, by + hook, kThinBar });
                p.glyphs.push_back ({ smufl::tuplet3, mid - 0.45f, by + 0.6f, 0.85f });
            }
        }
    };
}

Layout layout (const Part& part, const PageSetup& setup)
{
    return Engraver (part, setup).run();
}

namespace
{
    /** Song position at an x inside a bar: between the note columns, then on to the bar line. */
    Tick tickInBar (const BarBox& bar, float x)
    {
        std::vector<std::pair<float, Tick>> points;
        for (const auto& [tick, cx] : bar.columns)
            points.push_back ({ cx, tick });
        if (points.empty())
            points.push_back ({ bar.x1 + 1.0f, 0 });
        points.push_back ({ std::max (bar.x2, points.back().first + 0.1f), bar.length });
        if (x <= points.front().first)
            return bar.start + points.front().second;
        for (std::size_t i = 0; i + 1 < points.size(); ++i)
            if (x <= points[i + 1].first)
            {
                const float f = (x - points[i].first) / std::max (0.01f, points[i + 1].first - points[i].first);
                return bar.start + points[i].second + static_cast<Tick> (f * static_cast<float> (points[i + 1].second - points[i].second));
            }
        return bar.start + bar.length - 1;
    }
}

Hit hitTest (const Layout& layout, int page, float x, float y)
{
    const BarBox* best = nullptr;
    float bestDistance = 7.0f; // spaces from the middle line
    for (const auto& bar : layout.bars)
    {
        if (bar.page != page || x < bar.x1 || x > bar.x2)
            continue;
        const float d = std::abs (y - (bar.top + 2.0f));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = &bar;
        }
    }
    Hit hit;
    if (best == nullptr)
        return hit;
    hit.valid = true;
    hit.staff = best->staff;
    hit.clef = best->clef;
    hit.top = best->top;
    hit.tick = tickInBar (*best, x);
    hit.position = static_cast<int> (std::lround ((best->top + 4.0f - y) * 2.0f));
    return hit;
}

float xOfTick (const Layout& layout, int page, int staff, float top, Tick tick)
{
    for (const auto& bar : layout.bars)
    {
        if (bar.page != page || bar.staff != staff || std::abs (bar.top - top) > 0.01f || tick < bar.start || tick >= bar.start + bar.length)
            continue;
        const Tick local = tick - bar.start;
        std::vector<std::pair<Tick, float>> points (bar.columns.begin(), bar.columns.end());
        if (points.empty())
            points.push_back ({ 0, bar.x1 + 1.0f });
        points.push_back ({ bar.length, std::max (bar.x2, points.back().second + 0.1f) });
        if (local <= points.front().first)
            return points.front().second;
        for (std::size_t i = 0; i + 1 < points.size(); ++i)
            if (local <= points[i + 1].first)
            {
                const float f = static_cast<float> (local - points[i].first) / static_cast<float> (std::max<Tick> (1, points[i + 1].first - points[i].first));
                return points[i].second + f * (points[i + 1].second - points[i].second);
            }
    }
    return -1.0f;
}

} // namespace arranger::score
