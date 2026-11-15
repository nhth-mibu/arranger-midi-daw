#pragma once

#include <juce_graphics/juce_graphics.h>

namespace arranger::ui::icons
{

enum class Icon { play, stop, record, loop, metronome, settings, sparkle, instrument, waveform, guitar, electricPiano, synth, brass, bass, drums };

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
        case Icon::guitar:
        {
            juce::Path body;
            body.addEllipse (juce::Rectangle<float> (s * 0.34f, s * 0.34f).withCentre ({ c.x - s * 0.15f, c.y + s * 0.16f }));
            body.addEllipse (juce::Rectangle<float> (s * 0.26f, s * 0.26f).withCentre ({ c.x + s * 0.03f, c.y + s * 0.04f }));
            g.strokePath (body, juce::PathStrokeType (s * 0.07f));
            g.drawLine (c.x + s * 0.02f, c.y + s * 0.02f, c.x + s * 0.34f, c.y - s * 0.34f, s * 0.07f);
            g.fillEllipse (juce::Rectangle<float> (s * 0.14f, s * 0.14f).withCentre ({ c.x - s * 0.15f, c.y + s * 0.16f }));
            break;
        }
        case Icon::bass:
        {
            juce::Path body;
            body.addEllipse (juce::Rectangle<float> (s * 0.28f, s * 0.30f).withCentre ({ c.x - s * 0.18f, c.y + s * 0.18f }));
            g.strokePath (body, juce::PathStrokeType (s * 0.07f));
            g.drawLine (c.x - s * 0.04f, c.y + s * 0.08f, c.x + s * 0.36f, c.y - s * 0.36f, s * 0.07f);
            g.fillRect (juce::Rectangle<float> (s * 0.07f, s * 0.20f).withCentre ({ c.x + s * 0.34f, c.y - s * 0.34f }));
            break;
        }
        case Icon::electricPiano:
        {
            const auto keys = juce::Rectangle<float> (s * 0.64f, s * 0.28f).withCentre ({ c.x, c.y - s * 0.02f });
            g.drawRoundedRectangle (keys, s * 0.04f, s * 0.06f);
            for (int i = 1; i < 5; ++i)
                g.drawLine (keys.getX() + keys.getWidth() * static_cast<float> (i) / 5.0f, keys.getY(),
                            keys.getX() + keys.getWidth() * static_cast<float> (i) / 5.0f, keys.getBottom(), s * 0.045f);
            g.drawLine (keys.getX() + s * 0.08f, keys.getBottom(), keys.getX() + s * 0.04f, c.y + s * 0.38f, s * 0.07f);
            g.drawLine (keys.getRight() - s * 0.08f, keys.getBottom(), keys.getRight() - s * 0.04f, c.y + s * 0.38f, s * 0.07f);
            break;
        }
        case Icon::synth:
        {
            const auto panel = juce::Rectangle<float> (s * 0.62f, s * 0.44f).withCentre (c);
            g.drawRoundedRectangle (panel, s * 0.04f, s * 0.06f);
            for (int i = 0; i < 3; ++i)
                g.drawEllipse (juce::Rectangle<float> (s * 0.09f, s * 0.09f).withCentre ({ panel.getX() + s * 0.14f + static_cast<float> (i) * s * 0.17f, panel.getY() + s * 0.12f }), s * 0.035f);
            g.drawLine (panel.getX() + s * 0.06f, panel.getBottom() - s * 0.09f, panel.getRight() - s * 0.06f, panel.getBottom() - s * 0.09f, s * 0.05f);
            break;
        }
        case Icon::brass:
        {
            juce::Path p;
            p.startNewSubPath (c.x - s * 0.38f, c.y - s * 0.02f);
            p.lineTo (c.x - s * 0.08f, c.y + s * 0.02f);
            p.lineTo (c.x + s * 0.10f, c.y + s * 0.18f);
            p.lineTo (c.x + s * 0.34f, c.y + s * 0.26f);
            p.lineTo (c.x + s * 0.34f, c.y - s * 0.20f);
            p.lineTo (c.x + s * 0.10f, c.y - s * 0.12f);
            p.lineTo (c.x - s * 0.08f, c.y + s * 0.02f);
            g.strokePath (p, juce::PathStrokeType (s * 0.07f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (c.x - s * 0.02f, c.y - s * 0.10f, c.x - s * 0.02f, c.y + s * 0.12f, s * 0.06f);
            break;
        }
        case Icon::drums:
        {
            const auto kick = juce::Rectangle<float> (s * 0.38f, s * 0.38f).withCentre ({ c.x - s * 0.12f, c.y + s * 0.14f });
            g.drawEllipse (kick, s * 0.055f);
            g.drawEllipse (kick.reduced (s * 0.09f), s * 0.04f);
            const auto snare = juce::Rectangle<float> (s * 0.24f, s * 0.18f).withCentre ({ c.x + s * 0.22f, c.y + s * 0.08f });
            g.drawRoundedRectangle (snare, s * 0.04f, s * 0.05f);
            g.drawLine (snare.getX(), snare.getBottom(), snare.getX(), c.y + s * 0.36f, s * 0.05f);
            g.drawLine (snare.getRight(), snare.getBottom(), snare.getRight(), c.y + s * 0.36f, s * 0.05f);
            g.drawLine (c.x - s * 0.30f, c.y - s * 0.34f, c.x + s * 0.12f, c.y - s * 0.34f, s * 0.055f);
            g.drawLine (c.x - s * 0.22f, c.y - s * 0.34f, c.x - s * 0.02f, c.y + s * 0.06f, s * 0.05f);
            break;
        }
        case Icon::instrument:
        {
            auto keys = juce::Rectangle<float> (s * 0.62f, s * 0.46f).withCentre (c);
            g.drawRoundedRectangle (keys, s * 0.05f, s * 0.07f);
            for (int i = 1; i < 4; ++i)
                g.drawLine (keys.getX() + keys.getWidth() * static_cast<float> (i) / 4.0f, keys.getY(),
                            keys.getX() + keys.getWidth() * static_cast<float> (i) / 4.0f, keys.getBottom(), s * 0.05f);
            for (int i = 0; i < 3; ++i)
            {
                const float x = keys.getX() + keys.getWidth() * (0.17f + 0.25f * static_cast<float> (i));
                g.fillRect (juce::Rectangle<float> (x, keys.getY(), s * 0.07f, keys.getHeight() * 0.58f));
            }
            break;
        }
        case Icon::waveform:
        {
            const float heights[] = { 0.30f, 0.62f, 0.92f, 0.48f, 0.74f };
            const float bar = s * 0.075f, gap = s * 0.065f;
            const float total = 5.0f * bar + 4.0f * gap;
            for (int i = 0; i < 5; ++i)
            {
                const float h = s * heights[i];
                g.fillRoundedRectangle (juce::Rectangle<float> (c.x - total / 2.0f + static_cast<float> (i) * (bar + gap),
                                                                c.y - h / 2.0f, bar, h), bar / 2.0f);
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
