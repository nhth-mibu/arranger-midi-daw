#include "AudioFiles.h"

namespace arranger::audio_files
{

namespace
{
    juce::AudioFormatManager& formats()
    {
        struct Ready
        {
            Ready() { manager.registerBasicFormats(); }
            juce::AudioFormatManager manager;
        };
        static Ready ready;
        return ready.manager;
    }

    /** A file longer than this does not go into memory: 45 minutes at 48 kHz, about 1 GB in stereo. */
    constexpr juce::int64 kMaxFrames = 45 * 60 * 48000LL;
}

juce::String wildcard()
{
    auto all = formats().getWildcardForAllFormats();
    if (! all.containsIgnoreCase ("*.wave"))
        all += ";*.wave";
    return all;
}

juce::String formatNames()
{
    juce::StringArray names;
    for (auto* f : formats())
        names.addIfNotAlreadyThere (f->getFormatName().upToFirstOccurrenceOf (" file", false, true));
    return names.joinIntoString (", ");
}

bool isAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("wave") || juce::WildcardFileFilter (formats().getWildcardForAllFormats(), {}, {}).isFileSuitable (file);
}

Sample read (const std::string& path)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    // Our own WAV reader first: it also reads 64-bit float files.
    if (file.hasFileExtension ("wav;wave"))
    {
        try
        {
            return wav::read (path);
        }
        catch (const wav::Error&)
        {
            // A WAV file of a kind that we do not read ourselves: the library has a try.
        }
    }
    const std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (file));
    if (reader == nullptr)
        throw wav::Error ("Can't read " + path);
    if (reader->lengthInSamples <= 0 || reader->lengthInSamples > kMaxFrames || reader->numChannels == 0 || reader->sampleRate <= 0.0)
        throw wav::Error ("Can't use " + path);

    const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2));
    const int frames = static_cast<int> (reader->lengthInSamples);
    juce::AudioBuffer<float> buffer (channels, frames);
    if (! reader->read (&buffer, 0, frames, 0, true, channels > 1))
        throw wav::Error ("Can't read " + path);

    Sample s;
    s.name = file.getFileNameWithoutExtension().toStdString();
    s.sampleRate = reader->sampleRate;
    s.channels.resize (static_cast<std::size_t> (channels));
    for (int c = 0; c < channels; ++c)
        s.channels[static_cast<std::size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + frames);
    return s;
}

} // namespace arranger::audio_files
