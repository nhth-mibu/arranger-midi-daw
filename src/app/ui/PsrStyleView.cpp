#include "PsrStyleView.h"
#include "arranger/core/PsrStyle.h"

namespace arranger::ui
{

namespace
{
    constexpr const char* kProfileId = "yamaha-psr-s970";
    const char* const kRoots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::Label& heading (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, currentTheme().colour ("text"));
        return label;
    }
}

PsrStyleView::PsrStyleView (Session& s) : session (s)
{
    addAndMakeVisible (title);
    title.setText ("Yamaha PSR-S970 Style Remote", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, currentTheme().colour ("text"));

    addAndMakeVisible (status);
    status.setFont (juce::FontOptions (12.0f));
    status.setColour (juce::Label::textColourId, currentTheme().colour ("textDim"));

    addAndMakeVisible (heading (sectionLabel, "Sections"));
    for (const auto& sct : psr_style::sections())
    {
        auto* b = sectionButtons.add (new juce::TextButton (sct.name));
        b->onClick = [this, code = sct.code] { sendSection (code); };
        addAndMakeVisible (b);
    }

    addAndMakeVisible (heading (tempoLabel, "Tempo"));
    tempo.setSliderStyle (juce::Slider::LinearBar);
    tempo.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 90, 20);
    tempo.setRange (5.0, 500.0, 1.0);
    tempo.setValue (120.0, juce::dontSendNotification);
    tempo.setTextValueSuffix (" BPM");
    addAndMakeVisible (tempo);
    sendTempoButton.onClick = [this] { sendTempo(); };
    addAndMakeVisible (sendTempoButton);

    addAndMakeVisible (heading (chordLabel, "Chord"));
    for (int i = 0; i < 12; ++i)
    {
        root.addItem (kRoots[i], i + 1);
        bass.addItem (kRoots[i], i + 1);
    }
    root.setSelectedId (1, juce::dontSendNotification);
    bass.addItem ("No bass", 13);
    bass.setSelectedId (13, juce::dontSendNotification);
    for (const auto& type : psr_style::chordTypes())
        chordType.addItem (type.name, type.code + 1);
    chordType.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (root);
    addAndMakeVisible (chordType);
    addAndMakeVisible (bass);
    sendChordButton.onClick = [this] { sendChord(); };
    addAndMakeVisible (sendChordButton);

    addAndMakeVisible (heading (styleLabel, "Style number (unverified)"));
    styleNumber.setSliderStyle (juce::Slider::LinearBar);
    styleNumber.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 80, 20);
    styleNumber.setRange (0.0, 16383.0, 1.0);
    addAndMakeVisible (styleNumber);
    sendStyleButton.onClick = [this] {
        if (styleNumber.getValue() >= 0)
            send (psr_style::styleNumber (static_cast<int> (styleNumber.getValue())));
    };
    addAndMakeVisible (sendStyleButton);

    addAndMakeVisible (heading (splitLabel, "Split point"));
    splitPoint.setSliderStyle (juce::Slider::LinearBar);
    splitPoint.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 80, 20);
    splitPoint.setRange (0.0, 127.0, 1.0);
    splitPoint.setValue (60.0, juce::dontSendNotification);
    addAndMakeVisible (splitPoint);
    sendSplitButton.onClick = [this] { send (psr_style::splitPoint (static_cast<int> (splitPoint.getValue()))); };
    addAndMakeVisible (sendSplitButton);

    updateStatus();
    setSize (760, 500);
    startTimer (500);
}

PsrStyleView::~PsrStyleView() { stopTimer(); }

const DeviceProfile* PsrStyleView::profile() const
{
    return session.deviceLibrary().byId (kProfileId);
}

void PsrStyleView::send (const MidiBytes& message)
{
    const auto* p = profile();
    if (p == nullptr)
        return;
    const auto port = session.outputFor (*p);
    if (port.empty())
    {
        status.setText ("Not plugged in. Choose the PSR-S970 MIDI output in Audio and MIDI Settings.", juce::dontSendNotification);
        return;
    }
    session.deviceHost().sendNow (port, message);
}

void PsrStyleView::sendSection (int code) { send (psr_style::section (code)); }
void PsrStyleView::sendTempo() { send (psr_style::tempo (tempo.getValue())); }

void PsrStyleView::sendChord()
{
    const int rootPc = std::max (0, root.getSelectedId() - 1);
    const int type = std::max (0, chordType.getSelectedId() - 1);
    const int bassId = bass.getSelectedId();
    send (psr_style::chord (rootPc, type, bassId >= 1 && bassId <= 12 ? bassId - 1 : -1));
}

void PsrStyleView::updateStatus()
{
    const auto* p = profile();
    const auto port = p != nullptr ? session.outputFor (*p) : std::string();
    status.setText (port.empty() ? "Not plugged in. Shows the last style controls sent."
                                 : "Plugged in: " + juce::String (port) + ". Style Section and Chord receive must be on in the keyboard's MIDI settings.",
                    juce::dontSendNotification);
}

void PsrStyleView::timerCallback() { updateStatus(); }

void PsrStyleView::paint (juce::Graphics& g) { g.fillAll (currentTheme().colour ("background")); }

void PsrStyleView::resized()
{
    auto r = getLocalBounds().reduced (16);
    title.setBounds (r.removeFromTop (30));
    status.setBounds (r.removeFromTop (20));
    r.removeFromTop (10);

    auto sections = r.removeFromTop (124);
    sectionLabel.setBounds (sections.removeFromTop (20));
    const int columns = 4;
    const int w = std::max (1, sections.getWidth() / columns);
    for (int i = 0; i < sectionButtons.size(); ++i)
        sectionButtons[i]->setBounds ((i % columns) * w + 2, (i / columns) * 30, w - 4, 26);
    r.removeFromTop (10);

    auto tempoRow = r.removeFromTop (28);
    tempoLabel.setBounds (tempoRow.removeFromLeft (90));
    sendTempoButton.setBounds (tempoRow.removeFromRight (110));
    tempoRow.removeFromRight (6);
    tempo.setBounds (tempoRow);
    r.removeFromTop (12);

    auto chordRow = r.removeFromTop (28);
    chordLabel.setBounds (chordRow.removeFromLeft (60));
    root.setBounds (chordRow.removeFromLeft (70));
    chordRow.removeFromLeft (6);
    chordType.setBounds (chordRow.removeFromLeft (180));
    chordRow.removeFromLeft (6);
    bass.setBounds (chordRow.removeFromLeft (110));
    chordRow.removeFromLeft (6);
    sendChordButton.setBounds (chordRow.removeFromRight (110));
    r.removeFromTop (12);

    auto styleRow = r.removeFromTop (28);
    styleLabel.setBounds (styleRow.removeFromLeft (190));
    sendStyleButton.setBounds (styleRow.removeFromRight (130));
    styleRow.removeFromRight (6);
    styleNumber.setBounds (styleRow);
    r.removeFromTop (8);

    auto splitRow = r.removeFromTop (28);
    splitLabel.setBounds (splitRow.removeFromLeft (100));
    sendSplitButton.setBounds (splitRow.removeFromRight (110));
    splitRow.removeFromRight (6);
    splitPoint.setBounds (splitRow);
}

} // namespace arranger::ui
