#include "ReportPage.h"

namespace fa::ui
{
namespace
{
    constexpr int toolbarH = 44;

    int resultStamp (const AutoTuneEngine::Result& r)
    {
        return (int) r.measurements.size() * 7919 + r.score * 131 + (int) (r.seconds * 100.0) + (int) r.report.notes.size();
    }
}

ReportPage::ReportPage (Session& s) : Page (s), document (s)
{
    viewport.setViewedComponent (&document, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addChildComponent (viewport);
    addChildComponent (empty);
    empty.onAction = [this] { if (shell != nullptr) shell->showPage (PageId::autoTune); };

    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton, &sessionButton })
        addChildComponent (b);
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

ReportPage::~ReportPage() = default;

void ReportPage::refresh()
{
    const bool has = session.hasTuneResult();
    if (has)
    {
        const int stamp = resultStamp (session.getTuneResult());
        if (! shownResult || stamp != shownResultStamp)
        {
            document.setResult (session.getTuneResult());
            shownResultStamp = stamp;
            viewport.setViewPosition (0, 0);
        }
    }
    shownResult = has;
    viewport.setVisible (has);
    empty.setVisible (! has);
    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton, &sessionButton })
        b->setVisible (has);
    resized();
    repaint();
}

void ReportPage::pageShown()     { refresh(); }
void ReportPage::sessionChanged() { refresh(); }

void ReportPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    if (! shownResult)
        return;

    auto bar = getLocalBounds().reduced (theme::padLg, theme::pad).removeFromTop (toolbarH);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    g.drawText ("EXPORT", bar.removeFromLeft (60), juce::Justification::centredLeft);
}

void ReportPage::resized()
{
    auto area = getLocalBounds().reduced (theme::padLg, theme::pad);
    empty.setBounds (area.withTrimmedTop (toolbarH));

    auto bar = area.removeFromTop (toolbarH);
    bar.removeFromLeft (60);
    constexpr int rbw = 128;
    openButton.setBounds (bar.removeFromRight (rbw).withSizeKeepingCentre (rbw, 34));
    bar.removeFromRight (8);
    importButton.setBounds (bar.removeFromRight (rbw).withSizeKeepingCentre (rbw, 34));
    bar.removeFromRight (8);
    const int bw = std::max (80, std::min (150, (bar.getWidth() - 4 * 8 - 160) / 4));
    for (auto* b : { &textButton, &rewButton, &apoButton, &jsonButton })
    {
        b->setBounds (bar.removeFromLeft (bw).withSizeKeepingCentre (bw, 34));
        bar.removeFromLeft (8);
    }
    sessionButton.setBounds (bar.removeFromRight (std::min (140, bar.getWidth())).withSizeKeepingCentre (std::min (140, bar.getWidth()), 34));

    area.removeFromTop (theme::pad / 2);
    viewport.setBounds (area);
    int w = viewport.getWidth();
    int h = document.idealHeight (w);
    if (h > viewport.getHeight())                         // leave room for the scrollbar only when it shows
    {
        w -= viewport.getScrollBarThickness() + 6;
        h = document.idealHeight (w);
    }
    document.setSize (w, std::max (viewport.getHeight(), h));
}

//==============================================================================
juce::String ReportPage::defaultBaseName() const
{
    auto name = session.getConfig().name.trim();
    if (name.isEmpty())
        name = "FalseAudio tune";
    return juce::File::createLegalFileName (name) + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M");
}

void ReportPage::writeAndToast (const juce::File& f, const juce::String& content, const juce::String& what)
{
    if (f == juce::File())
        return;
    const bool ok = f.replaceWithText (content);
    if (shell != nullptr)
        shell->toast (ok ? what + " saved to " + f.getFileName() : "Could not write " + f.getFullPathName(),
                      ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
}

void ReportPage::openSession()
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

void ReportPage::importPreset()
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

void ReportPage::exportAs (Export kind)
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
