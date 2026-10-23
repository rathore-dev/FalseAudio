#include "Icons.h"

namespace fa::ui::icons
{
namespace
{
    constexpr float sw = 1.75f;

    juce::Path stroked (const juce::Path& p, float width = sw, bool round = true)
    {
        juce::Path out;
        juce::PathStrokeType st (width,
                                 round ? juce::PathStrokeType::curved : juce::PathStrokeType::mitered,
                                 round ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt);
        st.createStrokedPath (out, p);
        return out;
    }

    juce::Path line (float x1, float y1, float x2, float y2)
    {
        juce::Path p;
        p.startNewSubPath (x1, y1);
        p.lineTo (x2, y2);
        return p;
    }

    juce::Path circle (float cx, float cy, float r)
    {
        juce::Path p;
        p.addEllipse (cx - r, cy - r, r * 2, r * 2);
        return p;
    }

    juce::Path roundRect (float x, float y, float w, float h, float r)
    {
        juce::Path p;
        p.addRoundedRectangle (x, y, w, h, r);
        return p;
    }

    juce::Path poly (std::initializer_list<juce::Point<float>> pts, bool close)
    {
        juce::Path p;
        bool first = true;
        for (auto pt : pts)
        {
            if (first) { p.startNewSubPath (pt); first = false; }
            else        p.lineTo (pt);
        }
        if (close)
            p.closeSubPath();
        return p;
    }

    juce::Path build (Id id)
    {
        juce::Path s;   // stroked parts
        juce::Path f;   // filled parts

        switch (id)
        {
            case Id::bolt:
                f = poly ({ {13, 2}, {4.5f, 13.5f}, {11, 13.5f}, {10, 22}, {19.5f, 10.5f}, {13, 10.5f}, {14, 2} }, true);
                break;

            case Id::car:
            {
                // top-down car body
                juce::Path body;
                body.addRoundedRectangle (6.0f, 2.5f, 12.0f, 19.0f, 4.5f);
                s.addPath (body);
                s.addPath (line (8, 8.5f, 16, 8.5f));
                s.addPath (line (8, 16.5f, 16, 16.5f));
                s.addPath (poly ({ {8.5f, 8.5f}, {9.5f, 11.5f}, {14.5f, 11.5f}, {15.5f, 8.5f} }, false));
                f.addRectangle (3.5f, 6.0f, 2.0f, 4.0f);
                f.addRectangle (18.5f, 6.0f, 2.0f, 4.0f);
                f.addRectangle (3.5f, 14.0f, 2.0f, 4.0f);
                f.addRectangle (18.5f, 14.0f, 2.0f, 4.0f);
                break;
            }

            case Id::plug:
                s.addPath (line (9, 2.5f, 9, 7));
                s.addPath (line (15, 2.5f, 15, 7));
                s.addPath (poly ({ {6, 7}, {18, 7}, {18, 11}, {16, 15}, {8, 15}, {6, 11} }, true));
                s.addPath (line (12, 15, 12, 21.5f));
                break;

            case Id::waveform:
                s.addPath (line (2.5f, 12, 5.5f, 12));
                s.addPath (poly ({ {5.5f, 12}, {8, 5}, {11, 19}, {14, 8}, {16.5f, 15}, {18.5f, 12} }, false));
                s.addPath (line (18.5f, 12, 21.5f, 12));
                break;

            case Id::activity:
            {
                juce::Path p;
                p.startNewSubPath (2.5f, 13.0f);
                p.lineTo (6.0f, 13.0f);
                p.lineTo (9.0f, 4.0f);
                p.lineTo (14.0f, 20.0f);
                p.lineTo (17.0f, 13.0f);
                p.lineTo (21.5f, 13.0f);
                s.addPath (p);
                break;
            }

            case Id::clock:
                s.addPath (circle (12, 12, 9));
                s.addPath (poly ({ {12, 7}, {12, 12.5f}, {15.5f, 14.5f} }, false));
                break;

            case Id::filter:
            {
                // crossover curves
                juce::Path a;
                a.startNewSubPath (2.5f, 18.0f);
                a.cubicTo (7.0f, 18.0f, 8.0f, 6.0f, 13.0f, 6.0f);
                a.cubicTo (16.0f, 6.0f, 18.0f, 6.0f, 21.5f, 6.0f);
                juce::Path b;
                b.startNewSubPath (2.5f, 6.0f);
                b.cubicTo (6.0f, 6.0f, 8.0f, 6.0f, 11.0f, 6.0f);
                b.cubicTo (16.0f, 6.0f, 17.0f, 18.0f, 21.5f, 18.0f);
                s.addPath (a);
                s.addPath (b);
                break;
            }

            case Id::chip:
                s.addPath (roundRect (6, 6, 12, 12, 2));
                s.addPath (roundRect (9.5f, 9.5f, 5, 5, 1));
                for (float x : { 9.0f, 12.0f, 15.0f })
                {
                    s.addPath (line (x, 2.5f, x, 6));
                    s.addPath (line (x, 18, x, 21.5f));
                    s.addPath (line (2.5f, x, 6, x));
                    s.addPath (line (18, x, 21.5f, x));
                }
                break;

            case Id::document:
                s.addPath (poly ({ {14, 2.5f}, {5.5f, 2.5f}, {5.5f, 21.5f}, {18.5f, 21.5f}, {18.5f, 7} }, true));
                s.addPath (poly ({ {14, 2.5f}, {14, 7}, {18.5f, 7} }, false));
                s.addPath (line (8.5f, 12, 15.5f, 12));
                s.addPath (line (8.5f, 16, 13.5f, 16));
                break;

            case Id::play:
                f = poly ({ {7, 4}, {20, 12}, {7, 20} }, true);
                break;

            case Id::stop:
                f.addRoundedRectangle (6.0f, 6.0f, 12.0f, 12.0f, 2.0f);
                break;

            case Id::check:
                s.addPath (poly ({ {4.5f, 12.5f}, {9.5f, 17.5f}, {19.5f, 6.5f} }, false));
                break;

            case Id::warning:
                s.addPath (poly ({ {12, 3.5f}, {21.5f, 20}, {2.5f, 20} }, true));
                s.addPath (line (12, 9.5f, 12, 14));
                f.addEllipse (11.0f, 16.0f, 2.0f, 2.0f);
                break;

            case Id::cross:
                s.addPath (line (6, 6, 18, 18));
                s.addPath (line (18, 6, 6, 18));
                break;

            case Id::plus:
                s.addPath (line (12, 5, 12, 19));
                s.addPath (line (5, 12, 19, 12));
                break;

            case Id::trash:
                s.addPath (line (4, 6.5f, 20, 6.5f));
                s.addPath (poly ({ {9, 6.5f}, {9, 3.5f}, {15, 3.5f}, {15, 6.5f} }, false));
                s.addPath (poly ({ {6, 6.5f}, {7, 20.5f}, {17, 20.5f}, {18, 6.5f} }, false));
                s.addPath (line (10, 10.5f, 10.3f, 17));
                s.addPath (line (14, 10.5f, 13.7f, 17));
                break;

            case Id::eye:
            {
                juce::Path e;
                e.startNewSubPath (2.5f, 12.0f);
                e.cubicTo (6.0f, 5.5f, 18.0f, 5.5f, 21.5f, 12.0f);
                e.cubicTo (18.0f, 18.5f, 6.0f, 18.5f, 2.5f, 12.0f);
                s.addPath (e);
                s.addPath (circle (12, 12, 3));
                break;
            }

            case Id::eyeOff:
            {
                juce::Path e;
                e.startNewSubPath (2.5f, 12.0f);
                e.cubicTo (6.0f, 5.5f, 18.0f, 5.5f, 21.5f, 12.0f);
                e.cubicTo (18.0f, 18.5f, 6.0f, 18.5f, 2.5f, 12.0f);
                s.addPath (e);
                s.addPath (line (4, 20, 20, 4));
                break;
            }

            case Id::copy:
                s.addPath (roundRect (9, 9, 11, 11, 2));
                s.addPath (poly ({ {6, 15}, {4, 15}, {4, 4}, {15, 4}, {15, 6} }, false));
                break;

            case Id::folder:
                s.addPath (poly ({ {3, 6}, {3, 19}, {21, 19}, {21, 8.5f}, {12, 8.5f}, {10, 5.5f}, {3, 5.5f} }, true));
                break;

            case Id::download:
                s.addPath (line (12, 3.5f, 12, 15));
                s.addPath (poly ({ {7.5f, 11}, {12, 15.5f}, {16.5f, 11} }, false));
                s.addPath (poly ({ {4, 15.5f}, {4, 20}, {20, 20}, {20, 15.5f} }, false));
                break;

            case Id::refresh:
            {
                juce::Path arc;
                arc.addCentredArc (12, 12, 7.5f, 7.5f, 0, juce::MathConstants<float>::pi * 0.15f,
                                   juce::MathConstants<float>::pi * 1.75f, true);
                s.addPath (arc);
                f = poly ({ {15.5f, 2.5f}, {20.5f, 6.5f}, {15.0f, 9.0f} }, true);
                break;
            }

            case Id::settings:
            {
                juce::Path gear;
                for (int i = 0; i < 8; ++i)
                {
                    const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                    const float r1 = 9.0f, r2 = 6.5f;
                    const float d = juce::MathConstants<float>::twoPi / 16.0f * 0.5f;
                    auto pt = [] (float r, float ang) { return juce::Point<float> (12 + r * std::sin (ang), 12 - r * std::cos (ang)); };
                    if (i == 0) gear.startNewSubPath (pt (r2, a - d * 1.6f));
                    else        gear.lineTo (pt (r2, a - d * 1.6f));
                    gear.lineTo (pt (r1, a - d));
                    gear.lineTo (pt (r1, a + d));
                    gear.lineTo (pt (r2, a + d * 1.6f));
                }
                gear.closeSubPath();
                s.addPath (gear);
                s.addPath (circle (12, 12, 2.5f));
                break;
            }

            case Id::mic:
                s.addPath (roundRect (9, 3, 6, 11, 3));
                {
                    juce::Path arc;
                    arc.addCentredArc (12, 11.5f, 6, 6, 0, juce::MathConstants<float>::pi * 0.5f,
                                       juce::MathConstants<float>::pi * 1.5f, true);
                    s.addPath (arc);
                }
                s.addPath (line (12, 17.5f, 12, 21));
                s.addPath (line (8.5f, 21, 15.5f, 21));
                break;

            case Id::speaker:
                f = poly ({ {4, 9}, {8, 9}, {13, 4.5f}, {13, 19.5f}, {8, 15}, {4, 15} }, true);
                {
                    juce::Path a1; a1.addCentredArc (13, 12, 4, 4, 0, 0.6f, juce::MathConstants<float>::pi - 0.6f, true);
                    juce::Path a2; a2.addCentredArc (13, 12, 7.5f, 7.5f, 0, 0.6f, juce::MathConstants<float>::pi - 0.6f, true);
                    s.addPath (a1);
                    s.addPath (a2);
                }
                break;

            case Id::chevronDown:
                s.addPath (poly ({ {6.5f, 9.5f}, {12, 15}, {17.5f, 9.5f} }, false));
                break;

            case Id::chevronRight:
                s.addPath (poly ({ {9.5f, 6.5f}, {15, 12}, {9.5f, 17.5f} }, false));
                break;

            case Id::info:
                s.addPath (circle (12, 12, 9));
                s.addPath (line (12, 11, 12, 16.5f));
                f.addEllipse (11.0f, 7.0f, 2.0f, 2.0f);
                break;

            case Id::save:
                s.addPath (poly ({ {4, 4}, {16.5f, 4}, {20, 7.5f}, {20, 20}, {4, 20} }, true));
                s.addPath (poly ({ {8, 4}, {8, 9.5f}, {15, 9.5f}, {15, 4} }, false));
                s.addPath (roundRect (8, 14, 8, 6, 1));
                break;

            case Id::open:
                s.addPath (poly ({ {3, 6}, {3, 19}, {19, 19}, {21, 11}, {6, 11}, {4, 19} }, false));
                s.addPath (poly ({ {3, 6}, {9.5f, 6}, {11.5f, 8.5f}, {18, 8.5f}, {18, 11} }, false));
                break;

            case Id::link:
            {
                juce::Path a;
                a.addRoundedRectangle (3.5f, 9.0f, 9.0f, 6.0f, 3.0f);
                juce::Path b;
                b.addRoundedRectangle (11.5f, 9.0f, 9.0f, 6.0f, 3.0f);
                s.addPath (a);
                s.addPath (b);
                break;
            }

            case Id::unlink:
                s.addPath (roundRect (2.5f, 9, 8, 6, 3));
                s.addPath (roundRect (13.5f, 9, 8, 6, 3));
                s.addPath (line (10, 5, 14, 19));
                break;

            case Id::sun:
                s.addPath (circle (12, 12, 4));
                for (int i = 0; i < 8; ++i)
                {
                    const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                    s.addPath (line (12 + 7 * std::cos (a), 12 + 7 * std::sin (a), 12 + 9.5f * std::cos (a), 12 + 9.5f * std::sin (a)));
                }
                break;

            case Id::move:
                s.addPath (line (12, 3, 12, 21));
                s.addPath (line (3, 12, 21, 12));
                s.addPath (poly ({ {9, 6}, {12, 3}, {15, 6} }, false));
                s.addPath (poly ({ {9, 18}, {12, 21}, {15, 18} }, false));
                s.addPath (poly ({ {6, 9}, {3, 12}, {6, 15} }, false));
                s.addPath (poly ({ {18, 9}, {21, 12}, {18, 15} }, false));
                break;

            case Id::target:
                s.addPath (circle (12, 12, 9));
                s.addPath (circle (12, 12, 5));
                f.addEllipse (10.5f, 10.5f, 3.0f, 3.0f);
                break;

            case Id::sliders:
                for (int i = 0; i < 3; ++i)
                {
                    const float y = 6.0f + (float) i * 6.0f;
                    s.addPath (line (3, y, 21, y));
                    const float kx = i == 0 ? 15.0f : i == 1 ? 8.0f : 12.0f;
                    f.addEllipse (kx - 2.2f, y - 2.2f, 4.4f, 4.4f);
                }
                break;

            case Id::external:
                s.addPath (poly ({ {10, 5}, {4, 5}, {4, 20}, {19, 20}, {19, 14} }, false));
                s.addPath (poly ({ {14, 4}, {20, 4}, {20, 10} }, false));
                s.addPath (line (20, 4, 11, 13));
                break;

            case Id::minimise:
                s.addPath (line (6, 12, 18, 12));
                break;

            case Id::maximise:
                s.addPath (roundRect (6, 6, 12, 12, 1.5f));
                break;

            case Id::restore:
                s.addPath (roundRect (5, 8, 11, 11, 1.5f));
                s.addPath (poly ({ {9, 8}, {9, 5}, {19, 5}, {19, 15}, {16, 15} }, false));
                break;
        }

        juce::Path out = stroked (s);
        out.addPath (f);
        return out;
    }
}

juce::Path get (Id id)
{
    static std::map<int, juce::Path> cache;
    auto it = cache.find ((int) id);
    if (it != cache.end())
        return it->second;
    auto p = build (id);
    cache[(int) id] = p;
    return p;
}

void draw (juce::Graphics& g, Id id, juce::Rectangle<float> area, juce::Colour colour)
{
    auto p = get (id);
    const auto t = juce::AffineTransform::scale (area.getWidth() / 24.0f, area.getHeight() / 24.0f)
                       .translated (area.getX(), area.getY());
    g.setColour (colour);
    g.fillPath (p, t);
}
} // namespace fa::ui::icons
