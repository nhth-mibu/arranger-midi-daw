#include "L6maxMixerView.h"

namespace arranger::ui
{

namespace
{
    constexpr const char* kProfileId = "zoom-l6max";

    int muteCc (int channel)
    {
        static constexpr int ccs[] = { 93, 94, 95, 102, 103, 104, 105, 106 };
        return ccs[std::clamp (channel, 0, 7)];
    }

    int modeCc (int channel)
    {
        if (channel == 4) return 109;
        if (channel == 5) return 110;
        if (channel == 6) return 113;
        if (channel == 7) return 114;
        return -1;
    }

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

        static const char* labels[] = { "HIGH", "MID FREQ", "MID", "LOW", "SUB", "AUX1", "AUX2", "EFX", "PAN", "LEVEL" };
        static const int bases[] = { 1, 11, 21, 33, 41, 49, 57, 65, 73, 81 };
        for (int i = 0; i < 10; ++i)
        {
            auto* c = controls.add (new Control());
            c->cc = bases[i] + channel;
            c->caption.setText (labels[i], juce::dontSendNotification);
            c->caption.setFont (juce::FontOptions (10.0f));
            c->caption.setJustificationType (juce::Justification::centred);
            c->caption.setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));
            styleSlider (c->slider);
            c->slider.setValue (owner.valueFor (c->cc), juce::dontSendNotification);
            c->slider.onValueChange = [this, c] { owner.sendCc (c->cc, static_cast<int> (c->slider.getValue())); };
            addAndMakeVisible (c->caption);
            addAndMakeVisible (c->slider);
        }

        mute.setButtonText ("Mute");
        mute.onClick = [this] {
            const bool on = ! muted;
            owner.sendCc (muteCc (channel), on ? 127 : 0);
            muted = on;
            refresh();
        };
        addAndMakeVisible (mute);

        if (modeCc (channel) >= 0)
        {
            mode.setButtonText (modeName (channel));
            mode.onClick = [this] {
                const bool on = ! modeOn;
                owner.sendCc (modeCc (channel), on ? 127 : 0);
                modeOn = on;
                refresh();
            };
            addAndMakeVisible (mode);
        }
        refresh();
    }

    void refresh()
    {
        muted = owner.valueFor (muteCc (channel)) >= 64;
        modeOn = modeCc (channel) >= 0 && owner.valueFor (modeCc (channel)) >= 64;
        mute.setButtonText (muted ? "Muted" : "Mute");
        if (modeCc (channel) >= 0)
            mode.setButtonText (modeOn ? modeName (channel) + " On" : modeName (channel));
        for (auto* c : controls)
            c->slider.setValue (owner.valueFor (c->cc), juce::dontSendNotification);
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
            mute.setBounds (buttons.removeFromLeft (90));
            if (mode.isVisible())
                mode.setBounds (buttons.removeFromLeft (94));
            const int w = std::max (1, r.getWidth() / 10);
            for (auto* c : controls)
            {
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
        mute.setBounds (buttons.removeFromLeft (90));
        if (mode.isVisible())
            mode.setBounds (buttons.removeFromLeft (94));
    }

private:
    struct Control
    {
        juce::Label caption;
        juce::Slider slider;
        int cc = 0;
    };

    L6maxMixerView& owner;
    int channel = 0;
    juce::Label name, eqGroup, sendGroup, mainGroup;
    juce::OwnedArray<Control> controls;
    juce::TextButton mute { "Mute" }, mode;
    bool muted = false, modeOn = false, compact = false;
};

// ---- the page ---------------------------------------------------------------------------------

L6maxMixerView::L6maxMixerView (Session& s) : session (s)
{
    if (const auto* p = session.deviceLibrary().byId (kProfileId))
        profile = *p;

    for (const auto& page : profile.editor.pages)
        for (const auto& param : page.params)
            if (param.kind == SynthParameter::Kind::cc)
                params[param.number] = { &page, &param };

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

    resetButton.onClick = [this] { resetValues(); };
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

    if (const auto* type = paramFor (117))
    {
        juce::StringArray types;
        for (const auto& choice : type->choices)
            types.add (choice);
        effectType.addItemList (types, 1);
        effectType.setSelectedItemIndex (std::max (0, type->choiceIndex (valueFor (117))), juce::dontSendNotification);
        effectType.onChange = [this, type] {
            const int i = effectType.getSelectedItemIndex();
            if (i >= 0)
                sendCc (117, type->choiceValue (static_cast<std::size_t> (i)));
        };
        addAndMakeVisible (effectType);
    }

    compressor.onClick = [this] {
        const bool on = valueFor (119) < 64;
        sendCc (119, on ? 127 : 0);
        compressor.setButtonText (on ? "Compressor On" : "Compressor Off");
    };
    addAndMakeVisible (compressor);

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

const SynthParameter* L6maxMixerView::paramFor (int cc) const
{
    const auto it = params.find (cc);
    return it != params.end() ? it->second.second : nullptr;
}

int L6maxMixerView::valueFor (int cc) const
{
    const auto it = params.find (cc);
    if (it == params.end())
        return 0;
    const auto& [page, param] = it->second;
    return session.synthValue (profile.id, page->name + "/" + param->name).value_or (param->defaultValue);
}

void L6maxMixerView::sendCc (int cc, int value)
{
    const auto it = params.find (cc);
    if (it == params.end())
        return;
    const auto& [page, param] = it->second;
    if (! session.sendSynthParameter (profile, *page, *param, value))
        status.setText ("Not plugged in. Changes are kept until the mixer is connected.", juce::dontSendNotification);
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
    for (const auto& [cc, entry] : params)
        sendCc (cc, entry.second->defaultValue);
    for (auto* card : cards)
        card->refresh();
    if (const auto* type = paramFor (117))
        effectType.setSelectedItemIndex (std::max (0, type->choiceIndex (valueFor (117))), juce::dontSendNotification);
    compressor.setButtonText (valueFor (119) >= 64 ? "Compressor On" : "Compressor Off");
}

void L6maxMixerView::updateStatus()
{
    const auto port = session.outputFor (profile);
    status.setText (port.empty() ? "Not plugged in. Use the L6max Mixer Control port."
                                 : "Plugged in: " + juce::String (port),
                    juce::dontSendNotification);
    compressor.setButtonText (valueFor (119) >= 64 ? "Compressor On" : "Compressor Off");
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
