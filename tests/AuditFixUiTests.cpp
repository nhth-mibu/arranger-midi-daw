#include "ui/ArrangeView.h"
#include "ui/ChordsView.h"
#include "ui/ClipGridView.h"
#include "ui/MainComponent.h"
#include "ui/PianoRoll.h"
#include "ui/ProposalCard.h"
#include "ui/SpeechView.h"
#include "ui/TimelineViews.h"

#include "arranger/core/ChordInput.h"
#include "arranger/core/SongEdit.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <optional>
#include <vector>

#if JUCE_LINUX
// Under bare Xvfb (no window manager), atoms like WM_PROTOCOLS do not exist (0x0), so JUCE's
// XWindowSystem::createWindow() triggers a benign X11 BadAtom reply. Real JUCE apps survive this
// because JUCEApplicationBase::isStandaloneApp() is true, causing XWindowSystem to call
// XInitThreads() and install JUCE's non-fatal X11 error handler. Catch2 test binaries have no
// JUCEApplicationBase (createInstance == nullptr), so JUCE treats them like a plugin and skips
// both, letting libX11's default fatal error handler abort the process.
class LinuxJuceGuiListener final : public Catch::EventListenerBase
{
public:
    using Catch::EventListenerBase::EventListenerBase;

    ~LinuxJuceGuiListener() override { shutdown(); }

    void testRunStarting (Catch::TestRunInfo const&) override
    {
        if (active)
            return;
        active = true;
        previousCreateInstance = juce::JUCEApplicationBase::createInstance;
        if (juce::JUCEApplicationBase::createInstance == nullptr)
            juce::JUCEApplicationBase::createInstance = []() -> juce::JUCEApplicationBase* { return nullptr; };
        guiInit.emplace();
    }

    void testRunEnded (Catch::TestRunStats const&) override { shutdown(); }

private:
    void shutdown()
    {
        if (! active)
            return;
        active = false;
        // Shut down JUCE GUI (including XWindowSystem::removeXErrorHandlers) before restoring createInstance.
        guiInit.reset();
        juce::JUCEApplicationBase::createInstance = previousCreateInstance;
        previousCreateInstance = nullptr;
    }

    bool active = false;
    juce::JUCEApplicationBase::CreateInstanceFunction previousCreateInstance = nullptr;
    std::optional<juce::ScopedJuceInitialiser_GUI> guiInit;
};

CATCH_REGISTER_LISTENER (LinuxJuceGuiListener)
#endif

namespace
{

void collectTextButtons (juce::Component& root, std::vector<juce::TextButton*>& out)
{
    for (int i = 0; i < root.getNumChildComponents(); ++i)
    {
        auto* child = root.getChildComponent (i);
        if (auto* btn = dynamic_cast<juce::TextButton*> (child))
            out.push_back (btn);
        collectTextButtons (*child, out);
    }
}

juce::TextButton* findButtonWithText (juce::Component& root, const juce::String& text)
{
    std::vector<juce::TextButton*> buttons;
    collectTextButtons (root, buttons);
    for (auto* b : buttons)
        if (b->getButtonText() == text)
            return b;
    return nullptr;
}

juce::TextEditor* findFirstTextEditor (juce::Component& root)
{
    for (int i = 0; i < root.getNumChildComponents(); ++i)
    {
        auto* child = root.getChildComponent (i);
        if (auto* ed = dynamic_cast<juce::TextEditor*> (child))
            return ed;
        if (auto* nested = findFirstTextEditor (*child))
            return nested;
    }
    return nullptr;
}

template <typename Predicate>
void pumpUntil (Predicate&& done, int maxSteps = 50)
{
    for (int i = 0; i < maxSteps && ! done(); ++i)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

} // namespace

TEST_CASE ("C1: TrackList menu IDs do not collide between colours and plugin control automation", "[audit][ui][c1]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);
    arranger::Session session (engine, devices);
    arranger::ui::TimelineGeometry geo;

    arranger::Song song;
    auto& track = arranger::edit::addTrack (song, "Synth");
    track.effects.push_back ({ "EQ", "VST3", "eq-1" });
    const auto trackId = track.id;
    const int controlLane = arranger::edit::addControlLine (song, trackId, 0, 0, "Cutoff");
    REQUIRE (controlLane == arranger::Track::kFirstControlLane);
    arranger::edit::setAutomationPoint (song, trackId, controlLane, { 0, 0.75f });
    session.load (std::move (song));

    arranger::ui::TrackList trackList (session, geo);

    // 1. Selecting a track colour changes only the colour and does not open an automation lane.
    trackList.handleMenuResult (trackId, arranger::ui::TrackList::kFirstColourItem + 2);
    CHECK (session.song().tracks.front().colour == arranger::kTrackColours[2]);
    CHECK (geo.automationOf (trackId) == 0);

    // 2. Showing a plugin control automation lane does not collide with track colours.
    trackList.handleMenuResult (trackId, arranger::ui::TrackList::kFirstShowControlItem + controlLane);
    CHECK (geo.automationOf (trackId) == controlLane + 1);
    CHECK (session.song().tracks.front().colour == arranger::kTrackColours[2]);

    // 3. Removing a plugin control automation lane clears the lane without OOB reading kTrackColours.
    REQUIRE_FALSE (session.song().tracks.front().automationLine (controlLane).empty());
    trackList.handleMenuResult (trackId, arranger::ui::TrackList::kFirstRemoveControlItem + controlLane);
    CHECK (session.song().tracks.front().automationLine (controlLane).empty());
    CHECK (geo.automationOf (trackId) == 0);
    CHECK (session.song().tracks.front().colour == arranger::kTrackColours[2]);

    // 4. Adding a plugin control automation lane (r >= kFirstPluginControlItem) does not fall through to kTrackColours.
    trackList.handleMenuResult (trackId, arranger::ui::TrackList::kFirstPluginControlItem + 5000 + 3);
    CHECK (session.song().tracks.front().laneCount() == arranger::Track::kFirstControlLane + 1);
    CHECK (geo.automationOf (trackId) == arranger::Track::kFirstControlLane + 1);
    CHECK (session.song().tracks.front().colour == arranger::kTrackColours[2]);

    // 5. Out-of-bounds colour IDs are ignored safely.
    trackList.handleMenuResult (trackId, arranger::ui::TrackList::kFirstColourItem + 50);
    CHECK (session.song().tracks.front().colour == arranger::kTrackColours[2]);
}

TEST_CASE ("C2: Clicking UI buttons that close or rebuild their own parent defers button destruction", "[audit][ui][c2]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);
    arranger::Session session (engine, devices);

    SECTION ("Site 1: SpeechView closeButton defers DialogWindow deletion")
    {
        arranger::SpeechService speech (session, devices, nullptr);
        auto view = std::make_unique<arranger::ui::SpeechView> (speech);
        bool closedCallbackRan = false;
        view->onClose = [&closedCallbackRan] { closedCallbackRan = true; };

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Speech Test";
        options.dialogBackgroundColour = juce::Colours::black;
        options.content.setOwned (view.release());
        options.useNativeTitleBar = false;
        juce::Component::SafePointer<juce::DialogWindow> window = options.launchAsync();
        REQUIRE (window != nullptr);

        auto* closeBtn = findButtonWithText (*window, "Close");
        REQUIRE (closeBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeBtn (closeBtn);

        closeBtn->onClick();
        CHECK (closedCallbackRan);
        // The button and window must still be alive when onClick() returns to Button::sendClickMessage.
        CHECK (safeBtn != nullptr);
        CHECK (window != nullptr);

        pumpUntil ([&] { return safeBtn == nullptr && window == nullptr; });
        CHECK (safeBtn == nullptr);
        CHECK (window == nullptr);
    }

    SECTION ("Sites 2 & 3: ArrangeView ProposalCard Use and Dismiss buttons stay alive during onClick")
    {
        arranger::Song song;
        song.tracks.clear();
        const arranger::Tick beat = arranger::kTicksPerQuarter;
        const arranger::Tick bar = beat * 4;
        constexpr int kBars = 16;
        auto addBandTrack = [&] (const std::string& name, int channel, const std::function<void (std::vector<arranger::Note>&)>& fill) {
            auto& t = arranger::edit::addTrack (song, name);
            t.output = { arranger::TrackOutput::Kind::soundFont, {}, channel };
            arranger::Clip c;
            c.start = 0;
            c.length = bar * kBars;
            fill (c.notes);
            arranger::edit::normalise (c);
            arranger::edit::addClip (song, t.id, c);
        };
        addBandTrack ("Drums", 10, [&] (auto& n) {
            for (arranger::Tick t = 0; t < bar * kBars; t += beat / 2)
                n.push_back ({ t, 60, 42, 80 });
        });
        addBandTrack ("Bass", 2, [&] (auto& n) {
            for (arranger::Tick t = 0; t < bar * kBars; t += beat)
                n.push_back ({ t, beat, 36, 90 });
        });
        addBandTrack ("Piano", 3, [&] (auto& n) {
            for (arranger::Tick t = 0; t < bar * kBars; t += beat / 2)
                for (int p : { 60, 64, 67 })
                    n.push_back ({ t, beat / 2, static_cast<std::uint8_t> (p), 90 });
        });
        addBandTrack ("Melody", 4, [&] (auto& n) {
            const int tune[] = { 64, 67, 69, 67 };
            for (arranger::Tick t = 0, i = 0; t < bar * kBars; t += beat, ++i)
                n.push_back ({ t, beat, static_cast<std::uint8_t> (tune[i % 4]), 100 });
        });
        session.load (std::move (song));

        arranger::ui::ArrangeView arrange (session, "arrangement_doctor");
        arrange.setSize (500, 700);
        arrange.choose ("clear_melody", 0, 0);

        auto* dismissBtn = findButtonWithText (arrange, "Dismiss");
        REQUIRE (dismissBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeDismiss (dismissBtn);
        dismissBtn->onClick();
        CHECK (safeDismiss != nullptr);
        pumpUntil ([&] { return safeDismiss == nullptr; });
        CHECK (safeDismiss == nullptr);

        arrange.choose ("clear_melody", 0, 0);
        auto* useBtn = findButtonWithText (arrange, "Use");
        REQUIRE (useBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeUse (useBtn);
        useBtn->onClick();
        CHECK (safeUse != nullptr);
        pumpUntil ([&] { return safeUse == nullptr; });
        CHECK (safeUse == nullptr);
    }

    SECTION ("Sites 4 & 5: ChordsView suggestion buttons and reharmonize ProposalCard buttons stay alive during onClick")
    {
        arranger::Song song;
        arranger::edit::addTrack (song, "Chords");
        const auto typed = arranger::chord_input::typeChords ("C | Am | F | G", arranger::chord_input::songKey (song), song.timeSignature, 0);
        arranger::chord_input::writeChords (song, 0, typed.end, typed.marks, &session.deviceLibrary());
        session.load (std::move (song));

        arranger::ui::ChordsView chords (session);
        chords.setSize (500, 700);

        // Site 4: suggestion button click triggers typed.setText(..., true) -> refreshSuggestions().
        std::vector<juce::TextButton*> buttons;
        collectTextButtons (chords, buttons);
        juce::TextButton* sugBtn = nullptr;
        for (auto* b : buttons)
            if (b->getTooltip().endsWith ("Click to add it."))
            {
                sugBtn = b;
                break;
            }
        REQUIRE (sugBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeSug (sugBtn);
        sugBtn->onClick();
        CHECK (safeSug != nullptr);
        pumpUntil ([&] { return safeSug == nullptr; });
        CHECK (safeSug == nullptr);

        // Site 5a: reharmonize ProposalCard Dismiss button.
        chords.reharmonizeAt (0);
        auto* dismissBtn = findButtonWithText (chords, "Dismiss");
        REQUIRE (dismissBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeDismiss (dismissBtn);
        dismissBtn->onClick();
        CHECK (safeDismiss != nullptr);
        pumpUntil ([&] { return safeDismiss == nullptr; });
        CHECK (safeDismiss == nullptr);

        // Site 5b: reharmonize ProposalCard Use button.
        chords.reharmonizeAt (0);
        auto* useBtn = findButtonWithText (chords, "Use");
        REQUIRE (useBtn != nullptr);
        juce::Component::SafePointer<juce::TextButton> safeUse (useBtn);
        useBtn->onClick();
        CHECK (safeUse != nullptr);
        pumpUntil ([&] { return safeUse == nullptr; });
        CHECK (safeUse == nullptr);
    }

    SECTION ("Site 6: Timeline Arrangement::ChordEntry suggestion button stays alive during onClick")
    {
        arranger::Song song;
        arranger::edit::addTrack (song, "Piano");
        session.load (std::move (song));

        arranger::ui::TimelineGeometry geo;
        arranger::ui::Arrangement arrangement (session, geo);
        arrangement.setSize (800, 400);
        arrangement.typeChordsAt (0);

        std::vector<juce::TextButton*> buttons;
        collectTextButtons (arrangement, buttons);
        REQUIRE_FALSE (buttons.empty());
        auto* sugBtn = buttons.front();
        juce::Component::SafePointer<juce::TextButton> safeSug (sugBtn);

        sugBtn->onClick();
        CHECK (safeSug != nullptr);
        pumpUntil ([&] { return safeSug == nullptr; });
        CHECK (safeSug == nullptr);
    }
}

TEST_CASE ("C3: ClipGridView::startRename onFocusLost does not self-destruct its executing lambda", "[audit][ui][c3]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);
    arranger::Session session (engine, devices);

    arranger::Song song;
    arranger::edit::addTrack (song, "Loops");
    session.load (std::move (song));

    arranger::ui::ClipGridView grid (session);
    grid.setSize (800, 500);

    // Double-clicking the scene 0 cell in the scene column triggers renameScene(0) -> startRename().
    const juce::Point<float> scene0Pos (60.0f, 100.0f);
    juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(),
                         scene0Pos,
                         juce::ModifierKeys(),
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         &grid, &grid,
                         juce::Time::getCurrentTime(),
                         scene0Pos,
                         juce::Time::getCurrentTime(),
                         2, false);
    grid.mouseDoubleClick (ev);

    auto* editor = findFirstTextEditor (grid);
    REQUIRE (editor != nullptr);
    REQUIRE ( static_cast<bool> (editor->onFocusLost));

    editor->setText ("Verse Loop", false);
    // Invoking onFocusLost previously assigned nameEditor->onFocusLost = nullptr before calling close() and done(name).
    editor->onFocusLost();

    REQUIRE_FALSE (session.song().scenes.empty());
    CHECK (session.song().scenes[0] == "Verse Loop");

    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (findFirstTextEditor (grid) == nullptr);
}

TEST_CASE ("H2: MainComponent closes floating windows on destruction", "[audit][ui][h2]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);

    const int beforeWindows = juce::TopLevelWindow::getNumTopLevelWindows();
    {
        auto main = std::make_unique<arranger::ui::MainComponent> (engine, devices, nullptr, nullptr);
        main->showAnalysis (0, false);
        main->openSpeechSettings();
        main->commandManager().invokeDirectly (arranger::ui::MainComponent::cmdShortcuts, false);
        main->commandManager().invokeDirectly (arranger::ui::MainComponent::cmdAudioSettings, false);
        CHECK (juce::TopLevelWindow::getNumTopLevelWindows() >= beforeWindows + 4);
    }
    CHECK (juce::TopLevelWindow::getNumTopLevelWindows() == beforeWindows);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (juce::TopLevelWindow::getNumTopLevelWindows() == beforeWindows);
}

TEST_CASE ("H3: Queued async UI callbacks do not UAF when their component is destroyed first", "[audit][ui][h3]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);
    arranger::Session session (engine, devices);
    arranger::ui::TimelineGeometry geo;

    arranger::Song song;
    const auto trackId = arranger::edit::addTrack (song, "Track 1").id;
    session.load (std::move (song));

    {
        auto trackList = std::make_unique<arranger::ui::TrackList> (session, geo);
        trackList->setSize (240, 400);
        // Menu item 1 opens the inline track rename editor, which posts callAsync on commit/escape.
        trackList->handleMenuResult (trackId, 1);
        auto* editor = findFirstTextEditor (*trackList);
        REQUIRE (editor != nullptr);
        editor->setText ("Renamed", false);
        editor->onReturnKey();
        // Destroy TrackList before the queued callAsync lambda runs.
        trackList.reset();
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (session.song().tracks.front().name == "Renamed");
}

TEST_CASE ("H4: PianoRoll::mouseDrag does not dereference empty selected vector when selection is pruned mid-drag", "[audit][ui][h4]")
{
    juce::ScopedJuceInitialiser_GUI init;
    arranger::Engine engine;
    arranger::platform::JuceDeviceHost devices (engine);
    arranger::Session session (engine, devices);

    arranger::Song song;
    auto& track = arranger::edit::addTrack (song, "Lead");
    arranger::Clip clip;
    clip.name = "Part";
    clip.start = 0;
    clip.length = song.timeSignature.ticksPerBar();
    clip.notes.push_back ({ 0, 480, 60, 100 });
    const auto clipId = arranger::edit::addClip (song, track.id, clip);
    session.load (std::move (song));
    session.openInEditor (clipId);

    arranger::ui::PianoRoll roll (session);
    roll.setSize (800, 500);
    roll.resized();

    // Find the rendered note by scanning the grid for a point where mouseDown selects the note.
    juce::Point<float> notePoint (-1.0f, -1.0f);
    for (int y = 65; y < 420 && notePoint.x < 0.0f; y += 4)
    {
        for (int x = 60; x < 300; x += 6)
        {
            const juce::Point<float> p (static_cast<float> (x), static_cast<float> (y));
            juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(),
                                   p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                   0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   &roll, &roll, juce::Time::getCurrentTime(),
                                   p, juce::Time::getCurrentTime(), 1, false);
            roll.mouseDown (down);
            if (roll.hasNoteSelection())
            {
                notePoint = p;
                break;
            }
        }
    }
    REQUIRE (notePoint.x > 0.0f);
    REQUIRE (roll.hasNoteSelection());

    // Externally clear the clip's notes mid-drag (e.g. via Undo or MCP edit), pruning `selected` to empty.
    session.edit ("Clear Notes Externally", [clipId] (arranger::Song& s) {
        arranger::edit::modifyClip (s, clipId, [] (arranger::Clip& c) { c.notes.clear(); });
    });
    CHECK_FALSE (roll.hasNoteSelection());

    // Dragging the mouse now must not dereference selected.front().
    const juce::Point<float> dragPos = notePoint + juce::Point<float> (40.0f, -24.0f);
    juce::MouseEvent dragEvent (juce::Desktop::getInstance().getMainMouseSource(),
                                dragPos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                &roll, &roll, juce::Time::getCurrentTime(),
                                notePoint, juce::Time::getCurrentTime(), 1, false);
    roll.mouseDrag (dragEvent);
    CHECK_FALSE (roll.hasNoteSelection());
}
