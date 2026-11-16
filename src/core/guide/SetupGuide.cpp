#include "SetupGuide.h"
#include "../dsp/Spectrum.h"

namespace fa::guide
{
namespace
{
    enum class MicKind { usbMeasurement, xlrCondenser, xlrDynamic, unknown };

    MicKind micKind (const gear::Product* mic)
    {
        if (mic == nullptr)
            return MicKind::unknown;

        const auto name = (mic->brand + " " + mic->model).toLowerCase();
        if (mic->mic.has_value())
        {
            const auto& s = *mic->mic;
            if (s.type.equalsIgnoreCase ("USB"))
                return MicKind::usbMeasurement;
            if (s.type.equalsIgnoreCase ("XLR"))
                return s.needsPhantom ? MicKind::xlrCondenser : MicKind::xlrDynamic;
        }
        if (name.contains ("umik") || name.contains ("usb"))
            return MicKind::usbMeasurement;
        if (mic->mic.has_value() || mic->category == gear::Category::measurementMic)
            return MicKind::xlrCondenser;   // measurement mics on XLR are (nearly) always condensers
        return MicKind::unknown;
    }

    bool calFileAvailable (const gear::Product* mic)
    {
        if (mic == nullptr)
            return false;
        if (micKind (mic) == MicKind::usbMeasurement)
            return true;   // UMIK-style mics ship with per-serial 0/90 degree files
        return mic->mic.has_value() && mic->mic->calFileAvailable;
    }

    juce::String seatName (int seat)
    {
        switch (seat)
        {
            case 1:  return "passenger seat";
            case 2:  return "centre of the car (between the front seats)";
            default: return "driver's seat";
        }
    }

    juce::String inName (int index)  { return "Input " + juce::String (index + 1); }
    juce::String outName (int index) { return "Output " + juce::String (index + 1); }

    void setStatus (Step& s, Step::Status st)
    {
        // Never downgrade a worse status.
        if ((int) st > (int) s.status || s.status == Step::Status::pending)
            s.status = st;
    }

    /** Waits up to `ms`, returning false if cancelled. */
    bool waitFor (int ms, std::atomic<bool>& cancel)
    {
        const auto start = juce::Time::getMillisecondCounter();
        while ((int) (juce::Time::getMillisecondCounter() - start) < ms)
        {
            if (cancel.load())
                return false;
            juce::Thread::sleep (20);
        }
        return ! cancel.load();
    }

    double rmsDb (const std::vector<float>& x)
    {
        double acc = 0.0;
        for (auto v : x)
            acc += (double) v * v;
        // dBFS referenced to a full-scale sine (RMS 0.707 = 0 dBFS).
        return dbFromPower (2.0 * acc / (double) std::max<size_t> (1, x.size()));
    }

    double peakDb (const std::vector<float>& x)
    {
        float p = 0.0f;
        for (auto v : x)
            p = std::max (p, std::abs (v));
        return dbFromMag (p);
    }

    /** Same convention as MicSelfTest (checked against REW with a UMIK-1): dBFS_rms + 124.02 - Sens Factor. */
    double splOffsetFor (const MicCalibration& cal, bool& absolute)
    {
        absolute = cal.isValid() && cal.sensitivityDb != 0.0;
        return absolute ? 124.0206 - cal.sensitivityDb : 0.0;
    }
} // namespace

//==============================================================================
MicPlacement micPlacement (const SystemConfig& config, int positions, const gear::Product* mic)
{
    using P = MicPlacement::Position;
    const P centre   { "Centre (between the ears)", 0, 0, 0 };
    const P left     { "12 cm left", -12, 0, 0 };
    const P right    { "12 cm right", 12, 0, 0 };
    const P earL     { "Left ear (10 cm left)", -10, 0, 0 };
    const P earR     { "Right ear (10 cm right)", 10, 0, 0 };
    const P forward  { "12 cm forward", 0, 12, 0 };
    const P back     { "10 cm back (stay clear of the headrest)", 0, -10, 0 };
    const P up       { "8 cm up", 0, 0, 8 };
    const P down     { "8 cm down", 0, 0, -8 };
    const P leftFwd  { "12 cm left, 12 cm forward", -12, 12, 0 };
    const P rightFwd { "12 cm right, 12 cm forward", 12, 12, 0 };

    const auto deg = juce::String::charToString ((juce::juce_wchar) 0x00b0);
    MicPlacement out;
    switch (juce::jlimit (1, 9, positions))
    {
        case 1:  out.positions = { centre }; break;
        case 2:  out.positions = { earL, earR }; break;
        case 3:  out.positions = { centre, left, right }; break;
        case 4:  out.positions = { centre, left, right, forward }; break;
        case 5:  out.positions = { centre, left, right, forward, back }; break;
        case 6:  out.positions = { centre, left, right, forward, back, up }; break;
        case 7:  out.positions = { centre, left, right, forward, back, up, down }; break;
        case 8:  out.positions = { centre, left, right, leftFwd, rightFwd, back, up, down }; break;
        default: out.positions = { centre, left, right, forward, back, leftFwd, rightFwd, up, down }; break;
    }
    for (size_t i = 0; i < out.positions.size(); ++i)
        out.positions[i].label = juce::String ((int) i + 1) + ": " + out.positions[i].label;

    const auto kind = micKind (mic);
    const auto micName = mic != nullptr ? mic->displayName() : juce::String ("the measurement mic");
    switch (kind)
    {
        case MicKind::usbMeasurement:
            out.orientation = "Pointing straight up at the roof (load the 90" + deg + " calibration file for " + micName + ")";
            break;
        case MicKind::xlrCondenser:
        case MicKind::xlrDynamic:
            out.orientation = calFileAvailable (mic)
                ? "Pointing straight up at the roof (use the 90" + deg + " calibration file)"
                : "Pointing at the roof if you have a 90" + deg + " calibration file; otherwise pointing forward at the windscreen (0" + deg + " file)";
            break;
        case MicKind::unknown:
            out.orientation = "Pointing straight up at the roof with a 90" + deg + " calibration file; without one, point it forward (0" + deg + ")";
            break;
    }

    auto& ins = out.instructions;
    ins.add ("Measure at the " + seatName (config.listeningSeat) + ": the centre position is the middle of the listener's head, at ear height.");
    if (config.listeningSeat == 2)
        ins.add ("Centre seat tuning favours both front seats equally; the image will be centred between them rather than for one seat.");
    else
        ins.add ("Set the seat and backrest to your normal driving/listening position first - the tune is only right for that position.");
    ins.add ("Mount the mic on a tripod or boom (not hand-held), capsule at ear height (usually 5-10 cm below the headliner on a seated adult).");
    ins.add ("Mic orientation: " + out.orientation + ".");
    ins.add ("Keep the capsule at least 10 cm away from the headrest, seat surfaces and windows - close surfaces cause comb filtering.");
    ins.add ("Left/right/forward are as seen sitting in the seat, facing forward. Move the mic to each position when asked: "
             + juce::String ((int) out.positions.size()) + (out.positions.size() == 1 ? " position." : " positions (averaged)."));
    ins.add ("Close all doors and windows; switch off fans/HVAC; nobody in the car while sweeping (or sit still and quiet).");
    ins.add ("Engine: measure with the engine off for the lowest noise (keep the battery charged), or idling if the amps sag without it - be consistent.");
    if (kind == MicKind::usbMeasurement)
        ins.add ("USB mic: plug it directly into the laptop (no hub) and route the cable so it doesn't pull the tripod.");
    else if (kind == MicKind::xlrCondenser)
        ins.add ("XLR mic: turn 48 V phantom power ON before measuring; keep the cable away from power wiring.");
    return out;
}

//==============================================================================
std::vector<Step> interfaceSetup (const gear::Product* audioInterface, const gear::Product* mic, const AudioRouting& routing)
{
    std::vector<Step> steps;
    const gear::InterfaceSpecs* spec = (audioInterface != nullptr && audioInterface->audioInterface.has_value())
                                           ? &*audioInterface->audioInterface : nullptr;
    const auto ifName = audioInterface != nullptr ? audioInterface->displayName() : juce::String ("your audio interface");
    const auto kind = micKind (mic);
    const bool usbMic = kind == MicKind::usbMeasurement;

    // 1 Driver
    {
        Step s;
        s.id = "driver";
        s.title = "Audio driver";
        if (spec != nullptr && spec->asio)
        {
            s.detail = "Select " + ifName + " with its ASIO driver if the Audio settings list ASIO; otherwise use Windows Audio (WASAPI) in exclusive mode.";
            s.bullets.add ("ASIO and WASAPI exclusive both bypass the Windows mixer (bit-exact, fixed latency).");
        }
        else
        {
            s.detail = "Use Windows Audio (WASAPI) in exclusive mode for " + ifName + ". Avoid DirectSound/MME - they resample and add variable latency.";
        }
        if (usbMic)
            s.bullets.add ("USB mic: select the interface as the output device and the USB mic as the input device (WASAPI allows separate devices).");
        s.bullets.add ("Close other audio apps (browser, music, calls) so nothing else holds the device.");
        steps.push_back (s);
    }

    // 2 Sample rate
    {
        Step s;
        s.id = "samplerate";
        s.title = "Sample rate: 48 kHz";
        s.detail = "Run the interface at 48 kHz - the rate of most car DSPs - so nothing is resampled between the laptop and the DSP.";
        if (spec != nullptr && ! spec->sampleRates.isEmpty())
        {
            bool has48 = false;
            for (auto& r : spec->sampleRates)
                has48 = has48 || r.retainCharacters ("0123456789.").getDoubleValue() == 48000.0
                              || r.retainCharacters ("0123456789.").getDoubleValue() == 48.0;
            if (has48)
                s.bullets.add (ifName + " supports 48 kHz.");
            else
            {
                s.bullets.add (ifName + " lists " + spec->sampleRates.joinIntoString (", ") + " - pick 48 kHz if available, else the closest (44.1 kHz).");
                s.status = Step::Status::warning;
            }
        }
        s.bullets.add ("Set the same rate in Windows Sound settings > device > Properties > Advanced (both playback and recording).");
        if (usbMic)
            s.bullets.add ("Set the USB mic to 48 kHz too (UMIK-1 is fixed at 48 kHz).");
        steps.push_back (s);
    }

    // 3 Buffer
    {
        Step s;
        s.id = "buffer";
        s.title = "Buffer size";
        s.detail = "Use a relaxed buffer (512-1024 samples). Latency doesn't matter for measuring; dropouts do.";
        s.bullets.add ("If you hear clicks or get 'dropout' warnings, raise the buffer.");
        steps.push_back (s);
    }

    // 4 Phantom power
    {
        Step s;
        s.id = "phantom";
        s.title = "Phantom power (48 V)";
        if (usbMic)
            s.detail = "Not needed - the USB mic is powered by USB. Leave phantom power OFF on the interface.";
        else if (kind == MicKind::xlrDynamic)
            s.detail = "Leave phantom power OFF - this mic doesn't need it.";
        else
        {
            s.detail = "Turn 48 V phantom power ON for the mic input" + juce::String (routing.micInputs.size() > 1 ? "s" : "")
                     + " (condenser measurement mics need it). Keep it OFF on any input that has a line/loopback cable.";
            if (spec != nullptr && ! spec->phantomPower)
            {
                s.bullets.add (ifName + " has no phantom power - use an external 48 V supply or an interface that has it.");
                s.status = Step::Status::warning;
            }
        }
        if (! usbMic)
        {
            juce::StringArray ins;
            for (auto i : routing.micInputs)
                ins.add (inName (i));
            s.bullets.add ("Mic input" + juce::String (ins.size() > 1 ? "s: " : ": ") + ins.joinIntoString (", ") + ".");
            s.bullets.add ("Switch phantom on/off only with the input gain at minimum (avoids a loud pop).");
        }
        steps.push_back (s);
    }

    // 5 Input gain
    {
        Step s;
        s.id = "gain";
        s.title = "Mic input gain";
        s.detail = "Set the gain so the measurement sweep peaks between -12 and -6 dBFS at the mic: loud enough for good signal-to-noise, with headroom for resonances.";
        if (usbMic)
            s.bullets.add ("USB mic: leave its Windows recording level at 100 % (calibrated gain) - set the level with the interface output/DSP volume instead.");
        else if (spec != nullptr && spec->maxGainDb > 0.0)
            s.bullets.add ("Start around " + juce::String (juce::roundToInt (spec->maxGainDb * 0.5)) + " dB of gain (half of "
                           + ifName + "'s " + juce::String (spec->maxGainDb, 0) + " dB), then fine-tune with the gain check.");
        else
            s.bullets.add ("Start with the gain knob around 12 o'clock, then fine-tune with the gain check.");
        if (routing.micInputs.size() > 1)
            s.bullets.add ("Several mics: set every mic input to the same gain (match them with the gain check).");
        s.bullets.add ("Run the gain check: it plays pink noise and tells you exactly how many dB to change.");
        steps.push_back (s);
    }

    // 6 Direct monitor
    {
        Step s;
        s.id = "monitor";
        s.title = "Direct monitoring OFF";
        s.detail = "Switch direct/zero-latency monitoring OFF (and any 'mix' knob fully to playback) so the mic signal isn't sent back to the outputs and into the DSP.";
        if (! usbMic)
            s.bullets.add ("Otherwise the mic feeds the speakers - feedback and wrong measurements.");
        steps.push_back (s);
    }

    // 7 Output level
    {
        Step s;
        s.id = "output";
        s.title = "Output level into the DSP";
        s.detail = "Interface outputs " + outName (routing.leftOutput) + " -> DSP left input, " + outName (routing.rightOutput)
                 + " -> DSP right input. Set the output so a -18 dBFS sweep is clearly audible but not loud (roughly normal listening level).";
        s.bullets.add ("Start with the head unit/DSP master volume low and the interface output at about 70 %, then raise slowly.");
        if (spec != nullptr && spec->maxOutputDbu > 0.0)
            s.bullets.add (ifName + " outputs up to +" + juce::String (spec->maxOutputDbu, 0) + " dBu ("
                           + juce::String (0.775 * magFromDb (spec->maxOutputDbu), 1) + " V RMS) at full scale - make sure the DSP input sensitivity can take it, or turn the output down.");
        s.bullets.add ("Never exceed the level where the tweeters sound harsh - the tune only needs a clean, moderate level.");
        steps.push_back (s);
    }

    // 8 Loopback
    {
        Step s;
        s.id = "loopback";
        s.title = "Loopback cable (timing reference)";
        if (routing.loopbackOutput >= 0 && routing.loopbackInput >= 0)
        {
            s.detail = "Connect a short cable from " + outName (routing.loopbackOutput) + " straight into " + inName (routing.loopbackInput)
                     + ". The app records its own output there, removing interface latency and drift from the timing.";
            s.bullets.add ("Phantom power OFF on " + inName (routing.loopbackInput) + "; set its gain so the loopback peaks around -12 dBFS.");
            const bool clashIn = std::find (routing.micInputs.begin(), routing.micInputs.end(), routing.loopbackInput) != routing.micInputs.end();
            const bool clashOut = routing.loopbackOutput == routing.leftOutput || routing.loopbackOutput == routing.rightOutput;
            if (clashIn)
            {
                s.bullets.add (inName (routing.loopbackInput) + " is also a mic input - pick another input for the loopback.");
                s.status = Step::Status::failed;
            }
            if (clashOut)
            {
                s.bullets.add (outName (routing.loopbackOutput) + " also feeds the DSP - use a separate output for the loopback.");
                s.status = Step::Status::failed;
            }
            if (usbMic)
            {
                s.bullets.add ("Note: a USB mic runs on its own clock, so the loopback can't time-reference it - absolute arrival times are approximate (relative alignment is unaffected).");
                setStatus (s, Step::Status::warning);
            }
        }
        else
        {
            s.detail = "Optional. Without a loopback the app uses its own output timing; relative time alignment between drivers is still exact.";
            const int ins = spec != nullptr ? spec->inputs : 0;
            const int outs = spec != nullptr ? spec->outputs : 0;
            if (! usbMic && ((spec != nullptr && spec->hasLoopback) || (ins > (int) routing.micInputs.size() && outs > 2)))
            {
                int freeIn = 0;
                while (std::find (routing.micInputs.begin(), routing.micInputs.end(), freeIn) != routing.micInputs.end())
                    ++freeIn;
                const int freeOut = std::max (routing.leftOutput, routing.rightOutput) + 1;
                s.bullets.add (ifName + " has spare channels: cable " + outName (freeOut) + " -> " + inName (freeIn)
                               + " and set them as loopback output/input in Audio settings for the most accurate timing.");
            }
            s.status = Step::Status::skipped;
        }
        steps.push_back (s);
    }

    // 9 Windows
    {
        Step s;
        s.id = "windows";
        s.title = "Windows sound settings";
        s.detail = "Disable everything that alters the signal: audio enhancements, spatial sound, loudness equalisation, communications ducking.";
        s.bullets.add ("Sound settings > device > Properties: turn 'Audio enhancements' OFF and Spatial sound OFF (playback and recording).");
        s.bullets.add ("Advanced: tick 'Allow applications to take exclusive control' and 'Give exclusive mode applications priority'.");
        s.bullets.add ("Communications tab: 'Do nothing' (so Windows never lowers the volume during a sweep).");
        s.bullets.add ("Plug the laptop in and use a high-performance power plan (avoids USB power saving dropouts).");
        steps.push_back (s);
    }
    return steps;
}

//==============================================================================
Step runGainStagingCheck (AudioEngine& audio, int micIndex, double testLevelDbFS, std::atomic<bool>& cancel)
{
    Step s;
    s.id = "gaincheck";
    s.title = "Gain staging check";
    s.detail = "Plays pink noise at the sweep level and measures the mic level and the noise floor.";

    const auto routing = audio.getRouting();
    if (micIndex < 0 || micIndex >= (int) routing.micInputs.size())
    {
        s.status = Step::Status::failed;
        s.result = "No microphone input is configured for mic " + juce::String (micIndex + 1) + ".";
        return s;
    }
    const int input = routing.micInputs[(size_t) micIndex];
    const double sr = audio.getSampleRate() > 0.0 ? audio.getSampleRate() : 48000.0;
    const int n = (int) std::min (sr, 65536.0);
    std::vector<float> buf ((size_t) n);

    const auto previous = audio.getGenerator();
    auto restore = [&audio, previous] { audio.setGenerator (previous); };

    // Noise floor first (generator off).
    GeneratorSettings off;
    off.signal = GeneratorSettings::Signal::off;
    audio.setGenerator (off);
    if (! waitFor (600, cancel))
        { restore(); s.status = Step::Status::skipped; s.result = "Cancelled."; return s; }
    if (! audio.copyLatestInput (input, buf.data(), n))
    {
        restore();
        s.status = Step::Status::failed;
        s.result = "Can't read " + inName (input) + " - is the audio device running?";
        return s;
    }
    const double noiseDb = rmsDb (buf);

    // Pink noise at the test level on both feeds.
    GeneratorSettings g;
    g.signal = GeneratorSettings::Signal::pinkPeriodic;
    g.levelDbFS = testLevelDbFS;
    g.feed = Feed::both;
    audio.setGenerator (g);
    if (! waitFor (1000, cancel))
        { restore(); s.status = Step::Status::skipped; s.result = "Cancelled."; return s; }
    audio.getInputPeakDb (input);   // reset the peak hold
    if (! waitFor (1500, cancel))
        { restore(); s.status = Step::Status::skipped; s.result = "Cancelled."; return s; }
    const double holdPeakDb = audio.getInputPeakDb (input);
    const bool ok = audio.copyLatestInput (input, buf.data(), n);
    restore();
    if (! ok)
    {
        s.status = Step::Status::failed;
        s.result = "Can't read " + inName (input) + " - is the audio device running?";
        return s;
    }

    const double signalRmsDb = rmsDb (buf);
    const double signalPeakDb = std::max (holdPeakDb, peakDb (buf));
    const double snr = signalRmsDb - noiseDb;

    // Pink noise is RMS-normalised to the level; a sweep at the same level has its sine peak at
    // that level and concentrates energy at each frequency, so its mic peaks land about 3 dB
    // above the pink-noise RMS (more on strong resonances).
    const double predictedSweepPeak = signalRmsDb + 3.0;
    const double targetPeak = -9.0;
    const double changeDb = targetPeak - predictedSweepPeak;
    auto fmt = [] (double v) { return juce::String (v, 1); };

    s.bullets.add ("Mic RMS " + fmt (signalRmsDb) + " dBFS, peak " + fmt (signalPeakDb) + " dBFS");
    s.bullets.add ("Noise floor " + fmt (noiseDb) + " dBFS RMS (signal-to-noise " + fmt (snr) + " dB)");
    s.bullets.add ("Predicted sweep peak " + fmt (predictedSweepPeak) + " dBFS (target -12...-6 dBFS)");

    if (snr < 6.0)
    {
        s.status = Step::Status::failed;
        s.result = "No test signal at the mic (only " + fmt (snr) + " dB above the noise). Check the output routing ("
                 + outName (routing.leftOutput) + "/" + outName (routing.rightOutput) + " -> DSP), the DSP/amp power, mutes and volume, and phantom power for an XLR mic.";
        return s;
    }
    if (signalPeakDb > -1.0)
    {
        s.status = Step::Status::failed;
        s.result = "The mic input is clipping (peak " + fmt (signalPeakDb) + " dBFS). Lower the input gain by about "
                 + juce::String (std::max (6.0, -changeDb), 0) + " dB (or turn the DSP volume down).";
        return s;
    }
    if (predictedSweepPeak > -6.0)
    {
        s.status = Step::Status::warning;
        s.result = "Too hot: lower the input gain ~" + juce::String (-changeDb, 0) + " dB (sweep peaks would reach "
                 + fmt (predictedSweepPeak) + " dBFS).";
    }
    else if (predictedSweepPeak < -12.0)
    {
        const double maxRaise = snr - 40.0;   // raising the gain doesn't improve acoustic SNR
        s.status = Step::Status::warning;
        s.result = "Too low: raise the input gain ~" + juce::String (changeDb, 0) + " dB (sweep peaks only "
                 + fmt (predictedSweepPeak) + " dBFS).";
        if (maxRaise < 0.0)
            s.result << " If the noise rises with the gain, raise the DSP/amp volume ~" + juce::String (-maxRaise, 0) + " dB instead.";
    }
    else
    {
        s.status = Step::Status::ok;
        s.result = "Gain is right: sweep peaks will be about " + fmt (predictedSweepPeak) + " dBFS.";
    }

    if (snr < 25.0)
    {
        setStatus (s, Step::Status::failed);
        s.result << " Signal-to-noise is only " << fmt (snr) << " dB (need 40+): raise the playback volume ~"
                 << juce::String (40.0 - snr, 0) << " dB or reduce the noise (engine/HVAC off, doors closed).";
    }
    else if (snr < 40.0)
    {
        setStatus (s, Step::Status::warning);
        s.result << " Signal-to-noise " << fmt (snr) << " dB is marginal - raise the playback volume ~"
                 << juce::String (40.0 - snr, 0) << " dB for cleaner low-bass results.";
    }
    return s;
}

//==============================================================================
Step runNoiseCheck (AudioEngine& audio, int micIndex, std::atomic<bool>& cancel)
{
    Step s;
    s.id = "noisecheck";
    s.title = "Background noise check";
    s.detail = "Records ~3 s of ambient sound and shows the level per octave.";

    const auto routing = audio.getRouting();
    if (micIndex < 0 || micIndex >= (int) routing.micInputs.size())
    {
        s.status = Step::Status::failed;
        s.result = "No microphone input is configured for mic " + juce::String (micIndex + 1) + ".";
        return s;
    }
    const int input = routing.micInputs[(size_t) micIndex];
    const double sr = audio.getSampleRate() > 0.0 ? audio.getSampleRate() : 48000.0;

    const auto previous = audio.getGenerator();
    GeneratorSettings off;
    off.signal = GeneratorSettings::Signal::off;
    audio.setGenerator (off);
    const bool done = waitFor (3200, cancel);
    const int n = (int) std::min (3.0 * sr, 120000.0);
    std::vector<float> x ((size_t) n);
    const bool ok = done && audio.copyLatestInput (input, x.data(), n);
    audio.setGenerator (previous);
    if (! done)
        { s.status = Step::Status::skipped; s.result = "Cancelled."; return s; }
    if (! ok)
    {
        s.status = Step::Status::failed;
        s.result = "Can't read " + inName (input) + " - is the audio device running?";
        return s;
    }

    // Welch power spectrum (Hann, 50 % overlap), normalised so mean-square is preserved.
    const int fft = 8192;
    std::vector<double> power ((size_t) fft / 2 + 1, 0.0);
    std::vector<float> w ((size_t) fft), seg ((size_t) fft);
    double w2 = 0.0;
    for (int i = 0; i < fft; ++i)
    {
        w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / fft));
        w2 += (double) w[(size_t) i] * w[(size_t) i];
    }
    int frames = 0;
    for (int start = 0; start + fft <= n; start += fft / 2, ++frames)
    {
        for (int i = 0; i < fft; ++i)
            seg[(size_t) i] = x[(size_t) (start + i)] * w[(size_t) i];
        const auto bins = realFFT (seg.data(), fft, fft);
        for (size_t k = 1; k < power.size() && k < bins.size(); ++k)
            power[k] += 2.0 * std::norm (bins[k]) / ((double) fft * w2);
    }
    for (auto& p : power)
        p /= std::max (1, frames);

    const auto cal = audio.getCalibration (micIndex);
    bool absolute = false;
    const double offset = splOffsetFor (cal, absolute);
    const juce::String unit = absolute ? " dB SPL" : " dBFS";

    static const double centres[] = { 31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    std::vector<std::pair<double, double>> bands;
    double totalMs = 0.0;
    for (auto fc : centres)
    {
        const double lo = fc / std::sqrt (2.0), hi = std::min (fc * std::sqrt (2.0), sr * 0.5);
        if (lo >= sr * 0.5)
            break;
        double ms = 0.0;
        for (size_t k = 1; k < power.size(); ++k)
        {
            const double f = (double) k * sr / fft;
            if (f >= lo && f < hi)
                ms += power[k];
        }
        totalMs += ms;
        double db = dbFromPower (2.0 * ms) + offset;
        if (cal.isValid())
            db -= cal.deviationDb (fc);
        bands.emplace_back (fc, db);
    }
    const double totalDb = dbFromPower (2.0 * totalMs) + offset;

    juce::StringArray levels;
    for (auto& [f, db] : bands)
        levels.add ((f >= 1000.0 ? juce::String (f / 1000.0, f == 31.5 ? 1 : 0) + "k" : juce::String (f, f == 31.5 ? 1 : 0))
                    + " Hz: " + juce::String (db, 1) + unit);
    s.bullets = levels;
    s.bullets.insert (0, "Overall " + juce::String (totalDb, 1) + unit + (absolute ? "" : " (no calibration sensitivity - relative levels)"));

    auto bandAt = [&bands] (double f)
    {
        for (auto& b : bands)
            if (std::abs (b.first - f) < 1.0) return b.second;
        return -200.0;
    };
    const double low = std::max (bandAt (31.5), bandAt (63.0));
    const double mid = std::max (bandAt (500.0), bandAt (1000.0));

    juce::StringArray advice;
    bool bad = false, marginal = false;
    if (absolute)
    {
        // Sweeps at ~75-85 dB SPL need the noise >= 40 dB below to give clean results.
        if (low > 65.0)       { bad = true;      advice.add ("Strong low-frequency noise (" + juce::String (low, 0) + " dB SPL): switch the engine and HVAC fan off."); }
        else if (low > 55.0)  { marginal = true; advice.add ("Some low-frequency rumble (" + juce::String (low, 0) + " dB SPL) - engine/HVAC off or park away from traffic for clean bass."); }
        if (mid > 45.0)       { bad = true;      advice.add ("Midrange noise is high (" + juce::String (mid, 0) + " dB SPL): close doors/windows, stop fans, keep quiet during sweeps."); }
        else if (mid > 35.0)  { marginal = true; advice.add ("Midrange noise " + juce::String (mid, 0) + " dB SPL - acceptable, but quieter is better."); }
    }
    else
    {
        // Relative to a sweep peaking near -9 dBFS at the mic.
        if (totalDb > -45.0)      { bad = true;      advice.add ("Noise is only " + juce::String (-9.0 - totalDb, 0) + " dB below a typical sweep level - reduce the noise or raise the playback level."); }
        else if (totalDb > -60.0) { marginal = true; advice.add ("Noise floor " + juce::String (totalDb, 0) + " dBFS - usable; quieter gives cleaner low bass."); }
        if (low - mid > 15.0)
            advice.add ("The noise is mostly low-frequency rumble (engine, HVAC, traffic): engine and fan off if possible.");
    }
    if (advice.isEmpty())
        advice.add ("Quiet enough for accurate measurements.");

    s.status = bad ? Step::Status::failed : marginal ? Step::Status::warning : Step::Status::ok;
    s.result = advice.joinIntoString (" ");
    return s;
}
} // namespace fa::guide
