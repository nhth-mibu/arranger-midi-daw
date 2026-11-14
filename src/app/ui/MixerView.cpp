#include "MixerView.h"

#include <cmath>

namespace arranger::ui
{

namespace
{
    constexpr int kStripWidth = 96;
    constexpr int kMasterWidth = 116;
    constexpr int kGap = 6;

    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    /** GM volume curve: CC 7 to decibels. */
    float volumeToDb (int v) { return v <= 0 ? -100.0f : 40.0f * std::log10 (static_cast<float> (v) / 127.0f); }

    /** Meter scale: -60 dB at the bottom, +6 at the top. */
    float levelToMeter (float linear)
    {
        if (linear <= 0.000001f)
            return 0.0f;
        const float db = 20.0f * std::log10 (linear);
        return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    }

    juce::String panText (int pan)
    {
        if (pan == 64)
            return "C";
        return pan < 64 ? "L" + juce::String (64 - pan) : "R" + juce::String (pan - 64);
    }

    int gesture = 0;
}

/**
    One mixer strip, drawn by hand so it can be small and still easy to grab.
    A strip for TrackId 0 is the master.
*/
class ChannelStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChannelStrip (Session& s, TrackId id) : session (s), track (id) {}

    TrackId trackId() const noexcept { return track; }

    void setLevel (float left, float right)
    {
        // Fast attack, slow release.
        const float l = levelToMeter (left), r = levelToMeter (right);
        meterL = l > meterL ? l : meterL * 0.86f;
        meterR = r > meterR ? r : meterR * 0.86f;
        holdL = std::max (holdL * 0.995f, meterL);
        holdR = std::max (holdR * 0.995f, meterR);
        const int shown = juce::roundToInt ((meterL + meterR) * 200.0f);
        if (shown != lastShown)
        {
            lastShown = shown;
            repaint (meterArea().expanded (2.0f).getSmallestIntegerContainer());
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto r = getLocalBounds().toFloat();
        const bool isMaster = track == 0;
        const auto* tr = isMaster ? nullptr : edit::findTrack (session.song(), track);
        if (! isMaster && tr == nullptr)
            return;
        const bool selected = ! isMaster && session.selectedTrack() == track;
        const auto colour = isMaster ? t.colour ("accent") : t.trackColour (tr->colour);

        g.setColour (selected ? t.colour ("panelRaised").brighter (0.06f) : t.colour ("panelRaised"));
        g.fillRoundedRectangle (r, 8.0f);
        if (selected)
        {
            g.setColour (colour.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.2f);
        }
        g.setColour (colour);
        g.fillRoundedRectangle (r.withHeight (4.0f).reduced (10.0f, 0.0f).translated (0.0f, 6.0f), 2.0f);

        // Name and output
        g.setColour (t.colour ("text"));
        g.setFont (t.font (13.0f, true));
        g.drawText (isMaster ? juce::String ("Master") : utf8 (tr->name), nameArea(), juce::Justification::centred, true);
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (11.0f));
        juce::String sub;
        if (isMaster)
            sub = "Built-in";
        else if (tr->output.kind == TrackOutput::Kind::none)
            sub = "No output";
        else if (tr->output.kind == TrackOutput::Kind::sampler)
            sub = "Sampler";
        else
            sub = (tr->output.kind == TrackOutput::Kind::soundFont ? juce::String ("SF") : utf8 (tr->output.port).substring (0, 10)) + " " + juce::String (tr->output.channel);
        if (showsSends())
            g.drawText (sub, subArea(), juce::Justification::centred, true);

        if (! isMaster)
        {
            if (showsSends())
            {
                drawKnob (g, knobArea (0), tr->mixer.reverbSend, 0, "Rev", colour);
                drawKnob (g, knobArea (1), tr->mixer.chorusSend, 0, "Cho", colour);
            }
            if (showsPan())
                drawKnob (g, panArea(), tr->mixer.pan, 64, panText (tr->mixer.pan), colour);

            drawButton (g, buttonArea (0), "M", tr->mixer.mute, t.colour ("warning"));
            drawButton (g, buttonArea (1), "S", tr->mixer.solo, t.colour ("ok"));
            drawButton (g, buttonArea (2), "R", tr->armed, t.colour ("recording"));
        }
        else if (showsSends())
        {
            g.setColour (t.colour ("textDim"));
            g.setFont (t.font (11.0f));
            g.drawFittedText (session.soundFontStatus(), panArea().withHeight (54.0f).expanded (20.0f, 0.0f).toNearestInt(), juce::Justification::centred, 3);
        }

        // Fader track with scale
        const auto fader = faderArea();
        const float pos = faderPosition();
        g.setColour (t.isDark ? t.colour ("background") : t.colour ("lineStrong"));
        g.fillRoundedRectangle (juce::Rectangle<float> (fader.getCentreX() - 2.0f, fader.getY(), 4.0f, fader.getHeight()), 2.0f);
        g.setColour (colour.withAlpha (0.55f));
        const float capY = fader.getBottom() - pos * fader.getHeight();
        g.fillRoundedRectangle (juce::Rectangle<float> (fader.getCentreX() - 2.0f, capY, 4.0f, fader.getBottom() - capY), 2.0f);

        g.setFont (t.font (9.5f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float p = isMaster ? (db + 60.0f) / 66.0f : std::pow (10.0f, db / 40.0f);
            if (p > 1.0f)
                continue;
            const float y = fader.getBottom() - p * fader.getHeight();
            g.setColour (t.colour ("line"));
            g.fillRect (fader.getX(), y, 6.0f, 1.0f);
            g.setColour (t.colour ("textDim"));
            g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (fader.getX() - 26.0f, y - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight, false);
        }

        // Cap
        const auto cap = juce::Rectangle<float> (fader.getCentreX() - 14.0f, capY - 9.0f, 28.0f, 18.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 4.0f);
        g.setColour (t.isDark ? juce::Colour (0xffd8d2c6) : juce::Colour (0xfffbf8f2));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colour (0xff4a443b));
        g.fillRect (cap.getX() + 5.0f, cap.getCentreY() - 0.75f, cap.getWidth() - 10.0f, 1.5f);

        drawMeter (g);

        // Value
        g.setColour (t.colour ("text"));
        g.setFont (t.font (12.0f, true));
        juce::String value;
        if (isMaster)
            value = juce::String (session.song().master.gainDb, 1) + " dB";
        else if (tr->mixer.volume == 0)
            value = "-inf";
        else
            value = juce::String (volumeToDb (tr->mixer.volume), 1) + " dB";
        g.drawText (value, valueArea(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        ++gesture;
        active = Part::none;
        const auto p = e.position;
        if (track != 0)
        {
            session.selectTrack (track);
            for (int i = 0; i < 3; ++i)
                if (buttonArea (i).contains (p))
                {
                    toggle (i);
                    return;
                }
            if (knobArea (0).expanded (4.0f).contains (p)) active = Part::reverb;
            else if (knobArea (1).expanded (4.0f).contains (p)) active = Part::chorus;
            else if (panArea().expanded (4.0f).contains (p)) active = Part::pan;
        }
        if (faderArea().expanded (16.0f, 12.0f).contains (p))
        {
            active = Part::fader;
            // Click beside the cap jumps; grabbing the cap keeps the offset.
            const auto fader = faderArea();
            const float capY = fader.getBottom() - faderPosition() * fader.getHeight();
            grabOffset = std::abs (p.y - capY) < 12.0f ? p.y - capY : 0.0f;
            if (grabOffset == 0.0f)
                setFaderFromY (p.y);
        }
        startValue = currentValue();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        switch (active)
        {
            case Part::fader: setFaderFromY (e.position.y - grabOffset); break;
            case Part::reverb:
            case Part::chorus:
            case Part::pan:
            {
                const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
                const int v = juce::jlimit (0, 127, startValue - juce::roundToInt (static_cast<float> (e.getDistanceFromDragStartY()) * 0.6f * fine));
                setValue (active, v);
                break;
            }
            case Part::none: break;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        active = Part::none;
        session.endMerge();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Double-click resets to the default.
        const auto p = e.position;
        if (track == 0)
        {
            if (faderArea().expanded (16.0f, 12.0f).contains (p))
                session.edit ("Reset Master", [] (Song& s) { s.master.gainDb = 0.0f; });
            return;
        }
        if (knobArea (0).contains (p)) setValue (Part::reverb, 40, "reset");
        else if (knobArea (1).contains (p)) setValue (Part::chorus, 0, "reset");
        else if (panArea().contains (p)) setValue (Part::pan, 64, "reset");
        else if (faderArea().expanded (16.0f, 12.0f).contains (p)) setValue (Part::fader, 100, "reset");
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int step = w.deltaY > 0 ? 1 : -1;
        const auto p = e.position;
        Part part = Part::none;
        if (track != 0 && knobArea (0).contains (p)) part = Part::reverb;
        else if (track != 0 && knobArea (1).contains (p)) part = Part::chorus;
        else if (track != 0 && panArea().contains (p)) part = Part::pan;
        else if (faderArea().expanded (16.0f, 12.0f).contains (p)) part = Part::fader;
        if (part == Part::none)
        {
            Component::mouseWheelMove (e, w);
            return;
        }
        active = part;
        if (track == 0)
            session.edit ("Master Level", [step] (Song& s) { s.master.gainDb = juce::jlimit (-60.0f, 6.0f, s.master.gainDb + 0.5f * static_cast<float> (step)); }, "wheel-master");
        else
            setValue (part, juce::jlimit (0, 127, currentValue() + step), "wheel");
        active = Part::none;
    }

private:
    enum class Part { none, fader, pan, reverb, chorus };

    juce::Rectangle<int> nameArea() const { return { 4, 14, getWidth() - 8, 20 }; }
    // The strip drops parts when it is short: first the sends and the output line, then the pan.
    bool showsSends() const { return getHeight() >= 330; }
    bool showsPan() const { return getHeight() >= 250; }
    float sendsY() const { return showsSends() ? 58.0f : 38.0f; }
    float panY() const { return sendsY() + (showsSends() ? 50.0f : 0.0f); }
    float buttonsY() const { return panY() + (showsPan() ? 64.0f : 4.0f); }

    juce::Rectangle<int> subArea() const { return { 4, 34, getWidth() - 8, showsSends() ? 16 : 0 }; }
    juce::Rectangle<float> knobArea (int i) const
    {
        if (! showsSends())
            return { 0.0f, -400.0f, 0.0f, 0.0f };
        const float w = static_cast<float> (getWidth()) / 2.0f;
        return { static_cast<float> (i) * w + (w - 34.0f) / 2.0f, sendsY(), 34.0f, 46.0f };
    }
    juce::Rectangle<float> panArea() const
    {
        if (! showsPan())
            return { 0.0f, -400.0f, 0.0f, 0.0f };
        return { static_cast<float> (getWidth()) / 2.0f - 22.0f, panY(), 44.0f, 56.0f };
    }
    juce::Rectangle<float> buttonArea (int i) const
    {
        const float w = 24.0f, gap = 4.0f, total = 3.0f * w + 2.0f * gap;
        return { (static_cast<float> (getWidth()) - total) / 2.0f + static_cast<float> (i) * (w + gap), buttonsY(), w, 22.0f };
    }
    juce::Rectangle<float> faderArea() const
    {
        const float top = buttonsY() + (track == 0 ? 0.0f : 38.0f);
        return { static_cast<float> (getWidth()) / 2.0f - 4.0f, top, 8.0f, std::max (40.0f, static_cast<float> (getHeight()) - top - 44.0f) };
    }
    juce::Rectangle<float> meterArea() const
    {
        const auto f = faderArea();
        return { f.getRight() + 18.0f, f.getY(), 10.0f, f.getHeight() };
    }
    juce::Rectangle<int> valueArea() const { return { 4, getHeight() - 32, getWidth() - 8, 22 }; }

    float faderPosition() const
    {
        if (track == 0)
            return juce::jlimit (0.0f, 1.0f, (session.song().master.gainDb + 60.0f) / 66.0f);
        const auto* tr = edit::findTrack (session.song(), track);
        return tr == nullptr ? 0.0f : static_cast<float> (tr->mixer.volume) / 127.0f;
    }

    int currentValue() const
    {
        const auto* tr = edit::findTrack (session.song(), track);
        if (tr == nullptr)
            return 0;
        switch (active)
        {
            case Part::fader: return tr->mixer.volume;
            case Part::pan: return tr->mixer.pan;
            case Part::reverb: return tr->mixer.reverbSend;
            case Part::chorus: return tr->mixer.chorusSend;
            case Part::none: break;
        }
        return 0;
    }

    void setFaderFromY (float y)
    {
        const auto f = faderArea();
        const float pos = juce::jlimit (0.0f, 1.0f, (f.getBottom() - y) / f.getHeight());
        if (track == 0)
        {
            const float db = std::round ((pos * 66.0f - 60.0f) * 10.0f) / 10.0f;
            session.edit ("Master Level", [db] (Song& s) { s.master.gainDb = db; }, "master-" + std::to_string (gesture));
            return;
        }
        setValue (Part::fader, juce::roundToInt (pos * 127.0f));
    }

    void setValue (Part part, int v, const std::string& keyPrefix = "drag")
    {
        static const char* names[] = { "", "Volume", "Pan", "Reverb", "Chorus" };
        const auto id = track;
        session.edit (std::string ("Change ") + names[static_cast<int> (part)],
                      [id, part, v] (Song& s) {
                          if (auto* t = edit::findTrack (s, id))
                          {
                              switch (part)
                              {
                                  case Part::fader: t->mixer.volume = v; break;
                                  case Part::pan: t->mixer.pan = v; break;
                                  case Part::reverb: t->mixer.reverbSend = v; break;
                                  case Part::chorus: t->mixer.chorusSend = v; break;
                                  case Part::none: break;
                              }
                          }
                      },
                      keyPrefix + "-mixer-" + std::to_string (id) + "-" + std::to_string (static_cast<int> (part)) + "-" + std::to_string (gesture));
    }

    void toggle (int which)
    {
        const auto id = track;
        static const char* names[] = { "Mute", "Solo", "Record Arm" };
        session.edit (names[which], [id, which] (Song& s) {
            auto* t = edit::findTrack (s, id);
            if (t == nullptr)
                return;
            if (which == 0) t->mixer.mute = ! t->mixer.mute;
            if (which == 1) t->mixer.solo = ! t->mixer.solo;
            if (which == 2)
            {
                const bool on = ! t->armed;
                for (auto& other : s.tracks)
                    other.armed = false; // one armed track at a time
                t->armed = on;
            }
        });
    }

    void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, int value, int centre, const juce::String& label, juce::Colour colour) const
    {
        const auto& t = currentTheme();
        const auto circle = area.removeFromTop (area.getWidth()).reduced (3.0f);
        const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
        const auto angle = [&] (int v) { return start + (end - start) * static_cast<float> (v) / 127.0f; };
        const auto c = circle.getCentre();
        const float radius = circle.getWidth() / 2.0f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
        g.setColour (t.isDark ? t.colour ("background") : t.colour ("lineStrong"));
        g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path arc;
        const float a0 = angle (centre), a1 = angle (value);
        arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, std::min (a0, a1), std::max (a0, a1) + 0.001f, true);
        g.setColour (colour);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (t.colour ("panel"));
        g.fillEllipse (circle.reduced (5.0f));
        const float a = angle (value) - juce::MathConstants<float>::halfPi * 0.0f;
        const juce::Point<float> tip (c.x + (radius - 7.0f) * std::sin (a), c.y - (radius - 7.0f) * std::cos (a));
        g.setColour (t.colour ("text"));
        g.drawLine ({ c, tip }, 1.6f);

        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (10.5f));
        g.drawText (label, area, juce::Justification::centredTop, false);
    }

    void drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool on, juce::Colour onColour) const
    {
        const auto& t = currentTheme();
        g.setColour (on ? onColour : t.colour ("panel"));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? onColour.darker (0.3f) : t.colour ("lineStrong"));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (on ? t.colour ("accentText") : t.colour ("textMuted"));
        g.setFont (t.font (11.5f, true));
        g.drawText (text, r, juce::Justification::centred, false);
    }

    void drawMeter (juce::Graphics& g) const
    {
        const auto& t = currentTheme();
        const auto m = meterArea();
        const auto* tr = track == 0 ? nullptr : edit::findTrack (session.song(), track);
        const bool hasAudio = track == 0 || (tr != nullptr && (tr->output.kind == TrackOutput::Kind::soundFont || tr->output.kind == TrackOutput::Kind::sampler || tr->output.kind == TrackOutput::Kind::audio));
        const float w = (m.getWidth() - 2.0f) / 2.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto bar = juce::Rectangle<float> (m.getX() + static_cast<float> (ch) * (w + 2.0f), m.getY(), w, m.getHeight());
            g.setColour (t.isDark ? t.colour ("background") : t.colour ("line"));
            g.fillRoundedRectangle (bar, 1.5f);
            if (! hasAudio)
                continue;
            const float level = ch == 0 ? meterL : meterR;
            const float hold = ch == 0 ? holdL : holdR;
            const auto lit = bar.withTop (bar.getBottom() - level * bar.getHeight());
            juce::ColourGradient grad (t.colour ("ok"), 0.0f, bar.getBottom(), t.colour ("recording"), 0.0f, bar.getY(), false);
            grad.addColour (0.82, t.colour ("warning"));
            g.setGradientFill (grad);
            g.fillRect (lit);
            if (hold > 0.01f)
            {
                g.setColour (t.colour ("text").withAlpha (0.7f));
                g.fillRect (bar.getX(), bar.getBottom() - hold * bar.getHeight(), bar.getWidth(), 1.0f);
            }
        }
        if (! hasAudio)
        {
            g.setColour (t.colour ("textDim"));
            g.setFont (t.font (9.0f));
            g.drawText ("MIDI", juce::Rectangle<float> (m.getX() - 6.0f, m.getBottom() + 2.0f, 22.0f, 10.0f), juce::Justification::centred, false);
        }
    }

    Session& session;
    TrackId track;
    Part active = Part::none;
    int startValue = 0;
    float grabOffset = 0.0f;
    float meterL = 0.0f, meterR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int lastShown = -1;
};

// =====================================================================================
MixerView::MixerView (Session& s) : session (s)
{
    session.addChangeListener (this);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    master = std::make_unique<ChannelStrip> (session, 0);
    addAndMakeVisible (*master);
    rebuild();
}

MixerView::~MixerView() { session.removeChangeListener (this); }

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    std::vector<TrackId> ids;
    for (const auto& t : session.song().tracks)
        ids.push_back (t.id);
    if (ids != shownTracks)
        rebuild();
    for (auto& s : strips)
        s->repaint();
    master->repaint();
}

void MixerView::rebuild()
{
    shownTracks.clear();
    strips.clear();
    for (const auto& t : session.song().tracks)
    {
        shownTracks.push_back (t.id);
        auto strip = std::make_unique<ChannelStrip> (session, t.id);
        holder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    master->setBounds (r.removeFromRight (kMasterWidth));
    r.removeFromRight (12);
    viewport.setBounds (r);

    const int h = r.getHeight() - (static_cast<int> (strips.size()) * (kStripWidth + kGap) > r.getWidth() ? viewport.getScrollBarThickness() : 0);
    int x = 0;
    for (auto& s : strips)
    {
        s->setBounds (x, 0, kStripWidth, h);
        x += kStripWidth + kGap;
    }
    holder.setSize (std::max (x, r.getWidth()), h);
}

void MixerView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    if (strips.empty())
    {
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (14.0f));
        g.drawText ("Add a track to see its mixer strip", viewport.getBounds(), juce::Justification::centred);
    }
}

void MixerView::refresh()
{
    // Each SoundFont channel's level goes to every track that plays on it.
    std::array<float, 16> channel {};
    if (const auto font = session.soundFont())
        for (int ch = 0; ch < 16; ++ch)
            channel[static_cast<std::size_t> (ch)] = font->takeChannelPeak (ch);

    for (auto& s : strips)
    {
        const auto* t = edit::findTrack (session.song(), s->trackId());
        float level = 0.0f;
        if (t != nullptr && t->output.kind == TrackOutput::Kind::soundFont && ! session.isMuted (*t))
            level = channel[static_cast<std::size_t> (juce::jlimit (1, 16, t->output.channel) - 1)];
        else if (auto* sampler = session.samplerFor (s->trackId()))
            level = sampler->takePeak();
        s->setLevel (level, level);
    }
    master->setLevel (session.masterPeak (0), session.masterPeak (1));
}

} // namespace arranger::ui
