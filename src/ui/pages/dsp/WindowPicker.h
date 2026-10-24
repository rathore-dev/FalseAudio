#pragma once

#include "DspCommon.h"

namespace fa::ui::dsp
{
    /** Popup list of other applications' top-level windows (process + title). The callback is
        called on the message thread with the chosen window, or not at all if dismissed. */
    void showWindowPicker (juce::Component& anchor, std::function<void (const DspWindowHost::WindowInfo&)> onChosen);
} // namespace fa::ui::dsp
