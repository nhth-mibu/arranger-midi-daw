#include "HeaderBar.h"

#include "BinaryData.h"

namespace arranger::ui
{

// ---------------------------------------------------------------------------
IconButton::IconButton (const juce::String& name, icons::Icon i, juce::String onKey)
    : juce::Button (name), icon (i), onColourKey (std::move (onKey))
{
    setTooltip (name);
    setWantsKeyboardFocus (false);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = currentTheme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    const auto onColour = t.colour (onColourKey);

    auto fill = on ? onColour.withAlpha (0.22f).overlaidWith (t.colour ("panelRaised").withAlpha (0.4f)) : t.colour ("panelRaised");
    if (down)
        fill = fill.darker (0.2f);
    else if (highlighted && isEnabled())
        fill = fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, t.metric ("radius", 8.0f));
    g.setColour (on ? onColour.withAlpha (0.65f) : t.colour ("lineStrong"));
    g.drawRoundedRectangle (r, t.metric ("radius", 8.0f), 1.0f);

    auto iconColour = on ? onColour : t.colour ("text");
    if (icon == icons::Icon::record)
        iconColour = t.colour ("recording");
    if (icon == icons::Icon::play && ! on)
        iconColour = t.colour ("playing");
    if (icon == icons::Icon::loop || icon == icons::Icon::metronome)
        iconColour = on ? onColour : t.colour ("textMuted");
    if (! isEnabled())
        iconColour = iconColour.withMultipliedAlpha (0.35f);

    const float iconSize = icon == icons::Icon::play ? 24.0f : 18.0f;
    icons::draw (g, icon, r.withSizeKeepingCentre (iconSize, iconSize), iconColour);
}

// ---------------------------------------------------------------------------
AiButton::AiButton() : juce::Button ("Ask AI")
{
    setTooltip ("AI Chat: tell the AI what to make or change, in plain words (Cmd+J)");
    setWantsKeyboardFocus (false);
}

void AiButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = currentTheme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float radius = r.getHeight() / 2.0f;
    const bool on = getToggleState();
    auto accent = t.colour ("accent");
    if (down)
        accent = accent.darker (0.15f);
    else if (highlighted)
        accent = accent.brighter (0.1f);

    // Soft glow while the chat is open
    if (on)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (r, radius);
    }

    juce::ColourGradient grad (accent.brighter (0.22f), r.getTopLeft(), accent.darker (0.22f), r.getBottomRight(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (on ? r.reduced (2.0f) : r, radius);
    g.setColour (on ? t.colour ("accentText").withAlpha (0.55f) : accent.darker (0.35f));
    g.drawRoundedRectangle ((on ? r.reduced (2.0f) : r).reduced (0.5f), radius, on ? 1.5f : 1.0f);

    auto inner = r.reduced (radius * 0.7f, 0.0f);
    const auto ink = t.colour ("accentText");
    icons::draw (g, icons::Icon::sparkle, inner.removeFromLeft (20.0f).withSizeKeepingCentre (20.0f, 20.0f), ink);
    inner.removeFromLeft (7.0f);
    g.setColour (ink);
    g.setFont (t.font (14.5f, true));
    g.drawText (getButtonText(), inner, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
LcdDisplay::LcdDisplay()
{
    addChildComponent (tempoEditor);
    tempoEditor.setJustification (juce::Justification::centred);
    tempoEditor.setInputRestrictions (6, "0123456789.");
    tempoEditor.onReturnKey = [this] {
        if (onTempoChange)
            onTempoChange (tempoEditor.getText().getDoubleValue());
        tempoEditor.setVisible (false);
    };
    tempoEditor.onEscapeKey = [this] { tempoEditor.setVisible (false); };
    tempoEditor.onFocusLost = [this] { tempoEditor.setVisible (false); };
    setTooltip ("Drag or double-click the tempo to change it. Click the meter to pick a time signature.");
}

void LcdDisplay::setValues (double ticks, double bpm, TimeSignature sig)
{
    const bool changed = formatBarBeat (toBarBeat (static_cast<Tick> (ticks), sig)) != formatBarBeat (toBarBeat (static_cast<Tick> (position), meter))
                      || bpm != tempo || sig != meter;
    position = ticks;
    tempo = bpm;
    meter = sig;
    if (changed)
        repaint();
}

juce::Rectangle<int> LcdDisplay::cell (int index) const
{
    const int widths[] = { 142, 116, 92 };
    int x = 0;
    for (int i = 0; i < index; ++i)
        x += widths[i];
    return { x, 0, widths[index], getHeight() };
}

void LcdDisplay::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto radius = t.metric ("radius", 8.0f);
    const auto outer = getLocalBounds().toFloat().reduced (0.5f);
    const auto screen = outer.reduced (4.0f);

    g.setColour (t.colour ("lcdBezel"));
    g.fillRoundedRectangle (outer, radius);
    g.setColour (t.colour ("lineStrong").withAlpha (0.8f));
    g.drawRoundedRectangle (outer, radius, 1.0f);

    juce::ColourGradient glass (t.colour ("lcdBack").brighter (0.05f), screen.getX(), screen.getY(),
                                t.colour ("lcdBack").darker (0.14f), screen.getX(), screen.getBottom(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (screen, radius - 2.0f);

    g.saveState();
    g.reduceClipRegion (screen.toNearestInt());
    // Fine horizontal lines and a soft top highlight give the display its glass look.
    g.setColour (juce::Colours::black.withAlpha (0.07f));
    for (float y = screen.getY() + 1.0f; y < screen.getBottom(); y += 3.0f)
        g.fillRect (juce::Rectangle<float> (screen.getX(), y, screen.getWidth(), 1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.11f));
    g.fillRoundedRectangle (screen.withHeight (screen.getHeight() * 0.40f), radius - 2.0f);
    g.restoreState();

    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.drawHorizontalLine (juce::roundToInt (screen.getY() + 0.5f), screen.getX() + 4.0f, screen.getRight() - 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.drawHorizontalLine (juce::roundToInt (screen.getBottom() - 1.5f), screen.getX() + 4.0f, screen.getRight() - 4.0f);

    const auto bb = toBarBeat (static_cast<Tick> (position), meter);
    const juce::String values[] = {
        juce::String (bb.bar) + " . " + juce::String (bb.beat) + " . " + juce::String (bb.sixteenth),
        juce::String (tempo, 2),
        juce::String (meter.numerator) + " / " + juce::String (meter.denominator),
    };
    const char* labels[] = { "BAR \xc2\xb7 BEAT", "TEMPO", "METER" };

    for (int i = 0; i < 3; ++i)
    {
        auto c = cell (i);
        if (i > 0)
        {
            g.setColour (t.colour ("lcdDim").withAlpha (0.35f));
            g.fillRect (c.getX(), 12, 1, getHeight() - 22);
        }
        c = c.reduced (14, 6);
        auto label = c.removeFromTop (11);
        g.setColour (t.colour ("lcdDim"));
        g.setFont (t.font (9.5f, true).withExtraKerningFactor (0.15f));
        g.drawText (juce::String::fromUTF8 (labels[i]), label, juce::Justification::centredLeft);
        g.setColour (t.colour ("lcdInk"));
        g.setFont (t.font (20.0f, true).withExtraKerningFactor (0.04f));
        g.drawText (values[i], c, juce::Justification::centredLeft);
    }
}

void LcdDisplay::resized()
{
    tempoEditor.setBounds (cell (1).reduced (8, 10));
}

void LcdDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (cell (1).contains (e.getPosition()))
    {
        draggingTempo = true;
        dragStartTempo = tempo;
        return;
    }

    if (cell (2).contains (e.getPosition()))
    {
        const TimeSignature options[] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
        juce::PopupMenu menu;
        for (int i = 0; i < 8; ++i)
            menu.addItem (i + 1, juce::String (options[i].numerator) + "/" + juce::String (options[i].denominator), true, options[i] == meter);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (cell (2))),
                            [this, options] (int result) {
                                if (result > 0 && onMeterChange)
                                    onMeterChange (options[result - 1]);
                            });
    }
}

void LcdDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingTempo || onTempoChange == nullptr)
        return;
    const double step = e.mods.isShiftDown() ? 0.01 : 0.25;
    const double newTempo = dragStartTempo - e.getDistanceFromDragStartY() * step;
    onTempoChange (std::round (newTempo / step) * step);
}

void LcdDisplay::mouseUp (const juce::MouseEvent&) { draggingTempo = false; }

void LcdDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (cell (1).contains (e.getPosition()))
        editTempo();
}

void LcdDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (cell (1).contains (e.getPosition()) && onTempoChange)
        onTempoChange (std::round (tempo + (w.deltaY > 0 ? 1.0 : -1.0)));
}

void LcdDisplay::editTempo()
{
    tempoEditor.setFont (currentTheme().font (18.0f, true));
    tempoEditor.setText (juce::String (tempo, 2), false);
    tempoEditor.setVisible (true);
    tempoEditor.grabKeyboardFocus();
    tempoEditor.selectAll();
}

// ---------------------------------------------------------------------------
ViewTabs::ViewTabs()
{
    setTooltip ("Views: Timeline, Score, Drums and Loops");
}

void ViewTabs::setSelected (int index)
{
    if (index != current)
    {
        current = index;
        repaint();
    }
}

juce::Rectangle<int> ViewTabs::tabBounds (int index) const
{
    const auto inner = getLocalBounds().reduced (5);
    const int w = inner.getWidth() / names.size();
    return { inner.getX() + index * w, inner.getY(), w, inner.getHeight() };
}

void ViewTabs::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.setColour (t.colour ("lcd").withAlpha (t.isDark ? 1.0f : 0.06f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), t.metric ("radius", 8.0f) + 2.0f);
    g.setColour (t.colour ("line"));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), t.metric ("radius", 8.0f) + 2.0f, 1.0f);

    for (int i = 0; i < names.size(); ++i)
    {
        const auto r = tabBounds (i).toFloat();
        if (i == current)
        {
            g.setColour (t.colour ("accent").withAlpha (0.14f));
            g.fillRoundedRectangle (r, t.metric ("radiusSmall", 6.0f));
            g.setColour (t.colour ("accent").withAlpha (0.4f));
            g.drawRoundedRectangle (r.reduced (0.5f), t.metric ("radiusSmall", 6.0f), 1.0f);
            g.setColour (t.colour ("accent"));
            const float underlineWidth = juce::jmin (26.0f, r.getWidth() * 0.4f);
            g.fillRoundedRectangle (juce::Rectangle<float> (underlineWidth, 2.0f)
                                        .withCentre ({ r.getCentreX(), r.getBottom() - 7.0f }), 1.0f);
        }
        g.setColour (i == current ? t.colour ("text") : t.colour ("textMuted"));
        g.setFont (t.font (14.0f, i == current));
        g.drawText (names[i], r, juce::Justification::centred);
    }
}

void ViewTabs::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < names.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
        {
            setSelected (i);
            if (onChange)
                onChange (i);
        }
}

// ---------------------------------------------------------------------------
HeaderBar::HeaderBar (Session& s, const LiveState& l) : session (s), live (l)
{
    for (auto* b : { &stopButton, &playButton, &recordButton, &loopButton, &clickButton })
        addAndMakeVisible (b);
    addAndMakeVisible (lcd);
    addAndMakeVisible (tabs);
    addAndMakeVisible (mixerButton);
    addAndMakeVisible (aiButton);
    for (auto* b : { &zoomOutButton, &zoomInButton })
    {
        addAndMakeVisible (*b);
        b->setWantsKeyboardFocus (false);
    }
    zoomOutButton.setTooltip ("Zoom out in time");
    zoomInButton.setTooltip ("Zoom in in time");
    zoomOutButton.onClick = [this] { if (onZoomHorizontal) onZoomHorizontal (1.0 / 1.2); };
    zoomInButton.onClick = [this] { if (onZoomHorizontal) onZoomHorizontal (1.2); };
    mixerButton.setTooltip ("Show or hide the mixer under the view");

    recordButton.setTooltip ("Record on the armed (or selected) track (R)");
    loopButton.setTooltip ("Loop between the locators (L). Drag in the ruler to set them.");
    playButton.setTooltip ("Play / stop (Space)");
    stopButton.setTooltip ("Stop. Press twice to go back to the start (Home)");
    clickButton.setTooltip ("Metronome on/off (C)");
}

void HeaderBar::refresh()
{
    const auto& song = session.song();
    playButton.setToggleState (live.playing, juce::dontSendNotification);
    recordButton.setToggleState (session.isRecording(), juce::dontSendNotification);
    loopButton.setToggleState (song.loop.enabled, juce::dontSendNotification);
    clickButton.setToggleState (song.metronome.enabled, juce::dontSendNotification);
    lcd.setValues (live.positionTicks, song.baseTempo(), song.timeSignature);

    juce::String status;
    if (live.audio.running)
        status = juce::String (live.audio.driver) + " \xc2\xb7 " + juce::String (live.audio.bufferSize) + "|CPU " + juce::String (juce::roundToInt (live.audio.cpuLoad * 100.0)) + "%";
    else
        status = "No audio device|CPU 0%";

    const auto title = juce::String (song.title) + juce::String (song.baseTempo(), 2) + juce::String (song.key);
    if (status != shownStatus)
    {
        shownStatus = status;
        repaint (statusArea);
    }
    if (title != shownTitle)
    {
        shownTitle = title;
        repaint (titleArea);
    }
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const auto& song = session.song();
    g.setColour (t.colour ("panel"));
    g.fillRect (getLocalBounds());
    g.setColour (t.colour ("line"));
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // App badge and song title
    auto area = titleArea;
    const auto badge = area.removeFromLeft (36).withSizeKeepingCentre (36, 36);
    const auto icon = juce::ImageCache::getFromMemory (BinaryData::appicon256_png, BinaryData::appicon256_pngSize);
    if (icon.isValid())
        g.drawImageWithin (icon, badge.getX(), badge.getY(), badge.getWidth(), badge.getHeight(),
                           juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, false);
    else
    {
        g.setColour (t.colour ("accent"));
        g.fillRoundedRectangle (badge.toFloat(), 10.0f);
        g.setColour (t.colour ("accentText"));
        g.setFont (t.font (17.0f, true));
        g.drawText ("A", badge, juce::Justification::centred);
    }

    area.removeFromLeft (14);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (16.0f, true));
    g.drawText (juce::String::fromUTF8 (song.title.c_str()), area.removeFromTop (getHeight() / 2).withTrimmedTop (6), juce::Justification::bottomLeft, true);
    g.setColour (t.colour ("textMuted"));
    g.setFont (t.font (12.5f));
    const auto subtitle = juce::String (juce::roundToInt (song.baseTempo())) + " BPM \xc2\xb7 " + juce::String (song.timeSignature.numerator) + "/"
                        + juce::String (song.timeSignature.denominator) + " \xc2\xb7 " + juce::String::fromUTF8 (song.key.c_str());
    g.drawText (juce::String::fromUTF8 (subtitle.toRawUTF8()), area.withTrimmedTop (2), juce::Justification::topLeft, true);

    // Status pills, right side: audio device and CPU
    auto pills = statusArea;
    auto parts = juce::StringArray::fromTokens (shownStatus, "|", "");
    auto pillWidth = [&t] (const juce::String& part, bool device) {
        const float textW = juce::GlyphArrangement::getStringWidth (t.font (13.0f), juce::String::fromUTF8 (part.toRawUTF8()));
        return static_cast<int> (std::ceil (textW)) + (device ? 50 : 34);
    };
    // In a narrow window, drop the CPU pill before the pills run into the view tabs.
    int total = 0;
    for (int i = 0; i < parts.size(); ++i)
        total += pillWidth (parts[i], i == 0) + 8;
    if (total > pills.getWidth() && parts.size() > 1)
        parts.removeRange (1, parts.size() - 1);

    for (int i = parts.size(); --i >= 0;)
    {
        const bool device = i == 0;
        const auto text = juce::String::fromUTF8 (parts[i].toRawUTF8());
        const int w = juce::jmin (pillWidth (parts[i], device), pills.getWidth());
        const auto pill = pills.removeFromRight (w).withSizeKeepingCentre (w, 32).toFloat();
        pills.removeFromRight (8);

        g.setColour (t.colour ("panel").brighter (0.03f));
        g.fillRoundedRectangle (pill, 16.0f);
        g.setColour (t.colour ("lineStrong").withAlpha (0.7f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 16.0f, 1.0f);

        auto inner = pill.reduced (13.0f, 0.0f);
        if (device)
        {
            g.setColour (live.audio.running ? t.colour ("ok") : t.colour ("warning"));
            g.fillEllipse (inner.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
            inner.removeFromLeft (7.0f);
        }
        g.setColour (t.colour ("text"));
        g.setFont (t.font (13.0f));
        g.drawText (text, inner, juce::Justification::centredLeft);
    }
}

void HeaderBar::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    titleArea = r.removeFromLeft (220);

    const int transportWidth = 54 + 4 * 44 + 4 * 8;
    auto buttons = r.removeFromLeft (transportWidth).withSizeKeepingCentre (transportWidth, 54);
    for (auto* b : { &stopButton, &playButton, &recordButton, &loopButton, &clickButton })
    {
        const int size = b == &playButton ? 54 : 44;
        b->setBounds (buttons.removeFromLeft (size).withSizeKeepingCentre (size, size));
        buttons.removeFromLeft (8);
    }

    r.removeFromLeft (16);
    lcd.setBounds (r.removeFromLeft (350).withSizeKeepingCentre (350, 52));

    aiButton.setBounds (r.removeFromRight (112).withSizeKeepingCentre (112, 40));
    r.removeFromRight (10);
    auto zoomArea = r.removeFromRight (64).withSizeKeepingCentre (64, 30);
    zoomOutButton.setBounds (zoomArea.removeFromLeft (28));
    zoomArea.removeFromLeft (8);
    zoomInButton.setBounds (zoomArea.removeFromLeft (28));
    r.removeFromRight (10);
    statusArea = r.removeFromRight (juce::jmin (260, r.getWidth() / 3));
    auto tabArea = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 16, 440), 44);
    mixerButton.setBounds (tabArea.removeFromRight (72).withSizeKeepingCentre (72, 34));
    tabArea.removeFromRight (10);
    tabs.setBounds (tabArea);
}

} // namespace arranger::ui
