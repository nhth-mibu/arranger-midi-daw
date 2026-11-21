#include "Localization.h"

#include <BinaryData.h>
#include <juce_graphics/juce_graphics.h>
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
    CHECK (normalise ("pseudo") == "pseudo");
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

TEST_CASE ("Pseudo-localization covers every translated key")
{
    const auto read = [] (const char* code) {
        juce::LocalisedStrings strings (juce::File (juce::String (ARRANGER_LANGUAGES_DIR) + "/" + code + ".txt"), true);
        return strings;
    };
    const auto pseudo = read ("pseudo");
    for (const auto* code : { "da", "fr", "vi" })
    {
        const auto translated = read (code);
        for (const auto& key : translated.getMappings().getAllKeys())
        {
            INFO ("Missing pseudo translation for " << code << ": " << key);
            CHECK (pseudo.translate (key, "__missing__") != "__missing__");
        }
    }
    CHECK (pseudo.translate ("File", "__missing__").length() > juce::String ("File").length());
}

TEST_CASE ("The embedded font covers Vietnamese")
{
    const juce::ScopedJuceInitialiser_GUI init;
    const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_ttf,
                                                                  static_cast<std::size_t> (BinaryData::InterRegular_ttfSize));
    REQUIRE (typeface != nullptr);
    const juce::Font font (juce::FontOptions (14.0f).withTypeface (typeface));
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (font, juce::String::fromUTF8 ("Tiếng Việt ăâđêôơư ắằẳẵặ ấầẩẫậ ớờởỡợ"), 0.0f, 0.0f);
    REQUIRE (glyphs.getNumGlyphs() > 20);
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
        CHECK (glyphs.getGlyph (i).getGlyphIndex() != 0);
}

TEST_CASE ("Small keys are translated")
{
    const juce::LocalisedStrings fr (juce::File (juce::String (ARRANGER_LANGUAGES_DIR) + "/fr.txt"), true);
    CHECK (fr.translate ("on", "__missing__") == "sur");
}
