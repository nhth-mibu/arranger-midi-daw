#include "AppLookAndFeel.h"

namespace arranger::ui
{

AppLookAndFeel::AppLookAndFeel()
{
    applyTheme();
}

void AppLookAndFeel::applyTheme()
{
    const auto& t = currentTheme();
    const auto bg = t.colour ("background"), panel = t.colour ("panel"), raised = t.colour ("panelRaised");
    const auto text = t.colour ("text"), muted = t.colour ("textMuted"), line = t.colour ("lineStrong");
    const auto accent = t.colour ("accent");

    setColourScheme ({ bg, panel, raised, line, text, accent, t.colour ("accentText"), raised, text });

    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::DocumentWindow::textColourId, text);
    setColour (juce::TextButton::buttonColourId, raised);
    setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.25f));
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, raised);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::backgroundColourId, line);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ListBox::textColourId, text);
    setColour (juce::ScrollBar::thumbColourId, line);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, line);
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, line);

    setDefaultSansSerifTypeface (t.regularTypeface());
}

juce::Typeface::Ptr AppLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return currentTheme().font (12.0f, f.isBold()).getTypefacePtr();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void AppLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto& t = currentTheme();
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = t.metric ("radiusSmall", 6.0f);

    auto fill = b.getToggleState() ? t.colour ("accent").withAlpha (0.18f) : t.colour ("panelRaised");
    if (down)
        fill = fill.darker (0.15f);
    else if (highlighted)
        fill = fill.brighter (0.06f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (b.getToggleState() ? t.colour ("accent").withAlpha (0.6f) : t.colour ("lineStrong"));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

juce::Font AppLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return currentTheme().font (13.0f, true);
}

void AppLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A small switch on the right, label on the left.
    const auto& t = currentTheme();
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromRight (34.0f).withSizeKeepingCentre (34.0f, 20.0f);

    const bool on = b.getToggleState();
    g.setColour (on ? t.colour ("accent") : t.colour ("lineStrong").withAlpha (highlighted ? 1.0f : 0.8f));
    g.fillRoundedRectangle (sw, 10.0f);

    const auto knob = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ on ? sw.getRight() - 10.0f : sw.getX() + 10.0f, sw.getCentreY() });
    g.setColour (on ? t.colour ("accentText") : t.colour ("text"));
    g.fillEllipse (knob);

    g.setColour (b.isEnabled() ? t.colour ("text") : t.colour ("textDim"));
    g.setFont (t.font (13.0f));
    g.drawText (b.getButtonText(), r, juce::Justification::centredLeft);
}

void AppLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                       float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, s);
        return;
    }

    const auto& t = currentTheme();
    const auto track = juce::Rectangle<float> (static_cast<float> (x), y + height * 0.5f - 2.0f, static_cast<float> (width), 4.0f);
    g.setColour (t.colour ("lineStrong"));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (t.colour ("accent"));
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);

    g.setColour (t.colour ("text"));
    g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ sliderPos, track.getCentreY() }));
}

void AppLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& t = currentTheme();
    const auto r = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height)).reduced (0.5f);
    g.setColour (t.colour ("panelRaised"));
    g.fillRoundedRectangle (r, t.metric ("radiusSmall", 6.0f));
    g.setColour (box.hasKeyboardFocus (true) ? t.colour ("accent") : t.colour ("lineStrong"));
    g.drawRoundedRectangle (r, t.metric ("radiusSmall", 6.0f), 1.0f);

    juce::Path arrow;
    const float cx = static_cast<float> (width) - 14.0f, cy = static_cast<float> (height) * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (t.colour ("textMuted"));
    g.fillPath (arrow);
}

juce::Font AppLookAndFeel::getComboBoxFont (juce::ComboBox&) { return currentTheme().font (13.0f); }
juce::Font AppLookAndFeel::getPopupMenuFont() { return currentTheme().font (13.0f); }
juce::Font AppLookAndFeel::getLabelFont (juce::Label&) { return currentTheme().font (13.0f); }

void AppLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    const auto& t = currentTheme();
    g.setColour (t.colour ("panelRaised"));
    g.fillRoundedRectangle (0, 0, static_cast<float> (width), static_cast<float> (height), t.metric ("radiusSmall", 6.0f));
}

void AppLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    const auto& t = currentTheme();
    g.setColour (e.hasKeyboardFocus (true) ? t.colour ("accent") : t.colour ("lineStrong"));
    g.drawRoundedRectangle (0.5f, 0.5f, static_cast<float> (width) - 1.0f, static_cast<float> (height) - 1.0f,
                            t.metric ("radiusSmall", 6.0f), 1.0f);
}

} // namespace arranger::ui
