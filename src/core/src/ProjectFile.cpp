#include "arranger/core/ProjectFile.h"

#include "arranger/core/SongEdit.h"
#include "arranger/core/SongForm.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace
{
    // Path text as UTF-8 (path::string() throws on Windows for names outside the ANSI code page).
    std::string pathText (const std::filesystem::path& p)
    {
        const auto u = p.u8string();
        return std::string (u.begin(), u.end());
    }
}

namespace arranger::project_file
{

using nlohmann::json;

namespace
{
    template <typename T>
    T valueOr (const json& object, const char* key, T fallback)
    {
        const auto it = object.find (key);
        if (it == object.end() || it->is_null())
            return fallback;
        try
        {
            return it->get<T>();
        }
        catch (const json::exception&)
        {
            throw Error (std::string ("Field \"") + key + "\" has the wrong type");
        }
    }

    int clamp7 (int v) { return std::clamp (v, 0, 127); }

    json pluginToJson (const PluginSlot& p)
    {
        json j = { { "format", p.format }, { "id", p.id }, { "name", p.name }, { "state", p.state }, { "bypass", p.bypass } };
        if (! p.automation.empty())
        {
            json lines = json::array();
            for (const auto& l : p.automation)
            {
                json points = json::array();
                for (const auto& pt : l.points)
                    points.push_back ({ pt.at, pt.value });
                lines.push_back ({ { "control", l.control }, { "name", l.name }, { "points", points } });
            }
            j["automation"] = lines;
        }
        return j;
    }

    json pluginsToJson (const std::vector<PluginSlot>& slots)
    {
        json out = json::array();
        for (const auto& p : slots)
            out.push_back (pluginToJson (p));
        return out;
    }

    json busesToJson (const std::vector<Bus>& buses)
    {
        json out = json::array();
        for (const auto& b : buses)
            out.push_back ({ { "id", b.id }, { "name", b.name }, { "colour", b.colour }, { "volume", b.volume }, { "pan", b.pan }, { "mute", b.mute }, { "effects", pluginsToJson (b.effects) } });
        return out;
    }

    PluginSlot pluginFromJson (const json& j)
    {
        PluginSlot p;
        if (! j.is_object())
            return p;
        p.format = valueOr<std::string> (j, "format", "");
        p.id = valueOr<std::string> (j, "id", "");
        p.name = valueOr<std::string> (j, "name", "");
        p.state = valueOr<std::string> (j, "state", "");
        p.bypass = valueOr (j, "bypass", false);
        if (const auto it = j.find ("automation"); it != j.end() && it->is_array())
            for (const auto& l : *it)
            {
                if (! l.is_object())
                    continue;
                ControlLine line;
                line.control = std::max (0, valueOr (l, "control", 0));
                line.name = valueOr<std::string> (l, "name", "");
                if (const auto pts = l.find ("points"); pts != l.end() && pts->is_array())
                    for (const auto& pt : *pts)
                        if (pt.is_array() && pt.size() == 2 && pt[0].is_number() && pt[1].is_number())
                            line.points.push_back ({ std::max<Tick> (0, pt[0].get<Tick>()), std::clamp (pt[1].get<float>(), 0.0f, 127.0f) });
                std::stable_sort (line.points.begin(), line.points.end(), [] (const auto& a, const auto& b) { return a.at < b.at; });
                p.automation.push_back (std::move (line));
            }
        return p;
    }

    std::vector<PluginSlot> pluginsFromJson (const json& object, const char* key)
    {
        std::vector<PluginSlot> out;
        if (const auto it = object.find (key); it != object.end() && it->is_array())
            for (const auto& j : *it)
                if (auto p = pluginFromJson (j); ! p.empty())
                    out.push_back (std::move (p));
        return out;
    }

    const char* outputKindName (TrackOutput::Kind k)
    {
        switch (k)
        {
            case TrackOutput::Kind::none: return "none";
            case TrackOutput::Kind::midiPort: return "midiPort";
            case TrackOutput::Kind::sampler: return "sampler";
            case TrackOutput::Kind::audio: return "audio";
            case TrackOutput::Kind::plugin: return "plugin";
            case TrackOutput::Kind::soundFont: break;
        }
        return "soundFont";
    }

    TrackOutput::Kind outputKindFromName (const std::string& s)
    {
        if (s == "none") return TrackOutput::Kind::none;
        if (s == "midiPort") return TrackOutput::Kind::midiPort;
        if (s == "sampler") return TrackOutput::Kind::sampler;
        if (s == "audio") return TrackOutput::Kind::audio;
        if (s == "plugin") return TrackOutput::Kind::plugin;
        return TrackOutput::Kind::soundFont;
    }

    const char* controlKindName (ControlEvent::Kind k)
    {
        switch (k)
        {
            case ControlEvent::Kind::pitchBend: return "bend";
            case ControlEvent::Kind::program: return "program";
            case ControlEvent::Kind::channelPressure: return "pressure";
            case ControlEvent::Kind::controller: break;
        }
        return "cc";
    }

    ControlEvent::Kind controlKindFromName (const std::string& s)
    {
        if (s == "bend") return ControlEvent::Kind::pitchBend;
        if (s == "program") return ControlEvent::Kind::program;
        if (s == "pressure") return ControlEvent::Kind::channelPressure;
        return ControlEvent::Kind::controller;
    }

    json clipToJson (const Clip& c)
    {
        // Notes as compact rows: [start, length, pitch, velocity]
        json notes = json::array();
        for (const auto& n : c.notes)
            notes.push_back ({ n.start, n.length, n.pitch, n.velocity });
        json controls = json::array();
        for (const auto& e : c.controls)
            controls.push_back ({ e.at, controlKindName (e.kind), e.number, e.value });
        json j = { { "id", c.id }, { "name", c.name }, { "start", c.start }, { "length", c.length }, { "notes", notes }, { "controls", controls } };
        if (c.loopLength > 0)
            j["loopLength"] = c.loopLength;
        return j;
    }

    Clip clipFromJson (const json& j)
    {
        if (! j.is_object())
            throw Error ("A clip must be an object");
        Clip c;
        c.id = valueOr<ClipId> (j, "id", 0);
        c.name = valueOr<std::string> (j, "name", "");
        c.start = std::max<Tick> (0, valueOr<Tick> (j, "start", 0));
        c.length = std::max<Tick> (1, valueOr<Tick> (j, "length", c.length));
        c.loopLength = std::max<Tick> (0, valueOr<Tick> (j, "loopLength", 0));

        if (const auto it = j.find ("notes"); it != j.end() && it->is_array())
            for (const auto& row : *it)
            {
                if (! row.is_array() || row.size() < 4)
                    throw Error ("A note must be [start, length, pitch, velocity]");
                c.notes.push_back ({ row[0].get<Tick>(), row[1].get<Tick>(), static_cast<std::uint8_t> (clamp7 (row[2].get<int>())),
                                    static_cast<std::uint8_t> (std::clamp (row[3].get<int>(), 1, 127)) });
            }
        if (const auto it = j.find ("controls"); it != j.end() && it->is_array())
            for (const auto& row : *it)
            {
                if (! row.is_array() || row.size() < 4)
                    throw Error ("A control event must be [time, kind, number, value]");
                c.controls.push_back ({ row[0].get<Tick>(), controlKindFromName (row[1].get<std::string>()),
                                       static_cast<std::uint8_t> (clamp7 (row[2].get<int>())), row[3].get<int>() });
            }
        edit::normalise (c);
        return c;
    }

    std::string toHex (const std::vector<std::uint8_t>& bytes)
    {
        static const char* digits = "0123456789ABCDEF";
        std::string out;
        for (const auto b : bytes)
        {
            out.push_back (digits[b >> 4]);
            out.push_back (digits[b & 15]);
        }
        return out;
    }

    std::vector<std::uint8_t> fromHex (const std::string& text)
    {
        std::vector<std::uint8_t> out;
        const auto value = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1)); };
        for (std::size_t i = 0; i + 1 < text.size(); i += 2)
        {
            const int hi = value (text[i]), lo = value (text[i + 1]);
            if (hi < 0 || lo < 0)
                throw Error ("Bad hex data in the style part of the project");
            out.push_back (static_cast<std::uint8_t> (hi * 16 + lo));
        }
        return out;
    }

    json transposeToJson (const StyleTranspose& t)
    {
        return { { "ntr", static_cast<int> (t.ntr) }, { "ntt", static_cast<int> (t.ntt) }, { "bass", t.bass }, { "highKey", t.highKey },
                 { "low", t.lowLimit }, { "high", t.highLimit }, { "rtr", static_cast<int> (t.rtr) } };
    }

    StyleTranspose transposeFromJson (const json& j)
    {
        StyleTranspose t;
        if (! j.is_object())
            return t;
        t.ntr = static_cast<StyleTranspose::Ntr> (std::clamp (valueOr (j, "ntr", 0), 0, 2));
        t.ntt = static_cast<StyleTranspose::Ntt> (std::clamp (valueOr (j, "ntt", 2), 0, 10));
        t.bass = valueOr (j, "bass", false);
        t.highKey = std::clamp (valueOr (j, "highKey", 6), 0, 11);
        t.lowLimit = std::clamp (valueOr (j, "low", 0), 0, 127);
        t.highLimit = std::clamp (valueOr (j, "high", 127), 0, 127);
        t.rtr = static_cast<StyleTranspose::Rtr> (std::clamp (valueOr (j, "rtr", 1), 0, 5));
        return t;
    }

    json styleToJson (const StyleData& d)
    {
        json setup = json::array();
        for (const auto& e : d.setup)
            setup.push_back ({ e.at, toHex (e.bytes) });
        json sets = json::array();
        for (const auto& set : d.ruleSets)
        {
            json rules = json::array();
            for (const auto& r : set.rules)
                rules.push_back ({ { "source", r.sourceChannel }, { "name", r.name }, { "dest", r.destChannel }, { "editable", r.editable },
                                   { "noteMute", r.noteMute }, { "chordMute", r.chordMute }, { "root", r.sourceRoot }, { "chord", r.sourceChordType },
                                   { "middle", transposeToJson (r.transpose) }, { "middleLow", r.middleLow }, { "middleHigh", r.middleHigh },
                                   { "lowRange", transposeToJson (r.lowRange) }, { "highRange", transposeToJson (r.highRange) },
                                   { "special", toHex (r.special) } });
            sets.push_back ({ { "sections", set.sections }, { "rules", rules }, { "extra", toHex (set.extra) } });
        }
        json sectionEvents = json::object();
        for (const auto& [name, events] : d.sectionEvents)
        {
            json list = json::array();
            for (const auto& e : events)
                list.push_back ({ e.at, toHex (e.bytes) });
            sectionEvents[name] = std::move (list);
        }
        return { { "format", d.format }, { "setup", setup }, { "ruleSets", sets }, { "sectionEvents", sectionEvents }, { "extra", toHex (d.extra) } };
    }

    StyleData styleFromJson (const json& j)
    {
        StyleData d;
        d.format = std::clamp (valueOr (j, "format", 2), 1, 2);
        if (const auto it = j.find ("setup"); it != j.end() && it->is_array())
            for (const auto& e : *it)
                if (e.is_array() && e.size() == 2 && e[1].is_string())
                    d.setup.push_back ({ e[0].get<Tick>(), fromHex (e[1].get<std::string>()) });
        if (const auto it = j.find ("ruleSets"); it != j.end() && it->is_array())
            for (const auto& s : *it)
            {
                StyleRuleSet set;
                if (const auto names = s.find ("sections"); names != s.end() && names->is_array())
                    for (const auto& n : *names)
                        if (n.is_string())
                            set.sections.push_back (n.get<std::string>());
                if (const auto rules = s.find ("rules"); rules != s.end() && rules->is_array())
                    for (const auto& rj : *rules)
                    {
                        if (! rj.is_object())
                            continue;
                        StyleChannelRule r;
                        r.sourceChannel = std::clamp (valueOr (rj, "source", 9), 1, 16);
                        r.name = valueOr<std::string> (rj, "name", "");
                        r.destChannel = std::clamp (valueOr (rj, "dest", r.sourceChannel), 1, 16);
                        r.editable = valueOr (rj, "editable", true);
                        r.noteMute = static_cast<std::uint16_t> (valueOr<std::uint32_t> (rj, "noteMute", 0x0fff) & 0x0fff);
                        r.chordMute = valueOr<std::uint64_t> (rj, "chordMute", r.chordMute);
                        r.sourceRoot = std::clamp (valueOr (rj, "root", 0), 0, 11);
                        r.sourceChordType = std::clamp (valueOr (rj, "chord", 2), 0, 255);
                        if (const auto m = rj.find ("middle"); m != rj.end()) r.transpose = transposeFromJson (*m);
                        if (const auto m = rj.find ("lowRange"); m != rj.end()) r.lowRange = transposeFromJson (*m);
                        if (const auto m = rj.find ("highRange"); m != rj.end()) r.highRange = transposeFromJson (*m);
                        r.middleLow = std::clamp (valueOr (rj, "middleLow", 0), 0, 127);
                        r.middleHigh = std::clamp (valueOr (rj, "middleHigh", 127), 0, 127);
                        r.special = fromHex (valueOr<std::string> (rj, "special", ""));
                        set.rules.push_back (std::move (r));
                    }
                set.extra = fromHex (valueOr<std::string> (s, "extra", ""));
                d.ruleSets.push_back (std::move (set));
            }
        if (const auto it = j.find ("sectionEvents"); it != j.end() && it->is_object())
            for (const auto& [name, list] : it->items())
                if (list.is_array())
                    for (const auto& e : list)
                        if (e.is_array() && e.size() == 2 && e[1].is_string())
                            d.sectionEvents[name].push_back ({ e[0].get<Tick>(), fromHex (e[1].get<std::string>()) });
        d.extra = fromHex (valueOr<std::string> (j, "extra", ""));
        return d;
    }

    json trackToJson (const Track& t)
    {
        json clips = json::array();
        for (const auto& c : t.clips)
            clips.push_back (clipToJson (*c));

        json kit;
        if (t.output.kind == TrackOutput::Kind::sampler || ! t.kit.pads.empty())
        {
            json pads = json::array();
            for (const auto& p : t.kit.pads)
                pads.push_back ({ { "name", p.name }, { "note", p.note }, { "file", p.sampleFile }, { "start", p.sliceStart }, { "end", p.sliceEnd },
                                  { "gainDb", p.gainDb }, { "pan", p.pan }, { "tune", p.tuneSemitones }, { "choke", p.chokeGroup }, { "oneShot", p.oneShot } });
            kit = { { "name", t.kit.name }, { "pads", pads } };
        }

        json slots = json::array();
        {
            auto used = t.slots.size();
            while (used > 0 && t.slots[used - 1] == nullptr)
                --used;
            for (std::size_t i = 0; i < used; ++i)
                slots.push_back (t.slots[i] != nullptr ? clipToJson (*t.slots[i]) : json (nullptr));
        }

        json j = {
            { "id", t.id },
            { "name", t.name },
            { "colour", t.colour },
            { "output", { { "kind", outputKindName (t.output.kind) }, { "port", t.output.port }, { "channel", t.output.channel } } },
            { "sound", { { "name", t.sound.name }, { "bankMsb", t.sound.bankMsb }, { "bankLsb", t.sound.bankLsb }, { "program", t.sound.program } } },
            { "mixer",
              { { "volume", t.mixer.volume }, { "pan", t.mixer.pan }, { "reverb", t.mixer.reverbSend }, { "chorus", t.mixer.chorusSend },
                { "mute", t.mixer.mute }, { "solo", t.mixer.solo } } },
            { "armed", t.armed },
            { "inputPort", t.inputPort },
            { "inputChannel", t.inputChannel },
            { "clips", clips },
        };
        if (! kit.is_null())
            j["kit"] = kit;
        if (t.audioInputChannels > 0)
            j["audioInput"] = { { "first", t.audioInput }, { "channels", t.audioInputChannels } };
        if (! slots.empty())
            j["slots"] = slots;
        if (t.styleChannel > 0)
            j["styleChannel"] = t.styleChannel;
        const auto audioToJson = [] (const AudioClip& c) {
            json a { { "id", c.id }, { "name", c.name }, { "file", c.file }, { "start", c.start }, { "length", c.length },
                     { "offset", c.offset }, { "gainDb", c.gainDb }, { "fadeIn", c.fadeIn }, { "fadeOut", c.fadeOut }, { "bpm", c.bpm } };
            if (c.layer != 0)
                a["layer"] = c.layer;
            return a;
        };
        if (! t.audio.empty())
        {
            json audio = json::array();
            for (const auto& c : t.audio)
                audio.push_back (audioToJson (*c));
            j["audio"] = audio;
        }
        {
            auto used = t.audioSlots.size();
            while (used > 0 && t.audioSlots[used - 1] == nullptr)
                --used;
            json audioSlots = json::array();
            for (std::size_t i = 0; i < used; ++i)
                audioSlots.push_back (t.audioSlots[i] != nullptr ? audioToJson (*t.audioSlots[i]) : json (nullptr));
            if (used > 0)
                j["audioSlots"] = audioSlots;
        }
        const auto lineToJson = [] (const AutomationLine& line) {
            json out = json::array();
            for (const auto& p : line)
                out.push_back ({ p.at, p.value });
            return out;
        };
        if (! t.volumeAutomation.empty() || ! t.panAutomation.empty() || ! t.reverbAutomation.empty() || ! t.chorusAutomation.empty())
        {
            j["automation"] = { { "volume", lineToJson (t.volumeAutomation) }, { "pan", lineToJson (t.panAutomation) } };
            if (! t.reverbAutomation.empty())
                j["automation"]["reverb"] = lineToJson (t.reverbAutomation);
            if (! t.chorusAutomation.empty())
                j["automation"]["chorus"] = lineToJson (t.chorusAutomation);
        }
        if (! t.instrument.empty())
            j["instrument"] = pluginToJson (t.instrument);
        if (! t.effects.empty())
            j["effects"] = pluginsToJson (t.effects);
        if (t.bus != 0)
            j["bus"] = t.bus;
        if (! t.sends.empty())
        {
            json sends = json::array();
            for (const auto& s : t.sends)
                sends.push_back ({ { "bus", s.bus }, { "level", s.level } });
            j["sends"] = sends;
        }
        if (! t.marks.empty())
        {
            static constexpr const char* kKinds[] = { "chord", "lyric", "dynamic", "articulation" };
            json marks = json::array();
            for (const auto& m : t.marks)
                marks.push_back ({ { "kind", kKinds[static_cast<int> (m.kind)] }, { "at", m.at }, { "text", m.text } });
            j["marks"] = marks;
        }
        return j;
    }

    Track trackFromJson (const json& j, int fileVersion)
    {
        if (! j.is_object())
            throw Error ("A track must be an object");

        Track t;
        t.id = valueOr<TrackId> (j, "id", 0);
        t.name = valueOr (j, "name", t.name);
        t.styleChannel = std::clamp (valueOr (j, "styleChannel", 0), 0, 16);
        if (const auto it = j.find ("instrument"); it != j.end())
            t.instrument = pluginFromJson (*it);
        if (const auto it = j.find ("automation"); it != j.end() && it->is_object())
        {
            const auto lineFromJson = [&] (const char* key) {
                AutomationLine line;
                if (const auto l = it->find (key); l != it->end() && l->is_array())
                    for (const auto& p : *l)
                        if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number())
                            line.push_back ({ std::max<Tick> (0, p[0].get<Tick>()), std::clamp (p[1].get<float>(), 0.0f, 127.0f) });
                std::stable_sort (line.begin(), line.end(), [] (const auto& a, const auto& b) { return a.at < b.at; });
                return line;
            };
            t.volumeAutomation = lineFromJson ("volume");
            t.panAutomation = lineFromJson ("pan");
            t.reverbAutomation = lineFromJson ("reverb");
            t.chorusAutomation = lineFromJson ("chorus");
        }
        t.effects = pluginsFromJson (j, "effects");
        t.bus = valueOr<std::uint32_t> (j, "bus", 0);
        if (const auto it = j.find ("sends"); it != j.end() && it->is_array())
            for (const auto& s : *it)
                if (s.is_object())
                    t.sends.push_back ({ valueOr<std::uint32_t> (s, "bus", 0), std::clamp (valueOr (s, "level", 100), 0, 127) });
        const auto audioFromJson = [] (const json& a) {
            AudioClip c;
            c.id = valueOr<ClipId> (a, "id", 0);
            c.name = valueOr<std::string> (a, "name", "");
            c.file = valueOr<std::string> (a, "file", "");
            c.start = std::max<Tick> (0, valueOr<Tick> (a, "start", 0));
            c.length = std::max<Tick> (1, valueOr<Tick> (a, "length", kTicksPerQuarter * 4));
            c.offset = std::max (0.0, valueOr (a, "offset", 0.0));
            c.gainDb = std::clamp (valueOr (a, "gainDb", 0.0f), -60.0f, 24.0f);
            c.fadeIn = std::clamp<Tick> (valueOr<Tick> (a, "fadeIn", 0), 0, c.length);
            c.fadeOut = std::clamp<Tick> (valueOr<Tick> (a, "fadeOut", 0), 0, c.length);
            c.bpm = valueOr (a, "bpm", 0.0);
            c.layer = std::clamp (valueOr (a, "layer", 0), 0, 1000000);
            if (! (c.bpm >= 20.0 && c.bpm <= 400.0))
                c.bpm = 0.0;
            return c;
        };
        if (const auto audio = j.find ("audio"); audio != j.end() && audio->is_array())
            for (const auto& a : *audio)
                if (a.is_object())
                    t.audio.push_back (std::make_shared<const AudioClip> (audioFromJson (a)));
        if (const auto slots = j.find ("audioSlots"); slots != j.end() && slots->is_array())
            for (const auto& a : *slots)
            {
                if (! a.is_object())
                {
                    t.audioSlots.push_back (nullptr);
                    continue;
                }
                auto c = audioFromJson (a);
                c.start = 0;
                t.audioSlots.push_back (std::make_shared<const AudioClip> (std::move (c)));
            }
        if (const auto marks = j.find ("marks"); marks != j.end() && marks->is_array())
            for (const auto& m : *marks)
            {
                if (! m.is_object())
                    continue;
                ScoreMark mark;
                const auto kind = valueOr<std::string> (m, "kind", "chord");
                mark.kind = kind == "lyric" ? ScoreMark::Kind::lyric : kind == "dynamic" ? ScoreMark::Kind::dynamic
                          : kind == "articulation" ? ScoreMark::Kind::articulation : ScoreMark::Kind::chord;
                mark.at = std::max<Tick> (0, valueOr<Tick> (m, "at", 0));
                mark.text = valueOr<std::string> (m, "text", "");
                if (! mark.text.empty())
                    t.marks.push_back (std::move (mark));
            }
        t.colour = valueOr (j, "colour", t.colour);
        t.armed = valueOr (j, "armed", false);
        t.inputPort = valueOr<std::string> (j, "inputPort", "");
        t.inputChannel = std::clamp (valueOr (j, "inputChannel", 0), 0, 16);
        if (const auto it = j.find ("audioInput"); it != j.end() && it->is_object())
        {
            t.audioInput = std::clamp (valueOr (*it, "first", 0), 0, 255);
            t.audioInputChannels = std::clamp (valueOr (*it, "channels", 0), 0, 2);
        }

        if (fileVersion == 1)
            t.output.channel = std::clamp (valueOr (j, "midiChannel", 1), 1, 16);

        if (const auto it = j.find ("output"); it != j.end() && it->is_object())
        {
            t.output.kind = outputKindFromName (valueOr<std::string> (*it, "kind", "soundFont"));
            t.output.port = valueOr<std::string> (*it, "port", "");
            t.output.channel = std::clamp (valueOr (*it, "channel", 1), 1, 16);
        }
        if (const auto it = j.find ("sound"); it != j.end() && it->is_object())
        {
            t.sound.name = valueOr<std::string> (*it, "name", "");
            t.sound.bankMsb = std::clamp (valueOr (*it, "bankMsb", -1), -1, 127);
            t.sound.bankLsb = std::clamp (valueOr (*it, "bankLsb", -1), -1, 127);
            t.sound.program = std::clamp (valueOr (*it, "program", -1), -1, 127);
        }
        if (const auto it = j.find ("mixer"); it != j.end() && it->is_object())
        {
            auto& m = t.mixer;
            m.volume = clamp7 (valueOr (*it, "volume", m.volume));
            m.pan = clamp7 (valueOr (*it, "pan", m.pan));
            m.reverbSend = clamp7 (valueOr (*it, "reverb", m.reverbSend));
            m.chorusSend = clamp7 (valueOr (*it, "chorus", m.chorusSend));
            m.mute = valueOr (*it, "mute", false);
            m.solo = valueOr (*it, "solo", false);
        }
        if (const auto it = j.find ("slots"); it != j.end() && it->is_array())
            for (const auto& c : *it)
            {
                if (c.is_null())
                {
                    t.slots.push_back (nullptr);
                    continue;
                }
                auto clip = clipFromJson (c);
                clip.start = 0;
                t.slots.push_back (std::make_shared<const Clip> (std::move (clip)));
            }
        if (const auto it = j.find ("kit"); it != j.end() && it->is_object())
        {
            t.kit.name = valueOr<std::string> (*it, "name", "");
            if (const auto pads = it->find ("pads"); pads != it->end() && pads->is_array())
                for (const auto& p : *pads)
                {
                    if (! p.is_object())
                        throw Error ("A kit pad must be an object");
                    KitPad pad;
                    pad.name = valueOr<std::string> (p, "name", "");
                    pad.note = clamp7 (valueOr (p, "note", 36));
                    pad.sampleFile = valueOr<std::string> (p, "file", "");
                    pad.sliceStart = valueOr<std::uint64_t> (p, "start", 0);
                    pad.sliceEnd = valueOr<std::uint64_t> (p, "end", 0);
                    pad.gainDb = std::clamp (valueOr (p, "gainDb", 0.0f), -60.0f, 24.0f);
                    pad.pan = std::clamp (valueOr (p, "pan", 0.0f), -1.0f, 1.0f);
                    pad.tuneSemitones = std::clamp (valueOr (p, "tune", 0.0f), -48.0f, 48.0f);
                    pad.chokeGroup = std::clamp (valueOr (p, "choke", 0), 0, 16);
                    pad.oneShot = valueOr (p, "oneShot", true);
                    t.kit.pads.push_back (std::move (pad));
                }
        }
        if (const auto it = j.find ("clips"); it != j.end() && it->is_array())
        {
            for (const auto& c : *it)
                t.clips.push_back (std::make_shared<const Clip> (clipFromJson (c)));
            std::stable_sort (t.clips.begin(), t.clips.end(), [] (auto& a, auto& b) { return a->start < b->start; });
        }
        return t;
    }
}

std::string toJson (const Song& song)
{
    json tracks = json::array();
    for (const auto& t : song.tracks)
        tracks.push_back (trackToJson (t));

    json tempo = json::array();
    for (const auto& p : song.tempo.getPoints())
        tempo.push_back ({ p.at, p.bpm });

    json root = {
        { "format", kFormatName },
        { "version", kVersion },
        { "song",
          {
              { "title", song.title },
              { "tempo", tempo },
              { "timeSignature", { song.timeSignature.numerator, song.timeSignature.denominator } },
              { "key", song.key },
              { "guitarChords", song.guitarChords },
              { "loop", { { "enabled", song.loop.enabled }, { "start", song.loop.start }, { "end", song.loop.end } } },
              { "soundFont", song.soundFont },
              { "scenes", song.scenes },
              { "launchQuantise", song.launchQuantise },
              { "master", { { "gainDb", song.master.gainDb }, { "effects", pluginsToJson (song.master.effects) } } },
              { "buses", busesToJson (song.buses) },
              { "nextId", song.nextId },
              { "metronome",
                {
                    { "enabled", song.metronome.enabled },
                    { "level", song.metronome.level },
                    { "accentDownbeat", song.metronome.accentDownbeat },
                } },
              { "tracks", tracks },
          } },
    };

    if (song.style)
        root["song"]["style"] = styleToJson (*song.style);
    if (! song.sections.empty())
    {
        json sections = json::array();
        for (const auto& sec : song.sections)
        {
            json j = { { "name", sec.name }, { "start", sec.start }, { "end", sec.end } };
            if (sec.scene >= 0)
                j["scene"] = sec.scene;
            sections.push_back (j);
        }
        root["song"]["sections"] = sections;
    }
    if (! song.forms.empty())
    {
        json forms = json::array();
        for (const auto& f : song.forms)
        {
            json parts = json::array();
            for (const auto& p : f.parts)
            {
                json j = { { "section", p.section } };
                if (p.bars != 0)
                    j["bars"] = p.bars;
                if (! p.muted.empty())
                    j["muted"] = p.muted;
                if (p.into != Transition::automatic)
                    j["into"] = form::transitionId (p.into);
                if (! p.label.empty())
                    j["label"] = p.label;
                parts.push_back (j);
            }
            forms.push_back ({ { "name", f.name }, { "ending", form::transitionId (f.ending) }, { "parts", parts } });
        }
        root["song"]["forms"] = forms;
    }
    return root.dump (2) + "\n";
}

Song fromJson (std::string_view text)
{
    json root;
    try
    {
        root = json::parse (text);
    }
    catch (const json::parse_error& e)
    {
        throw Error (std::string ("Not valid JSON: ") + e.what());
    }

    if (! root.is_object() || valueOr<std::string> (root, "format", "") != kFormatName)
        throw Error ("Not a Lazy Arranger Midi DAW project");

    const int version = valueOr (root, "version", 0);
    if (version < 1)
        throw Error ("Project file has no valid version");
    if (version > kVersion)
        throw Error ("Project was saved by a newer version of the app (file version " + std::to_string (version) + ")");

    const auto songIt = root.find ("song");
    if (songIt == root.end() || ! songIt->is_object())
        throw Error ("Project has no song");
    const json& s = *songIt;

    try
    {
        Song song;
        song.title = valueOr (s, "title", song.title);
        song.key = valueOr (s, "key", song.key);
        song.guitarChords = valueOr (s, "guitarChords", false);
        song.soundFont = valueOr<std::string> (s, "soundFont", "");
        song.launchQuantise = std::max<Tick> (0, valueOr<Tick> (s, "launchQuantise", song.launchQuantise));
        if (const auto it = s.find ("style"); it != s.end() && it->is_object())
            song.style = styleFromJson (*it);
        if (const auto it = s.find ("scenes"); it != s.end() && it->is_array())
            for (const auto& name : *it)
                song.scenes.push_back (name.is_string() ? name.get<std::string>() : std::string());

        if (const auto it = s.find ("tempo"); it != s.end() && it->is_array())
        {
            std::vector<TempoPoint> points;
            for (const auto& p : *it)
                if (p.is_array() && p.size() == 2)
                    points.push_back ({ p[0].get<Tick>(), p[1].get<double>() });
            song.tempo.setPoints (points);
        }
        else
        {
            song.tempo = TempoMap (valueOr (s, "tempoBpm", 120.0)); // version 1
        }

        if (const auto it = s.find ("timeSignature"); it != s.end())
        {
            if (! it->is_array() || it->size() != 2 || ! (*it)[0].is_number_integer() || ! (*it)[1].is_number_integer())
                throw Error ("timeSignature must be [numerator, denominator]");
            TimeSignature sig { (*it)[0].get<int>(), (*it)[1].get<int>() };
            if (! sig.isValid())
                throw Error ("Unsupported time signature");
            song.timeSignature = sig;
        }

        if (const auto it = s.find ("loop"); it != s.end() && it->is_object())
        {
            song.loop.enabled = valueOr (*it, "enabled", false);
            song.loop.start = valueOr<Tick> (*it, "start", 0);
            song.loop.end = valueOr<Tick> (*it, "end", song.loop.end);
            if (! song.loop.isValid())
                song.loop = {};
        }

        if (const auto it = s.find ("master"); it != s.end() && it->is_object())
        {
            song.master.gainDb = std::clamp (valueOr (*it, "gainDb", 0.0f), -60.0f, 12.0f);
            song.master.effects = pluginsFromJson (*it, "effects");
        }

        if (const auto it = s.find ("buses"); it != s.end() && it->is_array())
            for (const auto& b : *it)
            {
                if (! b.is_object())
                    continue;
                Bus bus;
                bus.id = valueOr<std::uint32_t> (b, "id", 0);
                bus.name = valueOr<std::string> (b, "name", "Bus");
                bus.colour = valueOr<std::string> (b, "colour", "blue");
                bus.volume = std::clamp (valueOr (b, "volume", 100), 0, 127);
                bus.pan = std::clamp (valueOr (b, "pan", 64), 0, 127);
                bus.mute = valueOr (b, "mute", false);
                bus.effects = pluginsFromJson (b, "effects");
                song.buses.push_back (std::move (bus));
            }

        if (const auto it = s.find ("metronome"); it != s.end() && it->is_object())
        {
            auto& m = song.metronome;
            m.enabled = valueOr (*it, "enabled", m.enabled);
            m.level = std::clamp (valueOr (*it, "level", m.level), 0.0f, 1.0f);
            m.accentDownbeat = valueOr (*it, "accentDownbeat", m.accentDownbeat);
        }

        if (const auto it = s.find ("tracks"); it != s.end())
        {
            if (! it->is_array())
                throw Error ("tracks must be a list");
            for (const auto& t : *it)
                song.tracks.push_back (trackFromJson (t, version));
        }

        if (const auto it = s.find ("sections"); it != s.end() && it->is_array())
            for (const auto& j : *it)
            {
                if (! j.is_object())
                    continue;
                SongSection sec;
                sec.name = valueOr<std::string> (j, "name", "");
                sec.start = std::max<Tick> (0, valueOr<Tick> (j, "start", 0));
                sec.end = std::max<Tick> (sec.start, valueOr<Tick> (j, "end", 0));
                sec.scene = std::max (-1, valueOr (j, "scene", -1));
                if (! sec.name.empty() && sec.end > sec.start)
                    song.sections.push_back (sec);
            }
        if (const auto it = s.find ("forms"); it != s.end() && it->is_array())
            for (const auto& j : *it)
            {
                if (! j.is_object())
                    continue;
                SongForm f;
                f.name = valueOr<std::string> (j, "name", f.name);
                f.ending = form::parseTransition (valueOr<std::string> (j, "ending", "automatic")).value_or (Transition::automatic);
                if (const auto parts = j.find ("parts"); parts != j.end() && parts->is_array())
                    for (const auto& pj : *parts)
                    {
                        if (! pj.is_object())
                            continue;
                        FormPart p;
                        p.section = valueOr<std::string> (pj, "section", "");
                        p.bars = valueOr (pj, "bars", 0);
                        if (const auto m = pj.find ("muted"); m != pj.end() && m->is_array())
                            for (const auto& id : *m)
                                if (id.is_number_unsigned())
                                    p.muted.push_back (id.get<TrackId>());
                        p.into = form::parseTransition (valueOr<std::string> (pj, "into", "automatic")).value_or (Transition::automatic);
                        p.label = valueOr<std::string> (pj, "label", "");
                        if (! p.section.empty())
                            f.parts.push_back (p);
                    }
                song.forms.push_back (f);
            }

        // Make sure every track and clip has a unique id, and nextId is past all of them.
        std::uint32_t maxId = 0;
        for (const auto& b : song.buses)
            maxId = std::max (maxId, b.id);
        for (const auto& t : song.tracks)
        {
            maxId = std::max (maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max (maxId, c->id);
            for (const auto& c : t.slots)
                if (c != nullptr)
                    maxId = std::max (maxId, c->id);
            for (const auto& c : t.audio)
                maxId = std::max (maxId, c->id);
            for (const auto& c : t.audioSlots)
                if (c != nullptr)
                    maxId = std::max (maxId, c->id);
        }
        song.nextId = std::max (valueOr<std::uint32_t> (s, "nextId", 1), maxId + 1);
        std::vector<std::uint32_t> seen;
        for (auto& b : song.buses)
        {
            const auto old = b.id;
            if (b.id == 0 || std::find (seen.begin(), seen.end(), b.id) != seen.end())
                b.id = song.nextId++;
            seen.push_back (b.id);
            if (old != b.id) // a broken file: its tracks can't know the new number
                for (auto& t : song.tracks)
                {
                    if (t.bus == old)
                        t.bus = 0;
                    std::erase_if (t.sends, [old] (const BusSend& x) { return x.bus == old; });
                }
        }
        // Tracks keep only buses that are there, and only tracks that make sound go to buses.
        for (auto& t : song.tracks)
        {
            const auto there = [&] (std::uint32_t id) { return std::any_of (song.buses.begin(), song.buses.end(), [id] (const Bus& b) { return b.id == id; }); };
            const bool sound = t.output.kind == TrackOutput::Kind::soundFont || t.output.kind == TrackOutput::Kind::sampler
                            || t.output.kind == TrackOutput::Kind::plugin || t.output.kind == TrackOutput::Kind::audio;
            if (! sound || ! there (t.bus))
                t.bus = 0;
            std::erase_if (t.sends, [&] (const BusSend& x) { return ! sound || ! there (x.bus) || x.level <= 0; });
        }
        for (auto& t : song.tracks)
        {
            if (t.id == 0 || std::find (seen.begin(), seen.end(), t.id) != seen.end())
                t.id = song.nextId++;
            seen.push_back (t.id);
            for (auto& c : t.clips)
                if (c->id == 0 || std::find (seen.begin(), seen.end(), c->id) != seen.end())
                {
                    auto copy = std::make_shared<Clip> (*c);
                    copy->id = song.nextId++;
                    c = std::move (copy);
                    seen.push_back (c->id);
                }
                else
                {
                    seen.push_back (c->id);
                }
            for (auto& c : t.slots)
            {
                if (c == nullptr)
                    continue;
                if (c->id == 0 || std::find (seen.begin(), seen.end(), c->id) != seen.end())
                {
                    auto copy = std::make_shared<Clip> (*c);
                    copy->id = song.nextId++;
                    c = std::move (copy);
                }
                seen.push_back (c->id);
            }
            const auto uniqueAudio = [&] (AudioClipPtr& c) {
                if (c == nullptr)
                    return;
                if (c->id == 0 || std::find (seen.begin(), seen.end(), c->id) != seen.end())
                {
                    auto copy = std::make_shared<AudioClip> (*c);
                    copy->id = song.nextId++;
                    c = std::move (copy);
                }
                seen.push_back (c->id);
            };
            for (auto& c : t.audio)
                uniqueAudio (c);
            for (auto& c : t.audioSlots)
                uniqueAudio (c);
        }
        return song;
    }
    catch (const json::exception& e)
    {
        throw Error (std::string ("Project file is damaged: ") + e.what());
    }
}

namespace
{
    std::filesystem::path utf8Path (const std::string& s) { return std::filesystem::path (std::u8string (s.begin(), s.end())); }
    std::string utf8String (const std::u8string& s) { return std::string (s.begin(), s.end()); }

    /** Rewrites the file path of every audio clip with fn. */
    template <typename Fn>
    void mapAudioPaths (Song& song, Fn&& fn)
    {
        const auto remap = [&] (AudioClipPtr& c) {
            if (c == nullptr)
                return;
            auto file = fn (c->file);
            if (file == c->file)
                return;
            auto copy = std::make_shared<AudioClip> (*c);
            copy->file = std::move (file);
            c = std::move (copy);
        };
        for (auto& t : song.tracks)
        {
            for (auto& c : t.audio)
                remap (c);
            for (auto& c : t.audioSlots)
                remap (c);
        }
    }
} // namespace

void save (const Song& original, const std::filesystem::path& path)
{
    namespace fs = std::filesystem;

    // Audio files in the project's folder (or below it) are stored relative to
    // it, so the folder can move to another computer and still open.
    Song song = original;
    std::error_code pathError;
    const auto folder = fs::weakly_canonical (fs::absolute (path, pathError).parent_path(), pathError);
    mapAudioPaths (song, [&] (const std::string& file) -> std::string {
        if (file.empty() || folder.empty())
            return file;
        std::error_code ec;
        const auto p = utf8Path (file);
        if (! p.is_absolute())
            return file;
        const auto rel = fs::weakly_canonical (p, ec).lexically_relative (folder);
        if (ec || rel.empty() || *rel.begin() == "..")
            return file;
        return utf8String (rel.generic_u8string());
    });

    auto temp = path;
    temp += ".saving";

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
            throw Error ("Can't write " + pathText (temp));
        out << toJson (song);
        out.flush();
        if (! out)
            throw Error ("Write failed for " + pathText (temp));
    }

    std::error_code ec;
    fs::rename (temp, path, ec);
    if (ec)
    {
        fs::remove (temp, ec);
        throw Error ("Can't replace " + pathText (path));
    }
}

Song load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open " + pathText (path));

    std::stringstream buffer;
    buffer << in.rdbuf();
    auto song = fromJson (buffer.str());

    // Relative audio paths are relative to the project's folder.
    std::error_code ec;
    const auto folder = std::filesystem::absolute (path, ec).parent_path();
    mapAudioPaths (song, [&] (const std::string& file) -> std::string {
        const auto p = utf8Path (file);
        if (file.empty() || p.is_absolute())
            return file;
        return utf8String ((folder / p).lexically_normal().u8string());
    });
    return song;
}

} // namespace arranger::project_file
