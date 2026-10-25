#include "MeasurementSetup.h"
#include <map>

namespace fa::ui
{
namespace
{
    constexpr int checkRowH = 44, demoBannerH = 48, diagramMin = 180, diagramMax = 280;

    juce::Colour stepColour (guide::Step::Status s)
    {
        using S = guide::Step::Status;
        switch (s)
        {
            case S::ok:      return theme::good;
            case S::warning: return theme::warn;
            case S::failed:  return theme::bad;
            case S::skipped: return theme::textMuted;
            case S::pending:
            default:         return theme::accent;
        }
    }

    juce::String stepStatusName (guide::Step::Status s)
    {
        using S = guide::Step::Status;
        switch (s)
        {
            case S::ok:      return "OK";
            case S::warning: return "Warning";
            case S::failed:  return "Failed";
            case S::skipped: return "Skipped";
            case S::pending:
            default:         return "Pending";
        }
    }
}

//==============================================================================
MeasurementSetup::MeasurementSetup (Session& s)
    : Card ("Measurement setup", "Mic positions around the listening head, interface settings and two live checks before the first sweep"),
      session (s)
{
    addAndMakeVisible (diagram);
    addAndMakeVisible (orientation);
    addAndMakeVisible (steps);
    addAndMakeVisible (demoNote);
    addAndMakeVisible (liveBtn);
    addAndMakeVisible (gainCheck);
    addAndMakeVisible (noiseCheck);

    liveBtn.onClick = [this] { if (onSwitchToLive) onSwitchToLive(); };

    gainCheck.job = [this] (std::atomic<bool>& cancel)
    {
        return guide::runGainStagingCheck (session.getAudio(), 0, session.getAutoTuneOptions().sweepLevelDbFS, cancel);
    };
    noiseCheck.job = [this] (std::atomic<bool>& cancel)
    {
        return guide::runNoiseCheck (session.getAudio(), 0, cancel);
    };
    auto toastResult = [this] (const guide::Step& st)
    {
        if (! onToast) return;
        using S = guide::Step::Status;
        onToast (st.title + ": " + stepStatusName (st.status),
                 st.status == S::ok ? AppShell::ToastKind::good : st.status == S::failed ? AppShell::ToastKind::bad : AppShell::ToastKind::warn);
    };
    gainCheck.onFinished = toastResult;
    noiseCheck.onFinished = toastResult;
}

MeasurementSetup::~MeasurementSetup() = default;

void MeasurementSetup::refresh (const gear::Product* mic, const gear::Product* audioInterface)
{
    const auto& cfg = session.getConfig();
    diagram.placement = guide::micPlacement (cfg, session.getAutoTuneOptions().micPositions, mic);

    juce::String text = diagram.placement.orientation;
    for (auto& ins : diagram.placement.instructions)
        text << juce::newLine << "• " << ins;
    orientation.setText (text);

    steps.steps = guide::interfaceSetup (audioInterface, mic, session.getAudio().getRouting());

    demo = session.getMode() == Session::Mode::demo;
    juce::String whyNot;
    const bool ready = ! demo && session.getAudio().isReady (whyNot);
    gainCheck.setEnabledForMode (ready, demo ? "Needs Live mode" : whyNot);
    noiseCheck.setEnabledForMode (ready, demo ? "Needs Live mode" : whyNot);
    demoNote.setVisible (demo);
    liveBtn.setVisible (demo);
    if (! demo && whyNot.isNotEmpty())
    {
        demoNote.setText ("Audio interface not ready: " + whyNot);
        demoNote.setVisible (true);
    }
    else
        demoNote.setText ("These checks need Live mode: in Demo the microphones are simulated.");

    diagram.repaint();
    steps.repaint();
    resized();
}

int MeasurementSetup::diagramWidth (int width) const
{
    return twoColumns (width) ? juce::jmin (560, (int) (width * 0.46)) : width;
}

int MeasurementSetup::heightFor (int width) const
{
    const int inner = width - 2 * ikit::gap2;
    const int dw = diagramWidth (inner);
    const int leftH = diagram.heightFor (dw) + ikit::gap + orientation.heightFor (dw);

    const int rw = twoColumns (inner) ? inner - dw - ikit::gap2 * 2 : inner;
    int rightH = steps.heightFor (rw) + ikit::gap2;
    if (demoNote.isVisible())
        rightH += demoBannerH + ikit::gap;
    rightH += gainCheck.heightFor (rw) + ikit::gap + noiseCheck.heightFor (rw);

    const int body = twoColumns (inner) ? juce::jmax (leftH, rightH) : leftH + ikit::gap2 + rightH;
    return ikit::cardHead + body + ikit::gap2;
}

void MeasurementSetup::resized()
{
    auto r = getContentArea();
    const int inner = r.getWidth();
    const int dw = diagramWidth (inner);

    juce::Rectangle<int> left, right;
    if (twoColumns (inner))
    {
        left = r.removeFromLeft (dw);
        r.removeFromLeft (ikit::gap2);
        // vertical divider sits in the gap
        r.removeFromLeft (ikit::gap2);
        right = r;
    }
    else
    {
        left = r.removeFromTop (diagram.heightFor (dw) + ikit::gap + orientation.heightFor (dw));
        r.removeFromTop (ikit::gap2);
        right = r;
    }

    diagram.setBounds (left.removeFromTop (diagram.heightFor (dw)));
    left.removeFromTop (ikit::gap);
    orientation.setBounds (left.removeFromTop (orientation.heightFor (dw)));

    steps.setBounds (right.removeFromTop (steps.heightFor (right.getWidth())));
    right.removeFromTop (ikit::gap2);
    if (demoNote.isVisible())
    {
        auto banner = right.removeFromTop (demoBannerH);
        auto b = banner.reduced (ikit::gap2, 0);
        if (liveBtn.isVisible())
            liveBtn.setBounds (b.removeFromRight (140).withSizeKeepingCentre (140, ikit::btnH));
        b.removeFromRight (ikit::gap);
        demoNote.setBounds (b.withSizeKeepingCentre (b.getWidth(), demoNote.heightFor (b.getWidth())));
        right.removeFromTop (ikit::gap);
    }
    gainCheck.setBounds (right.removeFromTop (gainCheck.heightFor (right.getWidth())));
    right.removeFromTop (ikit::gap);
    noiseCheck.setBounds (right.removeFromTop (noiseCheck.heightFor (right.getWidth())));
}

//==============================================================================
int MeasurementSetup::MicDiagram::viewSize (int width)
{
    return juce::jlimit (diagramMin, diagramMax, (width - ikit::gap2) / 2);
}

int MeasurementSetup::MicDiagram::heightFor (int width) const
{
    return viewSize (width) + 22;
}

void MeasurementSetup::MicDiagram::paint (juce::Graphics& g)
{
    const int s = viewSize (getWidth());
    auto bounds = getLocalBounds();
    auto top  = bounds.removeFromLeft (s).withHeight (s + 22).toFloat();
    bounds.removeFromLeft (ikit::gap2);
    auto side = bounds.removeFromLeft (s).withHeight (s + 22).toFloat();
    drawView (g, top, true);
    drawView (g, side, false);
}

void MeasurementSetup::MicDiagram::drawView (juce::Graphics& g, juce::Rectangle<float> area, bool topView) const
{
    auto caption = area.removeFromBottom (22.0f);
    theme::drawEyebrow (g, topView ? "Top view  (x right, y forward)" : "Side view  (y forward, z up)", caption.toNearestInt().withTrimmedTop (6), theme::textMuted);

    // panel
    g.setColour (theme::bg1);
    g.fillRoundedRectangle (area, theme::radiusSm);
    g.setColour (theme::border);
    g.drawRoundedRectangle (area.reduced (0.5f), theme::radiusSm, 1.0f);

    const float scale = area.getWidth() / 64.0f;         // px per cm, +-32 cm visible
    const auto c = area.getCentre();
    auto px = [&] (double a, double b) { return juce::Point<float> (c.x + (float) a * scale, c.y - (float) b * scale); };

    // grid every 10 cm
    g.setColour (theme::border.withAlpha (0.45f));
    for (int cm = -30; cm <= 30; cm += 10)
    {
        if (cm == 0) continue;
        g.drawVerticalLine ((int) px (cm, 0).x, area.getY() + 1.0f, area.getBottom() - 1.0f);
        g.drawHorizontalLine ((int) px (0, cm).y, area.getX() + 1.0f, area.getRight() - 1.0f);
    }
    g.setColour (theme::borderHi.withAlpha (0.8f));
    g.drawVerticalLine ((int) c.x, area.getY() + 1.0f, area.getBottom() - 1.0f);
    g.drawHorizontalLine ((int) c.y, area.getX() + 1.0f, area.getRight() - 1.0f);

    // seat + head silhouette
    const auto silhouette = theme::textMuted.withAlpha (0.35f);
    if (topView)
    {
        // headrest behind the head (y < -12 cm), shoulders
        auto headrest = juce::Rectangle<float>::leftTopRightBottom (px (-13, -13).x, px (0, -13).y, px (13, -20).x, px (0, -20).y);
        g.setColour (silhouette);
        g.fillRoundedRectangle (headrest, 4.0f);
        auto shoulders = juce::Rectangle<float>::leftTopRightBottom (px (-22, -4).x, px (0, -4).y, px (22, -12).x, px (0, -12).y);
        g.setColour (silhouette.withAlpha (0.18f));
        g.fillRoundedRectangle (shoulders, 6.0f);
        // head: 15 x 19 cm ellipse, nose forward
        auto head = juce::Rectangle<float> (15.0f * scale, 19.0f * scale).withCentre (c);
        g.setColour (theme::bg3);
        g.fillEllipse (head);
        g.setColour (theme::textDim.withAlpha (0.8f));
        g.drawEllipse (head, 1.2f);
        juce::Path nose;
        nose.addTriangle (px (-2, 9), px (2, 9), px (0, 12));
        g.fillPath (nose);
        // ears
        g.fillEllipse (juce::Rectangle<float> (2.5f * scale, 4.0f * scale).withCentre (px (-8.5, 0)));
        g.fillEllipse (juce::Rectangle<float> (2.5f * scale, 4.0f * scale).withCentre (px (8.5, 0)));
    }
    else
    {
        // seat back + headrest behind (y < -12), torso below
        auto seatBack = juce::Rectangle<float>::leftTopRightBottom (px (-22, 6).x, px (0, 6).y, px (-13, -30).x, px (0, -30).y);
        g.setColour (silhouette.withAlpha (0.18f));
        g.fillRoundedRectangle (seatBack, 5.0f);
        auto headrest = juce::Rectangle<float>::leftTopRightBottom (px (-20, 10).x, px (0, 10).y, px (-12, -8).x, px (0, -8).y);
        g.setColour (silhouette);
        g.fillRoundedRectangle (headrest, 4.0f);
        auto torso = juce::Rectangle<float>::leftTopRightBottom (px (-12, -12).x, px (0, -12).y, px (10, -30).x, px (0, -30).y);
        g.setColour (silhouette.withAlpha (0.18f));
        g.fillRoundedRectangle (torso, 6.0f);
        auto head = juce::Rectangle<float> (19.0f * scale, 22.0f * scale).withCentre (c);
        g.setColour (theme::bg3);
        g.fillEllipse (head);
        g.setColour (theme::textDim.withAlpha (0.8f));
        g.drawEllipse (head, 1.2f);
        juce::Path nose;
        nose.addTriangle (px (9, 2), px (9, -2), px (12, 0));
        g.fillPath (nose);
        g.fillEllipse (juce::Rectangle<float> (3.0f * scale, 4.0f * scale).withCentre (px (-1, 0)));   // ear
    }

    // positions (merge those that overlap in this projection)
    struct Spot { juce::Point<float> p; juce::String nums; };
    std::vector<Spot> spots;
    for (size_t i = 0; i < placement.positions.size(); ++i)
    {
        auto& pos = placement.positions[i];
        auto p = topView ? px (pos.xCm, pos.yCm) : px (pos.yCm, pos.zCm);
        bool merged = false;
        for (auto& s : spots)
            if (s.p.getDistanceFrom (p) < 1.0f) { s.nums << "·" << juce::String ((int) i + 1); merged = true; break; }
        if (! merged)
            spots.push_back ({ p, juce::String ((int) i + 1) });
    }

    // connecting lines from centre
    g.setColour (theme::accent.withAlpha (0.35f));
    for (auto& s : spots)
        if (s.p.getDistanceFrom (c) > 2.0f)
            g.drawLine (juce::Line<float> (c, s.p), 1.0f);

    const float r = juce::jlimit (9.0f, 12.0f, 3.2f * scale);
    for (auto& s : spots)
    {
        const bool wide = s.nums.length() > 2;
        auto dot = juce::Rectangle<float> (wide ? r * 2.0f + 6.0f * (float) (s.nums.length() - 1) : r * 2.0f, r * 2.0f).withCentre (s.p);
        theme::drawGlow (g, dot, theme::accent, r, 6.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (dot, r);
        g.setColour (theme::onAccent);
        g.setFont (theme::fontBold (wide ? 10.0f : 11.5f));
        g.drawText (s.nums, dot.toNearestInt(), juce::Justification::centred, false);
    }

    // scale marker
    g.setColour (theme::textMuted);
    g.setFont (theme::font (10.0f));
    g.drawText ("10 cm", juce::Rectangle<int> ((int) px (-30, 0).x, (int) px (0, -31).y - 12, (int) (10.0f * scale), 12),
                juce::Justification::centred, false);
    g.drawHorizontalLine ((int) px (0, -31).y, px (-30, 0).x, px (-20, 0).x);
}

//==============================================================================
int MeasurementSetup::StepList::stepHeight (const guide::Step& s, int width) const
{
    const int textW = width - 40;
    int h = 22 + ikit::textHeight (theme::font (12.5f), s.detail, textW);
    for (auto& b : s.bullets)
        h += ikit::textHeight (theme::font (12.0f), b, textW - 14);
    if (s.result.isNotEmpty())
        h += ikit::textHeight (theme::font (12.0f), s.result, textW) + 2;
    return h + 10;
}

int MeasurementSetup::StepList::heightFor (int width) const
{
    int h = 20;
    if (steps.empty()) return h + 24;
    for (auto& s : steps) h += stepHeight (s, width);
    return h;
}

void MeasurementSetup::StepList::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    theme::drawEyebrow (g, "Interface setup", b.removeFromTop (20), theme::textMuted);
    if (steps.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawText ("Pick your audio interface and measurement mic on the Equipment page for exact settings.", b.removeFromTop (24),
                    juce::Justification::centredLeft, true);
        return;
    }
    const int textW = getWidth() - 40;
    for (size_t i = 0; i < steps.size(); ++i)
    {
        auto& s = steps[i];
        auto area = b.removeFromTop (stepHeight (s, getWidth()));
        auto num = area.removeFromLeft (28).withHeight (22).toFloat().withSizeKeepingCentre (22.0f, 22.0f);
        const auto col = s.status == guide::Step::Status::pending ? theme::accent : stepColour (s.status);
        g.setColour (col.withAlpha (0.14f));
        g.fillEllipse (num);
        g.setColour (col.withAlpha (0.6f));
        g.drawEllipse (num.reduced (0.5f), 1.0f);
        g.setColour (col);
        g.setFont (theme::fontBold (11.0f));
        g.drawText (juce::String ((int) i + 1), num.toNearestInt(), juce::Justification::centred, false);
        area.removeFromLeft (12);
        if (i + 1 < steps.size())
        {
            g.setColour (theme::border);
            g.fillRect ((int) num.getCentreX(), (int) num.getBottom() + 3, 1, area.getHeight() - 28);
        }

        g.setColour (theme::text);
        g.setFont (theme::fontMedium (13.5f));
        g.drawText (s.title, area.removeFromTop (22), juce::Justification::centredLeft, true);
        const int dh = ikit::textHeight (theme::font (12.5f), s.detail, textW);
        ikit::drawMultiline (g, theme::font (12.5f), theme::textDim, s.detail, area.removeFromTop (dh));
        for (auto& bl : s.bullets)
        {
            const int bh = ikit::textHeight (theme::font (12.0f), bl, textW - 14);
            auto line = area.removeFromTop (bh);
            g.setColour (theme::accent.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ (float) line.getX() + 4.0f, (float) line.getY() + 8.0f }));
            ikit::drawMultiline (g, theme::font (12.0f), theme::textDim, bl, line.withTrimmedLeft (14));
        }
        if (s.result.isNotEmpty())
        {
            const int rh = ikit::textHeight (theme::font (12.0f), s.result, textW);
            ikit::drawMultiline (g, theme::font (12.0f), stepColour (s.status), s.result, area.removeFromTop (rh + 2));
        }
    }
}

//==============================================================================
class MeasurementSetup::CheckRow::Worker : public juce::Thread
{
public:
    Worker (CheckRow& o, std::function<guide::Step (std::atomic<bool>&)> j)
        : juce::Thread ("FalseAudio live check"), owner (&o), job (std::move (j)) {}
    ~Worker() override { cancel = true; stopThread (8000); }

    void run() override
    {
        guide::Step s = job ? job (cancel) : guide::Step {};
        if (cancel)
        {
            s.status = guide::Step::Status::skipped;
            if (s.result.isEmpty()) s.result = "Cancelled.";
        }
        juce::MessageManager::callAsync ([sp = owner, s]
        {
            if (sp != nullptr)
                sp->finished (s);
        });
    }

    std::atomic<bool> cancel { false };

private:
    juce::Component::SafePointer<CheckRow> owner;
    std::function<guide::Step (std::atomic<bool>&)> job;
};

MeasurementSetup::CheckRow::CheckRow (const juce::String& n, const juce::String& h, icons::Id i)
    : name (n), hint (h), icon (i)
{
    addAndMakeVisible (button);
    button.onClick = [this]
    {
        if (isRunning()) cancelRun();
        else             start();
    };
}

MeasurementSetup::CheckRow::~CheckRow()
{
    thread.reset();     // cancels + joins
}

bool MeasurementSetup::CheckRow::isRunning() const noexcept
{
    return thread != nullptr && thread->isThreadRunning();
}

void MeasurementSetup::CheckRow::setEnabledForMode (bool isLive, const juce::String& whyNot)
{
    live = isLive;
    disabledReason = whyNot;
    button.setEnabled (live || isRunning());
    repaint();
}

int MeasurementSetup::CheckRow::heightFor (int width) const
{
    int h = checkRowH;
    if (hasResult && result.result.isNotEmpty())
        h += ikit::textHeight (theme::font (12.5f), result.result, width - 56 - ikit::gap2) + 10;
    return h;
}

void MeasurementSetup::CheckRow::resized()
{
    auto r = getLocalBounds().withHeight (checkRowH).reduced (ikit::gap, 0);
    button.setBounds (r.removeFromRight (92).withSizeKeepingCentre (92, ikit::btnH));
}

void MeasurementSetup::CheckRow::start()
{
    if (! live || isRunning())
        return;
    thread = std::make_unique<Worker> (*this, job);
    hasResult = false;
    button.setButtonText ("Cancel");
    button.setIcon (icons::Id::stop);
    button.setStyle (FaButton::Style::danger);
    thread->startThread();
    repaint();
}

void MeasurementSetup::CheckRow::cancelRun()
{
    if (thread != nullptr)
        thread->cancel = true;
    button.setEnabled (false);
    button.setButtonText ("Stopping");
}

void MeasurementSetup::CheckRow::finished (const guide::Step& s)
{
    result = s;
    hasResult = true;
    button.setButtonText ("Run again");
    button.setIcon (icons::Id::refresh);
    button.setStyle (FaButton::Style::secondary);
    button.setEnabled (live);
    // let the thread object finish on its own; it is joined when replaced or destroyed
    if (onFinished)
        onFinished (s);
    if (auto* p = getParentComponent())
        p->resized();
    if (auto* pp = getParentComponent() != nullptr ? getParentComponent()->getParentComponent() : nullptr)
        pp->resized();
    repaint();
}

void MeasurementSetup::CheckRow::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();
    const bool running = isRunning();
    const auto col = running ? theme::accent : hasResult ? stepColour (result.status) : (live ? theme::textDim : theme::textMuted);

    theme::fillCard (g, full, theme::bg1.withAlpha (0.7f), running ? theme::accent.withAlpha (0.5f) : theme::border, theme::radiusSm);
    if (running)
        theme::drawGlow (g, full, theme::accent, theme::radiusSm, 6.0f);

    auto r = getLocalBounds().withHeight (checkRowH).reduced (ikit::gap, 0);
    r.removeFromRight (92 + ikit::gap);

    auto ic = r.removeFromLeft (30).toFloat().withSizeKeepingCentre (26.0f, 26.0f);
    g.setColour (col.withAlpha (0.14f));
    g.fillRoundedRectangle (ic, 6.0f);
    icons::draw (g, icon, ic.reduced (6.0f), col);
    r.removeFromLeft (ikit::gap + 2);

    // status badge on the right of the text
    auto badgeArea = r.removeFromRight (96);
    ikit::drawBadge (g, badgeArea, running ? "Running" : hasResult ? stepStatusName (result.status) : (live ? "Ready" : "Live only"),
                     col, juce::Justification::centredRight);

    g.setColour (theme::text);
    g.setFont (theme::fontMedium (13.5f));
    g.drawText (name, r.withTrimmedTop (6).withHeight (18), juce::Justification::centredLeft, true);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (11.5f));
    g.drawText (live || hasResult ? hint : (disabledReason.isNotEmpty() ? disabledReason : hint),
                r.withTrimmedTop (24).withHeight (15), juce::Justification::centredLeft, true);

    if (hasResult && result.result.isNotEmpty())
    {
        auto body = getLocalBounds().withTrimmedTop (checkRowH - 2).reduced (ikit::gap2 + 40, 0).withTrimmedBottom (8);
        body.setX (ikit::gap + 40);
        body.setWidth (getWidth() - body.getX() - ikit::gap2);
        ikit::drawMultiline (g, theme::font (12.5f), stepColour (result.status).interpolatedWith (theme::text, 0.35f), result.result, body);
    }
}
} // namespace fa::ui
