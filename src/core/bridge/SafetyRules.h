#pragma once

#include <algorithm>

namespace fa::safety
{
/** Absolute floor for any tweeter high-pass: below this a dome tweeter is at risk even if its Fs is low. */
inline constexpr double kTweeterHpFloorHz = 1200.0;
/** Used when the tweeter's Fs is unknown. */
inline constexpr double kTweeterHpUnknownFsHz = 1800.0;
/** Output gain written to a DSP must never exceed this (no boost after the amp level was set). */
inline constexpr double kMaxOutputGainDb = 0.0;
/** Largest upward gain change per write; bigger rises are walked in steps. */
inline constexpr double kMaxGainStepDb = 3.0;

/** Minimum safe tweeter high-pass: max(Fs x 2, 1.2 kHz); 1.8 kHz when Fs is unknown. */
inline double minTweeterHpHz (double fsHz) noexcept
{
    return fsHz > 0.0 ? std::max (fsHz * 2.0, kTweeterHpFloorHz) : kTweeterHpUnknownFsHz;
}
} // namespace fa::safety
