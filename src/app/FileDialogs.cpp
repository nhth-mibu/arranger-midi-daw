#include "FileDialogs.h"

namespace arranger::file_dialogs
{

namespace
{
    juce::PropertiesFile* store = nullptr;

    juce::String key (const juce::String& kind) { return "lastFolder." + kind; }
}

void setSettings (juce::PropertiesFile* s) { store = s; }

juce::File defaultFolder()
{
    const auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    return music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

juce::File folder (const juce::String& kind, const juce::File& fallback)
{
    if (store != nullptr)
        if (const juce::File last (store->getValue (key (kind))); last != juce::File() && last.isDirectory())
            return last;
    return fallback;
}

juce::File saveTarget (const juce::String& kind, const juce::File& fallbackFolder, const juce::String& fileName)
{
    return folder (kind, fallbackFolder).getChildFile (fileName);
}

void remember (const juce::String& kind, const juce::File& chosen)
{
    if (store == nullptr || chosen == juce::File())
        return;
    const auto dir = chosen.isDirectory() ? chosen : chosen.getParentDirectory();
    if (dir.isDirectory())
        store->setValue (key (kind), dir.getFullPathName());
}

} // namespace arranger::file_dialogs
