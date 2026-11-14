#pragma once

#include "arranger/core/Notation.h"

#include <string>
#include <utility>
#include <vector>

/**
    Score layout: places a Part on pages. The result is a plain list of
    glyphs, lines, filled shapes and texts, in staff spaces (the distance
    between two staff lines), with y going down. The UI draws it with the
    Bravura font, and the same list makes the PDF, so the screen and the
    printout match. Nothing here depends on a graphics library.
*/
namespace arranger::score
{

/** SMuFL code points of the Bravura glyphs we use. */
namespace smufl
{
    inline constexpr char32_t brace = 0xE000;
    inline constexpr char32_t gClef = 0xE050, fClef = 0xE062, percussionClef = 0xE069;
    inline constexpr char32_t timeSig0 = 0xE080;
    inline constexpr char32_t noteheadWhole = 0xE0A2, noteheadHalf = 0xE0A3, noteheadBlack = 0xE0A4, noteheadXBlack = 0xE0A9;
    inline constexpr char32_t augmentationDot = 0xE1E7;
    inline constexpr char32_t metNoteQuarterUp = 0xE1D5;
    inline constexpr char32_t flag8thUp = 0xE240, flag8thDown = 0xE241, flag16thUp = 0xE242, flag16thDown = 0xE243, flag32ndUp = 0xE244, flag32ndDown = 0xE245;
    inline constexpr char32_t accidentalFlat = 0xE260, accidentalNatural = 0xE261, accidentalSharp = 0xE262, accidentalDoubleSharp = 0xE263, accidentalDoubleFlat = 0xE264;
    inline constexpr char32_t restWhole = 0xE4E3, restHalf = 0xE4E4, restQuarter = 0xE4E5, rest8th = 0xE4E6, rest16th = 0xE4E7, rest32nd = 0xE4E8;
    inline constexpr char32_t tuplet3 = 0xE883;
    inline constexpr char32_t articAccentAbove = 0xE4A0, articAccentBelow = 0xE4A1, articStaccatoAbove = 0xE4A2, articStaccatoBelow = 0xE4A3;
    inline constexpr char32_t articTenutoAbove = 0xE4A4, articTenutoBelow = 0xE4A5, articMarcatoAbove = 0xE4AC, fermataAbove = 0xE4C0;
    inline constexpr char32_t dynamicPiano = 0xE520, dynamicMezzo = 0xE521, dynamicForte = 0xE522, dynamicRinforzando = 0xE523, dynamicSforzando = 0xE524, dynamicZ = 0xE525;
}

struct Glyph
{
    char32_t code = 0;
    float x = 0, y = 0;  ///< the glyph's origin (SMuFL: left edge, on the baseline)
    float scale = 1.0f;  ///< 1 = the normal size for a staff (one em = four spaces)
};

struct Line
{
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float width = 0.1f;
};

/** A filled outline: beams and ties. */
struct Shape
{
    std::vector<std::pair<float, float>> points;
};

struct Text
{
    enum class Style : std::uint8_t { plain, bold, italic };
    enum class Align : std::uint8_t { left, centre, right };

    std::string text;   ///< UTF-8
    float x = 0, y = 0; ///< baseline
    float size = 2.0f;  ///< font height in spaces
    Style style = Style::plain;
    Align align = Align::left;
    bool serif = true;
};

struct Page
{
    float width = 0, height = 0;
    std::vector<Line> lines;
    std::vector<Shape> shapes;
    std::vector<Glyph> glyphs;
    std::vector<Text> texts;
};

struct PageSetup
{
    float width = 120.0f;     ///< A4 with 1.75 mm spaces (a 7 mm staff)
    float height = 169.7f;
    float margin = 8.0f;
    float spaceMm = 1.75f;
    std::string title;
    std::string subtitle;     ///< under the title, for example the key and tempo
    double bpm = 0.0;         ///< more than 0: print a tempo mark
    bool guitarChords = false; ///< draw a guitar chord box above each chord symbol
};

/** Where a notehead is on the page, for clicks. */
struct NoteBox
{
    int page = 0;
    float x = 0, y = 0; ///< the centre of the head
    int source = -1;    ///< Head::source
    int staff = 0;
};

/** Where one bar of one staff is on the page, for clicks. */
struct BarBox
{
    int page = 0;
    float x1 = 0, x2 = 0;
    float top = 0;      ///< y of the top staff line
    int staff = 0;
    notation::Clef clef = notation::Clef::treble;
    Tick start = 0, length = 0;                  ///< song position of the bar
    std::vector<std::pair<Tick, float>> columns; ///< tick in the bar -> x of the notes that start there
};

struct Layout
{
    std::vector<Page> pages;
    int systems = 0;
    std::vector<NoteBox> notes;
    std::vector<BarBox> bars;
};

/** What is under a point of a page. */
struct Hit
{
    bool valid = false;
    int staff = 0;
    notation::Clef clef = notation::Clef::treble;
    Tick tick = 0;     ///< song position, not snapped
    int position = 0;  ///< staff position: 0 = bottom line
    float top = 0;     ///< y of the top line of the staff that was hit
};

/** The staff, time and staff position at a point. Not valid when the point is far from every staff. */
Hit hitTest (const Layout&, int page, float x, float y);

/** The x of a song position on the staff of `near` (a hit on the same system), or a negative number. */
float xOfTick (const Layout&, int page, int staff, float top, Tick tick);

/** Lays out a part. With `setup.height` 0 there is one long page (for the screen). */
Layout layout (const notation::Part&, const PageSetup& = {});

} // namespace arranger::score
