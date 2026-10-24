#include "WindowPicker.h"

namespace fa::ui::dsp
{
void showWindowPicker (juce::Component& anchor, std::function<void (const DspWindowHost::WindowInfo&)> onChosen)
{
    auto windows = std::make_shared<std::vector<DspWindowHost::WindowInfo>> (DspWindowHost::listWindows());

    juce::PopupMenu menu;
    if (windows->empty())
    {
        menu.addItem (1, "No other windows found", false);
    }
    else
    {
        juce::PopupMenu::Item header;
        header.text = "Choose the DSP software window";
        header.isSectionHeader = true;
        menu.addItem (header);

        for (size_t i = 0; i < windows->size(); ++i)
        {
            const auto& w = (*windows)[i];
            juce::String title = w.title.length() > 60 ? w.title.substring (0, 57) + "..." : w.title;
            menu.addItem ((int) i + 1, w.processName + "   —   " + title);
        }
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withMinimumWidth (320),
                        [windows, onChosen] (int result)
                        {
                            if (result <= 0 || (size_t) result > windows->size() || onChosen == nullptr)
                                return;
                            onChosen ((*windows)[(size_t) result - 1]);
                        });
}
} // namespace fa::ui::dsp
