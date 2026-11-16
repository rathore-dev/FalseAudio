#include "ResultsPage.h"
#include "insights/ScoreStrip.h"
#include "insights/BeforeAfterExplorer.h"
#include "insights/BandTable.h"
#include "insights/DriverCards.h"
#include "insights/DistortionOverview.h"

namespace fa::ui
{
namespace
{
    constexpr int toolbarH = 44;
    constexpr int sectionLabelH = 28;
    constexpr int exportLabelW = 60;

    int resultStamp (const AutoTuneEngine::Result& r)
    {
        return (int) r.measurements.size() * 7919 + r.score * 131 + (int) (r.seconds * 100.0)
               + (int) r.report.notes.size() + (int) r.tunedConfig.channels.size() * 17;
    }
}

//==============================================================================
/** Score strip, before/after explorer, band table, driver cards and distortion. */
class ResultsPage::Analysis : public juce::Component
{
public:
    Analysis()
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
        h += distortion.idealHeight (w);
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
/** The scrolled column: analysis on top, the tune sheet (settings, findings, notes) below. */
class ResultsPage::Body : public juce::Component
{
public:
    Body (Analysis& a, ReportDocument& d) : analysis (a), document (d)
    {
        addAndMakeVisible (analysis);
        addAndMakeVisible (document);
    }

    int idealHeight (int w) const
    {
        return analysis.idealHeight (w) + insightsui::gap + document.idealHeight (w) + theme::pad;
    }

    void resized() override
    {
        auto a = getLocalBounds();
        const int w = a.getWidth();
        analysis.setBounds (a.removeFromTop (analysis.idealHeight (w)));
        a.removeFromTop (insightsui::gap);
        document.setBounds (a.removeFromTop (document.idealHeight (w)));
    }

private:
    Analysis& analysis;
    ReportDocument& document;
};

//==============================================================================
ResultsPage::ResultsPage (Session& s)
    : Page (s), analysis (std::make_unique<Analysis>()), document (s)
{
    document.setDetailsOnly (true);
    body = std::make_unique<Body> (*analysis, document);

    viewport.setViewedComponent (body.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addChildComponent (viewport);
    addChildComponent (shimmer);
    addChildComponent (empty);

    empty.onAction = [this] { if (shell != nullptr) shell->showPage (PageId::autoTune); };
    analysis->onExport = [this] { exportCsv(); };

    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton, &sessionButton })
        addChildComponent (b);
    textButton.setTooltip ("Every channel's settings as a plain-text sheet to type into any DSP");
    rewButton.setTooltip ("One REW filter file per output");
    apoButton.setTooltip ("Equalizer APO config for the whole tune");
    jsonButton.setTooltip ("FalseAudio preset (JSON) - load it again with Import preset");
    sessionButton.setTooltip ("Save the settings and measurements as a session file");
    openButton.setTooltip ("Open a saved FalseAudio session");
    importButton.setTooltip ("Load a FalseAudio JSON preset into the system config");
    textButton.onClick    = [this] { exportAs (Export::text); };
    rewButton.onClick     = [this] { exportAs (Export::rew); };
    apoButton.onClick     = [this] { exportAs (Export::apo); };
    jsonButton.onClick    = [this] { exportAs (Export::json); };
    sessionButton.onClick = [this] { exportAs (Export::session); };

    addAndMakeVisible (openButton);
    addAndMakeVisible (importButton);
    openButton.onClick   = [this] { openSession(); };
    importButton.onClick = [this] { importPreset(); };

    refresh();
}

ResultsPage::~ResultsPage()
{
    alive->store (false);
}

void ResultsPage::pageShown()      { refresh(); }
void ResultsPage::sessionChanged() { refresh(); }

void ResultsPage::refresh()
{
    hasResult = session.hasTuneResult();
    if (hasResult)
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
        shownStamp = -1;
    }

    viewport.setVisible (hasResult && haveContent && ! computing);
    shimmer.setVisible (hasResult && computing);
    empty.setVisible (! hasResult);
    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton, &sessionButton })
        b->setVisible (hasResult);
    resized();
    repaint();
}

void ResultsPage::startCompute (int stamp)
{
    computing = true;
    computingStamp = stamp;
    const int gen = ++generation;

    document.setResult (session.getTuneResult());

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
            analysis->set (*result, *in);
            shownStamp = stamp;
            computing = false;
            haveContent = true;
            viewport.setViewPosition (0, 0);
            refresh();
        });
    });
}

void ResultsPage::exportCsv()
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

void ResultsPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    if (! hasResult)
        return;

    auto bar = getLocalBounds().reduced (theme::padLg, theme::pad).removeFromTop (toolbarH);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    g.drawText ("EXPORT", bar.removeFromLeft (exportLabelW), juce::Justification::centredLeft);
}

void ResultsPage::resized()
{
    auto area = getLocalBounds().reduced (theme::padLg, theme::pad);
    empty.setBounds (area.withTrimmedTop (toolbarH));

    auto bar = area.removeFromTop (toolbarH);
    bar.removeFromLeft (exportLabelW);
    constexpr int rbw = 132;
    openButton.setBounds (bar.removeFromRight (rbw).withSizeKeepingCentre (rbw, 34));
    bar.removeFromRight (8);
    importButton.setBounds (bar.removeFromRight (rbw).withSizeKeepingCentre (rbw, 34));
    bar.removeFromRight (8);
    constexpr int sw = 136;
    sessionButton.setBounds (bar.removeFromRight (sw).withSizeKeepingCentre (sw, 34));
    bar.removeFromRight (theme::pad);
    const int bw = std::max (80, std::min (148, (bar.getWidth() - 3 * 8) / 4));
    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton })
    {
        b->setBounds (bar.removeFromLeft (bw).withSizeKeepingCentre (bw, 34));
        bar.removeFromLeft (8);
    }

    area.removeFromTop (theme::pad / 2);
    shimmer.setBounds (area);
    viewport.setBounds (area);
    const int w = std::max (320, viewport.getWidth() - viewport.getScrollBarThickness() - 6);
    body->setSize (w, std::max (viewport.getHeight(), body->idealHeight (w)));
}

//==============================================================================
juce::String ResultsPage::defaultBaseName() const
{
    auto name = session.getConfig().name.trim();
    if (name.isEmpty())
        name = "FalseAudio tune";
    return juce::File::createLegalFileName (name) + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M");
}

void ResultsPage::writeAndToast (const juce::File& f, const juce::String& content, const juce::String& what)
{
    if (f == juce::File())
        return;
    const bool ok = f.replaceWithText (content);
    if (shell != nullptr)
        shell->toast (ok ? what + " saved to " + f.getFileName() : "Could not write " + f.getFullPathName(),
                      ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
}

void ResultsPage::openSession()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Open a FalseAudio session", docs, "*.fasession");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;
        juce::String error;
        const bool ok = session.loadSession (f, error);
        if (shell != nullptr)
            shell->toast (ok ? "Session loaded from " + f.getFileName() : error,
                          ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
    });
}

void ResultsPage::importPreset()
{
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Import a FalseAudio preset", docs, "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;
        SystemConfig cfg;
        const bool ok = exporters::fromJSON (f.loadFileAsString(), cfg);
        if (ok)
            session.setConfig (cfg);
        if (shell != nullptr)
            shell->toast (ok ? "Preset " + f.getFileName() + " loaded into the system config"
                             : f.getFileName() + " is not a FalseAudio preset",
                          ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
    });
}

void ResultsPage::exportAs (Export kind)
{
    if (! session.hasTuneResult())
        return;

    const auto& cfg = session.getTuneResult().tunedConfig;
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto base = defaultBaseName();

    using Flags = juce::FileBrowserComponent;
    const int saveFlags = Flags::saveMode | Flags::canSelectFiles | Flags::warnAboutOverwriting;

    switch (kind)
    {
        case Export::text:
            chooser = std::make_unique<juce::FileChooser> ("Save DSP settings sheet", docs.getChildFile (base + "_settings.txt"), "*.txt");
            chooser->launchAsync (saveFlags, [this, cfg] (const juce::FileChooser& fc)
            {
                writeAndToast (fc.getResult(), exporters::toText (cfg), "Settings sheet");
            });
            break;

        case Export::rew:
            chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the REW filter files (one per output)", docs);
            chooser->launchAsync (Flags::openMode | Flags::canSelectDirectories, [this, cfg, base] (const juce::FileChooser& fc)
            {
                const auto dir = fc.getResult();
                if (dir == juce::File())
                    return;
                int written = 0;
                for (auto& ch : cfg.channels)
                {
                    const auto f = dir.getChildFile (base + "_" + juce::File::createLegalFileName (ch.id) + "_REW.txt");
                    if (f.replaceWithText (exporters::toREW (ch)))
                        ++written;
                }
                if (shell != nullptr)
                    shell->toast (juce::String (written) + " REW filter files saved to " + dir.getFileName(),
                                  written == (int) cfg.channels.size() ? AppShell::ToastKind::good : AppShell::ToastKind::warn);
            });
            break;

        case Export::apo:
            chooser = std::make_unique<juce::FileChooser> ("Save Equalizer APO config", docs.getChildFile (base + "_EqualizerAPO.txt"), "*.txt");
            chooser->launchAsync (saveFlags, [this, cfg] (const juce::FileChooser& fc)
            {
                writeAndToast (fc.getResult(), exporters::toEqualizerAPO (cfg), "Equalizer APO config");
            });
            break;

        case Export::json:
            chooser = std::make_unique<juce::FileChooser> ("Save FalseAudio preset", docs.getChildFile (base + ".fatune.json"), "*.json");
            chooser->launchAsync (saveFlags, [this, cfg] (const juce::FileChooser& fc)
            {
                writeAndToast (fc.getResult(), exporters::toJSON (cfg), "Preset");
            });
            break;

        case Export::session:
            chooser = std::make_unique<juce::FileChooser> ("Save session (settings + measurements)", docs.getChildFile (base + ".fasession"), "*.fasession");
            chooser->launchAsync (saveFlags, [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f == juce::File())
                    return;
                juce::String error;
                const bool ok = session.saveSession (f, error);
                if (shell != nullptr)
                    shell->toast (ok ? "Session saved to " + f.getFileName() : error,
                                  ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
            });
            break;
    }
}
} // namespace fa::ui
