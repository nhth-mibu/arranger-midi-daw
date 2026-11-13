#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

class ChannelStrip;

/**
    The mixer: one strip per track (volume, pan, reverb and chorus sends,
    mute, solo, record arm, meter) and the master strip. Track strips send
    CC 7, 10, 91 and 93 to the track's output; the master scales the
    built-in instrument's audio.
*/
class MixerView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit MixerView (Session&);
    ~MixerView() override;

    /** Meters; call often while the mixer is showing. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();

    Session& session;
    juce::Viewport viewport;
    juce::Component holder;
    std::vector<std::unique_ptr<ChannelStrip>> strips;
    std::unique_ptr<ChannelStrip> master;
    std::vector<TrackId> shownTracks;
};

} // namespace arranger::ui
