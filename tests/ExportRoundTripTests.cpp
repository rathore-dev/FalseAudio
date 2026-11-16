// Results exports for every topology preset: text sheet, REW, Equalizer APO and JSON preset all
// produce non-empty files on disk, and the JSON preset / saved session restore the same system.

#include "TestRegistry.h"
#include "core/Session.h"
#include "core/bridge/Exporters.h"

using namespace fa;
using fa::test::check;

FA_TEST (exportsAndSessionRoundTripAllPresets)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("fa_export_test");
    dir.deleteRecursively();
    dir.createDirectory();

    for (int p = 0; p <= (int) SystemConfig::Preset::singleDriver; ++p)
    {
        const auto cfg = SystemConfig::makePreset ((SystemConfig::Preset) p);
        const auto name = SystemConfig::presetName ((SystemConfig::Preset) p);

        auto txt = dir.getChildFile ("sheet" + juce::String (p) + ".txt");
        check (txt.replaceWithText (exporters::toText (cfg)) && txt.getSize() > 100, name + ": text sheet written");

        auto apo = dir.getChildFile ("apo" + juce::String (p) + ".txt");
        check (apo.replaceWithText (exporters::toEqualizerAPO (cfg)) && apo.getSize() > 0, name + ": Equalizer APO written");

        int rewOk = 0;
        for (auto& ch : cfg.channels)
            if (exporters::toREW (ch).isNotEmpty())
                ++rewOk;
        check (rewOk == (int) cfg.channels.size(), name + ": REW filters for every channel");

        auto json = dir.getChildFile ("preset" + juce::String (p) + ".json");
        check (json.replaceWithText (exporters::toJSON (cfg)), name + ": JSON preset written");
        SystemConfig back;
        check (exporters::fromJSON (json.loadFileAsString(), back), name + ": JSON preset imports");
        check (back.channels.size() == cfg.channels.size(), name + ": imported channel count matches",
               juce::String ((int) back.channels.size()) + " vs " + juce::String ((int) cfg.channels.size()));

        Session s;
        s.setConfig (cfg);
        auto f = dir.getChildFile ("s" + juce::String (p) + ".fasession");
        juce::String err;
        check (s.saveSession (f, err), name + ": session saved", err);
        Session s2;
        check (s2.loadSession (f, err), name + ": session opens", err);
        check (s2.getConfig().channels.size() == cfg.channels.size(), name + ": session restores channels");
    }
    dir.deleteRecursively();
}
