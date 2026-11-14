#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

class ChannelStrip;

/**
    The mixer: one strip per track (volume, pan, reverb and chorus sends,
    mute, solo, record arm, meter), one strip per bus, and the master strip.
    Track strips send CC 7, 10, 91 and 93 to the track's output; the master
    scales the built-in instrument's audio.
*/
class MixerView final : public juce::Component, private juce::ChangeListener
{
public:
    explicit MixerView (Session&);
    ~MixerView() override;

    /** Meters; call often while the mixer is showing. */
    void refresh();

    /** Opens the effects of a bus (or track) next to a component. */
    std::function<void (TrackId, juce::Component*)> onShowEffects;

    /** Adds a bus at the end of the bus strips and returns its id. */
    std::uint32_t addBus();
    void mouseUp (const juce::MouseEvent&) override;

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
    std::vector<std::unique_ptr<ChannelStrip>> busStrips;
    std::vector<std::uint32_t> shownBuses;
    friend class ChannelStrip;
};

} // namespace arranger::ui
