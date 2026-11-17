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
    constexpr const char* kVideoExtensions = "mp4;m4v;mov;mkv;webm;avi;wmv;mpg;mpeg;mts;m2ts;ts";

    juce::File ffmpegProgram()
    {
#if JUCE_WINDOWS
        const juce::String name = "ffmpeg.exe";
#else
        const juce::String name = "ffmpeg";
#endif
        const auto besideApp = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile (name);
        if (besideApp.existsAsFile())
            return besideApp;
        auto paths = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", ""),
#if JUCE_WINDOWS
                                                   ";",
#else
                                                   ":",
#endif
                                                   "\"");
#if JUCE_MAC
        paths.addIfNotAlreadyThere ("/opt/homebrew/bin");
        paths.addIfNotAlreadyThere ("/usr/local/bin");
#endif
        for (const auto& path : paths)
            if (juce::File::isAbsolutePath (path))
            {
                const auto candidate = juce::File (path).getChildFile (name);
                if (candidate.existsAsFile())
                    return candidate;
            }
        return {};
    }

    Sample readVideo (const juce::File& file)
    {
        const auto decoder = ffmpegProgram();
        if (decoder == juce::File())
            throw wav::Error ("Video audio import needs FFmpeg. Put ffmpeg on PATH or beside the app, then try again.");
        juce::TemporaryFile audio (".wav");
        // Accept local video containers only. Never follow network playlists.
        const juce::StringArray args {
            decoder.getFullPathName(), "-nostdin", "-hide_banner", "-loglevel", "error",
            "-protocol_whitelist", "file,pipe",
            "-format_whitelist", "mov,matroska,webm,avi,asf,mpeg,mpegts,ogg",
            "-i", file.getFullPathName(), "-map", "0:a:0", "-vn", "-sn", "-dn",
            "-ac", "2", "-ar", "48000", "-c:a", "pcm_f32le",
            "-t", "2700.01", "-n", audio.getFile().getFullPathName()
        };
        juce::ChildProcess process;
        if (! process.start (args, 0))
            throw wav::Error ("FFmpeg could not start. Check that the executable can run on this computer.");
        if (! process.waitForProcessToFinish (120000))
        {
            process.kill();
            process.waitForProcessToFinish (5000);
            throw wav::Error ("Video audio import timed out after two minutes.");
        }
        if (process.getExitCode() != 0)
            throw wav::Error ("The video has no readable audio stream, is damaged, or uses an unsupported codec: " + file.getFileName().toStdString());
        auto sample = read (audio.getFile().getFullPathName().toStdString());
        if (sample.frames() == 0 || sample.frames() > static_cast<std::size_t> (kMaxFrames))
            throw wav::Error ("Video audio must be no longer than 45 minutes.");
        sample.name = file.getFileNameWithoutExtension().toStdString();
        return sample;
    }
}

juce::String wildcard()
{
    auto all = formats().getWildcardForAllFormats();
    if (! all.containsIgnoreCase ("*.wave"))
        all += ";*.wave";
    for (const auto& extension : juce::StringArray::fromTokens (kVideoExtensions, ";", ""))
        if (! all.containsIgnoreCase ("*." + extension))
            all += ";*." + extension;
    return all;
}

juce::String formatNames()
{
    juce::StringArray names;
    for (auto* f : formats())
        names.addIfNotAlreadyThere (f->getFormatName().upToFirstOccurrenceOf (" file", false, true));
    return names.joinIntoString (", ") + ", video audio (MP4, MOV, MKV, WebM, AVI, WMV, MPEG; FFmpeg required)";
}

bool isVideoFile (const juce::File& file) { return file.hasFileExtension (kVideoExtensions); }

bool isAudioFile (const juce::File& file)
{
    return isVideoFile (file) || file.hasFileExtension ("wave") || juce::WildcardFileFilter (formats().getWildcardForAllFormats(), {}, {}).isFileSuitable (file);
}

Sample read (const std::string& path)
{
    const juce::File file (juce::String::fromUTF8 (path.c_str()));
    if (isVideoFile (file))
        return readVideo (file);
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
