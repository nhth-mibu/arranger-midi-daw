#pragma once

#include "Theme.h"
#include "../Session.h"

namespace arranger::ui
{

/** Zoom and scroll shared by the track list and the arrangement. */
struct TimelineGeometry
{
    double pixelsPerTick = 96.0 / 3840.0; ///< a 4/4 bar is 96 px
    double scrollTicks = 0.0;
    int scrollY = 0;
    int trackHeight = 64;
    int rulerHeight = 56;

    float tickToX (double tick) const noexcept { return static_cast<float> ((tick - scrollTicks) * pixelsPerTick); }
    double xToTick (float x) const noexcept { return scrollTicks + x / pixelsPerTick; }
    int trackY (int index) const noexcept { return rulerHeight + index * trackHeight - scrollY; }
    int trackAt (int y) const noexcept { return y < rulerHeight ? -1 : (y - rulerHeight + scrollY) / trackHeight; }

    /** Which automation line a track shows on the Timeline: 1 = volume, 2 = pan. Not in the map = none. */
    std::map<TrackId, int> automationShown;
    int automationOf (TrackId id) const { const auto it = automationShown.find (id); return it != automationShown.end() ? it->second : 0; }

    std::function<void()> onChange;
    void changed() { if (onChange) onChange(); }
};

/** Track headers: colour, name, output, mute/solo/arm. */
class TrackList final : public juce::Component, private juce::ChangeListener
{
public:
    TrackList (Session&, TimelineGeometry&);
    ~TrackList() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Adds a track after the selected one and selects it. */
    void addTrack();
    void addAudioTrack();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    juce::Rectangle<int> rowBounds (int index) const;
    juce::Rectangle<int> buttonBounds (int index, int which) const; ///< 0 mute, 1 solo, 2 arm
    void showMenu (TrackId);
    void rename (TrackId);

    Session& session;
    TimelineGeometry& geo;
    juce::TextButton addButton { "+" };
    std::unique_ptr<juce::TextEditor> nameEditor;
};

/** The arrangement: ruler with loop, clips on track lanes, playhead. */
class Arrangement final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::SettableTooltipClient,
                          private juce::ChangeListener
{
public:
    Arrangement (Session&, TimelineGeometry&);
    ~Arrangement() override;

    /** Moves the view along with the playhead; repaints only what moved. */
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

    /** Clip operations for menu commands. */
    void deleteSelected();
    void duplicateSelected();
    void splitAtPlayhead();

    /** Puts an audio file on the Timeline: on the given track if it is an audio track, else on a new audio track. */
    bool addAudioFile (const juce::File&, TrackId track, Tick at);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void zoom (double factor, float aroundX);

private:
    std::unique_ptr<juce::FileChooser> audioChooser;
    struct Hit
    {
        ClipId clip = 0;
        int trackIndex = -1;
        bool rightEdge = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    Hit hitTest (juce::Point<int>) const;
    juce::Rectangle<float> clipBounds (const Clip&, int trackIndex) const;
    Tick snap (double tick, const juce::ModifierKeys&) const;
    void drawClip (juce::Graphics&, const Clip&, juce::Rectangle<float>, juce::Colour, bool selected, bool ghost) const;
    void drawAudioClip (juce::Graphics&, const AudioClip&, juce::Rectangle<float>, juce::Colour, bool selected, bool ghost);
    bool clipSpan (ClipId, Tick& start, Tick& length) const;
    void showAudioMenu (ClipId);
    /** bpm: the file tempo; 0 turns following off; less than 0 finds the tempo from the file's length. */
    void setFollowTempo (ClipId, double bpm);
    void askFileTempo (std::set<ClipId>);

public:
    /** Audio to MIDI: puts the notes found in an audio clip on a new MIDI track under it. */
    bool convertToMidi (ClipId, bool drums);

private:
    const std::vector<std::pair<float, float>>& peaksFor (const std::string& file, double& framesPerPeak, double& sampleRate);

    Session& session;
    TimelineGeometry& geo;

    enum class Drag { none, move, resize, loop, locate, marquee, automation };
    void drawAutomation (juce::Graphics&, const Track&, int trackIndex, int lane) const;
    juce::Point<float> automationXY (const AutomationPoint&, int trackIndex) const;
    AutomationPoint automationPointAt (juce::Point<int>, int trackIndex, const juce::ModifierKeys&) const;
    int automationHit (const Track&, int trackIndex, int lane, juce::Point<int>) const;
    TrackId automationTrack = 0; ///< the track of the point that is dragged
    int automationIndex = -1;
    Drag drag = Drag::none;
    Tick dragDeltaTicks = 0;
    int dragDeltaTracks = 0;
    Tick dragNewLength = 0;
    ClipId dragClip = 0;
    Tick loopAnchor = 0;
    juce::Rectangle<int> marquee;
    int shownPlayheadX = -1;

    struct Peaks
    {
        std::vector<std::pair<float, float>> minMax;
        double sampleRate = 48000.0;
    };
    std::map<std::string, Peaks> peaks; ///< waveform overviews, one per audio file
};

} // namespace arranger::ui
