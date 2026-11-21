#pragma once

#include <array>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace arranger::localization
{
/** The settings key that stores the UI language. */
inline constexpr const char* kSettingKey = "uiLanguage";

/** A language that the UI can show. The code is stored in the settings file. */
struct Option
{
    const char* code;
    const char* nativeName;
};

/** English, Danish, French and Vietnamese, in menu order. */
const std::array<Option, 4>& options();

/** "system" stays "system"; a supported language code is returned as is;
    anything else falls back to English. Case and region suffixes are ignored.
*/
juce::String normalise (juce::String code);

/** The language of the operating system, or English when it is not supported. */
juce::String systemCode();

/** The language that the UI will use for this setting ("system" included). */
juce::String effectiveCode (const juce::String& setting);

/** The name of a language in that language: "Dansk", "Français", ... */
juce::String nativeName (const juce::String& code);

/** The setting selected in a language menu: 0 is "system", 1 to 4 are options(). */
juce::String settingFromMenuIndex (int index);

/** The saved language setting, normalised to "system" or a supported code. */
juce::String selectedSetting (juce::PropertiesFile* settings);

/** Loads the translations for this setting into JUCE. Call it before the UI is built. */
void apply (const juce::String& setting);
} // namespace arranger::localization
