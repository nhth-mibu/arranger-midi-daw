#include "StyleRuleEditor.h"

#include "arranger/core/Style.h"
#include "arranger/core/StyleChord.h"

namespace arranger::ui
{

namespace
{
    constexpr const char* kRoots[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    constexpr const char* kNtr[] = { "Move with the chord root", "Stay near the written pitch", "Guitar" };
    constexpr const char* kNtt[] = { "As written (bypass)", "Melody", "Chord", "Melodic minor", "Melodic minor, 5th varies", "Harmonic minor", "Harmonic minor, 5th varies",
                                     "Natural minor", "Natural minor, 5th varies", "Dorian", "Dorian, 5th varies" };
    constexpr const char* kRtr[] = { "Stop", "Shift the pitch", "Shift the pitch to the root", "Play again", "Play again at the root", "Note generator" };

    juce::String noteName (int pitch) { return juce::String (kRoots[pitch % 12]) + juce::String (pitch / 12 - 2); }

    template <std::size_t N>
    void fill (juce::ComboBox& box, const char* const (&items)[N])
    {
        for (std::size_t i = 0; i < N; ++i)
            box.addItem (items[i], static_cast<int> (i) + 1);
    }
}

StyleRuleEditor::StyleRuleEditor (Session& s, TrackId t, int sc) : session (s), track (t), scene (sc)
{
    const auto& song = session.song();
    const auto* tr = edit::findTrack (song, track);
    heading = "Chord rules" + (tr != nullptr ? ": " + juce::String::fromUTF8 (tr->name.c_str()) : juce::String());

    const bool named = scene >= 0 && static_cast<std::size_t> (scene) < song.scenes.size() && ! song.scenes[static_cast<std::size_t> (scene)].empty();
    scopeBox.addItem ("All sections", 1);
    if (named)
        scopeBox.addItem ("Only " + juce::String::fromUTF8 (song.scenes[static_cast<std::size_t> (scene)].c_str()), 2);
    scopeBox.setSelectedId (1, juce::dontSendNotification);
    scopeBox.setTooltip ("The sections that get the changes you make here");
    // The scope only says where the next change goes. It does not write a rule.

    fill (rootBox, kRoots);
    for (int type = 0; type < kChordTypeCount - 1; ++type)
    {
        auto name = juce::String (style_chord::name ({ 0, static_cast<ChordType> (type), -1 })).substring (1);
        typeBox.addItem (name.isEmpty() ? juce::String ("Major") : name, type + 1);
    }
    fill (ntrBox, kNtr);
    fill (nttBox, kNtt);
    fill (highKeyBox, kRoots);
    for (int p = 0; p < 128; ++p)
    {
        lowLimitBox.addItem (noteName (p), p + 1);
        highLimitBox.addItem (noteName (p), p + 1);
    }
    fill (rtrBox, kRtr);

    rootBox.setTooltip ("The chord that the notes of this part are written in. Most styles use CMaj7.");
    typeBox.setTooltip (rootBox.getTooltip());
    ntrBox.setTooltip ("How the notes move when the chord root changes");
    nttBox.setTooltip ("How the notes change when the chord type changes. Use Chord for chord parts, Melody for bass and phrases, As written for drums.");
    highKeyBox.setTooltip ("Chord roots above this note play one octave lower");
    lowLimitBox.setTooltip ("Notes below this limit move up by octaves");
    highLimitBox.setTooltip ("Notes above this limit move down by octaves");
    rtrBox.setTooltip ("What a held note does when the chord changes. The keyboard uses this rule; the preview in the app starts the new chord at the next note.");

    for (auto* box : { &scopeBox, &rootBox, &typeBox, &ntrBox, &nttBox, &highKeyBox, &lowLimitBox, &highLimitBox, &rtrBox })
    {
        addAndMakeVisible (*box);
        if (box != &scopeBox)
            box->onChange = [this] { store(); };
    }
    addAndMakeVisible (bassToggle);
    bassToggle.onClick = [this] { store(); };

    setSize (kWidth, kHeight);
    load();
}

void StyleRuleEditor::load()
{
    const auto& song = session.song();
    const auto* t = edit::findTrack (song, track);
    if (t == nullptr)
        return;
    const auto rule = style_convert::ruleFor (song, *t, scene);
    const juce::ScopedValueSetter<bool> guard (loading, true);
    const auto dont = juce::dontSendNotification;
    rootBox.setSelectedId (std::clamp (rule.sourceRoot, 0, 11) + 1, dont);
    typeBox.setSelectedId (std::clamp (rule.sourceChordType, 0, kChordTypeCount - 2) + 1, dont);
    ntrBox.setSelectedId (static_cast<int> (rule.transpose.ntr) + 1, dont);
    nttBox.setSelectedId (static_cast<int> (rule.transpose.ntt) + 1, dont);
    bassToggle.setToggleState (rule.transpose.bass, dont);
    highKeyBox.setSelectedId (std::clamp (rule.transpose.highKey, 0, 11) + 1, dont);
    lowLimitBox.setSelectedId (std::clamp (rule.transpose.lowLimit, 0, 127) + 1, dont);
    highLimitBox.setSelectedId (std::clamp (rule.transpose.highLimit, 0, 127) + 1, dont);
    rtrBox.setSelectedId (static_cast<int> (rule.transpose.rtr) + 1, dont);
}

StyleChannelRule StyleRuleEditor::current() const
{
    const auto& song = session.song();
    const auto* t = edit::findTrack (song, track);
    auto rule = t != nullptr ? style_convert::ruleFor (song, *t, scene) : StyleChannelRule {};
    // A part with one note range keeps its low and high ranges the same as the middle.
    const bool oneRange = rule.lowRange == rule.transpose && rule.highRange == rule.transpose;
    rule.sourceRoot = rootBox.getSelectedId() - 1;
    rule.sourceChordType = typeBox.getSelectedId() - 1;
    rule.transpose.ntr = static_cast<StyleTranspose::Ntr> (ntrBox.getSelectedId() - 1);
    rule.transpose.ntt = static_cast<StyleTranspose::Ntt> (nttBox.getSelectedId() - 1);
    rule.transpose.bass = bassToggle.getToggleState();
    rule.transpose.highKey = highKeyBox.getSelectedId() - 1;
    rule.transpose.lowLimit = lowLimitBox.getSelectedId() - 1;
    rule.transpose.highLimit = highLimitBox.getSelectedId() - 1;
    rule.transpose.rtr = static_cast<StyleTranspose::Rtr> (rtrBox.getSelectedId() - 1);
    if (oneRange)
        rule.lowRange = rule.highRange = rule.transpose;
    return rule;
}

void StyleRuleEditor::store()
{
    if (loading)
        return;
    const auto rule = current();
    const int target = scopeBox.getSelectedId() == 2 ? scene : -1;
    const auto id = track;
    session.edit ("Change Chord Rules", [id, target, rule] (Song& song) {
        if (const auto* t = edit::findTrack (song, id))
        {
            const Track copy = *t;
            style_convert::setRule (song, copy, target, rule);
        }
    });
}

void StyleRuleEditor::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.colour ("panel"));
    g.setColour (t.colour ("text"));
    g.setFont (t.font (15.0f, true));
    g.drawText (heading, 16, 10, getWidth() - 32, 24, juce::Justification::centredLeft);
    g.setFont (t.font (13.0f));
    g.setColour (t.colour ("textMuted"));
    for (const auto& r : rows)
        g.drawText (r.label, r.area, juce::Justification::centredLeft);
}

void StyleRuleEditor::resized()
{
    rows.clear();
    auto area = getLocalBounds().reduced (16, 0);
    area.removeFromTop (44);
    const auto row = [&] (const juce::String& label) {
        auto r = area.removeFromTop (30);
        area.removeFromTop (8);
        rows.push_back ({ label, r.removeFromLeft (130) });
        return r;
    };
    scopeBox.setBounds (row ("Change"));
    auto chord = row ("Written in");
    rootBox.setBounds (chord.removeFromLeft (72));
    chord.removeFromLeft (8);
    typeBox.setBounds (chord);
    ntrBox.setBounds (row ("Chord root"));
    nttBox.setBounds (row ("Chord type"));
    bassToggle.setBounds (area.removeFromTop (30));
    area.removeFromTop (8);
    highKeyBox.setBounds (row ("Highest root").removeFromLeft (72));
    auto limits = row ("Note limits");
    lowLimitBox.setBounds (limits.removeFromLeft (100));
    limits.removeFromLeft (8);
    highLimitBox.setBounds (limits.removeFromLeft (100));
    rtrBox.setBounds (row ("Held notes"));
}

} // namespace arranger::ui
