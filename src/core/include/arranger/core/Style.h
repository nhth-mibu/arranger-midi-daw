#pragma once

#include "arranger/core/Song.h"
#include "arranger/core/StyleRules.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace arranger
{

/** The eight parts of a Yamaha style. They play on MIDI channels 9 to 16. */
enum class StylePart : std::uint8_t { rhythm1, rhythm2, bass, chord1, chord2, pad, phrase1, phrase2 };

inline constexpr int kStylePartCount = 8;
inline constexpr const char* kStylePartNames[] = { "Rhythm 1", "Rhythm 2", "Bass", "Chord 1", "Chord 2", "Pad", "Phrase 1", "Phrase 2" };
inline constexpr int stylePartChannel (StylePart p) noexcept { return 9 + static_cast<int> (p); }

/** The section names a PSR-S970 knows, in the keyboard's order. */
inline constexpr const char* kStyleSectionNames[] = {
    "Intro A", "Intro B", "Intro C", "Intro D",
    "Main A", "Main B", "Main C", "Main D",
    "Fill In AA", "Fill In BB", "Fill In CC", "Fill In DD", "Fill In BA",
    "Ending A", "Ending B", "Ending C", "Ending D",
};

/** One section of a style: a short clip for each source channel. */
struct StyleSection
{
    std::string name;
    Tick length = kTicksPerQuarter * 4;
    std::map<int, Clip> parts;      ///< by source channel 1..16; notes start at 0
    std::vector<StyleEvent> events; ///< SysEx, tempo changes and text inside the section, kept as they are; times from the section start
    bool operator== (const StyleSection&) const = default;
};

/**
    An arranger style in our own model. It reads from and writes to Yamaha
    style files (SFF1 and SFF2/"SFF GE"), but nothing here is tied to that
    layout, so the file code can change without touching editors.
*/
struct Style
{
    std::string name = "New Style";
    int format = 2;                  ///< 1 = SFF1, 2 = SFF2 (PSR-S970)
    double tempo = 120.0;
    TimeSignature timeSignature;
    std::vector<StyleEvent> setup;   ///< the "SInt" bar: voices, volumes, effects, SysEx
    std::vector<StyleSection> sections;
    std::vector<StyleRuleSet> ruleSets;
    std::vector<std::uint8_t> extra; ///< chunks after CASM (One Touch Settings, Music Finder), kept as they are

    const StyleSection* section (const std::string& name) const;
    StyleSection* section (const std::string& name);

    /** The rule for a channel in a section; null when the file has none (the keyboard then uses defaults). */
    const StyleChannelRule* rule (const std::string& section, int sourceChannel) const;

    bool operator== (const Style&) const = default;
};

namespace style_file
{
    inline constexpr const char* kExtension = ".sty";

    /** The names Yamaha gives style files: .sty, and .prs/.sst/.pst/.pcs/.bcs/.fps for the style groups of newer keyboards. */
    inline constexpr const char* kExtensions[] = { ".sty", ".prs", ".sst", ".pst", ".pcs", ".bcs", ".fps" };
    bool hasStyleExtension (const std::string& fileName);

    struct Error : std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    /** Reads a Yamaha style file. Throws Error. */
    Style read (std::span<const std::uint8_t> bytes);
    Style load (const std::filesystem::path&);

    /** Writes a Yamaha style file in the style's format. */
    std::vector<std::uint8_t> write (const Style&);
    void save (const Style&, const std::filesystem::path&);
}

struct Chord;

/**
    A style in the app is a song whose Clip grid holds it: each scene is a
    section, each style track is a source channel, each slot is that channel's
    clip in that section. So the Clip grid, the note editor and the Beat view
    edit styles with no code of their own.
*/
namespace style_convert
{
    /** Opens a style as a song: scenes and slots as above, voices and mixer values from the setup bar. */
    Song toSong (const Style&);

    /** Collects the song's style tracks and scenes into a style. A slot shorter than its section repeats. */
    Style fromSong (const Song&);

    /** Turns a song into a style song: every track with slots becomes a style part on channels 9 to 16. */
    void makeStyle (Song&);

    /** The rule a style track uses in a scene: the file's rule, or the usual default for that part. */
    StyleChannelRule ruleFor (const Song&, const Track&, int scene);

    /**
        Sets the chord rule of a style track. `scene` -1 sets it for every
        section. With a scene, only that section changes: it gets its own
        rule set when it shared one with other sections.
    */
    void setRule (Song&, const Track&, int scene, StyleChannelRule);

    /**
        Copies a part of the Timeline into a section of the style (this is
        how a MIDI file becomes a style). Every track with notes between
        `from` and `to` gets a slot in the scene with this name; the scene is
        made when the song has none. The tracks become style parts, and the
        parts that follow chords get the chord the passage is in as their
        "written in" chord. Returns the scene, or -1 when the range is empty.
    */
    int sectionFromTimeline (Song&, Tick from, Tick to, const std::string& section);

    /** The song with the slots of its style tracks moved to this chord, ready to play. Timeline clips stay. */
    Song applyChord (const Song&, const Chord&);

    /** What fillTimeline writes. */
    struct Fill
    {
        std::string section = "Main A";
        std::vector<Chord> chords; ///< one chord for each bar; the list starts again when there are more bars. Empty: C major.
        Tick at = 0;               ///< where the first bar starts on the Timeline
        int bars = 8;
    };

    /**
        Writes a section of a style onto the Timeline as ordinary tracks:
        one new track for each part of the section, with the part's voice
        and mixer values, and one clip that plays the section again and
        again for `bars` bars, moved to the chord of each bar. The song's
        own tracks and its tempo stay as they are. A part goes to a
        SoundFont channel that no other sound uses, when one is free.
        Returns the new tracks; empty when the style has no such section.
    */
    std::vector<TrackId> fillTimeline (Song&, const Style&, const Fill&);
}

} // namespace arranger
