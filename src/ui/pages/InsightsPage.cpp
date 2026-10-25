#include "InsightsPage.h"
#include "insights/ScoreStrip.h"
#include "insights/BeforeAfterExplorer.h"
#include "insights/BandTable.h"
#include "insights/DriverCards.h"
#include "insights/DistortionOverview.h"

namespace fa::ui
{
namespace
{
    constexpr int sectionLabelH = 28;

    int resultStamp (const AutoTuneEngine::Result& r)
    {
        return (int) r.measurements.size() * 7919 + r.score * 131 + (int) (r.seconds * 100.0)
               + (int) r.report.notes.size() + (int) r.tunedConfig.channels.size() * 17;
    }
}

//==============================================================================
class InsightsPage::Content : public juce::Component
{
public:
    Content()
    {
        for (auto* c : std::initializer_list<juce::Component*> { &scores, &explorer, &table, &drivers, &distortion })
            addAndMakeVisible (c);
        table.onBandHover = [this] (double hz) { explorer.setHighlightBand (hz); };
        table.onExport = [this] { if (onExport) onExport(); };
    }

    std::function<void()> onExport;

    void set (const AutoTuneEngine::Result& r, const insights::Insights& in)
    {
        scores.set (r, in);
        explorer.set (r, in);
        table.set (in);
        drivers.set (in, r.tunedConfig);
        distortion.set (in, r.tunedConfig);
        numDrivers = drivers.numCards();
        resized();
        repaint();
    }

    int idealHeight (int w) const
    {
        int h = scores.idealHeight (w) + insightsui::gap;
        h += explorer.idealHeight (w) + insightsui::gap;
        h += table.idealHeight (w) + insightsui::gap;
        if (numDrivers > 0)
            h += sectionLabelH + drivers.idealHeight (w) + insightsui::gap;
        h += distortion.idealHeight (w) + theme::pad;
        return h;
    }

    void resized() override
    {
        auto a = getLocalBounds();
        const int w = a.getWidth();
        scores.setBounds (a.removeFromTop (scores.idealHeight (w)));
        a.removeFromTop (insightsui::gap);
        explorer.setBounds (a.removeFromTop (explorer.idealHeight (w)));
        a.removeFromTop (insightsui::gap);
        table.setBounds (a.removeFromTop (table.idealHeight (w)));
        a.removeFromTop (insightsui::gap);
        if (numDrivers > 0)
        {
            driversLabel = a.removeFromTop (sectionLabelH);
            drivers.setBounds (a.removeFromTop (drivers.idealHeight (w)));
            a.removeFromTop (insightsui::gap);
        }
        else
        {
            driversLabel = {};
            drivers.setBounds (0, 0, 0, 0);
        }
        distortion.setBounds (a.removeFromTop (distortion.idealHeight (w)));
    }

    void paint (juce::Graphics& g) override
    {
        if (driversLabel.isEmpty())
            return;
        auto r = driversLabel.withTrimmedBottom (6);
        theme::drawEyebrow (g, "PER-DRIVER DIAGNOSTICS", r.removeFromLeft (170), theme::textDim);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.5f));
        g.drawText (juce::String (numDrivers) + (numDrivers == 1 ? " channel" : " channels")
                    + "  |  usable band, resonances, arrival, polarity, SNR and THD per output",
                    r, juce::Justification::centredLeft, true);
    }

private:
    ScoreStrip scores;
    BeforeAfterExplorer explorer;
    BandTable table;
    DriverCardGrid drivers;
    DistortionOverview distortion;
    juce::Rectangle<int> driversLabel;
    int numDrivers = 0;
};

//==============================================================================
InsightsPage::InsightsPage (Session& s) : Page (s), content (std::make_unique<Content>())
{
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addChildComponent (viewport);
    addChildComponent (shimmer);
    addChildComponent (empty);

    empty.onAction = [this] { if (shell != nullptr) shell->showPage (PageId::autoTune); };
    content->onExport = [this] { exportCsv(); };

    refresh();
}

InsightsPage::~InsightsPage()
{
    alive->store (false);
}

void InsightsPage::pageShown()      { refresh(); }
void InsightsPage::sessionChanged() { refresh(); }

void InsightsPage::refresh()
{
    const bool has = session.hasTuneResult();
    if (has)
    {
        const int stamp = resultStamp (session.getTuneResult());
        if (stamp != shownStamp && ! (computing && stamp == computingStamp))
            startCompute (stamp);
    }
    else
    {
        ++generation;                 // drop any result still being computed
        computing = false;
        haveContent = false;
    }

    viewport.setVisible (has && haveContent && ! computing);
    shimmer.setVisible (has && computing);
    empty.setVisible (! has);
    resized();
    repaint();
}

void InsightsPage::startCompute (int stamp)
{
    computing = true;
    computingStamp = stamp;
    const int gen = ++generation;

    auto result = std::make_shared<AutoTuneEngine::Result> (session.getTuneResult());
    const double sweepSeconds = session.getAutoTuneOptions().sweepSeconds;
    double sampleRate = 48000.0;
    if (! result->measurements.empty())
        sampleRate = result->measurements.front().sampleRate;
    const double fStart = 10.0;
    const double fEnd = std::min (24000.0, sampleRate * 0.48);
    auto aliveFlag = alive;

    juce::Thread::launch ([this, result, sweepSeconds, fStart, fEnd, gen, aliveFlag, stamp]
    {
        auto in = std::make_shared<insights::Insights> (insights::compute (*result, sweepSeconds, fStart, fEnd));
        result->measurements.clear();          // the UI only needs curves, config and report

        juce::MessageManager::callAsync ([this, result, in, gen, aliveFlag, stamp]
        {
            if (! aliveFlag->load() || gen != generation)
                return;
            lastInsights = *in;
            content->set (*result, *in);
            shownStamp = stamp;
            computing = false;
            haveContent = true;
            viewport.setViewPosition (0, 0);
            refresh();
        });
    });
}

void InsightsPage::exportCsv()
{
    if (! haveContent)
        return;

    auto name = session.getConfig().name.trim();
    if (name.isEmpty())
        name = "FalseAudio tune";
    const auto base = juce::File::createLegalFileName (name) + "_bands_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M");
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    using Flags = juce::FileBrowserComponent;
    chooser = std::make_unique<juce::FileChooser> ("Save band table as CSV", docs.getChildFile (base + ".csv"), "*.csv");
    chooser->launchAsync (Flags::saveMode | Flags::canSelectFiles | Flags::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;
        const bool ok = f.replaceWithText (insights::bandsToCsv (lastInsights));
        if (shell != nullptr)
            shell->toast (ok ? "Band table saved to " + f.getFileName() : "Could not write " + f.getFullPathName(),
                          ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
    });
}

void InsightsPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void InsightsPage::resized()
{
    auto area = getLocalBounds().reduced (theme::padLg, theme::pad);
    empty.setBounds (area);
    shimmer.setBounds (area);
    viewport.setBounds (area);
    const int w = viewport.getMaximumVisibleWidth();
    content->setSize (w, std::max (viewport.getHeight(), content->idealHeight (w)));
}
} // namespace fa::ui
