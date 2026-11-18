#include "StyleFromAudioView.h"

#include "arranger/core/SongEdit.h"

#include <cmath>

namespace arranger::ui
{

namespace
{
    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }

    float textHeight (const juce::String& text, const juce::Font& font, int width)
    {
        if (text.isEmpty() || width <= 0)
            return 0.0f;
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font);
        juce::TextLayout layout;
        layout.createLayout (s, static_cast<float> (width));
        return std::ceil (layout.getHeight());
    }

    void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, juce::Rectangle<float> area)
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font, colour);
        juce::TextLayout layout;
        layout.createLayout (s, area.getWidth());
        layout.draw (g, area);
    }

    /** A thin meter: how much of 1 a value is. */
    void meter (juce::Graphics& g, juce::Rectangle<float> r, double value)
    {
        const auto& t = currentTheme();
        g.setColour (t.colour ("line"));
        g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
        g.setColour (t.colour ("accent"));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * static_cast<float> (std::clamp (value, 0.0, 1.0))), r.getHeight() / 2.0f);
    }

    constexpr int kRowHeight = 46;
}

/** What was made: the match, A/B, the sections with their meters, what the loop changed, save. */
class StyleFromAudioView::Results final : public juce::Component
{
public:
    Results (StyleFromAudioView& v) : view (v)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &v.section, &v.original, &v.styleButton, &v.both, &v.quiet, &v.saveButton, &v.openButton })
            addAndMakeVisible (*c);
    }

    std::shared_ptr<const style_from_audio::Made> made;
    juce::File saved;

    juce::String summary() const
    {
        if (made == nullptr)
            return {};
        const auto& m = *made;
        juce::String s;
        s << "Was " << percent (m.first.total) << " before refining. " << m.tried << " versions compared, " << (int) m.steps.size() << " changes kept. "
          << juce::String (m.grid.bpm, 1) << " BPM, " << m.plan.bars << " bars, bar 1 at " << juce::String (m.grid.firstDownbeat, 2) << " s.";
        return s;
    }

    juce::String partsText() const
    {
        juce::String s;
        for (const auto& p : made->parts)
            s << (s.isEmpty() ? "" : "   ") << kStylePartNames[std::clamp (p.channel, 9, 16) - 9] << ": " << utf8 (p.stem)
              << (p.instrument.name.empty() || p.drums ? juce::String() : " (" + utf8 (p.instrument.name) + ")");
        return s;
    }

    juce::String stepsText() const
    {
        juce::String s;
        const auto& steps = made->steps;
        const std::size_t shown = std::min<std::size_t> (steps.size(), 14);
        for (std::size_t i = 0; i < shown; ++i)
            s << "• " << utf8 (steps[i].change) << "  (" << percent (steps[i].before) << " → " << percent (steps[i].after) << ")\n";
        if (steps.size() > shown)
            s << "• ... and " << (int) (steps.size() - shown) << " more\n";
        if (steps.empty())
            s << "• No change made it better: the first version was the best.\n";
        for (const auto& w : made->warnings)
            s << "\n" << utf8 (w);
        if (saved != juce::File())
            s << "\nSaved: " << saved.getFullPathName();
        return s.trimEnd();
    }

    std::vector<const style_from_audio::SectionScore*> rows() const
    {
        std::vector<const style_from_audio::SectionScore*> out;
        if (made != nullptr)
            for (const auto& s : made->best.sections)
                if (s.bars > 0)
                    out.push_back (&s);
        return out;
    }

    int heightFor (int width) const
    {
        if (made == nullptr)
            return 0;
        const auto& t = currentTheme();
        float h = 34 + textHeight (summary(), t.font (12.0f), width) + 6 + textHeight (partsText(), t.font (11.5f), width) + 10;
        h += 28 + 10; // A/B
        h += 20 + static_cast<float> (rows().size() * kRowHeight) + 8;
        h += 20 + textHeight (stepsText(), t.font (11.5f), width) + 12;
        h += 28 + 8;
        return static_cast<int> (h);
    }

    void paint (juce::Graphics& g) override
    {
        if (made == nullptr)
            return;
        const auto& t = currentTheme();
        const int w = getWidth();
        float y = 0.0f;

        g.setColour (t.colour ("text"));
        g.setFont (t.font (22.0f, true));
        g.drawText ("Match " + percent (made->best.total), 0, 0, w, 30, juce::Justification::centredLeft);
        y = 34.0f;
        const auto block = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour) {
            const float h = textHeight (text, font, w);
            drawText (g, text, font, colour, { 0.0f, y, static_cast<float> (w), h });
            y += h;
        };
        block (summary(), t.font (12.0f), t.colour ("textMuted"));
        y += 6;
        block (partsText(), t.font (11.5f), t.colour ("textDim"));
        y += 10 + 28 + 10;

        g.setColour (t.colour ("text"));
        g.setFont (t.font (12.5f, true));
        g.drawText ("Sections", 0, static_cast<int> (y), w, 18, juce::Justification::centredLeft);
        y += 20;
        for (const auto* s : rows())
        {
            const auto name = utf8 (s->section);
            g.setColour (t.colour ("text"));
            g.setFont (t.font (12.5f, true));
            g.drawText (name, 0, static_cast<int> (y), 92, 18, juce::Justification::centredLeft);
            g.setColour (t.colour ("textDim"));
            g.setFont (t.font (11.0f));
            g.drawText ("bars " + utf8 (style_from_audio::barsOf (made->plan, s->section)), 92, static_cast<int> (y), w - 92 - 46, 18, juce::Justification::centredLeft, true);
            g.setColour (t.colour ("text"));
            g.setFont (t.font (12.5f, true));
            g.drawText (percent (s->total), w - 44, static_cast<int> (y), 44, 18, juce::Justification::centredRight);
            meter (g, { 0.0f, y + 20.0f, static_cast<float> (w), 4.0f }, s->total);
            juce::String parts;
            for (const auto& p : s->parts)
                parts << (parts.isEmpty() ? "" : "  ·  ") << utf8 (p.name) << " " << percent (p.total);
            g.setColour (t.colour ("textMuted"));
            g.setFont (t.font (11.0f));
            g.drawText (parts, 0, static_cast<int> (y + 25.0f), w, 14, juce::Justification::centredLeft, true);
            y += kRowHeight;
        }
        y += 8;
        g.setColour (t.colour ("text"));
        g.setFont (t.font (12.5f, true));
        g.drawText ("What the refine loop changed", 0, static_cast<int> (y), w, 18, juce::Justification::centredLeft);
        y += 20;
        block (stepsText(), t.font (11.5f), t.colour ("textMuted"));
    }

    void resized() override
    {
        if (made == nullptr)
            return;
        const auto& t = currentTheme();
        const int w = getWidth();
        int y = 34 + static_cast<int> (textHeight (summary(), t.font (12.0f), w)) + 6 + static_cast<int> (textHeight (partsText(), t.font (11.5f), w)) + 10;
        juce::Rectangle<int> ab (0, y, w, 28);
        for (auto* b : { &view.original, &view.styleButton, &view.both })
        {
            b->setBounds (ab.removeFromLeft (68));
            ab.removeFromLeft (4);
        }
        view.quiet.setBounds (ab.removeFromLeft (52));
        ab.removeFromLeft (8);
        view.section.setBounds (ab);
        y += 28 + 10 + 20 + static_cast<int> (rows().size()) * kRowHeight + 8 + 20 + static_cast<int> (textHeight (stepsText(), t.font (11.5f), w)) + 12;
        juce::Rectangle<int> save (0, y, w, 28);
        view.saveButton.setBounds (save.removeFromLeft (110));
        save.removeFromLeft (8);
        view.openButton.setBounds (save.removeFromLeft (120));
    }

private:
    StyleFromAudioView& view;
};

StyleFromAudioView::StyleFromAudioView (Session& s) : session (s), service (s.styleFromAudio())
{
    const auto& theme = currentTheme();
    for (auto* l : { &title, &about, &clipLabel, &tempoLabel, &lengthLabel, &status })
    {
        l->setColour (juce::Label::textColourId, theme.colour (l == &title ? "text" : "textMuted"));
        addAndMakeVisible (*l);
    }
    title.setText ("Style from Audio", juce::dontSendNotification);
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    about.setJustificationType (juce::Justification::topLeft);
    about.setMinimumHorizontalScale (1.0f);
    about.setText ("Make a PSR-S970 style from a recording. It plays the style with the song's chords, compares it with the recording "
                   "and refines it until it stops getting closer. Notes and rhythm, not the sound.",
                   juce::dontSendNotification);
    status.setJustificationType (juce::Justification::topLeft);

    clipLabel.setText ("Recording", juce::dontSendNotification);
    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    lengthLabel.setText ("Main", juce::dontSendNotification);
    tempo.setInputRestrictions (6, "0123456789.");
    tempo.setJustification (juce::Justification::centred);
    tempo.setTextToShowWhenEmpty ("find", theme.colour ("textDim"));
    tempo.setTooltip ("The tempo of the recording in BPM. Empty: found from the drums.");
    length.addItem ("Auto length", 1);
    length.addItem ("1 bar", 2);
    length.addItem ("2 bars", 3);
    length.addItem ("4 bars", 4);
    length.setSelectedId (1, juce::dontSendNotification);
    length.setTooltip ("How long the Main sections are. Auto: the refine loop chooses.");
    vocals.setTooltip ("Put the sung melody on the Phrase 1 part too");
    for (auto* c : std::initializer_list<juce::Component*> { &clip, &tempo, &length, &vocals, &makeButton, &stopButton })
        addAndMakeVisible (*c);

    makeButton.setTooltip ("Analyse the recording (when not done yet) and make the style");
    makeButton.onClick = [this] { start(); };
    stopButton.setTooltip ("Stop. The best version so far is kept.");
    stopButton.onClick = [this] { service.cancel(); };

    original.setTooltip ("Play the recording, looping the chosen bars");
    styleButton.setTooltip ("Play the style with the song's chords");
    both.setTooltip ("Play both together, to hear where they differ");
    quiet.setTooltip ("Stop playing; the song plays as it is again");
    original.onClick = [this] { play (style_from_audio::Listen::original); };
    styleButton.onClick = [this] { play (style_from_audio::Listen::style); };
    both.onClick = [this] { play (style_from_audio::Listen::both); };
    quiet.onClick = [this] {
        service.stopPlaying();
        playing = -1;
        update();
    };
    section.setTooltip ("The bars to loop");
    section.onChange = [this] {
        if (playing >= 0)
            play (static_cast<style_from_audio::Listen> (playing));
    };
    saveButton.setTooltip ("Save the style file (.sty) to copy to the keyboard");
    saveButton.onClick = [this] { save(); };
    openButton.setTooltip ("Open the saved style in the Loops view to edit it (in place of this song)");
    openButton.onClick = [this] {
        const auto file = service.status().saved;
        if (file.existsAsFile() && onOpenStyle)
            onOpenStyle (file);
    };

    results = std::make_unique<Results> (*this);
    viewport.setViewedComponent (results.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    fillClips();
    service.onChange = [this] { update(); };
    update();
}

StyleFromAudioView::~StyleFromAudioView()
{
    service.onChange = nullptr;
    if (playing >= 0)
        service.stopPlaying();
}

void StyleFromAudioView::fillClips()
{
    clip.clear (juce::dontSendNotification);
    clipIds.clear();
    // Stems that an analysis made are audio clips too: the recordings come first.
    for (const auto& t : session.song().tracks)
        for (const auto& c : t.audio)
        {
            clipIds.push_back (c->id);
            clip.addItem (utf8 (t.name) + " - " + utf8 (c->name.empty() ? std::filesystem::path (c->file).filename().string() : c->name), static_cast<int> (clipIds.size()));
        }
    if (! clipIds.empty())
        clip.setSelectedId (1, juce::dontSendNotification);
    clip.setTextWhenNoChoicesAvailable ("No audio clip: import a song first");
}

void StyleFromAudioView::chooseClip (ClipId id)
{
    for (std::size_t i = 0; i < clipIds.size(); ++i)
        if (clipIds[i] == id)
            clip.setSelectedId (static_cast<int> (i) + 1, juce::dontSendNotification);
}

void StyleFromAudioView::start()
{
    const int index = clip.getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int> (clipIds.size()))
    {
        status.setText ("Import a recording first (File > Import Audio).", juce::dontSendNotification);
        resized();
        return;
    }
    if (playing >= 0)
    {
        service.stopPlaying();
        playing = -1;
    }
    StyleFromAudioService::Request r;
    r.clip = clipIds[static_cast<std::size_t> (index)];
    r.bpm = tempo.getText().getDoubleValue();
    r.mainBars = std::array<int, 4> { 0, 1, 2, 4 }[static_cast<std::size_t> (std::clamp (length.getSelectedId() - 1, 0, 3))];
    r.vocals = vocals.getToggleState();
    const auto why = service.start (r);
    if (why.isNotEmpty())
        status.setText (why, juce::dontSendNotification);
    update();
}

void StyleFromAudioView::update()
{
    const auto st = service.status();
    using State = StyleFromAudioService::Status::State;
    const bool running = st.state == State::analysing || st.state == State::making;
    makeButton.setEnabled (! running);
    stopButton.setEnabled (running);
    progress = running ? st.progress : 0.0;
    if (running)
        status.setText (st.step, juce::dontSendNotification);
    else if (st.state == State::failed)
        status.setText (st.error, juce::dontSendNotification);
    else if (st.state == State::done)
        status.setText ({}, juce::dontSendNotification);

    if (st.made != shown)
    {
        shown = st.made;
        results->made = shown;
        section.clear (juce::dontSendNotification);
        if (shown != nullptr)
        {
            section.addItem ("Whole song", 1);
            int id = 2;
            for (const auto& name : shown->plan.sections)
            {
                const auto bars = style_from_audio::barsOf (shown->plan, name);
                if (! bars.empty())
                    section.addItem (utf8 (name) + "  (bars " + utf8 (bars) + ")", id);
                ++id;
            }
            section.setSelectedId (1, juce::dontSendNotification);
        }
    }
    results->saved = st.saved;
    openButton.setEnabled (st.saved.existsAsFile());
    for (auto* b : { &original, &styleButton, &both })
        b->setColour (juce::TextButton::buttonColourId, currentTheme().colour (playing == (b == &original ? 0 : b == &styleButton ? 1 : 2) ? "accent" : "panelRaised"));
    resized();
    repaint();
}

void StyleFromAudioView::play (style_from_audio::Listen listen)
{
    if (shown == nullptr)
        return;
    int from = 0, to = 0;
    const int id = section.getSelectedId();
    if (id >= 2)
    {
        const auto& name = shown->plan.sections[static_cast<std::size_t> (id - 2)];
        const auto& at = shown->plan.barSection;
        const auto first = std::find (at.begin(), at.end(), name);
        if (first != at.end())
        {
            from = static_cast<int> (first - at.begin());
            to = from;
            while (to < static_cast<int> (at.size()) && at[static_cast<std::size_t> (to)] == name)
                ++to;
            if (to - from < 4)
                to = std::min (static_cast<int> (at.size()), from + 4); // a fill: hear it go on into the next section
        }
    }
    const auto error = service.play (listen, from, to);
    if (error.isNotEmpty())
        status.setText (error, juce::dontSendNotification);
    playing = error.isEmpty() ? static_cast<int> (listen) : -1;
    update();
}

void StyleFromAudioView::save()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the style", service.defaultFile(), "*.sty");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto error = service.save (file);
                              status.setText (error, juce::dontSendNotification);
                              update();
                          });
}

void StyleFromAudioView::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));
    if (progress > 0.0 && ! progressArea.isEmpty())
        meter (g, progressArea.toFloat(), progress);
}

void StyleFromAudioView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    title.setBounds (r.removeFromTop (24));
    const int aboutHeight = static_cast<int> (textHeight (about.getText(), about.getFont(), r.getWidth() - 8)) + 12;
    about.setBounds (r.removeFromTop (aboutHeight));
    r.removeFromTop (6);
    auto line = r.removeFromTop (26);
    clipLabel.setBounds (line.removeFromLeft (72));
    clip.setBounds (line);
    r.removeFromTop (6);
    line = r.removeFromTop (26);
    tempoLabel.setBounds (line.removeFromLeft (72));
    tempo.setBounds (line.removeFromLeft (60));
    line.removeFromLeft (10);
    lengthLabel.setBounds (line.removeFromLeft (40));
    length.setBounds (line.removeFromLeft (120));
    r.removeFromTop (6);
    vocals.setBounds (r.removeFromTop (24).withTrimmedLeft (68));
    r.removeFromTop (6);
    auto buttons = r.removeFromTop (28);
    makeButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    stopButton.setBounds (buttons.removeFromLeft (70));
    r.removeFromTop (8);
    progressArea = progress > 0.0 ? r.removeFromTop (6) : juce::Rectangle<int>();
    if (progress > 0.0)
        r.removeFromTop (4);
    const auto statusText = status.getText();
    status.setBounds (r.removeFromTop (statusText.isEmpty() ? 0 : static_cast<int> (textHeight (statusText, status.getFont(), r.getWidth())) + 8));
    r.removeFromTop (6);
    viewport.setBounds (r);
    const int width = std::max (200, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
    results->setSize (width, std::max (10, results->heightFor (width)));
    results->resized();
}

} // namespace arranger::ui
