#include "arranger/core/MusicXml.h"

#include "arranger/core/Theory.h"

#include "arranger/core/SongEdit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <vector>

namespace arranger::musicxml
{

namespace
{
    // ----------------------------------------------------------------- write
    std::string escape (std::string_view s)
    {
        std::string out;
        for (const char c : s)
        {
            switch (c)
            {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    const char* typeName (notation::Value v)
    {
        switch (v)
        {
            case notation::Value::whole: return "whole";
            case notation::Value::half: return "half";
            case notation::Value::quarter: return "quarter";
            case notation::Value::eighth: return "eighth";
            case notation::Value::sixteenth: return "16th";
            case notation::Value::thirtySecond: return "32nd";
        }
        return "quarter";
    }

    constexpr char kStepNames[] = "CDEFGAB";

    void writeClef (std::ostream& o, notation::Clef clef, int number, bool numbered)
    {
        o << "        <clef";
        if (numbered)
            o << " number=\"" << number << "\"";
        o << ">";
        switch (clef)
        {
            case notation::Clef::treble: o << "<sign>G</sign><line>2</line>"; break;
            case notation::Clef::bass: o << "<sign>F</sign><line>4</line>"; break;
            case notation::Clef::percussion: o << "<sign>percussion</sign><line>2</line>"; break;
        }
        o << "</clef>\n";
    }

    struct ChordKind
    {
        const char* suffix;
        const char* kind;
    };
    constexpr ChordKind kChordKinds[] = {
        { "", "major" }, { "m", "minor" }, { "7", "dominant" }, { "maj7", "major-seventh" }, { "m7", "minor-seventh" },
        { "dim", "diminished" }, { "dim7", "diminished-seventh" }, { "m7b5", "half-diminished" }, { "aug", "augmented" },
        { "sus4", "suspended-fourth" }, { "sus2", "suspended-second" }, { "6", "major-sixth" }, { "m6", "minor-sixth" },
        { "9", "dominant-ninth" }, { "maj9", "major-ninth" }, { "m9", "minor-ninth" }, { "mMaj7", "major-minor" },
        { "11", "dominant-11th" }, { "13", "dominant-13th" }, { "5", "power" },
    };

    /** "Bbm7/F" as a harmony element. Text that is not a chord name goes out as words. */
    void writeChord (std::ostream& o, const std::string& text)
    {
        const auto letter = [] (char c) { return c >= 'A' && c <= 'G'; };
        if (text.empty() || ! letter (text[0]))
        {
            o << "      <direction placement=\"above\"><direction-type><words>" << escape (text) << "</words></direction-type></direction>\n";
            return;
        }
        std::size_t i = 1;
        int alter = 0;
        if (i < text.size() && (text[i] == '#' || text[i] == 'b'))
            alter = text[i++] == '#' ? 1 : -1;
        auto suffix = text.substr (i);
        std::string bass;
        if (const auto slash = suffix.rfind ('/'); slash != std::string::npos && slash + 1 < suffix.size() && letter (suffix[slash + 1]))
        {
            bass = suffix.substr (slash + 1);
            suffix = suffix.substr (0, slash);
        }
        const char* kind = "other";
        for (const auto& k : kChordKinds)
            if (suffix == k.suffix)
                kind = k.kind;
        o << "      <harmony><root><root-step>" << text[0] << "</root-step>";
        if (alter != 0)
            o << "<root-alter>" << alter << "</root-alter>";
        o << "</root><kind text=\"" << escape (suffix) << "\">" << kind << "</kind>";
        if (! bass.empty())
        {
            o << "<bass><bass-step>" << bass[0] << "</bass-step>";
            if (bass.size() > 1 && (bass[1] == '#' || bass[1] == 'b'))
                o << "<bass-alter>" << (bass[1] == '#' ? 1 : -1) << "</bass-alter>";
            o << "</bass>";
        }
        o << "</harmony>\n";
    }

    void writeStaffBar (std::ostream& o, const notation::Bar& bar, int staffNumber, bool numbered, bool drums,
                        std::set<int>& tiedIn, const std::vector<ScoreMark>* marks = nullptr)
    {
        std::map<int, int> beamSizes;
        for (const auto& e : bar.events)
            if (e.beamGroup >= 0)
                ++beamSizes[e.beamGroup];
        std::map<int, int> beamSeen;

        for (std::size_t eventIndex = 0; eventIndex < bar.events.size(); ++eventIndex)
        {
            const auto& e = bar.events[eventIndex];
            // The marks from this event up to the next one.
            std::vector<const ScoreMark*> here;
            if (marks != nullptr)
            {
                const Tick from = eventIndex == 0 ? bar.start : bar.start + e.start;
                const Tick to = eventIndex + 1 < bar.events.size() ? bar.start + bar.events[eventIndex + 1].start : bar.start + bar.length;
                for (const auto& m : *marks)
                    if (m.at >= from && m.at < to)
                        here.push_back (&m);
            }
            for (const auto* m : here)
            {
                if (m->kind == ScoreMark::Kind::chord)
                    writeChord (o, m->text);
                else if (m->kind == ScoreMark::Kind::dynamic)
                    o << "      <direction placement=\"below\"><direction-type><dynamics><" << escape (m->text) << "/></dynamics></direction-type></direction>\n";
            }
            const auto common = [&] (bool chord, const notation::Head* head) {
                o << "      <note>\n";
                if (chord)
                    o << "        <chord/>\n";
                if (head == nullptr)
                    o << (e.wholeBarRest ? "        <rest measure=\"yes\"/>\n" : "        <rest/>\n");
                else if (drums)
                {
                    const int diatonic = head->position + 4 * 7 + 2;
                    o << "        <unpitched><display-step>" << kStepNames[diatonic % 7] << "</display-step><display-octave>" << diatonic / 7
                      << "</display-octave></unpitched>\n";
                }
                else
                {
                    o << "        <pitch><step>" << kStepNames[head->spelling.step] << "</step>";
                    if (head->spelling.alter != 0)
                        o << "<alter>" << head->spelling.alter << "</alter>";
                    o << "<octave>" << head->spelling.octave << "</octave></pitch>\n";
                }
                o << "        <duration>" << e.length << "</duration>\n";
                const bool stop = head != nullptr && tiedIn.count (head->pitch) != 0;
                const bool start = head != nullptr && head->tieToNext;
                if (stop) o << "        <tie type=\"stop\"/>\n";
                if (start) o << "        <tie type=\"start\"/>\n";
                if (head != nullptr && drums)
                    o << "        <instrument id=\"P" << 0 << "-I" << head->pitch + 1 << "\"/>\n";
                o << "        <voice>" << staffNumber << "</voice>\n";
                if (! e.wholeBarRest)
                {
                    o << "        <type>" << typeName (e.value) << "</type>\n";
                    for (int d = 0; d < e.dots; ++d)
                        o << "        <dot/>\n";
                }
                if (head != nullptr && head->accidental != notation::Accidental::none)
                {
                    static constexpr const char* kNames[] = { "", "flat-flat", "flat", "natural", "sharp", "double-sharp" };
                    o << "        <accidental>" << kNames[static_cast<int> (head->accidental)] << "</accidental>\n";
                }
                if (e.triplet)
                    o << "        <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>\n";
                if (head != nullptr)
                {
                    if (e.value != notation::Value::whole)
                        o << "        <stem>" << (e.stemUp ? "up" : "down") << "</stem>\n";
                    if (head->cross)
                        o << "        <notehead>x</notehead>\n";
                }
                if (numbered)
                    o << "        <staff>" << staffNumber << "</staff>\n";
                if (head != nullptr && ! chord && e.beamGroup >= 0)
                {
                    const int index = beamSeen[e.beamGroup]++;
                    const int size = beamSizes[e.beamGroup];
                    o << "        <beam number=\"1\">" << (index == 0 ? "begin" : (index + 1 == size ? "end" : "continue")) << "</beam>\n";
                }
                std::string articulations, fermata, lyric;
                if (head != nullptr && ! chord)
                    for (const auto* m : here)
                    {
                        if (m->kind == ScoreMark::Kind::articulation)
                        {
                            if (m->text == "fermata") fermata = "<fermata/>";
                            else if (m->text == "marcato") articulations += "<strong-accent/>";
                            else if (m->text == "staccato" || m->text == "accent" || m->text == "tenuto") articulations += "<" + m->text + "/>";
                        }
                        else if (m->kind == ScoreMark::Kind::lyric && lyric.empty() && ! stop)
                            lyric = m->text;
                    }
                if (stop || start || ! articulations.empty() || ! fermata.empty())
                {
                    o << "        <notations>";
                    if (stop) o << "<tied type=\"stop\"/>";
                    if (start) o << "<tied type=\"start\"/>";
                    if (! articulations.empty()) o << "<articulations>" << articulations << "</articulations>";
                    o << fermata << "</notations>\n";
                }
                if (! lyric.empty())
                {
                    const bool more = lyric.size() > 1 && lyric.back() == '-';
                    o << "        <lyric number=\"1\"><syllabic>" << (more ? "begin" : "single") << "</syllabic><text>"
                      << escape (more ? lyric.substr (0, lyric.size() - 1) : lyric) << "</text></lyric>\n";
                }
                o << "      </note>\n";
            };

            if (e.rest)
            {
                common (false, nullptr);
                continue;
            }
            std::set<int> next;
            for (std::size_t i = 0; i < e.heads.size(); ++i)
            {
                common (i > 0, &e.heads[i]);
                if (e.heads[i].tieToNext)
                    next.insert (e.heads[i].pitch);
            }
            tiedIn = std::move (next);
        }
    }

    // ------------------------------------------------------------------ read
    struct Node
    {
        std::string name;
        std::map<std::string, std::string> attributes;
        std::vector<std::unique_ptr<Node>> children;
        std::string text;

        const Node* child (std::string_view n) const
        {
            for (const auto& c : children)
                if (c->name == n)
                    return c.get();
            return nullptr;
        }
        std::string childText (std::string_view n, std::string fallback = {}) const
        {
            const auto* c = child (n);
            return c != nullptr ? c->text : fallback;
        }
        std::string attribute (const std::string& n, std::string fallback = {}) const
        {
            const auto it = attributes.find (n);
            return it != attributes.end() ? it->second : fallback;
        }
    };

    std::string trim (std::string s)
    {
        const auto ws = [] (char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
        while (! s.empty() && ws (s.back())) s.pop_back();
        std::size_t i = 0;
        while (i < s.size() && ws (s[i])) ++i;
        return s.substr (i);
    }

    void appendUtf8 (std::string& out, unsigned long code)
    {
        if (code < 0x80) out += static_cast<char> (code);
        else if (code < 0x800) { out += static_cast<char> (0xc0 | (code >> 6)); out += static_cast<char> (0x80 | (code & 0x3f)); }
        else if (code < 0x10000) { out += static_cast<char> (0xe0 | (code >> 12)); out += static_cast<char> (0x80 | ((code >> 6) & 0x3f)); out += static_cast<char> (0x80 | (code & 0x3f)); }
        else { out += static_cast<char> (0xf0 | (code >> 18)); out += static_cast<char> (0x80 | ((code >> 12) & 0x3f)); out += static_cast<char> (0x80 | ((code >> 6) & 0x3f)); out += static_cast<char> (0x80 | (code & 0x3f)); }
    }

    std::string unescape (std::string_view s)
    {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] != '&')
            {
                out += s[i];
                continue;
            }
            const auto end = s.find (';', i);
            if (end == std::string_view::npos || end - i > 10)
            {
                out += s[i];
                continue;
            }
            const auto name = s.substr (i + 1, end - i - 1);
            if (name == "amp") out += '&';
            else if (name == "lt") out += '<';
            else if (name == "gt") out += '>';
            else if (name == "quot") out += '"';
            else if (name == "apos") out += '\'';
            else if (name.size() > 1 && name[0] == '#')
            {
                const bool hex = name[1] == 'x' || name[1] == 'X';
                const std::string digits (name.substr (hex ? 2 : 1));
                char* stop = nullptr;
                const auto code = std::strtoul (digits.c_str(), &stop, hex ? 16 : 10);
                if (stop != digits.c_str() && code > 0 && code < 0x110000)
                    appendUtf8 (out, code);
            }
            else
                out += std::string (s.substr (i, end - i + 1));
            i = end;
        }
        return out;
    }

    /** A small XML reader: elements, attributes and text. It skips comments, DOCTYPE and processing instructions. */
    class Parser
    {
    public:
        explicit Parser (std::string_view text) : s (text) {}

        std::unique_ptr<Node> parse()
        {
            auto root = std::make_unique<Node>();
            std::vector<Node*> stack { root.get() };
            while (p < s.size())
            {
                if (s[p] != '<')
                {
                    const auto next = std::min (s.find ('<', p), s.size());
                    stack.back()->text += unescape (s.substr (p, next - p));
                    p = next;
                    continue;
                }
                if (starts ("<!--")) { skipPast ("-->"); continue; }
                if (starts ("<![CDATA["))
                {
                    const auto end = s.find ("]]>", p);
                    if (end == std::string_view::npos) fail();
                    stack.back()->text += std::string (s.substr (p + 9, end - p - 9));
                    p = end + 3;
                    continue;
                }
                if (starts ("<?")) { skipPast ("?>"); continue; }
                if (starts ("<!")) { skipDoctype(); continue; }
                if (starts ("</"))
                {
                    skipPast (">");
                    if (stack.size() <= 1) fail();
                    stack.back()->text = trim (stack.back()->text);
                    stack.pop_back();
                    continue;
                }
                ++p;
                auto node = std::make_unique<Node>();
                node->name = name();
                if (node->name.empty()) fail();
                bool selfClosing = false;
                for (;;)
                {
                    space();
                    if (p >= s.size()) fail();
                    if (s[p] == '>') { ++p; break; }
                    if (s[p] == '/') { selfClosing = true; skipPast (">"); break; }
                    const auto key = name();
                    space();
                    if (key.empty() || p >= s.size() || s[p] != '=') fail();
                    ++p;
                    space();
                    if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) fail();
                    const char quote = s[p++];
                    const auto end = s.find (quote, p);
                    if (end == std::string_view::npos) fail();
                    node->attributes[key] = unescape (s.substr (p, end - p));
                    p = end + 1;
                }
                auto* raw = node.get();
                stack.back()->children.push_back (std::move (node));
                if (! selfClosing)
                {
                    if (stack.size() > 200) fail();
                    stack.push_back (raw);
                }
            }
            if (stack.size() != 1) fail();
            return root;
        }

    private:
        [[noreturn]] static void fail() { throw Error ("This is not a correct XML file"); }
        bool starts (std::string_view t) const { return s.substr (p, t.size()) == t; }
        void skipPast (std::string_view t)
        {
            const auto end = s.find (t, p);
            if (end == std::string_view::npos) fail();
            p = end + t.size();
        }
        void skipDoctype()
        {
            int depth = 0;
            for (; p < s.size(); ++p)
            {
                if (s[p] == '[') ++depth;
                else if (s[p] == ']') --depth;
                else if (s[p] == '>' && depth <= 0) { ++p; return; }
            }
            fail();
        }
        void space() { while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) ++p; }
        std::string name()
        {
            const auto start = p;
            while (p < s.size() && s[p] != ' ' && s[p] != '\n' && s[p] != '\r' && s[p] != '\t' && s[p] != '=' && s[p] != '>' && s[p] != '/')
                ++p;
            return std::string (s.substr (start, p - start));
        }

        std::string_view s;
        std::size_t p = 0;
    };

    double number (const std::string& text, double fallback = 0.0)
    {
        if (text.empty())
            return fallback;
        char* end = nullptr;
        const double v = std::strtod (text.c_str(), &end);
        return end == text.c_str() || ! std::isfinite (v) ? fallback : v;
    }

    int integer (const std::string& text, int fallback = 0)
    {
        return static_cast<int> (std::lround (std::clamp (number (text, fallback), -1.0e6, 1.0e6)));
    }

    int dynamicVelocity (const std::string& mark)
    {
        static const std::map<std::string, int> kMarks = { { "pppp", 20 }, { "ppp", 28 }, { "pp", 40 }, { "p", 52 }, { "mp", 66 }, { "mf", 80 },
                                                           { "f", 96 }, { "ff", 108 }, { "fff", 118 }, { "ffff", 127 }, { "sf", 110 },
                                                           { "sfz", 110 }, { "fz", 110 }, { "fp", 96 } };
        const auto it = kMarks.find (mark);
        return it != kMarks.end() ? it->second : 0;
    }

    /** The MusicXML mode of a key: major, minor, dorian ... */
    std::string modeText (const std::string& keyName)
    {
        const auto key = theory::parseKey (keyName);
        if (! key)
            return "major";
        const auto n = theory::name (*key);
        return n.substr (n.find (' ') + 1);
    }

    std::string keyName (int fifths, bool minor)
    {
        static constexpr const char* kMajors[15] = { "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#" };
        static constexpr const char* kMinors[15] = { "Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E", "B", "F#", "C#", "G#", "D#", "A#" };
        fifths = std::clamp (fifths, -7, 7);
        return std::string (minor ? kMinors[fifths + 7] : kMajors[fifths + 7]) + (minor ? " minor" : " major");
    }
}

// ---------------------------------------------------------------------------
std::string write (const Song& song, const notation::Options& base)
{
    struct Entry
    {
        const Track* track;
        notation::Part part;
    };
    std::vector<Entry> entries;
    const int fifths = notation::keyFifths (song.key);
    for (const auto& track : song.tracks)
    {
        const auto notes = notation::timelineNotes (track);
        if (notes.empty())
            continue;
        auto options = base;
        options.drums = notation::isDrumTrack (track);
        options.minorKey = notation::minorKey (song.key);
        entries.push_back ({ &track, notation::makePart (track.name, notes, song.contentEnd(), song.timeSignature, fifths, options) });
        entries.back().part.marks = track.marks;
    }

    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n"
      << "<score-partwise version=\"4.0\">\n"
      << "  <work><work-title>" << escape (song.title) << "</work-title></work>\n"
      << "  <identification><encoding><software>Lazy Arranger Midi DAW</software></encoding></identification>\n"
      << "  <part-list>\n";
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& t = *entries[i].track;
        const auto id = "P" + std::to_string (i + 1);
        o << "    <score-part id=\"" << id << "\">\n      <part-name>" << escape (t.name) << "</part-name>\n";
        if (entries[i].part.drums)
        {
            std::set<int> pitches;
            for (const auto& bar : entries[i].part.staves[0].bars)
                for (const auto& e : bar.events)
                    for (const auto& h : e.heads)
                        pitches.insert (h.pitch);
            for (const int pitch : pitches)
                o << "      <score-instrument id=\"" << id << "-I" << pitch + 1 << "\"><instrument-name>Drum " << pitch << "</instrument-name></score-instrument>\n";
            for (const int pitch : pitches)
                o << "      <midi-instrument id=\"" << id << "-I" << pitch + 1 << "\"><midi-channel>" << t.output.channel << "</midi-channel><midi-program>"
                  << std::max (0, t.sound.program) + 1 << "</midi-program><midi-unpitched>" << pitch + 1 << "</midi-unpitched></midi-instrument>\n";
        }
        else
        {
            o << "      <score-instrument id=\"" << id << "-I1\"><instrument-name>" << escape (t.name) << "</instrument-name></score-instrument>\n"
              << "      <midi-instrument id=\"" << id << "-I1\"><midi-channel>" << t.output.channel << "</midi-channel><midi-program>" << std::max (0, t.sound.program) + 1
              << "</midi-program></midi-instrument>\n";
        }
        o << "    </score-part>\n";
    }
    o << "  </part-list>\n";

    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const auto& part = entries[i].part;
        const auto id = "P" + std::to_string (i + 1);
        const bool numbered = part.staves.size() > 1;
        std::vector<std::set<int>> tied (part.staves.size());
        o << "  <part id=\"" << id << "\">\n";
        const std::size_t barCount = part.staves.empty() ? 0 : part.staves[0].bars.size();
        for (std::size_t b = 0; b < barCount; ++b)
        {
            o << "    <measure number=\"" << b + 1 << "\">\n";
            if (b == 0)
            {
                o << "      <attributes>\n        <divisions>" << kTicksPerQuarter << "</divisions>\n"
                  << "        <key><fifths>" << fifths << "</fifths><mode>"
                  << modeText (song.key) << "</mode></key>\n"
                  << "        <time><beats>" << part.time.numerator << "</beats><beat-type>" << part.time.denominator << "</beat-type></time>\n";
                if (numbered)
                    o << "        <staves>" << part.staves.size() << "</staves>\n";
                for (std::size_t s = 0; s < part.staves.size(); ++s)
                    writeClef (o, part.staves[s].clef, static_cast<int> (s) + 1, numbered);
                o << "      </attributes>\n";
                if (i == 0)
                    o << "      <direction placement=\"above\"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>"
                      << std::lround (song.baseTempo()) << "</per-minute></metronome></direction-type><sound tempo=\"" << song.baseTempo() << "\"/></direction>\n";
            }
            for (std::size_t s = 0; s < part.staves.size(); ++s)
            {
                if (s > 0)
                    o << "      <backup><duration>" << part.staves[s].bars[b].length << "</duration></backup>\n";
                std::ostringstream staff;
                writeStaffBar (staff, part.staves[s].bars[b], static_cast<int> (s) + 1, numbered, part.drums, tied[s], s == 0 ? &part.marks : nullptr);
                auto text = staff.str();
                // Drum instrument ids are written with part 0; put the real part id in.
                const std::string marker = "<instrument id=\"P0-I";
                for (auto at = text.find (marker); at != std::string::npos; at = text.find (marker, at + 1))
                    text.replace (at, marker.size(), "<instrument id=\"" + id + "-I");
                o << text;
            }
            if (b + 1 == barCount)
                o << "      <barline location=\"right\"><bar-style>light-heavy</bar-style></barline>\n";
            o << "    </measure>\n";
        }
        o << "  </part>\n";
    }
    o << "</score-partwise>\n";
    return o.str();
}

void save (const Song& song, const std::filesystem::path& path, const notation::Options& options)
{
    std::ofstream out (path, std::ios::binary);
    out << write (song, options);
    if (! out)
        throw Error ("Can't write " + path.string());
}

// ---------------------------------------------------------------------------
Song read (std::string_view xml)
{
    if (xml.substr (0, 2) == "PK")
        throw Error ("This is a compressed MusicXML file (.mxl). Unpack it first");
    const auto document = Parser (xml).parse();
    const auto* score = document->child ("score-partwise");
    if (score == nullptr)
        throw Error (document->child ("score-timewise") != nullptr ? "Timewise MusicXML files are not supported. Export a partwise file"
                                                                    : "This is not a MusicXML file");

    Song song;
    song.title.clear();
    if (const auto* work = score->child ("work"))
        song.title = work->childText ("work-title");
    if (song.title.empty())
        song.title = score->childText ("movement-title");
    if (song.title.empty())
        song.title = "Imported score";

    struct PartInfo
    {
        std::string name;
        int channel = 0, program = -1;
        std::map<std::string, int> unpitched; // instrument id -> MIDI note
    };
    std::map<std::string, PartInfo> infos;
    if (const auto* list = score->child ("part-list"))
        for (const auto& sp : list->children)
        {
            if (sp->name != "score-part")
                continue;
            auto& info = infos[sp->attribute ("id")];
            info.name = sp->childText ("part-name");
            for (const auto& mi : sp->children)
            {
                if (mi->name != "midi-instrument")
                    continue;
                if (info.channel == 0 && mi->child ("midi-channel") != nullptr)
                    info.channel = std::clamp (integer (mi->childText ("midi-channel")), 1, 16);
                if (info.program < 0 && mi->child ("midi-program") != nullptr)
                    info.program = std::clamp (integer (mi->childText ("midi-program")) - 1, 0, 127);
                if (mi->child ("midi-unpitched") != nullptr)
                    info.unpitched[mi->attribute ("id")] = std::clamp (integer (mi->childText ("midi-unpitched")) - 1, 0, 127);
            }
        }

    bool haveKey = false, haveTime = false, haveTempo = false;
    std::vector<Tick> measureStarts; // from the first part; the other parts follow it
    Tick songEnd = 0;
    int usedChannels = 0;

    struct Built
    {
        PartInfo info;
        std::vector<Note> notes;
        std::vector<ScoreMark> marks;
        bool drums = false;
    };
    std::vector<Built> built;

    bool firstPart = true;
    for (const auto& partNode : score->children)
    {
        if (partNode->name != "part")
            continue;
        Built out;
        out.info = infos[partNode->attribute ("id")];

        double divisions = 1.0;
        int transpose = 0;
        int velocity = 80;
        Tick measureStart = 0;
        std::size_t measureIndex = 0;
        std::map<int, std::size_t> openTies; // pitch -> index in out.notes
        const auto ticks = [&] (const std::string& duration) {
            return static_cast<Tick> (std::llround (std::clamp (number (duration), 0.0, 1.0e7) * static_cast<double> (kTicksPerQuarter) / divisions));
        };

        for (const auto& measure : partNode->children)
        {
            if (measure->name != "measure")
                continue;
            if (! firstPart && measureIndex < measureStarts.size())
                measureStart = measureStarts[measureIndex];
            Tick cursor = measureStart, reached = measureStart, chordStart = measureStart;

            // A short first measure is a pickup: it ends where bar 1 ends.
            const auto measureLength = [&] {
                Tick position = 0, longest = 0;
                double div = divisions;
                for (const auto& item : measure->children)
                {
                    if (item->name == "attributes" && item->child ("divisions") != nullptr)
                        div = std::max (1.0e-3, number (item->childText ("divisions"), 1.0));
                    const auto d = static_cast<Tick> (std::llround (number (item->childText ("duration")) * static_cast<double> (kTicksPerQuarter) / div));
                    if (item->name == "note" && item->child ("chord") == nullptr && item->child ("grace") == nullptr) position += d;
                    else if (item->name == "forward") position += d;
                    else if (item->name == "backup") position = std::max<Tick> (0, position - d);
                    longest = std::max (longest, position);
                }
                return longest;
            };

            for (const auto& item : measure->children)
            {
                if (item->name == "attributes")
                {
                    if (item->child ("divisions") != nullptr)
                        divisions = std::max (1.0e-3, number (item->childText ("divisions"), 1.0));
                    if (const auto* key = item->child ("key"); key != nullptr && ! haveKey && key->child ("fifths") != nullptr)
                    {
                        song.key = keyName (integer (key->childText ("fifths")), key->childText ("mode") == "minor");
                        haveKey = true;
                    }
                    if (const auto* time = item->child ("time"); time != nullptr && ! haveTime)
                    {
                        const TimeSignature sig { integer (time->childText ("beats"), 4), integer (time->childText ("beat-type"), 4) };
                        if (sig.isValid())
                            song.timeSignature = sig;
                        haveTime = true;
                    }
                    if (const auto* tr = item->child ("transpose"))
                        transpose = integer (tr->childText ("chromatic")) + 12 * integer (tr->childText ("octave-change"));
                    if (const auto* clef = item->child ("clef"); clef != nullptr && clef->childText ("sign") == "percussion")
                        out.drums = true;
                }
                else if (item->name == "direction" || item->name == "sound")
                {
                    const auto* sound = item->name == "sound" ? item.get() : item->child ("sound");
                    if (sound != nullptr)
                    {
                        if (const auto tempo = number (sound->attribute ("tempo")); tempo > 0.0)
                        {
                            if (! haveTempo || cursor == 0)
                                song.tempo.set (0, tempo);
                            else
                                song.tempo.set (cursor, tempo);
                            haveTempo = true;
                        }
                        if (const auto dyn = number (sound->attribute ("dynamics")); dyn > 0.0)
                            velocity = std::clamp (static_cast<int> (std::lround (dyn * 0.9)), 1, 127);
                    }
                    for (const auto& type : item->children)
                        if (type->name == "direction-type")
                            if (const auto* dynamics = type->child ("dynamics"))
                                for (const auto& mark : dynamics->children)
                                    if (const int v = dynamicVelocity (mark->name); v > 0)
                                    {
                                        velocity = v;
                                        out.marks.push_back ({ ScoreMark::Kind::dynamic, cursor, mark->name });
                                    }
                }
                else if (item->name == "harmony")
                {
                    std::string text;
                    if (const auto* root = item->child ("root"))
                    {
                        text = root->childText ("root-step");
                        const int alter = integer (root->childText ("root-alter"));
                        text += alter > 0 ? "#" : (alter < 0 ? "b" : "");
                    }
                    if (const auto* kind = item->child ("kind"); kind != nullptr && ! text.empty())
                    {
                        if (kind->attributes.count ("text") != 0)
                            text += kind->attribute ("text");
                        else
                            for (const auto& k : kChordKinds)
                                if (kind->text == k.kind)
                                    text += k.suffix;
                    }
                    if (const auto* bass = item->child ("bass"); bass != nullptr && ! text.empty())
                    {
                        const int alter = integer (bass->childText ("bass-alter"));
                        text += "/" + bass->childText ("bass-step") + (alter > 0 ? "#" : (alter < 0 ? "b" : ""));
                    }
                    if (! text.empty())
                        out.marks.push_back ({ ScoreMark::Kind::chord, cursor, text });
                }
                else if (item->name == "backup")
                    cursor = std::max (measureStart, cursor - ticks (item->childText ("duration")));
                else if (item->name == "forward")
                {
                    cursor += ticks (item->childText ("duration"));
                    reached = std::max (reached, cursor);
                }
                else if (item->name == "note")
                {
                    if (item->child ("grace") != nullptr || item->child ("cue") != nullptr)
                        continue;
                    const Tick length = ticks (item->childText ("duration"));
                    const bool chord = item->child ("chord") != nullptr;
                    const Tick start = chord ? chordStart : cursor;
                    if (! chord)
                    {
                        chordStart = cursor;
                        cursor += length;
                        reached = std::max (reached, cursor);
                    }
                    if (item->child ("rest") != nullptr || length <= 0)
                        continue;
                    if (! chord)
                    {
                        for (const auto& sub : item->children)
                        {
                            if (sub->name == "lyric" && (sub->attribute ("number", "1") == "1") && sub->child ("text") != nullptr)
                            {
                                const auto syllabic = sub->childText ("syllabic");
                                out.marks.push_back ({ ScoreMark::Kind::lyric, start, sub->childText ("text") + (syllabic == "begin" || syllabic == "middle" ? "-" : "") });
                            }
                            if (sub->name != "notations")
                                continue;
                            if (sub->child ("fermata") != nullptr)
                                out.marks.push_back ({ ScoreMark::Kind::articulation, start, "fermata" });
                            if (const auto* artics = sub->child ("articulations"))
                                for (const auto& a : artics->children)
                                {
                                    if (a->name == "staccato" || a->name == "accent" || a->name == "tenuto")
                                        out.marks.push_back ({ ScoreMark::Kind::articulation, start, a->name });
                                    else if (a->name == "strong-accent")
                                        out.marks.push_back ({ ScoreMark::Kind::articulation, start, "marcato" });
                                }
                        }
                    }

                    int pitch = -1;
                    if (const auto* p = item->child ("pitch"))
                    {
                        static constexpr int kPc[7] = { 9, 11, 0, 2, 4, 5, 7 };
                        const auto step = p->childText ("step");
                        if (step.size() == 1 && step[0] >= 'A' && step[0] <= 'G')
                            pitch = kPc[step[0] - 'A'] + static_cast<int> (std::lround (number (p->childText ("alter")))) + 12 * (integer (p->childText ("octave"), 4) + 1) + transpose;
                    }
                    else if (item->child ("unpitched") != nullptr)
                    {
                        out.drums = true;
                        const auto* inst = item->child ("instrument");
                        const auto it = inst != nullptr ? out.info.unpitched.find (inst->attribute ("id")) : out.info.unpitched.end();
                        pitch = it != out.info.unpitched.end() ? it->second : 38;
                    }
                    if (pitch < 0 || pitch > 127)
                        continue;

                    bool tieStop = false, tieStart = false;
                    for (const auto& t : item->children)
                        if (t->name == "tie")
                        {
                            tieStop = tieStop || t->attribute ("type") == "stop";
                            tieStart = tieStart || t->attribute ("type") == "start";
                        }
                    const auto open = openTies.find (pitch);
                    if (tieStop && open != openTies.end() && out.notes[open->second].end() >= start - 1)
                    {
                        out.notes[open->second].length = start + length - out.notes[open->second].start;
                        if (! tieStart)
                            openTies.erase (open);
                        continue;
                    }
                    Note n;
                    n.start = start;
                    n.length = length;
                    n.pitch = static_cast<std::uint8_t> (pitch);
                    n.velocity = static_cast<std::uint8_t> (velocity);
                    out.notes.push_back (n);
                    if (tieStart)
                        openTies[pitch] = out.notes.size() - 1;
                    else
                        openTies.erase (pitch);
                }
            }

            if (firstPart)
            {
                Tick length = reached - measureStart;
                const Tick bar = song.timeSignature.ticksPerBar();
                if (measureIndex == 0 && length > 0 && length < bar && (measure->attribute ("implicit") == "yes" || measure->attribute ("number") == "0"))
                {
                    // Pickup: move its notes to the end of bar 1.
                    const Tick shift = bar - measureLength();
                    for (auto& n : out.notes)
                        n.start += shift;
                    for (auto& m : out.marks)
                        m.at += shift;
                    length = bar;
                }
                if (length <= 0)
                    length = bar;
                measureStarts.push_back (measureStart);
                measureStart += length;
            }
            else
            {
                if (measureIndex == 0 && measureStarts.size() > 1 && (measure->attribute ("implicit") == "yes" || measure->attribute ("number") == "0"))
                {
                    const Tick shift = std::max<Tick> (0, measureStarts[1] - measureLength());
                    for (auto& n : out.notes)
                        n.start += shift;
                    for (auto& m : out.marks)
                        m.at += shift;
                }
                measureStart = measureIndex + 1 < measureStarts.size() ? measureStarts[measureIndex + 1] : std::max (reached, measureStart);
            }
            songEnd = std::max ({ songEnd, reached, measureStart });
            ++measureIndex;
        }
        firstPart = false;
        built.push_back (std::move (out));
    }
    if (built.empty())
        throw Error ("This MusicXML file has no parts");

    const Tick bar = song.timeSignature.ticksPerBar();
    const Tick end = std::max (bar, ((songEnd + bar - 1) / bar) * bar);
    for (auto& b : built)
    {
        if (b.info.channel == 10)
            b.drums = true;
        auto& track = edit::addTrack (song, b.info.name.empty() ? std::string (b.drums ? "Drums" : "Part") : b.info.name);
        if (b.drums)
        {
            track.output.channel = 10;
            track.colour = "coral";
        }
        else
        {
            // Each part gets its own channel, so each part keeps its own sound.
            int channel = b.info.channel;
            if (channel == 0 || channel == 10 || (usedChannels & (1 << channel)) != 0)
                for (channel = 1; channel <= 16; ++channel)
                    if (channel != 10 && (usedChannels & (1 << channel)) == 0)
                        break;
            if (channel > 16)
                channel = b.info.channel > 0 && b.info.channel != 10 ? b.info.channel : 1;
            usedChannels |= 1 << channel;
            track.output.channel = channel;
        }
        if (b.info.program >= 0)
            track.sound.program = b.info.program;
        // One chord, lyric and dynamic per position; the first one in the file stays.
        for (auto& m : b.marks)
            if (m.kind == ScoreMark::Kind::articulation
                || std::none_of (track.marks.begin(), track.marks.end(), [&] (const ScoreMark& x) { return x.kind == m.kind && x.at == m.at; }))
                track.marks.push_back (std::move (m));
        std::stable_sort (track.marks.begin(), track.marks.end(), [] (const ScoreMark& x, const ScoreMark& y) { return x.at < y.at; });
        std::sort (b.notes.begin(), b.notes.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });
        Clip clip;
        clip.name = track.name;
        clip.start = 0;
        clip.length = end;
        clip.notes = std::move (b.notes);
        edit::addClip (song, track.id, std::move (clip));
    }
    return song;
}

Song load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    auto song = read (text.str());
    if (song.title == "Imported score")
        song.title = path.stem().string();
    return song;
}

} // namespace arranger::musicxml
