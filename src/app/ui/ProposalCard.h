#pragma once

#include "Theme.h"

#include "arranger/core/Arranging.h"
#include "arranger/core/SongEdit.h"

#include <memory>
#include <vector>

/** The proposal cards of the AI panel: a proposal with options to play (A/B ...) and use. */
namespace arranger::ui
{

namespace card
{
    inline juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    inline constexpr int kPad = 10;

    /** Wrapped text: its height follows its width. */
    inline float textHeight (const juce::String& text, const juce::Font& font, int width)
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

    inline void drawText (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, juce::Rectangle<float> area)
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append (text, font, colour);
        juce::TextLayout layout;
        layout.createLayout (s, area.getWidth());
        layout.draw (g, area);
    }

}

using card::kPad;
using card::textHeight;
using card::drawText;

/** Wrapped text that sets its own height. */
class TextBlock final : public juce::Component
{
public:
    juce::String text;
    float size = 12.5f;

    int heightFor (int width) const { return static_cast<int> (textHeight (text, currentTheme().font (size), width)) + 4; }
    void paint (juce::Graphics& g) override
    {
        drawText (g, text, currentTheme().font (size), currentTheme().colour ("textMuted"), getLocalBounds().toFloat());
    }
};

/** One proposal: what was seen, and its options to play and use. */
class ProposalCard final : public juce::Component
{
public:
    ProposalCard (const Song& song, const arranging::Proposal& p) : proposal (p)
    {
        juce::StringArray names;
        for (auto id : p.tracks)
            if (const auto* t = edit::findTrack (song, id))
                names.add (card::utf8 (t->name));
        meta = card::utf8 (arranging::barsText (song, p.from, p.to)) + (names.isEmpty() ? juce::String() : "  ·  " + names.joinIntoString (", "));
        for (std::size_t i = 0; i < p.options.size(); ++i)
        {
            auto row = std::make_unique<Row>();
            row->label = card::utf8 (p.options[i].label);
            row->summary = card::utf8 (p.options[i].summary);
            row->play.setButtonText (juce::String ("Play ") + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)));
            row->play.setTooltip ("Play this version of the bars, looping. Switch between the original and the options while it plays.");
            row->use.setButtonText ("Use");
            row->use.setTooltip ("Make this change (one undo step)");
            const int option = static_cast<int> (i);
            row->play.onClick = [this, option] { if (onPlay) onPlay (option); };
            row->use.onClick = [this, option] { if (onUse) onUse (option); };
            addAndMakeVisible (row->play);
            addAndMakeVisible (row->use);
            rows.push_back (std::move (row));
        }
        for (auto* b : { &original, &dismiss })
            addAndMakeVisible (*b);
        original.setTooltip ("Play the bars as they are now");
        dismiss.setTooltip ("Hide this proposal. The app remembers it, so this kind comes later next time.");
        original.onClick = [this] { if (onPlay) onPlay (-1); };
        dismiss.onClick = [this] { if (onDismiss) onDismiss(); };
    }

    std::function<void (int)> onPlay, onUse;
    std::function<void()> onDismiss;

    void setPlaying (int option)
    {
        playing = option;
        repaint();
    }

    int heightFor (int width) const
    {
        const auto& t = currentTheme();
        const int w = width - kPad * 2;
        float h = kPad + textHeight (card::utf8 (proposal.title), t.font (13.5f, true), w) + 4 + textHeight (card::utf8 (proposal.because), t.font (12.0f), w) + 4 + 18;
        for (const auto& r : rows)
            h += 8 + textHeight (r->label, t.font (12.5f, true), w) + textHeight (r->summary, t.font (11.5f), w) + 4 + 26;
        h += 10 + 26 + kPad;
        return static_cast<int> (h);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (t.colour ("panelRaised"));
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (playing != -2 ? t.colour ("accent") : t.colour ("line"));
        g.drawRoundedRectangle (box, 8.0f, playing != -2 ? 1.6f : 1.0f);

        const int w = getWidth() - kPad * 2;
        float y = kPad;
        const auto block = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour) {
            const float h = textHeight (text, font, w);
            drawText (g, text, font, colour, { static_cast<float> (kPad), y, static_cast<float> (w), h });
            y += h;
        };
        block (card::utf8 (proposal.title), t.font (13.5f, true), t.colour ("text"));
        y += 4;
        block (card::utf8 (proposal.because), t.font (12.0f), t.colour ("textMuted"));
        y += 4;
        g.setColour (t.colour ("textDim"));
        g.setFont (t.font (11.0f));
        g.drawText (meta, kPad, static_cast<int> (y), w, 16, juce::Justification::centredLeft, true);
        y += 18;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            y += 8;
            const auto mark = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)) + "  ";
            block (mark + rows[i]->label, t.font (12.5f, true), static_cast<int> (i) == playing ? t.colour ("accent") : t.colour ("text"));
            block (rows[i]->summary, t.font (11.5f), t.colour ("textMuted"));
            y += 4 + 26;
        }
        if (playing == -1)
        {
            g.setColour (t.colour ("accent"));
            g.setFont (t.font (11.0f, true));
            g.drawText ("Playing the original", kPad, getHeight() - kPad - 26, w - 200, 26, juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        const auto& t = currentTheme();
        const int w = getWidth() - kPad * 2;
        float y = kPad + textHeight (card::utf8 (proposal.title), t.font (13.5f, true), w) + 4 + textHeight (card::utf8 (proposal.because), t.font (12.0f), w) + 4 + 18;
        for (auto& r : rows)
        {
            y += 8 + textHeight (r->label, t.font (12.5f, true), w) + textHeight (r->summary, t.font (11.5f), w) + 4;
            juce::Rectangle<int> line (kPad, static_cast<int> (y), w, 24);
            r->play.setBounds (line.removeFromLeft (72));
            line.removeFromLeft (6);
            r->use.setBounds (line.removeFromLeft (64));
            y += 26;
        }
        juce::Rectangle<int> bottom (kPad, getHeight() - kPad - 24, w, 24);
        original.setBounds (bottom.removeFromLeft (90));
        bottom.removeFromLeft (6);
        dismiss.setBounds (bottom.removeFromLeft (76));
    }

private:
    struct Row
    {
        juce::String label, summary;
        juce::TextButton play, use;
    };
    arranging::Proposal proposal;
    juce::String meta;
    std::vector<std::unique_ptr<Row>> rows;
    juce::TextButton original { "Original" }, dismiss { "Dismiss" };
    int playing = -2;
};

} // namespace arranger::ui
