#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
    A very small PDF writer: pages of filled shapes, nothing else. The score
    turns glyphs and text into outlines before it calls this, so the file
    needs no fonts and looks the same everywhere.
*/
namespace arranger::pdf
{

class Document
{
public:
    /** Starts a page; sizes in points (1/72 inch). Coordinates on the page go down from the top-left corner. */
    void beginPage (float width, float height);

    void setColour (float r, float g, float b);
    void moveTo (float x, float y);
    void lineTo (float x, float y);
    void cubicTo (float x1, float y1, float x2, float y2, float x3, float y3);
    void closePath();
    /** Fills the path so far (non-zero winding) and starts a new one. */
    void fill();

    std::size_t pageCount() const noexcept { return pages.size(); }

    /** The finished file. */
    std::vector<std::uint8_t> finish (const std::string& title = {}) const;

private:
    struct PageData
    {
        float width = 0, height = 0;
        std::string content;
    };
    std::vector<PageData> pages;

    void point (float x, float y);
};

} // namespace arranger::pdf
