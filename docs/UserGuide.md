# FalseAudio User Guide

FalseAudio tunes a car audio system for you. It plays measurement sweeps through your speakers, records them with a measurement microphone at the listening position, works out the settings your DSP needs, writes them to the DSP (or tells you exactly what to type), and then measures again to check the result.

This guide is for installers and hobbyists. You do not need to be an acoustics expert, but you do need to be comfortable with car audio wiring and with your DSP.

---

## 1. What the app does

* **Measures** every speaker channel with a sweep, using a calibrated microphone.
* **Fixes** polarity, arrival time (delay), crossovers, levels and EQ, working toward a target curve (a "house curve").
* **Checks** the result: left and right sides, the left+right sum, the stereo image, and background noise.
* **Scores** the tune and gives you a report with before/after graphs and a settings sheet.
* **Controls your DSP** in the way that suits your hardware (see section 10), or exports the settings so you can enter them yourself.
* **Helps with the install**: from the equipment you list, it checks wiring and fuses and gives amplifier gain voltages to set with a multimeter.

The app has seven pages in the left rail: **Tune**, **Results**, **Car**, **Install & Gains**, **Rig**, **DSP** and **Expert**. The header has a **Demo / Live** switch.

### Demo mode and Live mode

* **Demo** uses a simulated car, DSP and microphones. Nothing real is touched and no sound is played. It is the best way to learn the app.
* **Live** uses your real audio interface, microphone and DSP. Sweeps are played through your speakers.

Switch with the Demo / Live control at the top right.

---

## 2. Hardware you need

* A Windows PC (64-bit).
* A **measurement microphone**. The app is built around a miniDSP **UMIK-1** (USB). A UMIK-1 plugs straight into the PC and needs no phantom power. XLR measurement microphones (for example an ECM8000 or UCM8000) also work, through an **audio interface** that has mic preamps and 48 V phantom power.
* An **audio interface or sound output** that feeds your DSP's inputs (one output for the left input, one for the right). The Rig page lets you choose any interface.
* Your **DSP** (and the amplifiers and speakers it drives).

### UMIK-1 notes

* Each UMIK-1 has a **serial number** (it looks like `7xx-xxxx`) and ships with its own **calibration files**, one for **0 degrees** and one for **90 degrees**.
* In a car, point the mic **straight up at the roof** and use the **90 degree** file. (If you point it forward, use the 0 degree file.)
* The Rig page can fetch the files for you: enter the serial, press **Open miniDSP page** (the serial is copied to the clipboard), submit it on miniDSP's page, then press **Import downloads**. The app files the downloaded files and loads the one for your chosen angle. You can also import a calibration file you already have.
* Without a calibration file the app assumes a flat microphone. That is fine for tuning, but levels are then relative and not absolute SPL.
* **Windows microphone level.** The app corrects its SPL readings for the Windows recording level, but recommends leaving the UMIK-1 at **0 dB, the Windows default**. A higher level costs headroom, and the level must not be changed between calibration and tuning. The Mic check (section 4) tells you if it is different and offers a **Set to recommended** button; it only changes the level when you click it.

---

## 3. First launch

1. Download the zip and use **Extract All**. Do not run the app from inside the zip.
2. Run `FalseAudio.exe`. Windows SmartScreen may warn that the app is unsigned: click **More info**, then **Run anyway**. The exe is self-contained.
3. The app opens on the **Tune** page. Click **Start Auto Tune** in Demo mode to see the complete workflow on the simulated car before you touch hardware.
4. When you are ready, go to **Rig**, set up your devices, then switch to **Live**.

---

## 4. Rig: devices, microphone, Mic check

Open **Rig**. Its parts:

* **Audio device.** Choose the driver (Windows Audio or an ASIO driver if your interface lists one), the **output device**, the **input device**, the **sample rate** and the **buffer size**. Use **Rescan** after plugging something in. The status line shows whether a device is open and how many channels it has.
  * Run at **48 kHz** (the rate of most car DSPs; the UMIK-1 is fixed at 48 kHz).
  * A relaxed buffer (512 to 1024 samples) is fine: latency does not matter for measuring, dropouts do.
  * Close other audio apps so nothing else holds the device.
* **Routing.** Say which interface output goes to the DSP's left input and which to the right. Add up to four measurement microphones with **Add microphone** and pick the input channel for each. A loopback pair can be set as a timing reference.
* **Measurement gear.** Pick your interface and microphone products from the gear database (or add your own).
* **Calibration** for each microphone (section 2).
* **Level check.** Press **Level check**: the app listens to the room noise, then plays pink noise at -20 dBFS through both DSP inputs and tells you the mic level and how many dB to adjust. **Turn your head unit and amplifiers down first; this plays sound.** It needs a real device; Demo mode measures a virtual car instead.
* **Mic check.** Press **Run mic check**. It records about 4 seconds from the active input, **input only, nothing is played**, and reports:
  * the calibrated room noise (dBA and dB SPL),
  * the peak level and whether any samples clipped,
  * mains hum (50 or 60 Hz and harmonics),
  * a verdict: **OK**, **Too quiet**, **Clipping** or **Mains hum**, with a plain-language explanation,
  * the Windows capture level, with the **Set to recommended** option described above.

  Keep the car quiet while it records. Fix any problem here before running a tune.

If you use an XLR microphone: turn 48 V phantom power on for the mic input (switch it only with the input gain at minimum), and turn **direct monitoring off** so the mic is not fed back into the DSP.

---

## 5. Car: describing your system

Open **Car**. It has two tabs, **Layout** and **Equipment**.

### Layout

* **Preset.** Pick the topology that matches your car:
  * **Active 3-way + sub**: separate DSP outputs for woofer/midbass, midrange and tweeter on each side, plus a sub.
  * **Active 2-way + sub**
  * **Passive 2-way front + sub**: a component set (woofer and tweeter behind a passive crossover) runs from one amp channel.
  * **Passive front only**
  * **Full-range pair**: one driver per side, no crossover stage.
  * **Single driver (test box)**: one driver only. Left/right checks are skipped.
* You can also set the number of ways and the subwoofer mode directly, the **listening seat** (driver, passenger or centre), and **left-hand or right-hand drive**.
* Changing the layout or preset rebuilds the channel list with fresh defaults, so do it first.
* For each channel you can set its type, for example **Passive set** when a component set or coaxial runs from one amp channel. For passive sets the tuner skips the crossover and tweeter work and tunes the whole set's high-pass, delay, level, polarity and EQ instead.
* **Target.** Choose a house curve (for example Car Reference, Harman or Flat, and the other curves that ship with the app) and shape it: bass boost, transition range, treble tilt and tilt start. You can import your own target curve file.
* **Safety limits.** Maximum EQ boost, maximum low-frequency boost, sweep level, and the minimum high-pass frequencies that protect tweeters and midranges. Leave these at their defaults unless you know why you are changing them.
* **DSP capabilities.** How many EQ bands per channel, the sample rate, the gain range and the maximum delay of your DSP. The tuner never asks for more than the DSP allows.

### Equipment

List what is in the car: head unit, DSP, DAC, amplifiers, drivers (tweeters, midranges, midbass, coaxials, component sets, subwoofers), power and speaker wire, RCA cables, fuse holders, battery, alternator, and your audio interface and microphone. Pick products from the built-in gear database, or add your own and enter the specs. The app never invents specs for safety-critical settings: if a value is unknown it says so and uses a cautious assumption.

---

## 6. Install & Gains

**Install & Gains** is built from your Equipment list. It contains:

* **Summary** numbers for the installation.
* **Wiring.** Power and speaker runs are checked against the current each amp really draws: voltage drop, power loss and the recommended wire gauge. It also gives the recommended **main fuse** (the next standard size above 1.25 times the calculated current, or the fuse your amp's manual specifies).
* **Step-by-step instructions.** Every manual setting with the exact value and the reason, ordered: wiring, source, DSP, amplifiers, microphone. Tick each step off as you go. Steps are marked as info, recommended, important or danger.
* **Per-channel gains.** For each amp channel: the load the amp sees, the power it delivers, the driver's rating, the DMM voltage to set the gain to, and a safe high-pass. Gains are set so that a driver never gets more than its RMS rating, with 3 dB of headroom for EQ boost.
* **Measurement setup.** Mic positions, interface settings and two live checks before the first sweep.

### Setting gains with a multimeter (DMM)

The app gives the exact steps for each channel. In outline:

1. **Disconnect the speakers** from the channel. Set all gains to minimum, filters off, bass boost at 0 dB, EQ flat.
2. Set the head unit to the "clean maximum" step the app gives.
3. Play a 0 dB sine tone (400 Hz or lower unless your meter is true-RMS at higher frequencies).
4. Put the DMM on **AC volts** across the channel's speaker terminals and slowly raise the amp gain until it reads the voltage in the table. **Stop there.**
5. Stop the tone. **Re-enable the channel's DSP high-pass and low-pass filters before you reconnect the speaker.** A tweeter or midrange on a full-range signal can be destroyed in seconds.

### Danger states

These are shown in the danger colour and must be fixed first:

* A load **below the amp's minimum stable impedance**.
* An amp whose gain is too high for the driver **even at minimum gain**: the app tells you to set the knob to minimum and reduce the channel level in the DSP or source by the stated number of dB.
* A **fuse larger than the power wire can carry**.
* A **tweeter** high-pass that is too low.
* Safety reminders that are always shown: disconnect the battery negative before running power wire and install the fuse last; gains are not volume controls; never exceed the head unit step given.

---

## 7. Mic placement

The **Measurement setup** card on Install & Gains shows the positions. The rules:

* Measure at the seat you chose on the Car page. Set the seat and backrest to your normal listening position first: the tune is right for that position only.
* Mount the mic on a **tripod or boom**, not hand-held, with the capsule at **ear height** (the middle of the listener's head).
* USB mic (UMIK-1): point it **straight up at the roof** and load the **90 degree** calibration file. Plug it directly into the PC (no hub).
* Keep the capsule at least **10 cm** away from the headrest, seat surfaces and windows.
* **Close doors and windows**, switch off fans and HVAC, and keep everyone out of the car (or sit still and quiet).
* Engine off gives the lowest noise (keep the battery charged); or idle if your amps sag without it. Be consistent.
* The number of **head positions** is set in the Tune options (1 to 9). Positions are offsets from the centre: for example 3 positions are centre, 12 cm left and 12 cm right, and 4 positions add 12 cm forward. The app asks you to move the mic between positions and averages them. Left, right and forward are as seen sitting in the seat facing forward.

---

## 8. Running Auto Tune

Go to **Tune** and press **Start Auto Tune**. Defaults suit most cars; the **Options** panel lets you change:

* **Mic positions** (head positions averaged per driver),
* **Sweep length** (longer means more noise rejection),
* **Sweep level** (auto-adjusted if clipping or noisy),
* **Verify passes** (closed-loop correction rounds),
* **Refine iterations**,
* switches for **Match right channel to left**, **Competition refine** (joint perceptual optimisation after solving), **Refine crossovers from measurements**, and **level-match each driver band to the house curve's band targets** (available when the target is a house curve with band levels).

During the run the Tune page shows a stage timeline and live graphs. You can cancel at any time. The configuration is locked while a tune runs.

### What each stage does

The stages shown in the app are: Preflight, Protect, Measure, Crossovers, Solve, L+R check, Imaging, Apply, Verify, Noise, Report. In plain terms:

1. **Preflight and Protect.** Checks the audio device and connects to the DSP, then writes **protective crossovers first** so tweeters are safe before any sweep is played.
2. **Measure.** A level check on a safe driver, then a sweep of every channel. The sweep level for each driver is capped by its role, with tweeters the lowest, and never above the safety limit.
3. **Polarity.** Checks and corrects the polarity of each driver.
4. **Delay.** Time-aligns the drivers so their sound arrives at the listening position together.
5. **LR4 crossovers.** Sets crossovers (Linkwitz-Riley, 24 dB/oct) from each driver's measured usable range, and refines the crossover phase.
6. **Left EQ to target.** Levels are matched, then the left side is equalised to the target curve (crossover points and left EQ may be refined within the methodology rules).
7. **Right matched.** The right side is matched to the finished left side, so the two sides are symmetrical.
8. **L+R check.** Measures left and right together to find summing cancellation and corrects it.
9. **Imaging by level.** Centres the stereo image using **level only**.
10. **Apply and Verify.** Writes the settings, measures the result and corrects any remaining error (up to the number of verify passes). The final left, right and both-sides responses are measured, not predicted.
11. **Noise check.** Records the silent floor and the noise on each channel to find hiss, hum and the like. It is skipped if the DSP is changed by hand (it would need a mute/unmute prompt per channel) or if the measurement source cannot record silence.
12. **Report.** Calculates the score and the insights.

Some stages are skipped when they do not apply: passive sets skip crossover and tweeter work, full-range drivers have no crossover stage, a single driver skips all left/right checks, and a system with no subwoofer does not score or boost the deepest bass. The report notes what was skipped and why.

---

## 9. Results and exports

**Results** shows everything about the last tune in one scroll:

* **Score** out of 100 with a headline label, plus sub-scores for tonal balance, imaging and cleanliness. The headline label (for example Excellent or Good) is capped when a key sub-score is poor, so a high overall number cannot hide a bad result. A simulated "sound quality judge" sheet shows category scores and comments.
* **Before / after explorer** with hover values, wheel zoom and drag pan.
* **Band table** in 1/3 octaves, sortable, with CSV export.
* **Per-driver diagnostics** (usable range, resonances, arrival time, polarity, signal-to-noise, distortion) and a **distortion overview**.
* **Tune sheet**: final DSP settings per channel, findings and notes.

The toolbar exports:

* **Text sheet**: every channel's settings as plain text, to type into any DSP.
* **REW filters**: one REW filter file per output.
* **Equalizer APO**: a config for the whole tune.
* **JSON preset**: a FalseAudio preset you can load again with **Import preset**.
* **Save session** and **Open session**: settings and measurements in a `.fasession` file.

The **DSP** page also has an export panel, which includes miniDSP biquad files for each channel.

---

## 10. Connecting to your DSP

Open **DSP** and choose a control mode:

* **AI operator (recommended for any DSP).** For a DSP that has Windows software but no direct USB support here. See below.
* **Direct USB.** FalseAudio talks to the DSP itself over USB. Supported: **miniDSP** units (for example 2x4 HD, C-DSP 8x12, DDRC, Flex, SHD) and the **Dayton Audio DSP-408**. No vendor software is needed, and every value lands exactly. A **backup** of the DSP is saved before the first write, and the app asks your permission before it writes.
  * miniDSP: the vendor plugin keeps its own copy of the settings, so it will not display values written this way, and pressing Sync in the plugin overwrites them. Keep it closed.
  * DSP-408: every write is permanent on the device. The backup is your undo (**Restore backup**). Close the vendor software while FalseAudio is connected.
* **Manual guided.** Works with every DSP ever made. FalseAudio measures and computes the tune, then shows each change as a checklist; you type the values into your DSP software and press **Done**.
* **Virtual demo.** The simulated DSP used in Demo mode.
* **UI Automation (advanced).** FalseAudio drives your DSP's Windows software through Windows UI Automation, using a profile you teach once per DSP model with the Teach wizard.

### The AI operator

The AI operator learns where your DSP software's controls are (gain, delay, polarity, crossover, EQ and mute for each output), then types the computed tune into the window and verifies each write.

1. **Attach window** (a running DSP program) or **Launch DSP software**. The software appears inside FalseAudio.
2. Choose a provider:
   * **Claude** needs your own Anthropic API key. Paste it and press **Save**: it is stored **encrypted on this PC**. **Test** checks it. Screenshots of the one attached window are sent to Anthropic. Choose a model: **Opus 5.5** (most reliable at reading unfamiliar software) or **Sonnet 5.5** (cheaper).
   * **Ollama (local, free)** runs on your PC, needs no key, and no screenshot leaves the PC. Enter the server address (default `http://localhost:11434`) and the model (default `qwen3-vl:4b`), then press **Test Ollama**. The button makes a real request to `<address>/api/tags` (in Demo mode too) and reports whether Ollama is running and the model is installed. Local runs are slow, roughly a minute per step.
3. Set the **budget cap** in dollars. The run **stops by itself when it reaches the cap**.
4. **Consent.** Switch on **Allow AI to control** the named window. Nothing is written until you do this and press the run button. The AI can click and type only inside that one window.
5. Press **Learn software and apply tune** (the first time) or **Apply tune with AI** (when a map exists). The steps shown are: Learning layout, Mapping outputs, Verifying with test tone, Applying tune, Read-back check.
6. **STOP** halts everything immediately.

**The map.** What the AI learns is saved as a map for that software. Later runs replay the map with **no AI calls and no cost**. The AI is called again only if a control cannot be found or a value reads back far off. You can **Re-learn** or **Delete map**.

**Verified.** The map is not trusted blindly. A "verify channel" check plays a short, quiet test tone and uses the mic to confirm which driver really plays on each output, and every written value is **read back and compared**. Limits are enforced in code, not by the AI: protected tweeter high-pass, a cap on gain steps, tweeters muted before crossover or polarity changes (refused if no mute control is mapped), and the window bounds.

Make sure your amps are not at full gain and that you are ready to press STOP. The test tones are capped at a low level, but they are real sound.

---

## 11. Expert tools

Open **Expert** for manual control. Auto Tune does all of this for you, so you rarely need it. Four tabs:

* **Measure.** Manual sweep measurements and analysis: SPL, phase, group delay, impulse response, ETC, step response, harmonic distortion and spectral decay. A library lists every measurement in the session, including those from Auto Tune.
* **Live RTA.** A real-time analyser (bars or line, peak hold, snapshot), a dual-channel transfer function with coherence, phase and a delay finder, an SPL meter and a signal generator.
* **Alignment.** A car view with arrival rings and per-channel delay, polarity, gain and mute, an arrival timeline, a zero-all button and a tape-measure helper.
* **Crossovers & EQ.** The high-pass and low-pass editors (frequency, slope, alignment), a plot with draggable EQ nodes and a parametric EQ table (frequency, gain, Q, type, on/off). **Add band** puts each new band at a free frequency, as far as possible from the existing bands; double-click the plot to place a band exactly. **Reset EQ** clears a channel and **Copy L to R** mirrors the left channels' settings onto the right (delays are kept). On a short window the channel list and filter editors use compact sizes so more channels are visible; the list scrolls if needed.

**Send to DSP** at the top of Expert pushes your manual changes to the DSP.

---

## 12. Safety notes

* **Protect the tweeters.** Tweeters can be destroyed in seconds by a full-range signal. FalseAudio writes protective crossovers before it plays a sweep, caps sweep levels (tweeters lowest), and enforces minimum tweeter high-pass limits. Do not lower the safety limits without a reason, and never reconnect a tweeter with its high-pass bypassed.
* **Start at low volume.** Turn the head unit and the amplifiers down before any Live measurement or Level check, and raise the level only if the app asks for it.
* **Stay near the controls.** You can cancel Auto Tune at any time, and the AI operator has a STOP button.
* **Set amp gains with a DMM, speakers disconnected.** Gains are not volume controls.
* **Wiring.** Disconnect the battery negative before running power wire, fuse every power wire close to the battery, and install the fuse last.
* **Direct USB writes change the DSP itself.** DSP-408 writes are permanent; keep the backup.
* The app is a tuning aid. You are responsible for your car's electrical safety and for your equipment.

---

## 13. Troubleshooting

| Problem | What to try |
| --- | --- |
| "No audio device is open" | On **Rig**, pick an output and an input device and press **Rescan**. In Demo mode nothing is needed. |
| Mic check says **Too quiet** | Wrong input device or channel, the mic is muted or unplugged, or (XLR) phantom power is off or the gain is too low. |
| Mic check says **Clipping** | Lower the mic gain or the level, and move away from loud sources. |
| Mic check says **Mains hum** | Move the mic and its cable away from power wiring, try a different USB port, or check grounding. |
| SPL numbers look wrong | Load the calibration file (90 degrees for a mic pointing up) and keep the Windows mic level at the recommended 0 dB. |
| Tune stops with an error about the DSP | Check the DSP connection and mode on the **DSP** page, and that the vendor software is closed in Direct USB mode. |
| **Test Ollama** says Ollama is not running | Start it (`ollama serve`), check the address, then try again. |
| Ollama says the model is not installed | Run `ollama pull <model>` in a terminal. |
| miniDSP plugin does not show the values | Expected: the plugin keeps its own copy. Pressing Sync in it overwrites FalseAudio's values. |
| Score is low | Open **Results**: the findings and band table show where the response misses the target. Check mic position, closed doors and low noise, then re-run. |
| The app crashes | Send `%APPDATA%\FalseAudio\crash.log` and what you were doing. Paste that path into Explorer's address bar. |

---

## 14. Where files live

Everything the app saves is in `%APPDATA%\FalseAudio` (paste it into Explorer's address bar). Subfolders:

* `calibration`: microphone calibration files you imported.
* `Gear`: your own products added to the gear database (`user.json`).
* `Targets`: target curves you imported.
* `DSP Profiles`: automation profiles taught for the UI Automation mode.
* `profiles`: maps learned by the AI operator.
* `backups`: DSP backups saved before Direct USB writes.
* `crash.log`: written if the app crashes.

Your API key is stored encrypted on this PC in the same folder. Exports and sessions go where you choose; the file dialogs start in your Documents folder.

An optional `data\gear` folder next to the exe overrides the built-in equipment database.
