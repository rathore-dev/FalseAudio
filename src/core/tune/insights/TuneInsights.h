#pragma once

#include "../AutoTuneEngine.h"

namespace fa::insights
{
/** Per-frequency before/after table and per-driver diagnostics computed from a tune result. */
struct BandRow
{
    double freqHz = 0;               // 1/3-octave centre
    double targetDb = 0;
    double leftBeforeDb = 0, leftAfterDb = 0;
    double rightBeforeDb = 0, rightAfterDb = 0;
    double sumAfterDb = 0;
    double eqAppliedLeftDb = 0, eqAppliedRightDb = 0;   // net EQ (all channels of that side)
};

struct DriverDiagnostics
{
    juce::String channelId;
    double usableLowHz = 0, usableHighHz = 0;    // -6 dB points of the raw driver
    double peakThdPercent = 0, thdAt100HzPercent = 0;
    std::vector<std::pair<double, double>> thdCurve;   // (Hz, %) within passband
    std::vector<std::pair<double, double>> resonances; // (Hz, dB) strong peaks found
    double arrivalMs = 0;
    bool polarityReversed = false;
    double snrDb = 0;
    juce::StringArray findings;                       // plain-language notes
};

struct Insights
{
    std::vector<BandRow> bands;
    std::vector<DriverDiagnostics> drivers;
    double tonalBalanceScore = 0;      // 0..100 vs target
    double imagingScore = 0;           // 0..100 from L/R match + sum dips
    double cleanlinessScore = 0;       // 0..100 from distortion + noise
    juce::StringArray summary;
};

/** sweepSeconds/fStart/fEnd describe how the measurements were captured (for distortion). */
Insights compute (const AutoTuneEngine::Result& result, double sweepSeconds, double fStart, double fEnd);

/** CSV export of the band table (for spreadsheets). */
juce::String bandsToCsv (const Insights& in);
} // namespace fa::insights
