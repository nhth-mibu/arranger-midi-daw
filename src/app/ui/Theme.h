#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace arranger::ui
{

/**
    The look, loaded from a theme file (resources/themes/*.json): colours,
    track palette and sizes. Change the file to restyle the app, no code needed.
*/
class Theme
{
public:
    static Theme fromJson (const juce::String& json);
    static Theme builtIn (bool dark);

    juce::String name;
    bool isDark = true;

    /** A named colour, for example "panel" or "accent". Unknown names show as magenta so they're easy to spot. */
    juce::Colour colour (const juce::String& key) const;

    /** A track colour by palette name ("teal", "coral", ...). */
    juce::Colour trackColour (const juce::String& key) const;

    float metric (const juce::String& key, float fallback) const;

    /** Inter, the UI font. `semibold` for titles and values. */
    juce::Font font (float height, bool semibold = false) const;

    juce::Typeface::Ptr regularTypeface() const;

private:
    std::map<juce::String, juce::Colour> colours, trackColours;
    std::map<juce::String, float> metrics;
};

/** The theme in use. Components read it when they paint. */
const Theme& currentTheme();
void setCurrentTheme (Theme);

} // namespace arranger::ui
