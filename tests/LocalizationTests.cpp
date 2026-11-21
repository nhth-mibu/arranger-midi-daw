#include "Localization.h"
#include "ui/AppTooltip.h"

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <string>

namespace
{
    juce::StringArray translationKeys()
    {
        juce::StringArray keys;
        const juce::File uiFolder (juce::String (ARRANGER_SOURCE_DIR) + "/src/app/ui");
        const std::regex pattern (R"re(TRANS \("([^"]+)"\))re");

        for (const auto& file : uiFolder.findChildFiles (juce::File::findFiles, false, "*.cpp"))
        {
            const auto text = file.loadFileAsString();
            REQUIRE (text.isNotEmpty());
            const auto source = text.toStdString();
            for (std::sregex_iterator it (source.begin(), source.end(), pattern), end; it != end; ++it)
                keys.addIfNotAlreadyThere (juce::String::fromUTF8 ((*it)[1].str().c_str()));
        }
        return keys;
    }

    class ShortcutTarget final : public juce::ApplicationCommandTarget
    {
    public:
        enum { command = 1234 };

        juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
        void getAllCommands (juce::Array<juce::CommandID>& ids) override { ids.add (command); }
        void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo& info) override
        {
            // The translated short name must not be used to find the command.
            info.setInfo ("Tidslinje", "Translated short name", "View", 0);
        }
        bool perform (const InvocationInfo&) override { return true; }
    };
} // namespace

TEST_CASE ("Language settings normalise to a supported code")
{
    using namespace arranger::localization;

    CHECK (normalise ("system") == "system");
    CHECK (normalise ("DA") == "da");
    CHECK (normalise ("fr") == "fr");
    CHECK (normalise ("vi") == "vi");
    CHECK (normalise ("de") == "en");
    CHECK (normalise ("") == "en");
    CHECK (effectiveCode ("da") == "da");
    CHECK (effectiveCode ("xx") == "en");
    CHECK (settingFromMenuIndex (0) == "system");
    CHECK (settingFromMenuIndex (1) == "en");
    CHECK (settingFromMenuIndex (2) == "da");
    CHECK (settingFromMenuIndex (3) == "fr");
    CHECK (settingFromMenuIndex (4) == "vi");
    CHECK (settingFromMenuIndex (5) == "system");
}

TEST_CASE ("Every TRANS string has a translation")
{
    const auto keys = translationKeys();
    REQUIRE (keys.size() > 90);

    for (const auto* code : { "da", "fr", "vi" })
    {
        const auto path = juce::String (ARRANGER_LANGUAGES_DIR) + "/" + code + ".txt";
        const juce::File file (path);
        REQUIRE (file.existsAsFile());

        const juce::LocalisedStrings strings (file, true);
        CHECK (strings.getLanguageName().isNotEmpty());

        for (const auto& key : keys)
        {
            INFO ("Missing " << code << " translation for: " << key);
            CHECK (strings.translate (key, "__missing__") != "__missing__");
        }
    }
}

TEST_CASE ("Tooltip shortcuts are found by command id, not translated name")
{
    const juce::ScopedJuceInitialiser_GUI init;
    ShortcutTarget target;
    juce::ApplicationCommandManager manager;
    manager.registerAllCommandsForTarget (&target);
    manager.setFirstCommandTarget (&target);

    REQUIRE (manager.getCommandForID (ShortcutTarget::command) != nullptr);
    manager.getKeyMappings()->addKeyPress (ShortcutTarget::command,
                                           juce::KeyPress ('1', juce::ModifierKeys::commandModifier, 0));

    const auto tip = arranger::ui::shortcuts::withShortcut ("Timeline", manager, ShortcutTarget::command);
    CHECK (tip.contains ("Timeline"));
    CHECK ((tip.contains ("Cmd+1") || tip.contains ("Ctrl+1")));
}
