#include "ExportPanel.h"

namespace fa::ui::dsp
{
ExportPanel::ExportPanel (Session& s) : session (s)
{
    struct Row { FaButton* b; Kind k; const char* tip; };
    const Row rows[] {
        { &text,    Kind::text,    "Human-readable sheet with every value per output - type it into any DSP." },
        { &rew,     Kind::rew,     "REW filter settings, one file per channel (import in REW's EQ window)." },
        { &apo,     Kind::apo,     "Equalizer APO configuration for PC-based systems." },
        { &miniDsp, Kind::miniDsp, "miniDSP-style biquad coefficients, one file per channel (crossovers + PEQ)." },
        { &json,    Kind::json,    "Complete FalseAudio preset (JSON) - reload it later or share it." },
    };
    for (auto& r : rows)
    {
        r.b->setTooltip (r.tip);
        r.b->onClick = [this, k = r.k] { run (k); };
        addAndMakeVisible (r.b);
    }
}

void ExportPanel::paint (juce::Graphics& g)
{
    drawSectionLabel (g, "Export tune", getLocalBounds().removeFromTop (18));
}

void ExportPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    const int gap = 6;
    const int w = (r.getWidth() - gap) / 2;

    auto row = r.removeFromTop (theme::controlHeight);
    text.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); rew.setBounds (row);
    r.removeFromTop (gap);
    row = r.removeFromTop (theme::controlHeight);
    apo.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); miniDsp.setBounds (row);
    r.removeFromTop (gap);
    row = r.removeFromTop (theme::controlHeight);
    json.setBounds (row.removeFromLeft (w));
}

void ExportPanel::report (bool ok, const juce::String& what)
{
    if (toast)
        toast (ok ? "Exported " + what : "Could not write " + what, ok ? AppShell::ToastKind::good : AppShell::ToastKind::bad);
}

void ExportPanel::run (Kind k)
{
    const auto base = juce::File::createLegalFileName (session.getConfig().name);
    switch (k)
    {
        case Kind::text:
            saveSingle ("Save tune sheet", base + " - tune sheet.txt", "*.txt", [this] { return exporters::toText (session.getConfig()); });
            break;
        case Kind::rew:
            savePerChannel ("Choose a folder for the REW filter files", "txt", [] (const OutputChannel& ch) { return exporters::toREW (ch); });
            break;
        case Kind::apo:
            saveSingle ("Save Equalizer APO config", base + " - EqualizerAPO.txt", "*.txt", [this] { return exporters::toEqualizerAPO (session.getConfig()); });
            break;
        case Kind::miniDsp:
            savePerChannel ("Choose a folder for the miniDSP biquad files", "txt",
                            [this] (const OutputChannel& ch) { return exporters::toMiniDSPBiquads (ch, session.getConfig().caps.sampleRate); });
            break;
        case Kind::json:
            saveSingle ("Save FalseAudio preset", base + ".falseaudio.json", "*.json", [this] { return exporters::toJSON (session.getConfig()); });
            break;
    }
}

void ExportPanel::saveSingle (const juce::String& title, const juce::String& suggestedName, const juce::String& pattern,
                              std::function<juce::String()> makeText)
{
    auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (suggestedName);
    chooser = std::make_unique<juce::FileChooser> (title, start, pattern);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, makeText] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              report (f.replaceWithText (makeText()), f.getFileName());
                          });
}

void ExportPanel::savePerChannel (const juce::String& title, const juce::String& extension,
                                  std::function<juce::String (const OutputChannel&)> makeText)
{
    chooser = std::make_unique<juce::FileChooser> (title, juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, makeText, extension] (const juce::FileChooser& fc)
                          {
                              auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              const auto base = juce::File::createLegalFileName (session.getConfig().name);
                              int written = 0;
                              bool ok = true;
                              for (auto& ch : session.getConfig().channels)
                              {
                                  auto f = dir.getChildFile (base + " - " + juce::File::createLegalFileName (ch.label) + "." + extension);
                                  ok = f.replaceWithText (makeText (ch)) && ok;
                                  ++written;
                              }
                              report (ok, juce::String (written) + " channel files to " + dir.getFileName());
                          });
}
} // namespace fa::ui::dsp
