#pragma once

#include <JuceHeader.h>

namespace fa::ui::anim
{
/** Easing curves (t in 0..1). */
inline float easeOutCubic (float t) noexcept   { const float u = 1.0f - t; return 1.0f - u * u * u; }
inline float easeInOutCubic (float t) noexcept { return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow (-2.0f * t + 2.0f, 3.0f) / 2.0f; }
inline float easeOutBack (float t) noexcept    { const float c = 1.70158f; const float u = t - 1.0f; return 1.0f + (c + 1.0f) * u * u * u + c * u * u; }

/** A float that glides towards its target at 60 fps and repaints its owner while moving.
    Frame-rate independent exponential approach; the timer stops as soon as the value settles,
    so idle widgets cost nothing. */
class Value : private juce::Timer
{
public:
    /** `speed` is how quickly the gap closes per second (10 = settles in ~0.3 s). */
    explicit Value (juce::Component& ownerToRepaint, float initial = 0.0f, float speed = 12.0f)
        : owner (ownerToRepaint), current (initial), target (initial), rate (speed) {}

    ~Value() override = default;

    void setTarget (float newTarget)
    {
        if (juce::approximatelyEqual (target, newTarget))
            return;
        target = newTarget;
        if (! isTimerRunning())
        {
            lastTick = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (60);
        }
    }

    /** Jumps straight to a value with no animation. */
    void snap (float v)
    {
        stopTimer();
        current = target = v;
        owner.repaint();
    }

    float get() const noexcept        { return current; }
    float getTarget() const noexcept  { return target; }
    bool  isAnimating() const noexcept { return isTimerRunning(); }
    void  setSpeed (float speed) noexcept { rate = speed; }

    /** Called every frame while moving (after the owner has been asked to repaint). */
    std::function<void()> onTick;

private:
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
        lastTick = now;

        const float k = 1.0f - std::exp (-rate * dt);
        current += (target - current) * k;

        if (std::abs (target - current) < 0.0015f)
        {
            current = target;
            stopTimer();
        }

        owner.repaint();
        if (onTick)
            onTick();
    }

    juce::Component& owner;
    float current, target, rate;
    double lastTick = 0.0;

    JUCE_DECLARE_NON_COPYABLE (Value)
};
} // namespace fa::ui::anim
