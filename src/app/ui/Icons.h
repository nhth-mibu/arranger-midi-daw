#pragma once

#include <juce_graphics/juce_graphics.h>

namespace arranger::ui::icons
{

enum class Icon { play, stop, record, loop, metronome, settings, sparkle };

/** Simple vector icons, drawn to fit `area`. */
inline void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour (colour);
    const auto c = area.getCentre();
    const float s = juce::jmin (area.getWidth(), area.getHeight());

    switch (icon)
    {
        case Icon::play:
        {
            juce::Path p;
            p.addTriangle (c.x - s * 0.28f, c.y - s * 0.34f, c.x - s * 0.28f, c.y + s * 0.34f, c.x + s * 0.36f, c.y);
            g.fillPath (p, {});
            break;
        }
        case Icon::stop:
            g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.56f, s * 0.56f).withCentre (c), s * 0.08f);
            break;
        case Icon::record:
            g.fillEllipse (juce::Rectangle<float> (s * 0.6f, s * 0.6f).withCentre (c));
            break;
        case Icon::loop:
        {
            juce::Path p;
            const float w = s * 0.62f, h = s * 0.42f;
            p.startNewSubPath (c.x - w / 2, c.y + h / 2);
            p.lineTo (c.x + w / 2 - h / 2, c.y + h / 2);
            p.addCentredArc (c.x + w / 2 - h / 2, c.y, h / 2, h / 2, 0.0f, juce::MathConstants<float>::pi, 0.0f);
            p.lineTo (c.x - w / 2 + 2, c.y - h / 2);
            g.strokePath (p, juce::PathStrokeType (s * 0.09f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.addTriangle (c.x - w / 2 - s * 0.02f, c.y - h / 2, c.x - w / 2 + s * 0.16f, c.y - h / 2 - s * 0.12f,
                              c.x - w / 2 + s * 0.16f, c.y - h / 2 + s * 0.12f);
            g.fillPath (head);
            break;
        }
        case Icon::metronome:
        {
            juce::Path body;
            body.startNewSubPath (c.x - s * 0.08f, c.y - s * 0.38f);
            body.lineTo (c.x + s * 0.08f, c.y - s * 0.38f);
            body.lineTo (c.x + s * 0.3f, c.y + s * 0.36f);
            body.lineTo (c.x - s * 0.3f, c.y + s * 0.36f);
            body.closeSubPath();
            g.strokePath (body, juce::PathStrokeType (s * 0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (c.x, c.y + s * 0.22f, c.x + s * 0.26f, c.y - s * 0.3f, s * 0.08f);
            break;
        }
        case Icon::settings:
        {
            g.drawEllipse (juce::Rectangle<float> (s * 0.34f, s * 0.34f).withCentre (c), s * 0.09f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = static_cast<float> (i) * juce::MathConstants<float>::twoPi / 8.0f;
                const auto p1 = c.getPointOnCircumference (s * 0.24f, a), p2 = c.getPointOnCircumference (s * 0.38f, a);
                g.drawLine ({ p1, p2 }, s * 0.11f);
            }
            break;
        }
        case Icon::sparkle:
        {
            // A big four-point star with a small one at its top right: the AI mark.
            auto star = [] (juce::Point<float> centre, float r) {
                juce::Path p;
                const float k = r * 0.22f;
                p.startNewSubPath (centre.x, centre.y - r);
                p.quadraticTo (centre.x + k, centre.y - k, centre.x + r, centre.y);
                p.quadraticTo (centre.x + k, centre.y + k, centre.x, centre.y + r);
                p.quadraticTo (centre.x - k, centre.y + k, centre.x - r, centre.y);
                p.quadraticTo (centre.x - k, centre.y - k, centre.x, centre.y - r);
                p.closeSubPath();
                return p;
            };
            g.fillPath (star (c.translated (-s * 0.08f, s * 0.08f), s * 0.4f));
            g.fillPath (star (c.translated (s * 0.3f, -s * 0.3f), s * 0.17f));
            break;
        }
    }
}

} // namespace arranger::ui::icons
