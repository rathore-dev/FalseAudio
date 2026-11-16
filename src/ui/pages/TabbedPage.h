#pragma once

#include "../Page.h"
#include "../AppShell.h"
#include "../components/Widgets.h"
#include "system/PageKit.h"

namespace fa::ui
{
/** A page that hosts other pages behind a sub-tab bar (Car: Layout / Equipment; Expert:
    Measure / Live / Alignment / Crossovers & EQ). The hosted pages are the ordinary Page
    components, unchanged: they get the shell pointer, pageShown()/pageHidden() bracket the
    time their tab is actually on screen, and the last tab is remembered across launches.

    The bar is one row: segmented tabs on the left, an optional area on the right that
    subclasses lay out in layoutBarExtras() (e.g. Expert's single "Send to DSP"). */
class TabbedPage : public Page
{
public:
    TabbedPage (Session& s, const juce::String& prefsKeyToUse) : Page (s), prefsKey (prefsKeyToUse)
    {
        addAndMakeVisible (tabBar);
        tabBar.onChange = [this] (int i) { selectIndex (i, true); };
    }

    static constexpr int barHeight = 52;

    // ---- Page
    juce::String getSubtitle() const override
    {
        return active() != nullptr ? active()->getSubtitle() : juce::String();
    }

    bool showTab (const juce::String& key) override
    {
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i].key.equalsIgnoreCase (key))
            {
                selectIndex ((int) i, false);
                return true;
            }
        return false;
    }

    juce::String getCurrentTab() const override
    {
        return juce::isPositiveAndBelow (current, (int) tabs.size()) ? tabs[(size_t) current].key : juce::String();
    }

    void setShell (AppShell* s) override
    {
        shell = s;
        for (auto& t : tabs)
            t.page->setShell (s);
    }

    void pageShown() override
    {
        shown = true;
        if (auto* p = active())
            p->pageShown();
    }

    void pageHidden() override
    {
        shown = false;
        if (auto* p = active())
            p->pageHidden();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::bg1);
        g.setColour (theme::border);
        g.fillRect (0, barHeight - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (barHeight).reduced (theme::padLg, 0);
        bar = bar.withSizeKeepingCentre (bar.getWidth(), theme::controlHeight);

        int tabsW = 0;
        for (auto& t : tabs)
            tabsW += t.width;
        tabBar.setBounds (bar.removeFromLeft (juce::jmin (tabsW, bar.getWidth())));
        bar.removeFromLeft (theme::pad);
        layoutBarExtras (bar);

        for (auto& t : tabs)
            t.page->setBounds (r);
    }

protected:
    /** Adds a hosted page. `width` is the tab's width in the segmented bar. */
    template <typename PageType>
    PageType& addTab (const juce::String& key, const juce::String& label, int width)
    {
        auto page = std::make_unique<PageType> (session);
        auto& ref = *page;
        addChildComponent (ref);
        tabs.push_back ({ key, label, width, std::move (page) });
        return ref;
    }

    /** Call once after the last addTab(): builds the bar and restores the remembered tab. */
    void finishTabs()
    {
        juce::StringArray labels;
        for (auto& t : tabs)
            labels.add (t.label);
        tabBar.setOptions (labels);

        int start = 0;
        const auto saved = kit::uiPrefs (session).getValue (prefsKey);
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i].key == saved)
                start = (int) i;
        current = -1;
        selectIndex (start, false);
    }

    /** Area right of the tabs (already vertically centred, control height). */
    virtual void layoutBarExtras (juce::Rectangle<int>) {}
    virtual void tabChanged() {}

    Page* active() const
    {
        return juce::isPositiveAndBelow (current, (int) tabs.size()) ? tabs[(size_t) current].page.get() : nullptr;
    }

    SegmentedControl tabBar;

private:
    struct Tab
    {
        juce::String key, label;
        int width = 120;
        std::unique_ptr<Page> page;
    };

    void selectIndex (int index, bool fromUser)
    {
        if (! juce::isPositiveAndBelow (index, (int) tabs.size()))
            return;
        if (index != current)
        {
            if (auto* prev = active())
            {
                prev->setVisible (false);
                if (shown)
                    prev->pageHidden();
            }
            current = index;
            auto* next = active();
            next->setBounds (getLocalBounds().withTrimmedTop (barHeight));
            next->setVisible (true);
            if (shown)
                next->pageShown();
            kit::uiPrefs (session).setValue (prefsKey, tabs[(size_t) index].key);
            tabChanged();
        }
        tabBar.setSelected (index);
        if (fromUser && shell != nullptr)
            shell->refreshHeader();
    }

    std::vector<Tab> tabs;
    juce::String prefsKey;
    int current = -1;
    bool shown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabbedPage)
};
} // namespace fa::ui
