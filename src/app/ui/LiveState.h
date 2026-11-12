#pragma once

#include "arranger/core/Song.h"
#include "arranger/platform/DeviceHost.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace arranger::ui
{

struct MidiInputActivity
{
    juce::String name;
    bool enabled = false;
    juce::String lastMessage;  ///< "Note On C4 · vel 96 · ch 1"
    double lastTimeMs = 0.0;   ///< juce::Time::getMillisecondCounterHiRes() of the last message
};

/** What the views show besides the song: refreshed by the main component's timer. */
struct LiveState
{
    bool playing = false;
    double positionTicks = 0.0;
    platform::AudioStatus audio;
    std::vector<MidiInputActivity> midiInputs;
    juce::String lastMidiMessage;
    double lastMidiTimeMs = 0.0;

    bool midiActiveRecently (double nowMs, double windowMs = 300.0) const { return nowMs - lastMidiTimeMs < windowMs; }
};

} // namespace arranger::ui
