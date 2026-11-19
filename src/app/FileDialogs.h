#pragma once

#include <juce_data_structures/juce_data_structures.h>

/**
    Remembers the last folder of each kind of file dialog, in the app settings.

    The kinds are short names such as "project", "midi" or "audio". Each kind
    keeps its own folder, so an export to a USB stick does not move the
    project dialog.
*/
namespace arranger::file_dialogs
{

/** The app settings to use. Without settings, nothing is remembered. */
void setSettings (juce::PropertiesFile*);

/** The last folder for `kind` when it still exists, else `fallback`. */
juce::File folder (const juce::String& kind, const juce::File& fallback);

/** A file named `fileName` in the folder for `kind` (for save dialogs). */
juce::File saveTarget (const juce::String& kind, const juce::File& fallbackFolder, const juce::String& fileName);

/** Stores the folder of `chosen` (or `chosen` itself, when it is a folder). */
void remember (const juce::String& kind, const juce::File& chosen);

/** Music, or Documents when there is no Music folder. */
juce::File defaultFolder();

} // namespace arranger::file_dialogs
