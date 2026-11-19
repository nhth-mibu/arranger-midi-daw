#include "Grooves.h"
#include "AudioFiles.h"

#include "arranger/core/SongEdit.h"

#include <stdexcept>

namespace arranger::grooves
{

juce::File folder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
                 .getChildFile ("Application Support")
#endif
                 .getChildFile ("Arranger MIDI DAW")
                 .getChildFile ("Grooves");
    f.createDirectory();
    return f;
}

std::vector<groove::Groove> load()
{
    std::vector<groove::Groove> out;
    for (const auto& file : folder().findChildFiles (juce::File::findFiles, false, "*.groove"))
    {
        try
        {
            out.push_back (groove::fromJson (file.loadFileAsString().toStdString()));
        }
        catch (const std::exception&)
        {
            // A broken file is skipped; the others still load.
        }
    }
    std::sort (out.begin(), out.end(), [] (const groove::Groove& a, const groove::Groove& b) { return a.name < b.name; });
    return out;
}

void save (const groove::Groove& g)
{
    const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (g.name.c_str())).trim();
    folder().getChildFile ((name.isEmpty() ? juce::String ("Groove") : name) + ".groove").replaceWithText (juce::String::fromUTF8 (groove::toJson (g).c_str()));
}

std::vector<groove::Groove> library (double bpm)
{
    auto list = groove::builtIns (bpm);
    for (auto& g : load())
        list.push_back (std::move (g));
    return list;
}

std::vector<groove::Hit> hitsInFile (const std::string& path)
{
    try
    {
        const auto sample = audio_files::read (path);
        return groove::findHits (sample.mono(), sample.sampleRate);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error (std::string ("can't read the audio file: ") + e.what());
    }
}

std::vector<groove::Hit> hitsInClip (const Song& song, ClipId id)
{
    const auto clip = edit::findAudioClip (song, id);
    if (clip == nullptr)
        throw std::runtime_error ("no audio clip " + std::to_string (id));
    Sample sample;
    try
    {
        sample = audio_files::read (clip->file);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error (std::string ("can't read the audio file: ") + e.what());
    }
    const double rate = sample.sampleRate;
    const double speed = edit::audioSpeed (song, *clip);
    const double startSeconds = song.tempo.secondsAt (static_cast<double> (clip->start));
    const double lengthSeconds = song.tempo.secondsAt (static_cast<double> (clip->end())) - startSeconds;
    const auto mono = sample.mono();
    const auto from = static_cast<std::size_t> (std::clamp (clip->offset * rate, 0.0, static_cast<double> (mono.size())));
    const auto to = static_cast<std::size_t> (std::clamp ((clip->offset + lengthSeconds * speed) * rate, static_cast<double> (from), static_cast<double> (mono.size())));
    std::vector<float> part (mono.begin() + static_cast<std::ptrdiff_t> (from), mono.begin() + static_cast<std::ptrdiff_t> (to));
    auto hits = groove::findHits (part, rate);
    for (auto& h : hits)
        h.seconds = startSeconds + h.seconds / std::max (0.01, speed);
    return hits;
}

} // namespace arranger::grooves
