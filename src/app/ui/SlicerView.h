#pragma once

#include "Theme.h"
#include "../Session.h"

#include "arranger/core/Transients.h"

namespace arranger::ui
{

/**
    The slicer: chops a loop into pads for a sampler track. Slices come from
    the transients, an even grid, or markers you place by hand. Each change
    goes straight to the track's kit, so a click on a slice plays it.
*/
class SlicerView final : public juce::Component
{
public:
    SlicerView (Session&, TrackId, const juce::File& loop);

    /** False when the file could not be read. */
    bool isLoaded() const noexcept { return sample != nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kFirstNote = 36;

private:
    enum Mode { transients = 1, grid, manual };

    void reslice();
    void applyToTrack();
    void makePattern();
    int sliceAt (int x) const;
    juce::Rectangle<int> waveArea() const;
    float xForFrame (std::size_t frame) const;
    std::size_t frameForX (int x) const;
    int guessBars() const;

    Session& session;
    TrackId track;
    std::string path;
    std::shared_ptr<const Sample> sample;
    std::vector<float> mono;
    std::vector<Slice> slices;
    int playing = -1;

    juce::ComboBox modeBox, gridBox, barsBox;
    juce::Slider sensitivity { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton patternButton { "Make a clip that plays the loop" };
    juce::Label info;
};

} // namespace arranger::ui
