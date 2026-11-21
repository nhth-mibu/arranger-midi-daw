#include "Localization.h"

#include <BinaryData.h>

namespace arranger::localization
{
namespace
{
    constexpr std::array<Option, 4> kOptions { {
        { "en", "English" },
        { "da", "Dansk" },
        { "fr", "Français" },
        { "vi", "Tiếng Việt" },
    } };

    juce::String supportedCode (juce::String code)
    {
        code = code.trim().toLowerCase();
        for (const auto& option : kOptions)
            if (code == option.code)
                return code;
        return {};
    }

    juce::String loadTranslation (const juce::String& code)
    {
        if (code == "da")
            return juce::String::fromUTF8 (BinaryData::da_txt, BinaryData::da_txtSize);
        if (code == "fr")
            return juce::String::fromUTF8 (BinaryData::fr_txt, BinaryData::fr_txtSize);
        if (code == "vi")
            return juce::String::fromUTF8 (BinaryData::vi_txt, BinaryData::vi_txtSize);
        return {};
    }
} // namespace

const std::array<Option, 4>& options()
{
    return kOptions;
}

juce::String normalise (juce::String code)
{
    code = code.trim().toLowerCase();
    if (code == "system")
        return code;
    if (const auto supported = supportedCode (code); supported.isNotEmpty())
        return supported;
    return "en";
}

juce::String systemCode()
{
    const auto language = juce::SystemStats::getDisplayLanguage().toLowerCase();
    if (language.startsWith ("da"))
        return "da";
    if (language.startsWith ("fr"))
        return "fr";
    if (language.startsWith ("vi"))
        return "vi";
    return "en";
}

juce::String effectiveCode (const juce::String& setting)
{
    const auto normalised = normalise (setting);
    return normalised == "system" ? systemCode() : normalised;
}

juce::String nativeName (const juce::String& code)
{
    const auto normalised = normalise (code);
    for (const auto& option : kOptions)
        if (normalised == option.code)
            return juce::String::fromUTF8 (option.nativeName);
    return "English";
}

juce::String settingFromMenuIndex (int index)
{
    if (index == 0)
        return "system";
    if (index > 0 && index <= static_cast<int> (kOptions.size()))
        return kOptions[static_cast<std::size_t> (index - 1)].code;
    return "system";
}

juce::String selectedSetting (juce::PropertiesFile* settings)
{
    if (settings == nullptr)
        return "system";
    return normalise (settings->getValue (kSettingKey, "system"));
}

void apply (const juce::String& setting)
{
    const auto code = effectiveCode (setting);
    juce::LocalisedStrings::setCurrentMappings (nullptr);

    if (const auto text = loadTranslation (code); text.isNotEmpty())
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (text, true));
}
} // namespace arranger::localization
