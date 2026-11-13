#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/**
    The chord rules of one style part: the chord its notes are written in
    and how the notes follow the chord you play. Opens from a slot of the
    style, for that section or for all sections.
*/
class StyleRuleEditor final : public juce::Component
{
public:
    StyleRuleEditor (Session&, TrackId, int scene);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 380, kHeight = 352;

private:
    void load();
    void store();
    StyleChannelRule current() const;

    Session& session;
    TrackId track;
    int scene;
    bool loading = false;

    juce::ComboBox scopeBox, rootBox, typeBox, ntrBox, nttBox, highKeyBox, lowLimitBox, highLimitBox, rtrBox;
    juce::ToggleButton bassToggle { "Follow the bass note of chords like C/E" };
    juce::String heading;

    struct Row { juce::String label; juce::Rectangle<int> area; };
    std::vector<Row> rows;
};

} // namespace arranger::ui
