#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace arranger::ui
{

/** A small, round button with a neutral grey face. Used for the zoom pairs. */
class RoundButton final : public juce::TextButton
{
public:
    explicit RoundButton (const juce::String& text) : juce::TextButton (text)
    {
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& t = currentTheme();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        auto face = t.colour ("lineStrong").withAlpha (0.72f);
        if (down)
            face = face.darker (0.30f);
        else if (over)
            face = face.brighter (0.16f);

        g.setColour (face);
        g.fillEllipse (r);
        g.setColour (t.colour ("line").withAlpha (0.85f));
        g.drawEllipse (r, 1.0f);
        g.setColour (t.colour ("text"));
        g.setFont (t.font (juce::jmax (13.0f, r.getHeight() * 0.55f), true));
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
    }
};

} // namespace arranger::ui
