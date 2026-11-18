#include "KidsView.h"

#include "Theme.h"

#include <algorithm>
#include <cmath>

namespace arranger::ui
{
namespace
{
    constexpr double kNoteLife = 1.6; // seconds a note floats
    constexpr double kRingLife = 0.7;
    constexpr int kBarHeight = 52;

    double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    juce::Colour noteColour (int index)
    {
        const auto& t = currentTheme();
        static const char* keys[] = { "accent", "playing", "ok", "warning" };
        return t.colour (keys[index % 4]);
    }

    /** An eighth note: a head, a stem and a flag. `size` is the height. */
    void drawNote (juce::Graphics& g, juce::Point<float> centre, float size, juce::Colour colour)
    {
        const float head = size * 0.38f;
        const auto headBox = juce::Rectangle<float> (head * 1.3f, head).withCentre (centre.translated (0.0f, size * 0.32f));
        g.setColour (colour);
        g.fillEllipse (headBox);
        const float stemX = headBox.getRight() - size * 0.05f;
        const float top = centre.y - size * 0.5f;
        g.fillRect (juce::Rectangle<float> (stemX - size * 0.07f, top, size * 0.07f, headBox.getCentreY() - top));
        juce::Path flag;
        flag.startNewSubPath (stemX, top);
        flag.quadraticTo (stemX + size * 0.38f, top + size * 0.18f, stemX + size * 0.2f, top + size * 0.5f);
        flag.quadraticTo (stemX + size * 0.22f, top + size * 0.26f, stemX - size * 0.02f, top + size * 0.2f);
        flag.closeSubPath();
        g.fillPath (flag);
    }
}

KidsView::KidsView (Session& s) : session (s)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    const auto& t = currentTheme();
    pauseLabel.setFont (t.font (14.0f));
    pauseLabel.setJustificationType (juce::Justification::centredRight);
    pauseLabel.setWantsKeyboardFocus (false);
    addAndMakeVisible (pauseLabel);

    pauseSlider.setRange (kids_mode::kMinPauseAfter, kids_mode::kMaxPauseAfter, 0.1);
    pauseSlider.setTextValueSuffix (" s");
    pauseSlider.setNumDecimalPlacesToDisplay (1);
    pauseSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 24);
    pauseSlider.setWantsKeyboardFocus (false);
    pauseSlider.setTooltip ("How long the song keeps playing after the last key");
    pauseSlider.onValueChange = [this] {
        auto k = session.kidsModeSettings();
        k.pauseAfterSeconds = pauseSlider.getValue();
        session.setKidsModeSettings (k);
    };
    addAndMakeVisible (pauseSlider);

    keysSound.setWantsKeyboardFocus (false);
    keysSound.setTooltip ("Off: the keys only play the song. On: you also hear the notes on the track you record on");
    keysSound.onClick = [this] {
        auto k = session.kidsModeSettings();
        k.keysSound = keysSound.getToggleState();
        session.setKidsModeSettings (k);
    };
    addAndMakeVisible (keysSound);

    exitButton.setWantsKeyboardFocus (false);
    exitButton.onClick = [this] { session.setKidsMode (false); };
    addAndMakeVisible (exitButton);

    seenPresses = session.kidsPresses();
    refresh();
}

KidsView::~KidsView() = default;

void KidsView::refresh()
{
    const auto& k = session.kidsModeSettings();
    pauseSlider.setValue (k.pauseAfterSeconds, juce::dontSendNotification);
    keysSound.setToggleState (k.keysSound, juce::dontSendNotification);
    repaint();
}

void KidsView::setPreview (bool playing)
{
    preview = playing;
    if (playing)
    {
        const double now = nowSeconds();
        for (int i = 0; i < 7; ++i)
            notes.push_back ({ now - 0.2 * i, static_cast<float> (i) * 0.93f + 0.4f, i });
        rings.push_back (now - 0.15);
        rings.push_back (now - 0.45);
        phase = 0.6;
    }
    repaint();
}

bool KidsView::playingNow() const { return preview || session.isPlaying(); }

void KidsView::visibilityChanged()
{
    if (isVisible())
    {
        seenPresses = session.kidsPresses();
        startTimerHz (30);
        grabKeyboardFocus();
    }
    else
        stopTimer();
}

void KidsView::timerCallback()
{
    const double now = nowSeconds();
    if (session.kidsPresses() != seenPresses)
    {
        const int added = std::min (3, session.kidsPresses() - seenPresses);
        seenPresses = session.kidsPresses();
        for (int i = 0; i < added; ++i)
            burst (now);
    }
    if (playingNow())
        phase += 1.0 / 30.0;
    std::erase_if (notes, [now] (const Note& n) { return now - n.born > kNoteLife; });
    std::erase_if (rings, [now] (double born) { return now - born > kRingLife; });
    if (! hasKeyboardFocus (true) && isShowing())
        grabKeyboardFocus();
    repaint();
}

void KidsView::burst (double now)
{
    if (notes.size() < 40)
        notes.push_back ({ now, random.nextFloat() * juce::MathConstants<float>::twoPi, random.nextInt (4) });
    if (rings.size() < 6)
        rings.push_back (now);
}

juce::Rectangle<float> KidsView::circleBounds() const
{
    auto area = getLocalBounds().withTrimmedBottom (kBarHeight).toFloat();
    const float d = std::min (area.getWidth(), area.getHeight()) * 0.42f;
    return juce::Rectangle<float> (d, d).withCentre (area.getCentre().translated (0.0f, area.getHeight() * 0.04f));
}

void KidsView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    const bool playing = playingNow();
    const double now = nowSeconds();
    const auto accent = t.colour ("accent");

    g.fillAll (t.colour ("background"));
    const auto circle = circleBounds();
    const auto centre = circle.getCentre();
    {
        const float glow = std::max (static_cast<float> (getWidth()), static_cast<float> (getHeight())) * 0.6f;
        juce::ColourGradient grad (accent.withAlpha (playing ? 0.30f : 0.14f), centre, accent.withAlpha (0.0f), centre.translated (glow, 0.0f), true);
        g.setGradientFill (grad);
        g.fillRect (getLocalBounds().withTrimmedBottom (kBarHeight));
    }

    // The words at the top.
    const auto words = getLocalBounds().withTrimmedBottom (kBarHeight).toFloat();
    const float titleSize = juce::jlimit (28.0f, 64.0f, circle.getWidth() * 0.16f);
    g.setColour (t.colour ("text"));
    g.setFont (t.font (titleSize, true));
    const auto titleBox = juce::Rectangle<float> (words.getX(), words.getY(), words.getWidth(), circle.getY() - words.getY()).reduced (16.0f, 0.0f);
    g.drawFittedText (playing ? "Keep playing!" : "Press any key to play!", titleBox.toNearestInt(), juce::Justification::centred, 1);

    // Rings that go out from the circle on each key.
    for (const double born : rings)
    {
        const float age = static_cast<float> ((now - born) / kRingLife);
        const float grow = 1.0f + age * 0.55f;
        g.setColour (accent.withAlpha (0.55f * (1.0f - age)));
        g.drawEllipse (juce::Rectangle<float> (circle.getWidth() * grow, circle.getHeight() * grow).withCentre (centre), 6.0f * (1.0f - age) + 1.0f);
    }

    // The big circle: it breathes while the song plays.
    const float pulse = playing ? 1.0f + 0.035f * static_cast<float> (std::sin (phase * juce::MathConstants<double>::twoPi * 1.2)) : 1.0f;
    const auto disc = juce::Rectangle<float> (circle.getWidth() * pulse, circle.getHeight() * pulse).withCentre (centre);
    if (playing)
    {
        g.setColour (accent);
        g.fillEllipse (disc);
    }
    else
    {
        g.setColour (t.colour ("panelRaised"));
        g.fillEllipse (disc);
        g.setColour (accent);
        g.drawEllipse (disc.reduced (4.0f), 8.0f);
    }

    const float r = disc.getWidth() * 0.5f;
    if (playing)
    {
        // Five dancing bars.
        const auto ink = t.colour ("accentText");
        const float barW = r * 0.16f, gap = r * 0.09f;
        const float total = barW * 5.0f + gap * 4.0f;
        for (int i = 0; i < 5; ++i)
        {
            const double wave = std::sin (phase * 7.0 + i * 1.3) * 0.5 + 0.5;
            const float h = r * (0.35f + 0.65f * static_cast<float> (wave));
            const float x = centre.x - total * 0.5f + static_cast<float> (i) * (barW + gap);
            g.setColour (ink);
            g.fillRoundedRectangle (x, centre.y + r * 0.5f - h, barW, h, barW * 0.5f);
        }
    }
    else
    {
        juce::Path play;
        const float s = r * 0.42f;
        play.addTriangle (centre.x - s * 0.7f, centre.y - s, centre.x - s * 0.7f, centre.y + s, centre.x + s, centre.y);
        g.setColour (accent);
        g.fillPath (play.createPathWithRoundedCorners (s * 0.2f));
    }

    // Notes that fly out on each key.
    for (const auto& n : notes)
    {
        const float age = static_cast<float> ((now - n.born) / kNoteLife);
        const float dist = r * (1.05f + age * 1.1f);
        const auto at = centre + juce::Point<float> (std::cos (n.angle), std::sin (n.angle)) * dist + juce::Point<float> (0.0f, -age * r * 0.4f);
        drawNote (g, at, r * 0.32f, noteColour (n.colour).withAlpha (1.0f - age * age));
    }

    // The song's name under the circle.
    const auto& name = session.song().title;
    if (! name.empty())
    {
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (juce::jlimit (16.0f, 28.0f, titleSize * 0.45f)));
        const auto below = juce::Rectangle<float> (words.getX(), circle.getBottom() + circle.getHeight() * 0.06f, words.getWidth(), titleSize).toNearestInt();
        g.drawFittedText (juce::String::fromUTF8 (name.c_str()), below.reduced (16, 0), juce::Justification::centredTop, 1);
    }

    // The bar for grown-ups.
    g.setColour (t.colour ("panel"));
    g.fillRect (controlBar);
    g.setColour (t.colour ("line"));
    g.fillRect (controlBar.withHeight (1));
}

void KidsView::resized()
{
    controlBar = getLocalBounds().removeFromBottom (kBarHeight);
    auto bar = controlBar.reduced (16, 10);
    exitButton.setBounds (bar.removeFromRight (140));
    bar.removeFromRight (16);
    pauseLabel.setBounds (bar.removeFromLeft (90));
    bar.removeFromLeft (6);
    pauseSlider.setBounds (bar.removeFromLeft (std::min (260, bar.getWidth() / 2)));
    bar.removeFromLeft (20);
    keysSound.setBounds (bar.removeFromLeft (std::min (300, bar.getWidth())));
}

bool KidsView::keyPressed (const juce::KeyPress& key)
{
    // Shortcuts with Cmd or Ctrl still work (Quit, Kids Mode itself).
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;
    session.kidsTap();
    return true;
}

void KidsView::mouseDown (const juce::MouseEvent& e)
{
    if (circleBounds().expanded (20.0f).contains (e.position))
        session.kidsTap();
}

} // namespace arranger::ui
