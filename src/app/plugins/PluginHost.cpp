#include "PluginHost.h"

namespace arranger
{

namespace
{
    void addFormats (juce::AudioPluginFormatManager& m)
    {
       #if JUCE_PLUGINHOST_VST3
        m.addFormat (std::make_unique<juce::VST3PluginFormat>());
       #endif
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        m.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
       #endif
    }
}

// ---- HostedProcessor -----------------------------------------------------------------
HostedProcessor::HostedProcessor (std::unique_ptr<juce::AudioPluginInstance> p, bool instrument)
    : instance (std::move (p)), isInstrument (instrument)
{
    instance->enableAllBuses();
    instance->setPlayHead (this);
    midi.ensureSize (8192);
}

HostedProcessor::~HostedProcessor()
{
    instance->setPlayHead (nullptr);
    if (prepared)
        instance->releaseResources();
}

void HostedProcessor::prepare (const ProcessSpec& spec)
{
    if (prepared)
        instance->releaseResources();
    instance->setRateAndBufferSizeDetails (spec.sampleRate, spec.maxBlockSize);
    instance->prepareToPlay (spec.sampleRate, spec.maxBlockSize);
    const int channels = std::max ({ 2, instance->getTotalNumInputChannels(), instance->getTotalNumOutputChannels() });
    buffer.setSize (channels, spec.maxBlockSize, false, true, false);
    prepared = true;
}

void HostedProcessor::reset() noexcept
{
    if (prepared)
        instance->reset();
}

int HostedProcessor::latencySamples() const noexcept
{
    return instance->getLatencySamples();
}

void HostedProcessor::setControl (int control, float value) noexcept
{
    const auto& controls = instance->getParameters();
    if (control < 0 || control >= controls.size())
        return;
    auto* c = controls.getUnchecked (control);
    value = juce::jlimit (0.0f, 1.0f, value);
    if (std::abs (c->getValue() - value) > 1.0e-6f)
        c->setValue (value);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> HostedProcessor::getPosition() const
{
    return position;
}

void HostedProcessor::process (const ProcessContext& ctx, AudioBlock& audio, const MidiEventBuffer& midiIn, MidiEventBuffer&) noexcept
{
    const int n = audio.numSamples;
    if (! prepared || n <= 0 || n > buffer.getNumSamples() || audio.numChannels <= 0)
        return;
    if (bypass.load (std::memory_order_relaxed) && ! isInstrument)
        return;

    // The song position for plugins that follow the tempo (delays, arpeggiators).
    const double quarter = ctx.tickAtStart / static_cast<double> (kTicksPerQuarter);
    const double barQuarters = ctx.timeSignature.ticksPerBar() / static_cast<double> (kTicksPerQuarter);
    position.setIsPlaying (ctx.playing);
    position.setBpm (ctx.tempoBpm);
    position.setPpqPosition (quarter);
    position.setPpqPositionOfLastBarStart (barQuarters > 0.0 ? std::floor (quarter / barQuarters) * barQuarters : 0.0);
    position.setTimeSignature (juce::AudioPlayHead::TimeSignature { ctx.timeSignature.numerator, ctx.timeSignature.denominator });
    position.setTimeInSamples (ctx.sampleTime);
    position.setTimeInSeconds (static_cast<double> (ctx.sampleTime) / ctx.sampleRate);

    const int channels = buffer.getNumChannels();
    const int ins = isInstrument ? 0 : std::min (audio.numChannels, channels);
    for (int c = 0; c < channels; ++c)
    {
        if (c < ins)
            buffer.copyFrom (c, 0, audio.channels[c], n);
        else if (! isInstrument && audio.numChannels == 1 && c == 1)
            buffer.copyFrom (c, 0, audio.channels[0], n); // a mono signal goes to both sides of a stereo effect
        else
            buffer.clear (c, 0, n);
    }

    midi.clear();
    for (const auto& e : midiIn)
        if (e.size > 0)
            midi.addEvent (e.bytes.data(), e.size, std::clamp (e.sampleOffset, 0, n - 1));

    {
        const juce::ScopedNoDenormals noDenormals;
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), channels, n);
        if (instance->isSuspended())
            view.clear();
        else if (bypass.load (std::memory_order_relaxed))
            instance->processBlockBypassed (view, midi);
        else
            instance->processBlock (view, midi);
    }

    const int outs = std::max (1, std::min (channels, instance->getTotalNumOutputChannels()));
    for (int c = 0; c < audio.numChannels; ++c)
    {
        const float* from = buffer.getReadPointer (std::min (c, outs - 1)); // a mono plugin plays on both sides
        float* to = audio.channels[c];
        if (isInstrument)
            for (int i = 0; i < n; ++i)
                to[i] += from[i];
        else
            std::copy (from, from + n, to);
    }
}

std::string HostedProcessor::state() const
{
    juce::MemoryBlock block;
    instance->getStateInformation (block);
    return block.toBase64Encoding().toStdString();
}

void HostedProcessor::setState (const std::string& text)
{
    if (text.empty())
        return;
    juce::MemoryBlock block;
    if (block.fromBase64Encoding (text) && block.getSize() > 0)
        instance->setStateInformation (block.getData(), static_cast<int> (block.getSize()));
}

// ---- PluginHost ----------------------------------------------------------------------
PluginHost::PluginHost()
{
    addFormats (formats);
}

juce::File PluginHost::listFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Arranger MIDI DAW").getChildFile ("plugins.xml");
}

void PluginHost::load()
{
    if (const auto xml = juce::parseXML (listFile()))
        known.recreateFromXml (*xml);
}

void PluginHost::save() const
{
    if (const auto xml = known.createXml())
    {
        listFile().getParentDirectory().createDirectory();
        xml->writeTo (listFile());
    }
}

std::vector<juce::PluginDescription> PluginHost::instruments() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : known.getTypes())
        if (d.isInstrument)
            out.push_back (d);
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : known.getTypes())
        if (! d.isInstrument)
            out.push_back (d);
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

juce::AudioPluginFormat* PluginHost::formatNamed (const juce::String& name) const
{
    for (auto* f : formats.getFormats())
        if (f->getName() == name)
            return f;
    return nullptr;
}

juce::StringArray PluginHost::filesToScan (const juce::FileSearchPath& extra)
{
    juce::StringArray out;
    for (auto* f : formats.getFormats())
    {
        auto paths = f->getDefaultLocationsToSearch();
        paths.addPath (extra);
        for (const auto& file : f->searchPathsForPlugins (paths, true, false))
            if (! known.getBlacklistedFiles().contains (file))
                out.add (f->getName() + "\n" + file);
    }
    return out;
}

bool PluginHost::scanFile (const juce::String& entry, int timeoutMs)
{
    const auto formatName = entry.upToFirstOccurrenceOf ("\n", false, false);
    const auto file = entry.fromFirstOccurrenceOf ("\n", false, false);
    if (formatNamed (formatName) == nullptr)
        return false;

    const auto result = juce::File::createTempFile (".xml");
    juce::ChildProcess child;
    juce::StringArray command { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(),
                                "--scan-plugin", formatName, file, result.getFullPathName() };
    bool ok = child.start (command, 0) && child.waitForProcessToFinish (timeoutMs) && child.getExitCode() == 0;
    if (child.isRunning())
        child.kill(); // a plugin that hangs

    int found = 0;
    if (ok)
        if (const auto xml = juce::parseXML (result))
            for (const auto* e : xml->getChildIterator())
            {
                juce::PluginDescription d;
                if (d.loadFromXml (*e))
                {
                    known.addType (d);
                    ++found;
                }
            }
    result.deleteFile();
    if (! ok)
        known.addToBlacklist (file);
    return ok && found > 0;
}

int PluginHost::runScanChild (const juce::StringArray& args)
{
    if (args.size() < 3)
        return 2;
    juce::AudioPluginFormatManager manager;
    addFormats (manager);
    for (auto* f : manager.getFormats())
        if (f->getName() == args[0])
        {
            juce::OwnedArray<juce::PluginDescription> found;
            f->findAllTypesForFile (found, args[1]);
            juce::XmlElement xml ("PLUGINS");
            for (const auto* d : found)
                xml.addChildElement (d->createXml().release());
            return xml.writeTo (juce::File (args[2])) ? 0 : 3;
        }
    return 2;
}

void PluginHost::removeMissing()
{
    for (const auto& d : known.getTypes())
        if (auto* f = formatNamed (d.pluginFormatName); f == nullptr || ! f->doesPluginStillExist (d))
            known.removeType (d);
}

PluginSlot PluginHost::slotFor (const juce::PluginDescription& d)
{
    PluginSlot s;
    s.format = d.pluginFormatName.toStdString();
    s.id = d.createIdentifierString().toStdString();
    s.name = d.name.toStdString();
    return s;
}

std::shared_ptr<HostedProcessor> PluginHost::create (const PluginSlot& slot, bool instrument, double sampleRate, int blockSize, juce::String& error) const
{
    const auto description = known.getTypeForIdentifierString (juce::String::fromUTF8 (slot.id.c_str()));
    if (description == nullptr)
    {
        error = "The plugin \"" + juce::String::fromUTF8 (slot.name.c_str()) + "\" is not on this computer.";
        return nullptr;
    }
    auto instance = formats.createPluginInstance (*description, sampleRate, blockSize, error);
    if (instance == nullptr)
        return nullptr;
    auto processor = std::make_shared<HostedProcessor> (std::move (instance), instrument);
    processor->setState (slot.state);
    processor->setBypass (slot.bypass);
    processor->prepare ({ sampleRate, blockSize });
    return processor;
}

} // namespace arranger
