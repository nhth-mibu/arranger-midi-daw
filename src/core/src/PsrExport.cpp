#include "arranger/core/PsrExport.h"

#include "arranger/core/GeneralMidi.h"
#include "arranger/core/MidiFile.h"
#include "arranger/core/Notation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <set>

namespace arranger::psr_export
{

namespace
{
    std::string lower (const std::string& s)
    {
        std::string out;
        for (char c : s)
            out += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    std::string squash (const std::string& s)
    {
        std::string out;
        for (char c : s)
            if (std::isalnum (static_cast<unsigned char> (c)))
                out += static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    /** "Pads > Fat Warm Pad" -> "Fat Warm Pad". */
    std::string soundName (const TrackSound& s)
    {
        const auto at = s.name.rfind ('>');
        auto name = at == std::string::npos ? s.name : s.name.substr (at + 1);
        while (! name.empty() && name.front() == ' ')
            name.erase (name.begin());
        return name;
    }

    /** The keyboard's own panel voices, not the Legacy, Mega Voice, XG and GM2 lists. */
    bool isPanelGroup (const std::string& group)
    {
        for (const char* prefix : { "Legacy", "Mega", "XG", "GM2", "DrumKit", "OrganFlutes" })
            if (group.rfind (prefix, 0) == 0)
                return false;
        return true;
    }

    /** True when the track's bank and program mean General MIDI sounds. */
    bool usesGmSounds (const Track& t, const DeviceLibrary& library)
    {
        if (t.sound.program < 0)
            return false;
        switch (t.output.kind)
        {
            case TrackOutput::Kind::soundFont: return true;
            case TrackOutput::Kind::midiPort: return library.forPort (t.output.port).dialect != "none";
            default: return false;
        }
    }

    /** Letters of the name that also appear as words in the voice name: a rough "how alike". */
    int likeness (const std::string& voice, const std::string& name)
    {
        const auto v = squash (voice);
        int score = 0;
        std::string word;
        const auto take = [&] {
            if (word.size() >= 3 && v.find (word) != std::string::npos)
                score += static_cast<int> (word.size());
            word.clear();
        };
        for (char c : lower (name))
        {
            if (std::isalnum (static_cast<unsigned char> (c)))
                word += c;
            else
                take();
        }
        take();
        return score;
    }

    struct Choice
    {
        TrackSound voice;
        std::string how;
    };

    std::optional<Choice> sameName (const DeviceProfile& psr, const std::string& name)
    {
        const auto wanted = squash (name);
        if (wanted.empty())
            return std::nullopt;
        for (const auto& g : psr.groups)
            for (const auto& s : g.sounds)
                if (squash (s.name) == wanted && g.name.find ("Drum") == std::string::npos)
                    return Choice { psr.trackSound (g, s), "same name" };
        return std::nullopt;
    }

    Choice forGmProgram (const DeviceProfile& psr, int program, const std::string& name, const std::string& how)
    {
        const SoundGroup* bestGroup = nullptr;
        const ProfileSound* best = nullptr;
        int bestScore = -1;
        for (const auto& g : psr.groups)
        {
            if (! isPanelGroup (g.name))
                continue;
            for (const auto& s : g.sounds)
                if (s.program == program)
                    if (const int score = likeness (s.name, name); score > bestScore)
                    {
                        bestScore = score;
                        best = &s;
                        bestGroup = &g;
                    }
        }
        if (best != nullptr)
            return { psr.trackSound (*bestGroup, *best), how };
        // The XG voice for the program: bank 0/0 plays the General MIDI sound.
        TrackSound gm;
        gm.name = std::string ("XG > ") + kGeneralMidiNames[program];
        gm.bankMsb = 0;
        gm.bankLsb = 0;
        gm.program = program;
        return { gm, how };
    }

    Choice drumKit (const DeviceProfile& psr, const Track& t)
    {
        // A General MIDI drum kit program picks the same kind of kit when there is one.
        static const std::array<std::pair<int, const char*>, 6> kinds { { { 8, "Room" }, { 16, "Power" }, { 24, "Electro" }, { 25, "Analog" }, { 40, "Brush" }, { 48, "Orch" } } };
        std::string want = "Real";
        for (const auto& [program, word] : kinds)
            if (t.sound.program == program)
                want = word;
        const ProfileSound* first = nullptr;
        const SoundGroup* firstGroup = nullptr;
        for (const auto& g : psr.groups)
        {
            if (g.name != "DrumKit")
                continue;
            for (const auto& s : g.sounds)
            {
                if (s.bankMsb != 127)
                    continue; // 126 holds sound effect kits
                if (first == nullptr)
                {
                    first = &s;
                    firstGroup = &g;
                }
                if (lower (s.name).find (lower (want)) != std::string::npos)
                    return { psr.trackSound (g, s), "drum kit" };
            }
        }
        if (first != nullptr)
            return { psr.trackSound (*firstGroup, *first), "drum kit" };
        return { TrackSound { "XG > Standard Kit", 127, 0, 0 }, "drum kit" };
    }

    bool isAudioOnly (const Track& t)
    {
        return t.output.kind == TrackOutput::Kind::audio;
    }

    bool hasNotes (const Track& t)
    {
        return std::any_of (t.clips.begin(), t.clips.end(), [] (const ClipPtr& c) { return c != nullptr && ! c->notes.empty(); });
    }
}

int gmProgramFromName (const std::string& raw)
{
    const auto name = lower (raw);
    const auto has = [&name] (const char* word) { return name.find (word) != std::string::npos; };
    // The more exact words come first: "synth bass" is a bass, not a lead.
    if (has ("bass"))
    {
        if (has ("slap")) return 36;
        if (has ("fretless")) return 35;
        if (has ("acoustic") || has ("upright") || has ("wood") || has ("jazz")) return 32;
        if (has ("pick")) return 34;
        if (has ("syn") || has ("analog") || has ("moog") || has ("acid") || has ("303") || has ("saw") || has ("square") || has ("sub")) return 38;
        if (has ("bassoon")) return 70;
        return 33;
    }
    if (has ("pad") || has ("warm") || has ("sweep") || has ("atmos"))
    {
        if (has ("choir") || has ("voice") || has ("vox")) return 91;
        if (has ("sweep")) return 95;
        if (has ("halo")) return 94;
        if (has ("bow")) return 92;
        if (has ("poly")) return 90;
        return 89;
    }
    if (has ("choir") || has ("aah") || has ("ooh") || has ("vox") || has ("voice")) return 52;
    if (has ("string") || has ("violin") || has ("cello") || has ("orchestra"))
        return has ("syn") ? 50 : (has ("pizz") ? 45 : 48);
    if (has ("brass") || has ("horn")) return has ("syn") ? 62 : (has ("french") ? 60 : 61);
    if (has ("trumpet")) return 56;
    if (has ("trombone")) return 57;
    if (has ("sax")) return has ("tenor") ? 66 : (has ("bari") ? 67 : (has ("soprano") ? 64 : 65));
    if (has ("clarinet")) return 71;
    if (has ("oboe")) return 68;
    if (has ("flute")) return has ("pan") ? 75 : 73;
    if (has ("organ")) return has ("church") || has ("pipe") ? 19 : (has ("rock") ? 18 : 16);
    if (has ("accordion")) return 21;
    if (has ("harmonica")) return 22;
    if (has ("e.piano") || has ("epiano") || has ("e-piano") || has ("electric piano") || has ("rhodes") || has ("tine") || has ("dx")) return 4;
    if (has ("clav")) return 7;
    if (has ("harpsi")) return 6;
    if (has ("piano") || has ("grand")) return 0;
    if (has ("guitar") || has ("gtr"))
    {
        if (has ("nylon") || has ("classic")) return 24;
        if (has ("steel") || has ("acoustic") || has ("folk")) return 25;
        if (has ("dist")) return 30;
        if (has ("overdrive") || has ("drive") || has ("crunch")) return 29;
        if (has ("mute")) return 28;
        if (has ("jazz")) return 26;
        return 27;
    }
    if (has ("vibra")) return 11;
    if (has ("marimba")) return 12;
    if (has ("bell") || has ("chime")) return 14;
    if (has ("harp")) return 46;
    if (has ("sitar")) return 104;
    if (has ("lead") || has ("saw") || has ("square") || has ("synth") || has ("syn")) return has ("square") ? 80 : 81;
    return -1;
}

Plan plan (const Song& song, const DeviceLibrary& library)
{
    const auto* psr = library.byId (kProfileId);
    if (psr == nullptr)
        throw Error ("the Yamaha PSR-S970 device profile is missing");

    Plan result;
    std::set<int> used;
    struct Wanted
    {
        std::size_t index;
        bool drums;
        int channel;
    };
    std::vector<Wanted> wanted;

    for (std::size_t i = 0; i < song.tracks.size(); ++i)
    {
        const auto& t = song.tracks[i];
        if (isAudioOnly (t))
        {
            result.skipped.push_back (t.name + ": an audio track (export it as a WAV file)");
            continue;
        }
        if (t.mixer.mute)
        {
            result.skipped.push_back (t.name + ": muted");
            continue;
        }
        if (! hasNotes (t))
        {
            result.skipped.push_back (t.name + ": no notes on the Timeline");
            continue;
        }
        wanted.push_back ({ i, notation::isDrumTrack (t), std::clamp (t.output.channel, 1, 16) });
    }

    // Drums first, on channel 10 as the keyboard expects; then each track keeps its channel when it is free.
    std::stable_sort (wanted.begin(), wanted.end(), [] (const Wanted& a, const Wanted& b) { return a.drums && ! b.drums; });
    const auto freeChannel = [&used] (int preferred, bool drums) {
        if (used.count (preferred) == 0 && (drums || preferred != 10))
            return preferred;
        for (int ch = 1; ch <= 16; ++ch)
            if (ch != 10 && used.count (ch) == 0)
                return ch;
        return used.count (10) == 0 ? 10 : 0;
    };

    for (const auto& w : wanted)
    {
        const auto& t = song.tracks[w.index];
        const int channel = freeChannel (w.drums ? 10 : w.channel, w.drums);
        if (channel == 0)
        {
            result.skipped.push_back (t.name + ": all 16 MIDI channels are in use");
            continue;
        }
        used.insert (channel);

        Choice choice;
        if (t.output.kind == TrackOutput::Kind::midiPort && psr->matchesPort (t.output.port) && t.sound.program >= 0)
            choice = { t.sound, "kept" };
        else if (w.drums)
            choice = drumKit (*psr, t);
        else
        {
            const auto name = soundName (t.sound);
            if (auto same = sameName (*psr, name))
                choice = *same;
            else if (usesGmSounds (t, library))
                choice = forGmProgram (*psr, std::clamp (t.sound.program, 0, 127), name, "same GM sound");
            else
            {
                int program = gmProgramFromName (name);
                if (program < 0)
                    program = gmProgramFromName (t.name);
                choice = forGmProgram (*psr, program < 0 ? 0 : program, name.empty() ? t.name : name, program < 0 ? "piano (no match)" : "from the name");
            }
        }
        result.parts.push_back ({ w.index, channel, choice.voice, choice.how });
    }

    std::sort (result.parts.begin(), result.parts.end(), [] (const Part& a, const Part& b) { return a.track < b.track; });
    return result;
}

std::vector<std::uint8_t> write (const Song& song, const Plan& plan)
{
    Song out;
    out.title = song.title;
    out.tempo = song.tempo;
    out.timeSignature = song.timeSignature;
    for (const auto& p : plan.parts)
    {
        if (p.track >= song.tracks.size())
            continue;
        auto t = song.tracks[p.track];
        t.output.kind = TrackOutput::Kind::midiPort;
        t.output.channel = p.channel;
        t.sound = p.voice;
        out.tracks.push_back (std::move (t));
    }

    midi_file::WriteOptions options;
    options.singleTrack = true;
    options.startSysEx = { { 0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7 } }; // XG System On
    options.setupAt = kTicksPerQuarter / 2; // the keyboard needs about 50 ms after XG System On
    options.offset = static_cast<Tick> (song.timeSignature.numerator) * kTicksPerQuarter * 4 / std::max (1, song.timeSignature.denominator);
    options.lyrics = true;
    return midi_file::write (out, options);
}

} // namespace arranger::psr_export
