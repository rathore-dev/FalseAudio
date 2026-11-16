# DSP vendor software: automation routes

Source data: `vendors.json`. Researched 2026-10-24 (worker R8). Fields set to `unknown` could not be confirmed from a source. Spec values come from retailer pages or reviews where no manual was available, so check them against the vendor tool before you rely on them.

## Routes

| Route | Meaning |
|---|---|
| `protocol` | FalseAudio talks to the DSP directly over USB HID, serial or TCP, using a reverse-engineered protocol that has open-source reference code. No vendor GUI is needed. |
| `presetFile` | FalseAudio generates a file that the user (or UIA) loads in the vendor tool. |
| `uia` | Embed the vendor's Windows tool and drive it with a taught `DspProfile`. Where the tool can import REW filter files, use the **hybrid** form: write a REW filter text file, then drive only the import dialog plus crossover and delay fields with UIA. |
| `manual` | There is no Windows tool (phone app only, or the device tunes itself). FalseAudio shows a numeric tuning sheet that the user types in. |

No vendor documents a preset or config file format. Because of that, `presetFile` is not a primary route for any vendor yet. The closest thing is **REW filter-text import**, which works in miniDSP, Helix PC-Tool (REW Generic EQ) and Audison bit Drive 2.0.

## Ranking: popularity × feasibility

| # | Vendor / tool | Route | Why |
|---|---|---|---|
| 1 | **miniDSP** (2x4 HD, C-DSP 8x12, Flex, etc.) | **protocol: native integration** | `mrene/minidsp-rs` (Apache-2.0, 172★) is mature. It covers gain, routing, PEQ biquad import, crossover biquads, delay, invert, FIR and compressor over USB HID or TCP:5333. Sending biquads directly avoids the Q-range and UI limits. Caveat: the official plugin cannot read back filter or delay changes made this way. |
| 2 | **Helix / Brax / Match: DSP PC-Tool** (V5.07.01; V6 announced) | **uia + REW import (hybrid)** | Biggest high-end ecosystem: one tool covers about 30 models. The app is Qt5, so UIA coverage may be partial and some widgets may need coordinate or keyboard fallback. Imports REW Generic EQ. Presets are `.afpx`, format undocumented. A serial protocol (`helix-unwound`) exists but has not been verified on hardware, so watch it as a future native route. |
| 3 | **Audison bit Drive 2.0** (bit One HD / Virtuoso, bit Ten…) | **uia + REW import (hybrid)** | Very popular. bit Drive 2.0 imports REW EQ and has built-in ACCORDO auto-tune. Framework and file format are unknown, so inspect with Inspect.exe or FlaUI first. |
| 4 | **Dayton Audio DSP-408** | **protocol: native integration** | A popular budget DSP whose protocol is fully decoded and proven on hardware (`malaiwah/dsp408-py` over USB HID; `nick0451/dsp408-tuner` over BT RFCOMM, with closed-loop tuning and rollback). Every write is non-volatile, so take a snapshot first. PEQ: 10 bands per channel, Q 0.404–28.85, ±12 dB. Delay: integer samples at 48 kHz, up to about 8.15 ms. |
| 5 | Mosconi / Gladen (6to8, 8to12) | uia | Popular in the EU. The filter engine is very capable (30-band PEQ, Q 0.5–40, slopes 6–120 dB/oct). Software details are unknown. |
| 6 | AudioControl DM-608/810 (DM Smart DSP) | uia | Popular in the US. 30-band EQ, LR 12/24 crossovers. PC and Mac builds. |
| 7 | JL Audio TüN (FiX / TwK) | uia | Popular, but the desktop tool is older. Drive it in Expert mode to get parametric EQ. |
| 8 | ARC Audio Pro-Series, Musway, Phoenix Gold, Alpine PXE-C80/C60 (PC app 1.4), Focal FSP-8 DSP Manager, Audison Prima / Hertz, Zapco DSP-Z8 IV | uia | Smaller user bases. Each has a Windows tool, but details are mostly unknown. Teach profiles on demand. |
| 9 | Generic Chinese HID DSP amps (Audiobeat OctaPro / SP601 OEM) | protocol (opportunistic) | One reverse-engineered OEM platform (`nikakoy131/audiobeat-octapro-8-2-reverse`) is probably rebadged across many brands. |
| 10 | Rockford DSR1 (PerfectTune, BT), Kenwood DSP Control, Taramps PRO BT, Banda X8AiR | manual | Phone app only, with no Windows tool. Give the user a tuning sheet fitted to the device grid, for example 15-band graphic for Taramps and 13-band for Kenwood. |
| 11 | Kicker KEY | manual (measure only) | The amp tunes itself with its own mic, so there is nothing to drive. |
| 12 | Dirac Live (miniDSP C-DSP 8x12 DL) | manual + miniDSP protocol | Dirac does its own correction. Handle crossovers and delay through the miniDSP route and either let Dirac do EQ or bypass it. |
| — | Sundown | unknown | No DSP software information found. |

## Dedicated native integrations worth building

1. **miniDSP via minidsp-rs.** Bundle the binary or run `minidsp server` and talk TCP:5333 or HTTP, or port the HID protocol to C++. Highest value per hour of work.
2. **Dayton DSP-408 via USB HID** (port `dsp408-py`). Small, well documented, and has a large DIY and budget user base.
3. **REW-format filter export** (shared component). One writer for REW "Generic" PEQ text and miniDSP biquad text would serve miniDSP, Helix PC-Tool and Audison bit Drive at once.
4. *(Later)* **Helix serial protocol**, once `helix-unwound` is verified on hardware.

## Notes for UIA profiles

- **Qt5 apps (Helix PC-Tool):** turn on accessibility and expect graph and knob widgets to be opaque. Prefer keyboard entry into numeric spin boxes.
- **Legacy miniDSP plugins (Adobe/Harman AIR):** these expose almost nothing to UIA. This is another reason to use the protocol route.
- **Profiles should record unit conventions per tool:** delay in ms, cm or samples, the Q definition (for example, RBJ half-gain Q on the DSP-408), and the sample rate (miniDSP 2x4 HD is 96 kHz; DSP-408 is 48 kHz). Get these wrong and filters land in the wrong place.

## Official download pages and install detection

Each vendor has `officialDownloadPage` (vendor's own domain only, or null), `officialDownloadPageVerified` (how it was checked on 2026-11-16) and `install` (`paths` with %ENV% variables, `exeNames`, `uninstallNames` = substrings matched against the Windows Uninstall registry DisplayName). Used by `src/core/DspSoftware.*`. The app only opens the page in the browser; it never downloads or runs installers.

## Official download pages and install detection

Each vendor has `officialDownloadPage` (vendor's own domain only, or null), `officialDownloadPageVerified` (how it was checked on 2026-11-16) and `install` (`paths` with %ENV% variables and forward slashes, `exeNames`, `uninstallNames` = substrings matched against the Windows Uninstall registry DisplayName). Used by `src/core/DspSoftware.*`. The app only opens the page in the browser; it never downloads or runs installers.
