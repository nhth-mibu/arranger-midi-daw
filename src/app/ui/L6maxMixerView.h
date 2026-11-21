#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The Zoom LiveTrak L6max mixer: one card for each channel, with the EQ,
    sends and main controls, and the effect, compressor and scene controls.
    The controls send the factory MIDI CC numbers from the device profile.
*/
class L6maxMixerView final : public juce::Component, private juce::Timer
{
public:
    explicit L6maxMixerView (Session&);
    ~L6maxMixerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class ChannelCard;

    struct Ref
    {
        const SynthPage* page = nullptr;
        const SynthParameter* param = nullptr;
        bool valid() const noexcept { return page != nullptr && param != nullptr; }
    };

    void timerCallback() override;
    Ref find (const char* pageName, const char* paramName) const;
    void sendParam (const Ref&, int value);
    void sendScene (int scene);
    void resetValues();
    void updateStatus();
    int valueFor (const Ref&) const;

    Session& session;
    DeviceProfile profile;

    juce::Label title, status, effectsLabel, channelLabel;
    juce::ComboBox channel;
    juce::TextButton compactButton { "Compact" }, resetButton { "Reset" };
    juce::Viewport viewport;
    juce::Component holder;
    juce::OwnedArray<ChannelCard> cards;
    juce::ComboBox effectType;
    juce::TextButton compressor { "Compressor Off" };
    Ref compressorRef;
    juce::OwnedArray<juce::TextButton> sceneButtons;
    bool compact = false;

    friend class ChannelCard;
};

} // namespace arranger::ui
