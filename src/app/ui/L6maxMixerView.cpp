#include "L6maxMixerView.h"

namespace arranger::ui
{

namespace
{
    constexpr const char* kProfileId = "zoom-l6max";

    juce::String modeName (int channel)
    {
        if (channel == 4 || channel == 5) return "MONO";
        if (channel == 6 || channel == 7) return "USB";
        return {};
    }

    void styleSlider (juce::Slider& slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 14);
        slider.setRange (0.0, 127.0, 1.0);
        slider.setDoubleClickReturnValue (true, 64.0);
    }
}

// ---- one channel card -------------------------------------------------------------------------

class L6maxMixerView::ChannelCard final : public juce::Component
{
public:
    ChannelCard (L6maxMixerView& o, int ch) : owner (o), channel (ch)
    {
        addAndMakeVisible (name);
        name.setText ("CH" + juce::String (ch + 1), juce::dontSendNotification);
        name.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, currentTheme().colour ("text"));

        for (auto* group : { &eqGroup, &sendGroup, &mainGroup })
        {
            group->setFont (juce::FontOptions (10.0f, juce::Font::bold));
            group->setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));
            addAndMakeVisible (group);
        }
        eqGroup.setText ("EQ", juce::dontSendNotification);
        sendGroup.setText ("SENDS", juce::dontSendNotification);
        mainGroup.setText ("MAIN", juce::dontSendNotification);

        static const char* pages[] = { "EQ High", "EQ Mid Freq", "EQ Mid", "EQ Low", "Sub Mix", "AUX1", "AUX2", "EFX", "Pan", "Level" };
        static const char* labels[] = { "HIGH", "MID FREQ", "MID", "LOW", "SUB", "AUX1", "AUX2", "EFX", "PAN", "LEVEL" };
        const auto channelName = "CH" + std::to_string (channel + 1);
        for (int i = 0; i < 10; ++i)
        {
            auto* c = controls.add (new Control());
            c->ref = owner.find (pages[i], channelName.c_str());
            c->caption.setText (labels[i], juce::dontSendNotification);
            c->caption.setFont (juce::FontOptions (10.0f));
            c->caption.setJustificationType (juce::Justification::centred);
            c->caption.setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));
            styleSlider (c->slider);
            c->slider.setValue (owner.valueFor (c->ref), juce::dontSendNotification);
            c->slider.onValueChange = [this, c] { owner.sendParam (c->ref, static_cast<int> (c->slider.getValue())); };
            addAndMakeVisible (c->caption);
            addAndMakeVisible (c->slider);
            c->caption.setVisible (c->ref.valid());
            c->slider.setVisible (c->ref.valid());
        }

        muteRef = owner.find ("Mute", channelName.c_str());
        mute.setButtonText ("Mute");
        mute.onClick = [this] {
            const bool on = ! muted;
            owner.sendParam (muteRef, on ? 127 : 0);
            muted = on;
            refresh();
        };
        addAndMakeVisible (mute);
        mute.setVisible (muteRef.valid());

        if (modeName (channel).isNotEmpty())
            modeRef = owner.find (channel < 6 ? "MONO x2" : "USB", channelName.c_str());
        mode.setButtonText (modeName (channel));
        mode.onClick = [this] {
            const bool on = ! modeOn;
            owner.sendParam (modeRef, on ? 127 : 0);
            modeOn = on;
            refresh();
        };
        addAndMakeVisible (mode);
        mode.setVisible (modeRef.valid());
        refresh();
    }

    void refresh()
    {
        muted = muteRef.valid() && owner.valueFor (muteRef) >= 64;
        modeOn = modeRef.valid() && owner.valueFor (modeRef) >= 64;
        mute.setButtonText (muted ? "Muted" : "Mute");
        if (modeRef.valid())
            mode.setButtonText (modeOn ? modeName (channel) + " On" : modeName (channel));
        for (auto* c : controls)
            if (c->ref.valid())
                c->slider.setValue (owner.valueFor (c->ref), juce::dontSendNotification);
    }

    void setCompact (bool c)
    {
        compact = c;
        for (auto* group : { &eqGroup, &sendGroup, &mainGroup })
            group->setVisible (! compact);
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = currentTheme();
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme.colour ("panelRaised"));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (theme.colour ("line"));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        name.setBounds (r.removeFromTop (compact ? 20 : 26));
        auto buttons = r.removeFromBottom (compact ? 24 : 26);
        if (compact)
        {
            buttons.removeFromLeft (std::max (0, buttons.getWidth() - 190));
            if (mute.isVisible())
                mute.setBounds (buttons.removeFromLeft (90));
            if (mode.isVisible())
                mode.setBounds (buttons.removeFromLeft (94));
            int count = 0;
            for (auto* c : controls)
                count += c->ref.valid() ? 1 : 0;
            const int w = std::max (1, r.getWidth() / std::max (1, count));
            for (auto* c : controls)
            {
                if (! c->ref.valid())
                    continue;
                auto cell = r.removeFromLeft (w);
                c->caption.setBounds (cell.removeFromTop (14));
                c->slider.setBounds (cell.removeFromTop (36));
            }
            return;
        }

        // EQ, sends and main, each with a heading.
        const auto section = [&] (juce::Label& heading, int first, int count, int columns, int height)
        {
            heading.setBounds (r.removeFromTop (14));
            const int cw = std::max (1, r.getWidth() / columns);
            const int ch = std::max (1, height / ((count + columns - 1) / columns));
            for (int i = 0; i < count; ++i)
            {
                auto* c = controls[first + i];
                if (! c->ref.valid())
                    continue;
                auto cell = juce::Rectangle<int> (r.getX() + (i % columns) * cw, r.getY() + (i / columns) * ch, cw, ch).reduced (3);
                c->caption.setBounds (cell.removeFromTop (15));
                c->slider.setBounds (cell);
            }
            r.removeFromTop (height);
        };
        const int controlHeight = std::max (0, r.getHeight() - 3 * 14);
        const int eqHeight = controlHeight * 2 / 5;
        const int sendsHeight = controlHeight * 2 / 5;
        section (eqGroup, 0, 4, 2, eqHeight);
        section (sendGroup, 4, 4, 2, sendsHeight);
        section (mainGroup, 8, 2, 2, std::max (0, controlHeight - eqHeight - sendsHeight));
        buttons.removeFromLeft (std::max (0, buttons.getWidth() - 190));
        if (mute.isVisible())
            mute.setBounds (buttons.removeFromLeft (90));
        if (mode.isVisible())
            mode.setBounds (buttons.removeFromLeft (94));
    }

private:
    struct Control
    {
        juce::Label caption;
        juce::Slider slider;
        L6maxMixerView::Ref ref;
    };

    L6maxMixerView& owner;
    int channel = 0;
    juce::Label name, eqGroup, sendGroup, mainGroup;
    juce::OwnedArray<Control> controls;
    juce::TextButton mute { "Mute" }, mode;
    L6maxMixerView::Ref muteRef, modeRef;
    bool muted = false, modeOn = false, compact = false;
};

// ---- the page ---------------------------------------------------------------------------------

L6maxMixerView::L6maxMixerView (Session& s) : session (s)
{
    if (const auto* p = session.deviceLibrary().byId (kProfileId))
        profile = *p;

    addAndMakeVisible (title);
    title.setText ("Zoom LiveTrak L6max", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, currentTheme().colour ("text"));

    addAndMakeVisible (status);
    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));

    addAndMakeVisible (channelLabel);
    channelLabel.setText ("MIDI channel", juce::dontSendNotification);
    channelLabel.setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));
    addAndMakeVisible (channel);
    for (int ch = 1; ch <= 16; ++ch)
        channel.addItem ("Channel " + juce::String (ch), ch);
    channel.setSelectedId (std::clamp (session.synthChannel (kProfileId), 1, 16), juce::dontSendNotification);
    channel.onChange = [this] { session.setSynthChannel (kProfileId, channel.getSelectedId()); };

    compactButton.setClickingTogglesState (true);
    compactButton.onClick = [this] {
        compact = compactButton.getToggleState();
        resized();
    };
    addAndMakeVisible (compactButton);

    resetButton.onClick = [this] {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
            juce::MessageBoxIconType::WarningIcon, "Reset the L6max mixer?",
            "This changes all eight channels on the mixer now: levels, EQ, sends, mutes, "
            "the effect type and the compressor. It cannot be undone from here.",
            "Reset", "Cancel", this);
        juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<L6maxMixerView> (this)] (int result) {
            if (safe != nullptr && result == 1)
                safe->resetValues();
        });
    };
    addAndMakeVisible (resetButton);

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&holder, false);
    viewport.setScrollBarsShown (true, true);
    for (int ch = 0; ch < 8; ++ch)
    {
        auto* card = cards.add (new ChannelCard (*this, ch));
        holder.addAndMakeVisible (card);
    }

    addAndMakeVisible (effectsLabel);
    effectsLabel.setText ("Effects and scenes", juce::dontSendNotification);
    effectsLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    effectsLabel.setColour (juce::Label::textColourId, currentTheme().colour ("text"));

    const auto typeRef = find ("EFX Type", "Type");
    if (typeRef.valid())
    {
        juce::StringArray types;
        for (const auto& choice : typeRef.param->choices)
            types.add (choice);
        effectType.addItemList (types, 1);
        effectType.setSelectedItemIndex (std::max (0, typeRef.param->choiceIndex (valueFor (typeRef))), juce::dontSendNotification);
        effectType.onChange = [this, typeRef] {
            const int i = effectType.getSelectedItemIndex();
            if (i >= 0)
                sendParam (typeRef, typeRef.param->choiceValue (static_cast<std::size_t> (i)));
        };
        addAndMakeVisible (effectType);
    }

    compressorRef = find ("Compressor", "On");
    compressor.onClick = [this] {
        const bool on = valueFor (compressorRef) < 64;
        sendParam (compressorRef, on ? 127 : 0);
        compressor.setButtonText (on ? "Compressor On" : "Compressor Off");
    };
    addAndMakeVisible (compressor);
    compressor.setVisible (compressorRef.valid());

    for (int i = 0; i < 4; ++i)
    {
        auto* b = sceneButtons.add (new juce::TextButton ("Scene " + juce::String (char ('A' + i))));
        b->onClick = [this, i] { sendScene (i); };
        addAndMakeVisible (b);
    }

    updateStatus();
    setSize (1200, 760);
    startTimer (500);
}

L6maxMixerView::~L6maxMixerView()
{
    stopTimer();
}

L6maxMixerView::Ref L6maxMixerView::find (const char* pageName, const char* paramName) const
{
    const SynthPage* page = nullptr;
    const auto* param = profile.editor.find (std::string (pageName) + "/" + paramName, &page);
    return { page, param };
}

int L6maxMixerView::valueFor (const Ref& ref) const
{
    if (! ref.valid())
        return 0;
    return session.synthValue (profile.id, ref.page->name + "/" + ref.param->name).value_or (ref.param->defaultValue);
}

void L6maxMixerView::sendParam (const Ref& ref, int value)
{
    if (! ref.valid())
        return;
    if (! session.sendSynthParameter (profile, *ref.page, *ref.param, value))
        status.setText ("Not plugged in. Shows the last values sent from this app.", juce::dontSendNotification);
}

void L6maxMixerView::sendScene (int scene)
{
    const auto port = session.outputFor (profile);
    if (port.empty())
    {
        status.setText ("Not plugged in. Connect the L6max Mixer Control port.", juce::dontSendNotification);
        return;
    }
    const auto ch = static_cast<std::uint8_t> ((std::clamp (session.synthChannel (profile.id), 1, 16) - 1) & 0x0f);
    session.deviceHost().sendNow (port, { static_cast<std::uint8_t> (0xc0 | ch), static_cast<std::uint8_t> (scene) });
}

void L6maxMixerView::resetValues()
{
    for (const auto& page : profile.editor.pages)
        for (const auto& param : page.params)
            sendParam ({ &page, &param }, param.defaultValue);
    for (auto* card : cards)
        card->refresh();
    const auto typeRef = find ("EFX Type", "Type");
    if (typeRef.valid())
        effectType.setSelectedItemIndex (std::max (0, typeRef.param->choiceIndex (valueFor (typeRef))), juce::dontSendNotification);
    compressor.setButtonText (valueFor (compressorRef) >= 64 ? "Compressor On" : "Compressor Off");
}

void L6maxMixerView::updateStatus()
{
    const auto port = session.outputFor (profile);
    status.setText (port.empty() ? "Not plugged in. Shows the last values sent from this app."
                                 : "Plugged in: " + juce::String (port) + ". Shows the last values sent from this app.",
                    juce::dontSendNotification);
    compressor.setButtonText (valueFor (compressorRef) >= 64 ? "Compressor On" : "Compressor Off");
}

void L6maxMixerView::timerCallback()
{
    updateStatus();
}

void L6maxMixerView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme().colour ("background"));
}

void L6maxMixerView::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (48);
    auto top = header.removeFromTop (30);
    title.setBounds (top.removeFromLeft (250));
    compactButton.setBounds (top.removeFromRight (86).reduced (2));
    resetButton.setBounds (top.removeFromRight (70).reduced (2));
    channelLabel.setBounds (top.removeFromRight (86));
    channel.setBounds (top.removeFromRight (100).reduced (2));
    status.setBounds (header);

    auto effects = r.removeFromBottom (64);
    effectsLabel.setBounds (effects.removeFromTop (20));
    auto controls = effects;
    effectType.setBounds (controls.removeFromLeft (150).reduced (2, 6));
    compressor.setBounds (controls.removeFromLeft (130).reduced (2, 6));
    for (auto* b : sceneButtons)
        b->setBounds (controls.removeFromLeft (76).reduced (2, 6));

    viewport.setBounds (r);
    const int cardW = compact ? std::max (720, viewport.getWidth() - 2) : 236;
    const int cardH = compact ? 110 : 400;
    holder.setSize (cardW, cardH * cards.size() + (compact ? 0 : 4));
    int y = 0;
    for (auto* card : cards)
    {
        card->setCompact (compact);
        card->setBounds (0, y, cardW, cardH);
        y += cardH;
    }
}

} // namespace arranger::ui
