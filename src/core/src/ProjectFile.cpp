#include "arranger/core/ProjectFile.h"

#include "arranger/core/SongEdit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

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

    const char* outputKindName (TrackOutput::Kind k)
    {
        switch (k)
        {
            case TrackOutput::Kind::none: return "none";
            case TrackOutput::Kind::midiPort: return "midiPort";
            case TrackOutput::Kind::sampler: return "sampler";
            case TrackOutput::Kind::soundFont: break;
        }
        return "soundFont";
    }

    TrackOutput::Kind outputKindFromName (const std::string& s)
    {
        if (s == "none") return TrackOutput::Kind::none;
        if (s == "midiPort") return TrackOutput::Kind::midiPort;
        if (s == "sampler") return TrackOutput::Kind::sampler;
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
            { "clips", clips },
        };
        if (! kit.is_null())
            j["kit"] = kit;
        if (! slots.empty())
            j["slots"] = slots;
        return j;
    }

    Track trackFromJson (const json& j, int fileVersion)
    {
        if (! j.is_object())
            throw Error ("A track must be an object");

        Track t;
        t.id = valueOr<TrackId> (j, "id", 0);
        t.name = valueOr (j, "name", t.name);
        t.colour = valueOr (j, "colour", t.colour);
        t.armed = valueOr (j, "armed", false);
        t.inputPort = valueOr<std::string> (j, "inputPort", "");

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

    const json root = {
        { "format", kFormatName },
        { "version", kVersion },
        { "song",
          {
              { "title", song.title },
              { "tempo", tempo },
              { "timeSignature", { song.timeSignature.numerator, song.timeSignature.denominator } },
              { "key", song.key },
              { "loop", { { "enabled", song.loop.enabled }, { "start", song.loop.start }, { "end", song.loop.end } } },
              { "soundFont", song.soundFont },
              { "scenes", song.scenes },
              { "launchQuantise", song.launchQuantise },
              { "master", { { "gainDb", song.master.gainDb } } },
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
        throw Error ("Not an Arranger MIDI DAW project");

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
        song.soundFont = valueOr<std::string> (s, "soundFont", "");
        song.launchQuantise = std::max<Tick> (0, valueOr<Tick> (s, "launchQuantise", song.launchQuantise));
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
            song.master.gainDb = std::clamp (valueOr (*it, "gainDb", 0.0f), -60.0f, 12.0f);

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

        // Make sure every track and clip has a unique id, and nextId is past all of them.
        std::uint32_t maxId = 0;
        for (const auto& t : song.tracks)
        {
            maxId = std::max (maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max (maxId, c->id);
            for (const auto& c : t.slots)
                if (c != nullptr)
                    maxId = std::max (maxId, c->id);
        }
        song.nextId = std::max (valueOr<std::uint32_t> (s, "nextId", 1), maxId + 1);
        std::vector<std::uint32_t> seen;
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
        }
        return song;
    }
    catch (const json::exception& e)
    {
        throw Error (std::string ("Project file is damaged: ") + e.what());
    }
}

void save (const Song& song, const std::filesystem::path& path)
{
    namespace fs = std::filesystem;

    auto temp = path;
    temp += ".saving";

    {
        std::ofstream out (temp, std::ios::binary | std::ios::trunc);
        if (! out)
            throw Error ("Can't write " + temp.string());
        out << toJson (song);
        out.flush();
        if (! out)
            throw Error ("Write failed for " + temp.string());
    }

    std::error_code ec;
    fs::rename (temp, path, ec);
    if (ec)
    {
        fs::remove (temp, ec);
        throw Error ("Can't replace " + path.string());
    }
}

Song load (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw Error ("Can't open " + path.string());

    std::stringstream buffer;
    buffer << in.rdbuf();
    return fromJson (buffer.str());
}

} // namespace arranger::project_file
