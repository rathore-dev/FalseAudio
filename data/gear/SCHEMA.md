# FalseAudio equipment database

One JSON file per brand per category: `data/gear/<category>/<brand-slug>.json`

```json
{
  "products": [
    {
      "id": "jl-audio-vx800-8i",
      "category": "dspAmp",
      "brand": "JL Audio",
      "model": "VX800/8i",
      "series": "VXi",
      "year": 2019,
      "sourceUrl": "https://www.jlaudio.com/...",
      "verified": true,
      "notes": "",
      "amp":    { "channels": 8, "rmsPerChannelW": { "4ohm": 75, "2ohm": 100 },
                  "inputSensitivityMinV": 0.2, "inputSensitivityMaxV": 8.0, "highLevelInputMaxV": 16,
                  "snrDb": 100, "thdPercent": 0.05, "fuseRatingA": 60, "efficiencyPercent": 80,
                  "amplifierClass": "D", "minStableOhms": 2 },
      "source": { "inputs": 8, "outputs": 8, "peqBandsPerChannel": 10, "maxDelayMs": 10,
                  "sampleRateHz": 48000, "software": "TüN", "softwareExe": "TuN.exe" }
    }
  ]
}
```

## Categories (folder names)
headUnit, dsp, dspAmp, dac, ampMulti, ampMono, driverTweeter, driverMidrange, driverMidbass,
driverCoax, componentSet, subwoofer, powerWire, speakerWire, rca, fuseHolder, battery, alternator,
audioInterface, measurementMic

## Spec blocks (include only those that apply; omit unknown fields — never invent numbers)
All fields are optional unless the app's custom-product form marks them required. Every field added later
(marked *new*) is optional and older files load unchanged.

- `driver` (speakers/subs): sizeInches, nominalOhms, reOhms, fsHz, qts, qes, qms, vasLitres, xmaxMm, sdCm2,
  bl, mmsGrams, leMh, sensitivityDb, sensitivityRef ("1W/1m" | "2.83V/1m"), powerRmsW, powerPeakW,
  freqLowHz, freqHighHz, voiceCoils, coilOhms, mountingDepthMm, cutoutMm, recommendedHpHz, recommendedLpHz
  - `reOhms` = DC resistance of ONE coil (also for dual-voice-coil subs). `coilOhms` = nominal per coil.
  - *new* `tsWiring`: "single" | "series" | "parallel" — how the coils were wired when the published
    Re / Le / BL (and the other T/S values) were measured. "single" = SVC or one coil of a DVC.
  - *new* `switchableImpedance`: free text, e.g. "2 or 4 ohm (JBL SSI)". Informational; still give
    nominalOhms for the default setting and explain in `notes`.
  - *new* `cutoutShape`: "round" (default when omitted) | "square". For square, `cutoutMm` = side length.
  - *new* `secondarySensitivityDb` + `secondarySensitivityRef`: a second published sensitivity
    (e.g. maker gives both 1W/1m and 2.83V/1m). Put the primary figure in sensitivityDb/sensitivityRef.
  - *new* `boxRecommendations`: `[ { "type": "sealed" | "ported", "litres": 28, "tuningHz": 32 } ]`
    (net litres; tuningHz only for ported). One entry per published recommendation.
- `amp`: channels, rmsPerChannelW {"4ohm","3ohm","2ohm","1ohm","0.5ohm","bridged4ohm","bridged2ohm"},
  inputSensitivityMinV, inputSensitivityMaxV, highLevelInputMaxV, snrDb, thdPercent, dampingFactor,
  fuseRatingA, efficiencyPercent, amplifierClass, minStableOhms, hasBassBoost, hasLpf, hasHpf, hasSubsonic,
  bassBoostMaxDb
  - `channels` = TOTAL output channels (a 4+1 five-channel amp = 5).
  - `rmsPerChannelW` / `minStableOhms` = the MAIN channel group (the first output channels, e.g. the 4 mains).
    *new* keys "3ohm" and "0.5ohm" are accepted (any "<ohms>ohm" / "bridged<ohms>ohm" key parses).
  - *new* `channelGroups`: the OTHER channel groups with their own ratings, in output order after the main
    group: `[ { "channels": 1, "rmsPerChannelW": { "4ohm": 300, "2ohm": 500 }, "minStableOhms": 2, "label": "sub" } ]`.
    The main group then has `channels - sum(channelGroups.channels)` channels. Omit for amps whose
    channels are all identical.
  - `inputSensitivityMinV` / `inputSensitivityMaxV` = the RCA / low-level gain range (always fill when known).
  - *new* `inputSensitivityRanges`: every published range, e.g. dual-range or high-level inputs:
    `[ { "minV": 0.2, "maxV": 4, "label": "low" }, { "minV": 0.5, "maxV": 8, "label": "high" } ]`.
  - `fuseRatingA` = TOTAL on-board fuse rating; *new* `fuseCount` = how many on-board fuses (2 x 30 A ->
    fuseRatingA 60, fuseCount 2).
  - *new* `recommendedFuseA`: external in-line fuse (at the battery) the manual recommends.
  - *new* `ratedVoltageV`: supply voltage the RMS ratings were measured at (e.g. 14.4, 13.8, 12.6).
- `source` (head unit / DSP / DAC): preoutVoltageV, preoutChannels, outputImpedanceOhms, maxVolumeStep,
  cleanVolumeStep, snrDb, timeAlignment, crossovers, eqBands, eqType, inputs, outputs,
  peqBandsPerChannel, maxDelayMs, sampleRateHz, maxInputVoltageV, software, softwareExe
  - `preoutChannels` = TOTAL pre-out channels (front + rear + sub pairs = 6), not pairs.
  - `cleanVolumeStep` = last volume step before the pre-out clips with a 0 dBFS signal and EQ/tone flat.
  - `snrDb` = the manufacturer's analog (or USB) source SNR figure.
  - `peqBandsPerChannel` = bands per OUTPUT channel (what auto-EQ uses).
  - *new* `peqBandsByStage`: `{ "input": 10, "output": 31, "virtual": 10 }` when bands differ per stage;
    `peqBandsPerChannel` is filled from `output` when omitted.
  - *new* `digitalInputs`: number of digital inputs (optical / coax / USB audio). An array of types
    (`["optical", "coax"]`) is also accepted; or give the types in *new* `digitalInputTypes`.
  - *new* `highLevelInputs`: number of speaker-level input channels.
  - *new* `snrDigitalDb`: SNR via the digital input (when the maker publishes both).
  - *new* `preoutRatedLoadOhms`: load the preoutVoltageV figure was rated into (e.g. 10000).
  - *new* `outputImpedanceIsUpperBound`: true when the manual gives outputImpedanceOhms as a maximum ("< 200 ohm").
  - *new* `delayRangeMs`: `[min, max]` per-channel delay range (a single number = max). `maxDelayMs` is
    filled from max when omitted.
- `wire`: awg (0 = 1/0, -1 = 2/0), mm2, material ("OFC" | "CCA" | "Tinned OFC"), ohmsPerMetre, ampacityA
- `audioInterface`: inputs, outputs, micPreamps, phantomPower, hasLoopback, asio, maxGainDb, maxOutputDbu,
  sampleRates [..], connection
- `mic`: type ("USB" | "XLR" | *new* "TRRS"), needsPhantom, sensitivityMvPa, calFileAvailable, calFileUrl,
  maxSplDb, *new* `sensitivityDbfs` (USB mics such as UMIK-1: dBFS at 94 dB SPL, negative, e.g. -18)
- *new* `battery`: ah (20 h rate), cca, ca, reserveMin (reserve capacity, minutes), maxAmps,
  chemistry ("AGM" | "flooded" | "LiFePO4" | "LTO")
- *new* `alternator`: ratedA (headline), idleA (at idle), hotRatedA
- *new* `fuse` (fuseHolder products, and power-wire kits that include a fuse): ratingA,
  type ("ANL" | "mini-ANL" | "MIDI" | "MAXI" | "AGU" | "ATC" | "breaker")

Example 5-channel amp:
```json
"amp": { "channels": 5, "rmsPerChannelW": { "4ohm": 75, "2ohm": 100 }, "minStableOhms": 2,
         "channelGroups": [ { "channels": 1, "label": "sub", "rmsPerChannelW": { "4ohm": 300, "2ohm": 500, "1ohm": 600 },
                              "minStableOhms": 1 } ],
         "inputSensitivityMinV": 0.2, "inputSensitivityMaxV": 4,
         "inputSensitivityRanges": [ { "minV": 0.2, "maxV": 4, "label": "low" }, { "minV": 2, "maxV": 16, "label": "high" } ],
         "fuseRatingA": 80, "fuseCount": 2, "recommendedFuseA": 100, "ratedVoltageV": 14.4 }
```

## Rules for contributors
- Real products only, specs from the manufacturer's page/manual (preferred) or reputable retailers;
  put the URL in `sourceUrl`; `verified: true` only for manufacturer sources.
- Unknown = omit the field. Do not guess values.
- `id`: lowercase `<brand>-<model>` with non-alphanumerics replaced by `-`.
- Valid JSON (no comments, no trailing commas), UTF-8.
