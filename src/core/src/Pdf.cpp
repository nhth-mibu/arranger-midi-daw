#include "arranger/core/Pdf.h"

#include <cstdio>
#include <stdexcept>

namespace arranger::pdf
{

namespace
{
    std::string number (float v)
    {
        char buffer[32];
        std::snprintf (buffer, sizeof (buffer), "%.2f", static_cast<double> (v));
        std::string s (buffer);
        while (! s.empty() && s.back() == '0')
            s.pop_back();
        if (! s.empty() && s.back() == '.')
            s.pop_back();
        if (s == "-0")
            s = "0";
        return s;
    }

    std::string escaped (const std::string& text)
    {
        std::string out;
        for (const char c : text)
        {
            if (c == '(' || c == ')' || c == '\\')
                out += '\\';
            if (static_cast<unsigned char> (c) >= 32 && static_cast<unsigned char> (c) < 127)
                out += c;
        }
        return out;
    }
}

void Document::beginPage (float width, float height)
{
    pages.push_back ({ width, height, {} });
}

void Document::point (float x, float y)
{
    if (pages.empty())
        throw std::logic_error ("beginPage first");
    auto& p = pages.back();
    p.content += number (x) + ' ' + number (p.height - y) + ' ';
}

void Document::setColour (float r, float g, float b)
{
    if (pages.empty())
        throw std::logic_error ("beginPage first");
    pages.back().content += number (r) + ' ' + number (g) + ' ' + number (b) + " rg\n";
}

void Document::moveTo (float x, float y)
{
    point (x, y);
    pages.back().content += "m\n";
}

void Document::lineTo (float x, float y)
{
    point (x, y);
    pages.back().content += "l\n";
}

void Document::cubicTo (float x1, float y1, float x2, float y2, float x3, float y3)
{
    point (x1, y1);
    point (x2, y2);
    point (x3, y3);
    pages.back().content += "c\n";
}

void Document::closePath()
{
    if (! pages.empty())
        pages.back().content += "h\n";
}

void Document::fill()
{
    if (! pages.empty())
        pages.back().content += "f\n";
}

std::vector<std::uint8_t> Document::finish (const std::string& title) const
{
    std::string out = "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
    std::vector<std::size_t> offsets;
    const auto object = [&] (const std::string& body) {
        offsets.push_back (out.size());
        out += std::to_string (offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };

    // 1 catalog, 2 page tree, 3 info, then a page and its content for each page.
    const std::size_t firstPage = 4;
    std::string kids;
    for (std::size_t i = 0; i < pages.size(); ++i)
        kids += std::to_string (firstPage + i * 2) + " 0 R ";
    object ("<< /Type /Catalog /Pages 2 0 R >>");
    object ("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string (pages.size()) + " >>");
    object ("<< /Title (" + escaped (title) + ") /Producer (Arranger MIDI DAW) >>");
    for (std::size_t i = 0; i < pages.size(); ++i)
    {
        const auto& p = pages[i];
        object ("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + number (p.width) + ' ' + number (p.height) + "] /Contents "
                + std::to_string (firstPage + i * 2 + 1) + " 0 R /Resources << >> >>");
        object ("<< /Length " + std::to_string (p.content.size()) + " >>\nstream\n" + p.content + "\nendstream");
    }
    const auto xref = out.size();
    out += "xref\n0 " + std::to_string (offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (const auto o : offsets)
    {
        char line[32];
        std::snprintf (line, sizeof (line), "%010zu 00000 n \n", o);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string (offsets.size() + 1) + " /Root 1 0 R /Info 3 0 R >>\nstartxref\n" + std::to_string (xref) + "\n%%EOF\n";
    return { out.begin(), out.end() };
}

} // namespace arranger::pdf
