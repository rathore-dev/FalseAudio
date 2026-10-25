#include "OverlayLayer.h"

namespace fa::ui
{
using namespace juce;

namespace
{
    constexpr int toastHeight = 46;
    constexpr int toastGap = 8;
    constexpr int toastMargin = 20;
    constexpr int maxToasts = 5;
}

// =============================================================================
/** Rounded, elevated panel around the sheet content. */
class OverlayLayer::SheetHolder : public Component
{
public:
    explicit SheetHolder (std::unique_ptr<Component> c) : content (std::move (c))
    {
        setOpaque (false);
        addAndMakeVisible (*content);
        setWantsKeyboardFocus (true);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced ((float) margin);
        theme::drawShadow (g, r, theme::radiusLg, 26.0f, 0.75f);
        theme::fillCard (g, r, theme::bg2, theme::borderHi, theme::radiusLg);
        g.setColour (theme::accent.withAlpha (0.35f));
        g.fillRoundedRectangle (r.withHeight (2.0f).reduced (theme::radiusLg, 0.0f), 1.0f);
    }

    void resized() override { content->setBounds (getLocalBounds().reduced (margin)); }

    Component& getContent() { return *content; }

    static constexpr int margin = 40;   // room for the shadow

private:
    std::unique_ptr<Component> content;
};

// =============================================================================
/** A single toast. Slides in from the right, dismisses itself after a few seconds or on click. */
class OverlayLayer::Toast : public Component, private Timer
{
public:
    Toast (OverlayLayer& o, const String& msg, AppShell::ToastKind k) : owner (o), message (msg), kind (k)
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        yAnim.onTick = [this] { updatePosition (owner.getWidth() - toastMargin); };
        appear.onTick = [this]
        {
            updatePosition (owner.getWidth() - toastMargin);
            if (leaving && appear.get() < 0.02f)
                owner.dismissToast (this);
        };
        appear.setTarget (1.0f);
        startTimer (kind == AppShell::ToastKind::bad ? 7000 : (kind == AppShell::ToastKind::warn ? 5500 : 4000));
    }

    void leave()
    {
        if (leaving)
            return;
        leaving = true;
        stopTimer();
        appear.setTarget (0.0f);
    }

    bool isLeaving() const noexcept { return leaving; }

    /** Target y (top) in overlay coordinates; animated. */
    void setTargetY (int y, bool animate)
    {
        if (animate) yAnim.setTarget ((float) y);
        else         yAnim.snap ((float) y);
    }

    void updatePosition (int rightEdge)
    {
        const float t = anim::easeOutCubic (appear.get());
        const int slide = (int) ((1.0f - t) * 28.0f);
        setBounds (rightEdge - theme::toastWidth + slide, (int) std::round (yAnim.get()), theme::toastWidth, toastHeight);
        setAlpha (jlimit (0.0f, 1.0f, appear.get()));
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const Colour c = colourFor (kind);
        theme::drawShadow (g, r, theme::radius, 10.0f, 0.5f);
        theme::fillCard (g, r, theme::bg3, theme::borderHi, theme::radius);

        g.saveState();
        Path clip;
        clip.addRoundedRectangle (r, theme::radius);
        g.reduceClipRegion (clip);
        g.setColour (c);
        g.fillRect (r.removeFromLeft (3.0f));
        g.restoreState();

        Rectangle<float> iconTile (14.0f, r.getCentreY() - 11.0f, 22.0f, 22.0f);
        g.setColour (c.withAlpha (0.16f));
        g.fillEllipse (iconTile);
        icons::draw (g, iconFor (kind), iconTile.reduced (4.5f), c);

        g.setColour (theme::text);
        g.setFont (theme::font (13.0f));
        g.drawFittedText (message, getLocalBounds().withTrimmedLeft (46).withTrimmedRight (14), Justification::centredLeft, 2, 0.9f);
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            leave();
    }

    void mouseEnter (const MouseEvent&) override { stopTimer(); }
    void mouseExit (const MouseEvent&) override  { if (! leaving) startTimer (1800); }

private:
    void timerCallback() override { leave(); }

    static Colour colourFor (AppShell::ToastKind k)
    {
        switch (k)
        {
            case AppShell::ToastKind::good: return theme::good;
            case AppShell::ToastKind::warn: return theme::warn;
            case AppShell::ToastKind::bad:  return theme::bad;
            case AppShell::ToastKind::info:
            default:                        return theme::accent;
        }
    }

    static icons::Id iconFor (AppShell::ToastKind k)
    {
        switch (k)
        {
            case AppShell::ToastKind::good: return icons::Id::check;
            case AppShell::ToastKind::warn: return icons::Id::warning;
            case AppShell::ToastKind::bad:  return icons::Id::cross;
            case AppShell::ToastKind::info:
            default:                        return icons::Id::info;
        }
    }

    OverlayLayer& owner;
    String message;
    AppShell::ToastKind kind;
    bool leaving = false;
    anim::Value appear { *this, 0.0f, 10.0f };
    anim::Value yAnim { *this, 0.0f, 14.0f };
};

// =============================================================================
OverlayLayer::OverlayLayer()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, true);
    dimAnim.onTick = [this]
    {
        layoutSheet();
        if (closing && dimAnim.get() < 0.02f)
        {
            sheet.reset();
            closing = false;
            updateInterception();
            repaint();
        }
    };
}

OverlayLayer::~OverlayLayer() = default;

void OverlayLayer::updateInterception()
{
    setInterceptsMouseClicks (sheet != nullptr, true);
}

void OverlayLayer::showSheet (std::unique_ptr<Component> content, int width, int height)
{
    sheet.reset();
    closing = false;
    sheetW = width;
    sheetH = height;
    sheet = std::make_unique<SheetHolder> (std::move (content));
    addAndMakeVisible (*sheet);
    sheet->toFront (false);
    for (auto& t : toasts)
        t->toFront (false);

    dimAnim.snap (0.0f);
    dimAnim.setTarget (1.0f);
    layoutSheet();
    updateInterception();
    sheet->getContent().grabKeyboardFocus();
    repaint();
}

void OverlayLayer::closeSheet()
{
    if (sheet == nullptr || closing)
        return;
    closing = true;
    sheet->setInterceptsMouseClicks (false, false);
    dimAnim.setTarget (0.0f);
}

void OverlayLayer::layoutSheet()
{
    if (sheet == nullptr)
        return;
    const float t = anim::easeOutCubic (jlimit (0.0f, 1.0f, dimAnim.get()));
    const int m = SheetHolder::margin;
    const int w = jmin (sheetW, getWidth() - 32);
    const int h = jmin (sheetH, getHeight() - 32);
    const int x = (getWidth() - w) / 2;
    const int y = (getHeight() - h) / 2 + (int) ((1.0f - t) * 18.0f);
    sheet->setBounds (x - m, y - m, w + m * 2, h + m * 2);
    sheet->setAlpha (t);
    repaint();
}

void OverlayLayer::resized()
{
    layoutSheet();
    layoutToasts();
}

void OverlayLayer::paint (Graphics& g)
{
    if (sheet != nullptr)
    {
        const float t = jlimit (0.0f, 1.0f, dimAnim.get());
        g.fillAll (theme::overlay.withMultipliedAlpha (t));
    }
}

void OverlayLayer::mouseDown (const MouseEvent&)
{
    // clicks on the backdrop are swallowed: sheets are modal and close via their own buttons
}

// ---- toasts -----------------------------------------------------------------
void OverlayLayer::toast (const String& message, AppShell::ToastKind kind)
{
    while ((int) toasts.size() >= maxToasts)
    {
        // drop the oldest that isn't already leaving
        auto it = std::find_if (toasts.begin(), toasts.end(), [] (auto& t) { return ! t->isLeaving(); });
        if (it == toasts.end())
            break;
        (*it)->leave();
        // make room immediately for layout purposes
        break;
    }

    auto t = std::make_unique<Toast> (*this, message, kind);
    addAndMakeVisible (*t);
    const int startY = getHeight() - toastMargin - toastHeight;
    t->setTargetY (startY, false);
    t->updatePosition (getWidth() - toastMargin);
    toasts.push_back (std::move (t));
    layoutToasts();
}

void OverlayLayer::layoutToasts()
{
    // newest at the bottom, older ones stack upwards
    int y = getHeight() - toastMargin - toastHeight;
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        auto& t = *it;
        t->setTargetY (y, true);
        t->updatePosition (getWidth() - toastMargin);
        if (! t->isLeaving())
            y -= toastHeight + toastGap;
    }
}

void OverlayLayer::dismissToast (Toast* toDismiss)
{
    // Called from inside the toast's animation tick: defer the destruction.
    Component::SafePointer<OverlayLayer> safe (this);
    MessageManager::callAsync ([safe, toDismiss]
    {
        if (safe == nullptr)
            return;
        auto& list = safe->toasts;
        list.erase (std::remove_if (list.begin(), list.end(), [toDismiss] (auto& t) { return t.get() == toDismiss; }), list.end());
        safe->layoutToasts();
    });
}
} // namespace fa::ui
