#pragma once

#include "Theme.h"
#include "../Session.h"

#include <algorithm>

namespace arranger::ui
{

/** Zoom and scroll shared by the track list and the arrangement. */
struct TimelineGeometry
{
    static constexpr int minTrackHeight = 40;
    static constexpr int maxTrackHeight = 160;

    double pixelsPerTick = 96.0 / 3840.0; ///< a 4/4 bar is 96 px
    double scrollTicks = 0.0;
    int scrollY = 0;
    int trackHeight = 64;
    int rulerHeight = 80;                ///< the bar numbers and loop, then the chord lane
    static constexpr int chordLaneHeight = 24;
    int chordLaneTop() const noexcept { return rulerHeight - chordLaneHeight; }

    float tickToX (double tick) const noexcept { return static_cast<float> ((tick - scrollTicks) * pixelsPerTick); }
    double xToTick (float x) const noexcept { return scrollTicks + x / pixelsPerTick; }
    int trackY (int index) const noexcept { return rulerHeight + index * trackHeight - scrollY; }
    int trackAt (int y) const noexcept { return y < rulerHeight ? -1 : (y - rulerHeight + scrollY) / trackHeight; }

    int contentHeight (int trackCount) const noexcept { return trackCount * trackHeight; }
    int maxScrollY (int trackCount, int viewportHeight) const noexcept
    {
        return std::max (0, contentHeight (trackCount) - std::max (0, viewportHeight));
    }

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

    /** Makes the track lanes shorter (factor < 1) or taller (factor > 1). */
    std::function<void (double)> onTrackZoom;

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

    /** Cut, Copy and Paste. With automation points selected, they work on the points, else on the selected clips.
        Paste puts the clipboard (clips, notes or points) on a track at a time. */
    void copySelected (bool cut);
    void paste (TrackId, Tick at);
    /** Selects all points of the line the selected track shows. False when it shows none. */
    bool selectAllPoints();
    bool hasSelectedPoints() const noexcept { return ! selectedPoints.empty(); }
    void clearSelectedPoints() { selectedPoints.clear(); repaint(); }
    void deletePoints();
    /** Adds Cut, Copy and Paste items (ids kCutItem...) to a menu, and handles them. */
    static constexpr int kCutItem = 9001, kCopyItem = 9002, kPasteItem = 9003;
    void addClipboardItems (juce::PopupMenu&, bool canCopy) const;
    bool handleClipboardItem (int result, TrackId, Tick at);

    /** Puts an audio file on the Timeline: on the given track if it is an audio track, else on a new audio track. */
    bool addAudioFile (const juce::File&, TrackId track, Tick at);
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void zoom (double factor, float aroundX);
    /** Changes the height of every track lane. `aroundY` is the row under the mouse; -1 centres it. */
    void zoomVertically (double factor, float aroundY = -1.0f);

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
    /** Audio clip menu > Analyse Audio; the main window sets it. */
    std::function<void (ClipId)> onAnalyse;
    /** Chord lane menu: other chords for the lane chord at a time, and the Smart Chords tool (from a bar, 1-based). */
    std::function<void (Tick)> onReharmonize;
    std::function<void (int bar)> onChordTools;

    /** Opens the chord box on the chord lane at a bar (0-based): type chords, with the next chord to try. */
    void typeChordsAt (Tick barStart);

private:
    class ChordEntry;
    std::unique_ptr<ChordEntry> chordEntry;
    Tick selectedChord = -1; ///< the time of the selected lane chord
    void drawChordLane (juce::Graphics&, int firstBarX, int lastBarX);
    void chordLaneDown (const juce::MouseEvent&);
    void showChordMenu (Tick at);
    void closeChordEntry();

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
    TrackId pointTrack = 0;        ///< the track and line of the selected points
    int pointLane = -1;
    std::set<Tick> selectedPoints; ///< by time, so they stay right when the line changes
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
