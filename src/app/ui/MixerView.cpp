#include "MixerView.h"

#include <cmath>

namespace arranger::ui
{

namespace
{
    constexpr int kStripWidth = 96;
    constexpr int kMasterWidth = 116;
    constexpr int kGap = 6;
    constexpr int kBusGap = 14;

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
class ChannelStrip final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    ChannelStrip (Session& s, MixerView& m, TrackId id) : session (s), mixer (m), track (id)
    {
        shownFader = targetFader = faderPosition();
    }

    TrackId trackId() const noexcept { return track; }

    /** Moves the drawn fader smoothly to the current value, unless the user is dragging it. */
    void syncFader (bool animate)
    {
        const float target = faderPosition();
        if (! animate || active == Part::fader || ! isShowing())
        {
            shownFader = targetFader = target;
            stopTimer();
            repaint();
            return;
        }
        if (std::abs (target - targetFader) < 0.0001f && isTimerRunning())
            return;
        if (std::abs (target - shownFader) < 0.0001f)
        {
            shownFader = targetFader = target;
            stopTimer();
            repaint();
            return;
        }
        startFader = shownFader;
        targetFader = target;
        faderStartMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    bool isAnimatingFader() const noexcept { return isTimerRunning(); }

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

    void timerCallback() override
    {
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - faderStartMs;
        const float t = juce::jlimit (0.0f, 1.0f, static_cast<float> (elapsed / 240.0));
        const float eased = 1.0f - std::pow (1.0f - t, 3.0f);
        shownFader = startFader + (targetFader - startFader) * eased;
        if (t >= 1.0f)
        {
            shownFader = targetFader;
            stopTimer();
            repaint(); // also restores the value text colour
            return;
        }
        repaint (faderArea().expanded (24.0f).getSmallestIntegerContainer());
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto r = getLocalBounds().toFloat();
        const bool isMaster = track == 0;
        const auto* bus = isMaster ? nullptr : edit::findBus (session.song(), track);
        if (bus != nullptr)
        {
            paintBus (g, *bus);
            return;
        }
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
        else if (tr->output.kind == TrackOutput::Kind::soundFont)
            sub = session.soundNameFor (*tr);
        else
            sub = utf8 (tr->output.port).substring (0, 10) + " " + juce::String (tr->output.channel);
        if (! isMaster)
            if (const auto* to = edit::findBus (session.song(), tr->bus))
                sub = juce::String::fromUTF8 ("\xe2\x86\x92 ") + utf8 (to->name); // the bus it goes to
        if (showsSends())
            g.drawFittedText (sub, subArea(), juce::Justification::centred, 1, 0.72f);

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

        paintFader (g, colour, isMaster);

        // Value
        g.setColour (t.colour (isAnimatingFader() ? "accent" : "text"));
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
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        if (isBus())
        {
            if (buttonArea (1).contains (p))
            {
                const auto id = track;
                session.edit ("Mute Bus", [id] (Song& s) { if (auto* b = edit::findBus (s, id)) b->mute = ! b->mute; });
                return;
            }
            if (effectsArea().contains (p))
            {
                if (mixer.onShowEffects)
                    mixer.onShowEffects (track, this);
                return;
            }
            if (panArea().expanded (4.0f).contains (p))
                active = Part::pan;
        }
        else if (track != 0)
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
        if (faderArea().expanded (16.0f, 16.0f).contains (p))
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
            if (faderArea().expanded (16.0f, 16.0f).contains (p))
                session.edit ("Reset Master", [] (Song& s) { s.master.gainDb = 0.0f; });
            return;
        }
        if (isBus())
        {
            if (nameArea().toFloat().contains (p))
                rename();
            else if (panArea().contains (p))
                setValue (Part::pan, 64, "reset");
            else if (faderArea().expanded (16.0f, 16.0f).contains (p))
                setValue (Part::fader, 100, "reset");
            return;
        }
        if (knobArea (0).contains (p)) setValue (Part::reverb, 40, "reset");
        else if (knobArea (1).contains (p)) setValue (Part::chorus, 0, "reset");
        else if (panArea().contains (p)) setValue (Part::pan, 64, "reset");
        else if (faderArea().expanded (16.0f, 16.0f).contains (p)) setValue (Part::fader, 100, "reset");
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int step = w.deltaY > 0 ? 1 : -1;
        const auto p = e.position;
        Part part = Part::none;
        if (track != 0 && ! isBus() && knobArea (0).contains (p)) part = Part::reverb;
        else if (track != 0 && ! isBus() && knobArea (1).contains (p)) part = Part::chorus;
        else if (track != 0 && panArea().contains (p)) part = Part::pan;
        else if (faderArea().expanded (16.0f, 16.0f).contains (p)) part = Part::fader;
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

    bool isBus() const { return track != 0 && edit::findBus (session.song(), track) != nullptr; }
    juce::Rectangle<float> effectsArea() const
    {
        if (! showsSends())
            return { 0.0f, -400.0f, 0.0f, 0.0f };
        return { 10.0f, sendsY() + 6.0f, static_cast<float> (getWidth()) - 20.0f, 26.0f };
    }

    void paintBus (juce::Graphics& g, const Bus& bus)
    {
        const auto& t = currentTheme();
        const auto r = getLocalBounds().toFloat();
        const auto colour = t.trackColour (bus.colour);
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colour.withAlpha (0.45f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.withHeight (4.0f).reduced (10.0f, 0.0f).translated (0.0f, 6.0f), 2.0f);

        g.setColour (t.colour ("text"));
        g.setFont (t.font (13.0f, true));
        g.drawText (utf8 (bus.name), nameArea(), juce::Justification::centred, true);
        if (showsSends())
        {
            // What goes in: tracks on the bus, and tracks that send to it.
            int in = 0, sends = 0;
            for (const auto& tr : session.song().tracks)
            {
                in += tr.bus == bus.id ? 1 : 0;
                sends += edit::sendLevel (tr, bus.id) > 0 ? 1 : 0;
            }
            g.setColour (t.colour ("textMuted"));
            g.setFont (t.font (11.0f));
            juce::String sub = "Bus";
            if (in > 0)
                sub = juce::String (in) + (in == 1 ? " track" : " tracks");
            if (sends > 0)
                sub += (in > 0 ? ", " : ": ") + juce::String (sends) + (sends == 1 ? " send" : " sends");
            g.drawFittedText (sub, subArea(), juce::Justification::centred, 1, 0.72f);

            const auto fx = effectsArea();
            g.setColour (t.colour ("panel"));
            g.fillRoundedRectangle (fx, 5.0f);
            g.setColour (t.colour ("lineStrong"));
            g.drawRoundedRectangle (fx.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (bus.effects.empty() ? t.colour ("textMuted") : t.colour ("text"));
            g.setFont (t.font (11.5f, true));
            g.drawText (bus.effects.empty() ? juce::String ("Effects") : "Effects (" + juce::String (static_cast<int> (bus.effects.size())) + ")", fx, juce::Justification::centred, false);
        }
        if (showsPan())
            drawKnob (g, panArea(), bus.pan, 64, panText (bus.pan), colour);
        drawButton (g, buttonArea (1), "M", bus.mute, t.colour ("warning"));
        paintFader (g, colour, false);
        g.setColour (t.colour (isAnimatingFader() ? "accent" : "text"));
        g.setFont (t.font (12.0f, true));
        g.drawText (bus.volume == 0 ? juce::String ("-inf") : juce::String (volumeToDb (bus.volume), 1) + " dB", valueArea(), juce::Justification::centred, false);
    }

    void paintFader (juce::Graphics& g, juce::Colour colour, bool isMaster)
    {
        const auto& t = currentTheme();
        const auto fader = faderArea();
        const float pos = shownFader >= 0.0f ? shownFader : faderPosition();
        const float capY = fader.getBottom() - pos * fader.getHeight();

        // A recessed slot with a brushed metal cap.
        const auto slot = juce::Rectangle<float> (fader.getCentreX() - 3.5f, fader.getY(), 7.0f, fader.getHeight());
        if (isAnimatingFader())
        {
            const auto phase = static_cast<float> (juce::Time::getMillisecondCounterHiRes() - faderStartMs);
            g.setColour (colour.withAlpha (0.20f + 0.07f * std::sin (phase * 0.024f)));
            g.fillRoundedRectangle (slot.expanded (5.0f, 4.0f), 7.0f);
        }
        g.setColour (t.colour ("faderEdge"));
        g.fillRoundedRectangle (slot, 3.5f);
        g.setColour (t.colour ("faderLine").withAlpha (0.22f));
        g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, 1.0f);

        g.setColour (colour.withAlpha (0.60f));
        g.fillRoundedRectangle (juce::Rectangle<float> (fader.getCentreX() - 1.5f, capY, 3.0f, std::max (0.0f, fader.getBottom() - capY)), 1.5f);

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

        const auto cap = juce::Rectangle<float> (fader.getCentreX() - 15.0f, capY - 14.0f, 30.0f, 28.0f);
        g.setColour (juce::Colours::black.withAlpha (0.50f));
        g.fillRoundedRectangle (cap.translated (0.0f, 3.0f), 5.0f);

        juce::ColourGradient metal (t.colour ("faderFaceTop"), cap.getX(), cap.getY(),
                                    t.colour ("faderFaceBottom"), cap.getX(), cap.getBottom(), false);
        metal.addColour (0.55, t.colour ("faderFaceTop").interpolatedWith (t.colour ("faderFaceBottom"), 0.45f));
        g.setGradientFill (metal);
        g.fillRoundedRectangle (cap, 5.0f);

        g.setColour (t.colour ("faderEdge"));
        g.drawRoundedRectangle (cap.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRoundedRectangle (cap.withHeight (1.5f).reduced (6.0f, 0.0f), 0.75f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (cap.getX() + 3.0f, cap.getBottom() - 2.0f, cap.getWidth() - 6.0f, 1.0f);

        // The dark groove splits the bright metal face, like a machined fader cap.
        const float grooveY = cap.getY() + cap.getHeight() * 0.58f;
        g.setColour (t.colour ("faderEdge").withAlpha (0.9f));
        g.fillRoundedRectangle (juce::Rectangle<float> (cap.getX() + 4.0f, grooveY, cap.getWidth() - 8.0f, 2.5f), 1.25f);
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.fillRect (cap.getX() + 5.0f, grooveY + 2.5f, cap.getWidth() - 10.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillRect (cap.getX() + 5.0f, cap.getY() + 3.0f, 1.0f, cap.getHeight() - 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRect (cap.getRight() - 5.0f, cap.getY() + 3.0f, 1.0f, cap.getHeight() - 8.0f);

        drawMeter (g);
    }

    void rename()
    {
        const auto* bus = edit::findBus (session.song(), track);
        if (bus == nullptr)
            return;
        nameEditor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible (*nameEditor);
        nameEditor->setFont (currentTheme().font (13.0f, true));
        nameEditor->setJustification (juce::Justification::centred);
        nameEditor->setText (utf8 (bus->name), false);
        nameEditor->setBounds (nameArea().expanded (0, 3));
        nameEditor->selectAll();
        nameEditor->grabKeyboardFocus();
        const auto id = track;
        auto commit = [this, id] {
            if (nameEditor == nullptr)
                return;
            const auto text = nameEditor->getText().trim().toStdString();
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChannelStrip> (this)] { if (safe != nullptr) safe->nameEditor = nullptr; });
            if (! text.empty())
                session.edit ("Rename Bus", [id, text] (Song& s) { if (auto* b = edit::findBus (s, id)) b->name = text; });
        };
        nameEditor->onReturnKey = commit;
        nameEditor->onFocusLost = commit;
        nameEditor->onEscapeKey = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ChannelStrip> (this)] { if (safe != nullptr) safe->nameEditor = nullptr; }); };
    }

    /** The sends of a track: one slider for each bus. */
    class SendsPanel final : public juce::Component
    {
    public:
        SendsPanel (Session& s, TrackId t) : session (s), track (t)
        {
            const auto* tr = edit::findTrack (session.song(), track);
            for (const auto& b : session.song().buses)
            {
                auto row = std::make_unique<Row>();
                row->bus = b.id;
                row->label.setText (utf8 (b.name), juce::dontSendNotification);
                row->label.setFont (currentTheme().font (13.0f));
                row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
                row->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
                row->slider.setRange (0.0, 127.0, 1.0);
                row->slider.setDoubleClickReturnValue (true, 100.0);
                row->slider.setValue (tr != nullptr ? edit::sendLevel (*tr, b.id) : 0, juce::dontSendNotification);
                const auto bus = b.id;
                row->slider.onValueChange = [this, bus, r = row.get()] {
                    const int v = juce::roundToInt (r->slider.getValue());
                    const auto id = track;
                    session.edit ("Change Send", [id, bus, v] (Song& song) { edit::setSend (song, id, bus, v); }, "send-" + std::to_string (id) + "-" + std::to_string (bus) + "-" + std::to_string (gesture));
                };
                row->slider.onDragEnd = [this] { session.endMerge(); ++gesture; };
                addAndMakeVisible (row->label);
                addAndMakeVisible (row->slider);
                rows.push_back (std::move (row));
            }
            setSize (300, 36 + static_cast<int> (rows.size()) * 34);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = currentTheme();
            g.setColour (t.colour ("textMuted"));
            g.setFont (t.font (11.0f, true));
            g.drawText ("SENDS AFTER THE FADER", 12, 6, getWidth() - 24, 22, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            int y = 32;
            for (auto& r : rows)
            {
                r->label.setBounds (8, y, 92, 28);
                r->slider.setBounds (100, y, getWidth() - 108, 28);
                y += 34;
            }
        }

    private:
        struct Row
        {
            std::uint32_t bus = 0;
            juce::Label label;
            juce::Slider slider;
        };
        Session& session;
        TrackId track;
        std::vector<std::unique_ptr<Row>> rows;
    };

    void showMenu()
    {
        juce::PopupMenu m;
        const auto& song = session.song();
        const auto id = track;
        juce::Component::SafePointer<MixerView> view (&mixer);
        if (isBus())
        {
            m.addItem ("Rename", [safe = juce::Component::SafePointer<ChannelStrip> (this)] { if (safe != nullptr) safe->rename(); });
            m.addItem ("Effects...", [this, id] { if (mixer.onShowEffects) mixer.onShowEffects (id, this); });
            m.addSeparator();
            m.addItem ("Remove Bus", [this, id] {
                session.capturePluginStates();
                session.edit ("Remove Bus", [id] (Song& s) { edit::removeBus (s, id); });
            });
        }
        else if (const auto* tr = edit::findTrack (song, track))
        {
            if (edit::makesSound (*tr))
            {
                juce::PopupMenu output;
                output.addItem ("Master", true, tr->bus == 0, [this, id] { session.edit ("Change Output", [id] (Song& s) { edit::setTrackBus (s, id, 0); }); });
                for (const auto& b : song.buses)
                {
                    const auto bus = b.id;
                    output.addItem (utf8 (b.name), true, tr->bus == bus, [this, id, bus] { session.edit ("Change Output", [id, bus] (Song& s) { edit::setTrackBus (s, id, bus); }); });
                }
                m.addSubMenu ("Output", output);
                m.addItem ("Sends...", ! song.buses.empty(), false, [this, id] {
                    juce::CallOutBox::launchAsynchronously (std::make_unique<SendsPanel> (session, id), getScreenBounds(), nullptr);
                });
            }
            else
                m.addItem ("Output and sends: only for tracks whose sound the app makes", false, false, [] {});
            if (tr->output.kind != TrackOutput::Kind::audio)
            {
                // Which keyboard plays and records this track.
                const auto setInput = [this, id] (std::string port, int channel) {
                    session.edit ("Change Track Input", [id, port, channel] (Song& s) {
                        if (auto* t = edit::findTrack (s, id))
                        {
                            t->inputPort = port;
                            t->inputChannel = channel;
                        }
                    });
                };
                juce::PopupMenu input;
                const auto channel = tr->inputChannel;
                input.addItem ("All Inputs", true, tr->inputPort.empty(), [setInput, channel] { setInput ({}, channel); });
                for (const auto& in : session.deviceHost().midiInputs())
                {
                    if (! in.enabled || session.controllerFor (in.name) != nullptr)
                        continue;
                    input.addItem (utf8 (in.name), true, tr->inputPort == in.name, [setInput, port = in.name, channel] { setInput (port, channel); });
                }
                if (! tr->inputPort.empty())
                {
                    bool listed = false;
                    for (const auto& in : session.deviceHost().midiInputs())
                        listed = listed || in.name == tr->inputPort;
                    if (! listed)
                        input.addItem (utf8 (tr->inputPort) + " (not plugged in)", true, true, [] {});
                }
                input.addSeparator();
                juce::PopupMenu channels;
                const auto port = tr->inputPort;
                channels.addItem ("All Channels", true, channel == 0, [setInput, port] { setInput (port, 0); });
                for (int c = 1; c <= 16; ++c)
                    channels.addItem ("Channel " + juce::String (c), true, channel == c, [setInput, port, c] { setInput (port, c); });
                input.addSubMenu ("Channel", channels);
                m.addSubMenu ("MIDI Input", input);
            }
        }
        m.addSeparator();
        m.addItem ("Add Bus", [view] { if (view != nullptr) view->addBus(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

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
        return { static_cast<float> (getWidth()) / 2.0f - 6.0f, top, 12.0f, std::max (40.0f, static_cast<float> (getHeight()) - top - 44.0f) };
    }
    juce::Rectangle<float> meterArea() const
    {
        const auto f = faderArea();
        return { f.getRight() + 14.0f, f.getY(), 14.0f, f.getHeight() };
    }
    juce::Rectangle<int> valueArea() const { return { 4, getHeight() - 32, getWidth() - 8, 22 }; }

    float faderPosition() const
    {
        if (track == 0)
            return juce::jlimit (0.0f, 1.0f, (session.song().master.gainDb + 60.0f) / 66.0f);
        if (const auto* b = edit::findBus (session.song(), track))
            return static_cast<float> (b->volume) / 127.0f;
        const auto* tr = edit::findTrack (session.song(), track);
        return tr == nullptr ? 0.0f : static_cast<float> (tr->mixer.volume) / 127.0f;
    }

    int currentValue() const
    {
        if (const auto* b = edit::findBus (session.song(), track))
            return active == Part::fader ? b->volume : (active == Part::pan ? b->pan : 0);
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
                          if (auto* b = edit::findBus (s, id))
                          {
                              if (part == Part::fader) b->volume = v;
                              if (part == Part::pan) b->pan = v;
                          }
                          else if (auto* t = edit::findTrack (s, id))
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
        const bool hasAudio = track == 0 || isBus() || (tr != nullptr && (tr->output.kind == TrackOutput::Kind::soundFont || tr->output.kind == TrackOutput::Kind::sampler || tr->output.kind == TrackOutput::Kind::audio));
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
    MixerView& mixer;
    TrackId track;
    std::unique_ptr<juce::TextEditor> nameEditor;
    Part active = Part::none;
    int startValue = 0;
    float grabOffset = 0.0f;
    float meterL = 0.0f, meterR = 0.0f, holdL = 0.0f, holdR = 0.0f;
    float shownFader = -1.0f, startFader = 0.0f, targetFader = 0.0f;
    double faderStartMs = 0.0;
    int lastShown = -1;
};

// =====================================================================================
MixerView::MixerView (Session& s) : session (s)
{
    session.addChangeListener (this);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (false, true);
    master = std::make_unique<ChannelStrip> (session, *this, 0);
    addAndMakeVisible (*master);
    rebuild();
}

MixerView::~MixerView() { session.removeChangeListener (this); }

void MixerView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    std::vector<TrackId> ids;
    for (const auto& t : session.song().tracks)
        ids.push_back (t.id);
    std::vector<std::uint32_t> buses;
    for (const auto& b : session.song().buses)
        buses.push_back (b.id);
    if (ids != shownTracks || buses != shownBuses)
        rebuild();
    for (auto& s : strips)
    {
        s->syncFader (true);
        s->repaint();
    }
    for (auto& s : busStrips)
    {
        s->syncFader (true);
        s->repaint();
    }
    master->syncFader (true);
    master->repaint();
}

void MixerView::rebuild()
{
    shownTracks.clear();
    strips.clear();
    for (const auto& t : session.song().tracks)
    {
        shownTracks.push_back (t.id);
        auto strip = std::make_unique<ChannelStrip> (session, *this, t.id);
        holder.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    shownBuses.clear();
    busStrips.clear();
    for (const auto& b : session.song().buses)
    {
        shownBuses.push_back (b.id);
        auto strip = std::make_unique<ChannelStrip> (session, *this, b.id);
        holder.addAndMakeVisible (*strip);
        busStrips.push_back (std::move (strip));
    }
    resized();
}

void MixerView::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    master->setBounds (r.removeFromRight (kMasterWidth));
    r.removeFromRight (12);
    viewport.setBounds (r);

    const int needed = static_cast<int> (strips.size() + busStrips.size()) * (kStripWidth + kGap) + (busStrips.empty() ? 0 : kBusGap);
    const int h = r.getHeight() - (needed > r.getWidth() ? viewport.getScrollBarThickness() : 0);
    int x = 0;
    for (auto& s : strips)
    {
        s->setBounds (x, 0, kStripWidth, h);
        x += kStripWidth + kGap;
    }
    if (! busStrips.empty())
        x += kBusGap; // the buses stand apart from the tracks
    for (auto& s : busStrips)
    {
        s->setBounds (x, 0, kStripWidth, h);
        x += kStripWidth + kGap;
    }
    holder.setSize (std::max (x, r.getWidth()), h);
}

std::uint32_t MixerView::addBus()
{
    std::uint32_t id = 0;
    const auto name = "Bus " + std::to_string (session.song().buses.size() + 1);
    session.edit ("Add Bus", [&] (Song& s) { id = edit::addBus (s, name).id; });
    return id;
}

void MixerView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    juce::PopupMenu m;
    m.addItem ("Add Bus", [this] { addBus(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
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
    for (auto& s : busStrips)
        s->setLevel (session.busPeak (s->trackId(), 0), session.busPeak (s->trackId(), 1));
    master->setLevel (session.masterPeak (0), session.masterPeak (1));
}

} // namespace arranger::ui
