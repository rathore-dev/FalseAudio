#pragma once

#include "Widgets.h"
#include "../AppShell.h"
#include "../../core/Session.h"

namespace fa::ui
{
/** "Send to DSP" action shared by the Crossover / Alignment pages: pushes the whole current
    config to the DSP. Automatic DSPs (and the Demo virtual car) are written in the background;
    a manual DSP gets a step list to copy. */
inline void sendConfigToDsp (Session& session, AppShell* shell)
{
    if (shell == nullptr)
        return;

    bool automatic = false;
    const auto steps = session.pushConfigToDsp (automatic);

    if (automatic)
    {
        shell->toast ("Sent " + juce::String ((int) session.getConfig().channels.size()) + " channels to "
                          + session.getDsp().getName(), AppShell::ToastKind::good);
        return;
    }
    if (steps.isEmpty())
    {
        shell->toast ("Can't send settings while Auto Tune is running", AppShell::ToastKind::warn);
        return;
    }

    auto sheet = std::make_unique<MessageSheet> ("Set these in your DSP software",
                                                 "This DSP is not controlled automatically. Enter these values, or copy them.",
                                                 steps, "Done", "Close", icons::Id::sliders);
    shell->showSheet (std::move (sheet), 560, MessageSheet::idealHeight (juce::jmin (steps.size(), 12)));
}
} // namespace fa::ui
