#include "Theme.h"

#include "BinaryData.h"

namespace arranger::ui
{

namespace
{
    juce::Typeface::Ptr interRegular()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
        return tf;
    }

    juce::Typeface::Ptr interSemiBold()
    {
        static auto tf = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        return tf;
    }

    Theme& themeStorage()
    {
        static Theme t = Theme::builtIn (true);
        return t;
    }

    void readColours (const juce::var& object, std::map<juce::String, juce::Colour>& into)
    {
        if (auto* o = object.getDynamicObject())
            for (const auto& p : o->getProperties())
                into[p.name.toString()] = juce::Colour::fromString ("ff" + p.value.toString().trimCharactersAtStart ("#"));
    }
}

Theme Theme::fromJson (const juce::String& json)
{
    Theme t;
    const auto root = juce::JSON::parse (json);
    t.name = root.getProperty ("name", "Theme").toString();
    readColours (root.getProperty ("colours", {}), t.colours);
    readColours (root.getProperty ("trackColours", {}), t.trackColours);

    if (auto* m = root.getProperty ("metrics", {}).getDynamicObject())
        for (const auto& p : m->getProperties())
            t.metrics[p.name.toString()] = static_cast<float> (static_cast<double> (p.value));

    t.isDark = t.colour ("background").getPerceivedBrightness() < 0.5f;
    return t;
}

Theme Theme::builtIn (bool dark)
{
    return dark ? fromJson (juce::String::fromUTF8 (BinaryData::dark_json, BinaryData::dark_jsonSize))
                : fromJson (juce::String::fromUTF8 (BinaryData::light_json, BinaryData::light_jsonSize));
}

juce::Colour Theme::colour (const juce::String& key) const
{
    const auto it = colours.find (key);
    return it != colours.end() ? it->second : juce::Colours::magenta;
}

juce::Colour Theme::trackColour (const juce::String& key) const
{
    const auto it = trackColours.find (key);
    return it != trackColours.end() ? it->second : colour ("accent");
}

float Theme::metric (const juce::String& key, float fallback) const
{
    const auto it = metrics.find (key);
    return it != metrics.end() ? it->second : fallback;
}

juce::Font Theme::font (float height, bool semibold) const
{
    return juce::Font (juce::FontOptions (semibold ? interSemiBold() : interRegular()).withPointHeight (height));
}

juce::Typeface::Ptr Theme::regularTypeface() const { return interRegular(); }

const Theme& currentTheme() { return themeStorage(); }
void setCurrentTheme (Theme t) { themeStorage() = std::move (t); }

juce::Colour clipFill (juce::Colour trackColour, bool bright)
{
    return trackColour.interpolatedWith (juce::Colours::white, bright ? 0.40f : 0.30f);
}

} // namespace arranger::ui
