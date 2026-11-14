#include "SlicerView.h"

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
}

SlicerView::SlicerView (Session& s, TrackId t, const juce::File& loop)
    : session (s), track (t), path (loop.getFullPathName().toStdString())
{
    sample = session.sampleFor (path);
    if (sample != nullptr)
        mono = sample->mono();

    addAndMakeVisible (modeBox);
    modeBox.addItem ("Slice at each hit", transients);
    modeBox.addItem ("Slice in equal parts", grid);
    modeBox.addItem ("Slice by hand", manual);
    modeBox.setSelectedId (transients, juce::dontSendNotification);
    modeBox.onChange = [this] { reslice(); };

    addAndMakeVisible (sensitivity);
    sensitivity.setRange (0.0, 1.0);
    sensitivity.setValue (0.5, juce::dontSendNotification);
    sensitivity.setTooltip ("How easily a hit starts a new slice");
    sensitivity.onValueChange = [this] { reslice(); };

    addAndMakeVisible (gridBox);
    for (const int n : { 2, 4, 8, 16, 32, 64 })
        gridBox.addItem (juce::String (n) + " parts", n);
    gridBox.setSelectedId (16, juce::dontSendNotification);
    gridBox.onChange = [this] { reslice(); };

    addAndMakeVisible (barsBox);
    for (const int n : { 1, 2, 4, 8, 16 })
        barsBox.addItem ("Loop is " + juce::String (n) + (n == 1 ? " bar" : " bars"), n);
    barsBox.setSelectedId (guessBars(), juce::dontSendNotification);
    barsBox.setTooltip ("The length of the loop in bars. The clip plays each slice at its place in these bars, at the tempo of the song.");

    addAndMakeVisible (patternButton);
    patternButton.onClick = [this] { makePattern(); };

    addAndMakeVisible (info);
    info.setJustificationType (juce::Justification::centredLeft);

    setSize (980, 420);
    reslice();
}

int SlicerView::guessBars() const
{
    if (sample == nullptr)
        return 1;
    // How many bars of the song's tempo fit the loop; then the nearest of 1, 2, 4, 8, 16.
    const auto& song = session.song();
    const double barSeconds = 60.0 / song.baseTempo() * 4.0 * song.timeSignature.numerator / song.timeSignature.denominator;
    const double bars = sample->seconds() / barSeconds;
    int best = 1;
    for (const int n : { 1, 2, 4, 8, 16 })
        if (std::abs (std::log2 (bars / n)) < std::abs (std::log2 (bars / best)))
            best = n;
    return best;
}

void SlicerView::reslice()
{
    if (sample == nullptr)
        return;
    const int mode = modeBox.getSelectedId();
    sensitivity.setVisible (mode == transients);
    gridBox.setVisible (mode == grid);

    if (mode == transients)
        slices = slicing::byTransients (*sample, { sensitivity.getValue(), 0.05 });
    else if (mode == grid)
        slices = slicing::byGrid (sample->frames(), gridBox.getSelectedId());
    // manual: keep the slices as they are and let the user move the markers

    if (slices.size() > 92) // notes 36..127
        slices.resize (92);
    applyToTrack();
}

void SlicerView::applyToTrack()
{
    const auto name = sample != nullptr ? sample->name : std::string();
    session.edit ("Slice Loop", [this, name] (Song& song) {
        auto* t = edit::findTrack (song, track);
        if (t == nullptr)
            return;
        t->output.kind = TrackOutput::Kind::sampler;
        t->kit.name = name;
        t->kit.pads.clear();
        int note = kFirstNote;
        for (const auto& s : slices)
        {
            KitPad pad;
            pad.name = "Slice " + std::to_string (note - kFirstNote + 1);
            pad.note = note++;
            pad.sampleFile = path;
            pad.sliceStart = s.start;
            pad.sliceEnd = s.end;
            pad.chokeGroup = 1; // each slice cuts the one before, like the loop itself
            t->kit.pads.push_back (std::move (pad));
        }
    }, "slicer-" + std::to_string (track));

    info.setText (juce::String (slices.size()) + " slices" + juce::String::fromUTF8 (" \xc2\xb7 ") + "click a slice to hear it" + juce::String::fromUTF8 (" \xc2\xb7 ")
                      + "double-click to add or remove a marker",
                  juce::dontSendNotification);
    info.setFont (currentTheme().font (13.0f));
    info.setColour (juce::Label::textColourId, currentTheme().colour ("textMuted"));
    repaint();
}

void SlicerView::makePattern()
{
    if (sample == nullptr || slices.empty())
        return;
    const int bars = juce::jmax (1, barsBox.getSelectedId());
    const Tick bar = session.song().timeSignature.ticksPerBar();
    const Tick length = bar * bars;
    Tick start = (static_cast<Tick> (session.position()) / bar) * bar;
    if (const auto* t = edit::findTrack (session.song(), track))
        for (const auto& c : t->clips)
            if (start < c->end() && start + length > c->start)
                start = ((c->end() + bar - 1) / bar) * bar;

    const auto frames = static_cast<double> (sample->frames());
    ClipId made = 0;
    session.endMerge();
    session.edit ("Make Clip From Slices", [&] (Song& song) {
        Clip c;
        c.name = sample->name;
        c.start = start;
        c.length = length;
        int note = kFirstNote;
        for (const auto& s : slices)
        {
            const auto at = static_cast<Tick> (std::llround (static_cast<double> (s.start) / frames * static_cast<double> (length)));
            const auto end = static_cast<Tick> (std::llround (static_cast<double> (s.end) / frames * static_cast<double> (length)));
            c.notes.push_back ({ at, std::max<Tick> (1, end - at), static_cast<std::uint8_t> (note++), 127 });
        }
        made = edit::addClip (song, track, c);
    });
    session.selectClips ({ made });
}

juce::Rectangle<int> SlicerView::waveArea() const
{
    return getLocalBounds().reduced (16).withTrimmedTop (48).withTrimmedBottom (56);
}

float SlicerView::xForFrame (std::size_t frame) const
{
    const auto area = waveArea().toFloat();
    return area.getX() + area.getWidth() * static_cast<float> (frame) / static_cast<float> (juce::jmax<std::size_t> (1, mono.size()));
}

std::size_t SlicerView::frameForX (int x) const
{
    const auto area = waveArea();
    const double f = juce::jlimit (0.0, 1.0, static_cast<double> (x - area.getX()) / juce::jmax (1, area.getWidth()));
    return static_cast<std::size_t> (f * static_cast<double> (mono.size()));
}

int SlicerView::sliceAt (int x) const
{
    const auto frame = frameForX (x);
    for (std::size_t i = 0; i < slices.size(); ++i)
        if (frame >= slices[i].start && frame < slices[i].end)
            return static_cast<int> (i);
    return -1;
}

void SlicerView::mouseDown (const juce::MouseEvent& e)
{
    if (! waveArea().contains (e.getPosition()))
        return;
    playing = sliceAt (e.x);
    if (playing >= 0)
        session.previewNote (track, kFirstNote + playing, 127, 4000);
    repaint();
}

void SlicerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sample == nullptr || ! waveArea().contains (e.getPosition()))
        return;

    // Near a marker: remove it. Otherwise: add one at the nearest zero crossing.
    std::vector<std::size_t> starts;
    for (const auto& s : slices)
        starts.push_back (s.start);
    bool removed = false;
    for (std::size_t i = 1; i < starts.size(); ++i)
        if (std::abs (xForFrame (starts[i]) - static_cast<float> (e.x)) < 6.0f)
        {
            starts.erase (starts.begin() + static_cast<std::ptrdiff_t> (i));
            removed = true;
            break;
        }
    if (! removed)
        starts.push_back (slicing::nearestZeroCrossing (mono, frameForX (e.x), static_cast<std::size_t> (sample->sampleRate * 0.002)));

    slices = slicing::byMarkers (sample->frames(), std::move (starts));
    modeBox.setSelectedId (manual, juce::dontSendNotification);
    reslice();
}

void SlicerView::resized()
{
    auto top = getLocalBounds().reduced (16).removeFromTop (36);
    modeBox.setBounds (top.removeFromLeft (190));
    top.removeFromLeft (10);
    const auto option = top.removeFromLeft (200);
    sensitivity.setBounds (option);
    gridBox.setBounds (option.withWidth (120));

    auto bottom = getLocalBounds().reduced (16).removeFromBottom (40).withTrimmedTop (4);
    patternButton.setBounds (bottom.removeFromRight (250));
    bottom.removeFromRight (10);
    barsBox.setBounds (bottom.removeFromRight (150));
    bottom.removeFromRight (10);
    info.setBounds (bottom);
}

void SlicerView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));
    const auto area = waveArea();

    g.setColour (t.colour ("lcd"));
    g.fillRoundedRectangle (area.toFloat(), 8.0f);

    if (sample == nullptr)
    {
        g.setColour (t.colour ("textMuted"));
        g.setFont (t.font (14.0f));
        g.drawText ("This file can't be read.", area, juce::Justification::centred);
        return;
    }

    const auto* tr = edit::findTrack (session.song(), track);
    const auto colour = t.trackColour (tr != nullptr ? utf8 (tr->colour) : juce::String ("teal"));

    // Slices as alternating bands, the last clicked one lit.
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        const float x1 = xForFrame (slices[i].start), x2 = xForFrame (slices[i].end);
        const bool lit = static_cast<int> (i) == playing;
        g.setColour (colour.withAlpha (lit ? 0.28f : (i % 2 == 0 ? 0.10f : 0.04f)));
        g.fillRect (juce::Rectangle<float> (x1, static_cast<float> (area.getY()), x2 - x1, static_cast<float> (area.getHeight())));
    }

    // The waveform: lowest and highest value under each pixel.
    g.setColour (colour);
    const float mid = static_cast<float> (area.getCentreY()), half = static_cast<float> (area.getHeight()) * 0.46f;
    const int width = area.getWidth();
    for (int px = 0; px < width; ++px)
    {
        const auto from = static_cast<std::size_t> (static_cast<double> (px) / width * static_cast<double> (mono.size()));
        const auto to = juce::jmin (mono.size(), juce::jmax (from + 1, static_cast<std::size_t> (static_cast<double> (px + 1) / width * static_cast<double> (mono.size()))));
        float lo = 0.0f, hi = 0.0f;
        for (auto i = from; i < to; ++i)
        {
            lo = juce::jmin (lo, mono[i]);
            hi = juce::jmax (hi, mono[i]);
        }
        g.drawVerticalLine (area.getX() + px, mid - hi * half - 0.5f, mid - lo * half + 0.5f);
    }

    // Markers and slice numbers.
    g.setFont (t.font (11.0f));
    for (std::size_t i = 0; i < slices.size(); ++i)
    {
        const float x = xForFrame (slices[i].start);
        g.setColour (t.colour ("playhead").withAlpha (0.75f));
        g.drawVerticalLine (static_cast<int> (x), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
        if (xForFrame (slices[i].end) - x > 16.0f)
        {
            g.setColour (t.colour ("textMuted"));
            g.drawText (juce::String (i + 1), juce::Rectangle<float> (x + 4.0f, static_cast<float> (area.getY()) + 3.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
}

} // namespace arranger::ui
