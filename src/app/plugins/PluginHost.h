#pragma once

#include "arranger/core/Song.h"
#include "arranger/engine/Processor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

namespace arranger
{

/**
    A plugin behind the engine's Processor interface. An effect changes the
    audio in place; an instrument adds its sound.

    process() runs on the audio thread. Everything else is for the UI thread.
*/
class HostedProcessor final : public Processor,
                              private juce::AudioPlayHead
{
public:
    HostedProcessor (std::unique_ptr<juce::AudioPluginInstance>, bool instrument);
    ~HostedProcessor() override;

    void prepare (const ProcessSpec&) override;
    void process (const ProcessContext&, AudioBlock&, const MidiEventBuffer& midiIn, MidiEventBuffer& midiOut) noexcept override;
    void reset() noexcept override;
    int latencySamples() const noexcept override;

    void setBypass (bool b) noexcept { bypass.store (b, std::memory_order_relaxed); }
    juce::AudioPluginInstance& plugin() noexcept { return *instance; }

    /** The plugin's settings as text for the project file. */
    std::string state() const;
    void setState (const std::string&);

private:
    juce::Optional<PositionInfo> getPosition() const override;

    std::unique_ptr<juce::AudioPluginInstance> instance;
    const bool isInstrument;
    std::atomic<bool> bypass { false };
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    PositionInfo position; ///< written by the audio thread just before the plugin reads it
    bool prepared = false;
};

/**
    The plugins on this computer: the list from the last scan, the scan
    itself, and making instances.

    Each plugin file is scanned in a child process (this same app, started
    with --scan-plugin). A plugin that crashes or hangs during the scan stops
    only the child; the app then puts it on a list of plugins to skip.
*/
class PluginHost
{
public:
    PluginHost();

    /** Reads and writes the list (in the app's settings folder). */
    void load();
    void save() const;

    std::vector<juce::PluginDescription> instruments() const;
    std::vector<juce::PluginDescription> effects() const;
    int pluginCount() const { return known.getNumTypes(); }
    juce::StringArray skipped() const { return known.getBlacklistedFiles(); }

    /** Every plugin file in the standard folders (and `extra`), as "format\npath". */
    juce::StringArray filesToScan (const juce::FileSearchPath& extra = {});
    /** Scans one entry of filesToScan() in a child process. False: it crashed, hung or held no plugin. */
    bool scanFile (const juce::String& entry, int timeoutMs = 30000);
    /** Forgets plugins whose files are gone. */
    void removeMissing();
    void clear() { known.clear(); }

    /** The child's side of scanFile(): `args` are the words after --scan-plugin. Returns the exit code. */
    static int runScanChild (const juce::StringArray& args);

    static PluginSlot slotFor (const juce::PluginDescription&);
    /** Makes the plugin of a slot with its saved settings. Null with a reason when the plugin is missing or fails. */
    std::shared_ptr<HostedProcessor> create (const PluginSlot&, bool instrument, double sampleRate, int blockSize, juce::String& error) const;

private:
    juce::AudioPluginFormat* formatNamed (const juce::String&) const;
    static juce::File listFile();

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
};

} // namespace arranger
