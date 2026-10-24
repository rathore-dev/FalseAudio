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
- `driver` (speakers/subs): sizeInches, nominalOhms, reOhms, fsHz, qts, qes, qms, vasLitres, xmaxMm, sdCm2,
  bl, mmsGrams, leMh, sensitivityDb, sensitivityRef ("1W/1m" | "2.83V/1m"), powerRmsW, powerPeakW,
  freqLowHz, freqHighHz, voiceCoils, coilOhms, mountingDepthMm, cutoutMm, recommendedHpHz, recommendedLpHz
- `amp`: channels, rmsPerChannelW {"4ohm","2ohm","1ohm","bridged4ohm","bridged2ohm"}, inputSensitivityMinV,
  inputSensitivityMaxV, highLevelInputMaxV, snrDb, thdPercent, dampingFactor, fuseRatingA,
  efficiencyPercent, amplifierClass, minStableOhms, hasBassBoost, hasLpf, hasHpf, hasSubsonic, bassBoostMaxDb
- `source` (head unit / DSP / DAC): preoutVoltageV, preoutChannels, outputImpedanceOhms, maxVolumeStep,
  cleanVolumeStep, snrDb, timeAlignment, crossovers, eqBands, eqType, inputs, outputs,
  peqBandsPerChannel, maxDelayMs, sampleRateHz, maxInputVoltageV, software, softwareExe
- `wire`: awg (0 = 1/0, -1 = 2/0), mm2, material ("OFC" | "CCA" | "Tinned OFC"), ohmsPerMetre, ampacityA
- `audioInterface`: inputs, outputs, micPreamps, phantomPower, hasLoopback, asio, maxGainDb, maxOutputDbu,
  sampleRates [..], connection
- `mic`: type ("USB" | "XLR"), needsPhantom, sensitivityMvPa, calFileAvailable, calFileUrl, maxSplDb

## Rules for contributors
- Real products only, specs from the manufacturer's page/manual (preferred) or reputable retailers;
  put the URL in `sourceUrl`; `verified: true` only for manufacturer sources.
- Unknown = omit the field. Do not guess values.
- `id`: lowercase `<brand>-<model>` with non-alphanumerics replaced by `-`.
- Valid JSON (no comments, no trailing commas), UTF-8.
