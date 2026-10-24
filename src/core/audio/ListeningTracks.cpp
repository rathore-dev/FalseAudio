#include "ListeningTracks.h"
#include "../dsp/Generators.h"

namespace fa
{
const std::vector<ListeningTrack>& ListeningTracks::all()
{
    static const std::vector<ListeningTrack> tracks
    {
        ListeningTrack::pinkFull, ListeningTrack::pinkBelow200, ListeningTrack::pink200to1k,
        ListeningTrack::pink1kto4k, ListeningTrack::pinkAbove4k,
        ListeningTrack::panLeft, ListeningTrack::panLeftCentre, ListeningTrack::panCentre,
        ListeningTrack::panRightCentre, ListeningTrack::panRight, ListeningTrack::panSweep,
        ListeningTrack::noiseCheck
    };
    return tracks;
}

juce::String ListeningTracks::name (ListeningTrack t)
{
    switch (t)
    {
        case ListeningTrack::pinkFull:       return "Pink noise (full range)";
        case ListeningTrack::pinkBelow200:   return "Pink noise < 200 Hz";
        case ListeningTrack::pink200to1k:    return "Pink noise 200 Hz - 1 kHz";
        case ListeningTrack::pink1kto4k:     return "Pink noise 1 - 4 kHz";
        case ListeningTrack::pinkAbove4k:    return "Pink noise > 4 kHz";
        case ListeningTrack::panLeft:        return "Pan: Left";
        case ListeningTrack::panLeftCentre:  return "Pan: Left-centre";
        case ListeningTrack::panCentre:      return "Pan: Centre";
        case ListeningTrack::panRightCentre: return "Pan: Right-centre";
        case ListeningTrack::panRight:       return "Pan: Right";
        case ListeningTrack::panSweep:       return "Pan sweep L - C - R";
        case ListeningTrack::noiseCheck:     return "Noise check (silence)";
    }
    return {};
}

juce::String ListeningTracks::instruction (ListeningTrack t)
{
    switch (t)
    {
        case ListeningTrack::pinkFull:
            return "Mono noise: it should sit in one small spot, centred on the dash in front of you. "
                   "If it leans to one side, nudge the image (lower the louder side) - use levels, not delays.";
        case ListeningTrack::pinkBelow200:
            return "Bass should come from the front and the centre, not from the sub or a door. "
                   "If it pulls to a side or the back, check the sub level and the sub/midbass crossover.";
        case ListeningTrack::pink200to1k:
        case ListeningTrack::pink1kto4k:
        case ListeningTrack::pinkAbove4k:
            return "Each band should image at the same centre spot and height as full-range noise. "
                   "A band that moves sideways needs a small level change on that band's drivers.";
        case ListeningTrack::panLeft:
        case ListeningTrack::panRight:
            return "Should come from the far edge of the stage (the A-pillar / door), not from inside the door.";
        case ListeningTrack::panLeftCentre:
        case ListeningTrack::panRightCentre:
            return "Should sit half-way between the centre and the edge - evenly spaced with the other positions.";
        case ListeningTrack::panCentre:
            return "Should be a tight, centred image straight ahead.";
        case ListeningTrack::panSweep:
            return "The sound should move smoothly and evenly from left to right with no jumps or holes.";
        case ListeningTrack::noiseCheck:
            return "Nothing is playing. With the engine running, listen for hiss, whine or alternator noise. "
                   "If you hear it, lower the DSP input / amp gains rather than the head unit volume.";
    }
    return {};
}

std::pair<double, double> ListeningTracks::bandLimits (ListeningTrack t)
{
    switch (t)
    {
        case ListeningTrack::pinkBelow200: return { 20.0, 200.0 };
        case ListeningTrack::pink200to1k:  return { 200.0, 1000.0 };
        case ListeningTrack::pink1kto4k:   return { 1000.0, 4000.0 };
        case ListeningTrack::pinkAbove4k:  return { 4000.0, 20000.0 };
        case ListeningTrack::noiseCheck:   return { 0.0, 0.0 };
        default:                           return { 20.0, 20000.0 };
    }
}

double ListeningTracks::panPosition (ListeningTrack t)
{
    switch (t)
    {
        case ListeningTrack::panLeft:        return 0.0;
        case ListeningTrack::panLeftCentre:  return 0.25;
        case ListeningTrack::panCentre:      return 0.5;
        case ListeningTrack::panRightCentre: return 0.75;
        case ListeningTrack::panRight:       return 1.0;
        default:                             return -1.0;
    }
}

std::pair<double, double> ListeningTracks::panGains (double position)
{
    const double theta = juce::jlimit (0.0, 1.0, position) * pi * 0.5;
    double l = std::cos (theta), r = std::sin (theta);
    if (l < 1.0e-9) l = 0.0;
    if (r < 1.0e-9) r = 0.0;
    return { l, r };
}

StereoTrack ListeningTracks::make (ListeningTrack t, double fs, double levelDbFS)
{
    StereoTrack out;
    out.sampleRate = fs;
    out.loop = true;
    out.name = name (t);
    out.instruction = instruction (t);

    if (t == ListeningTrack::noiseCheck)
    {
        out.left.assign ((size_t) fs, 0.0f);
        out.right.assign ((size_t) fs, 0.0f);
        return out;
    }

    const auto [fLow, fHigh] = bandLimits (t);
    const int period = 1 << 18; // ~5.5 s at 48 kHz; periodic, so the loop point is seamless
    const auto noise = makePeriodicPinkNoise (period, fs, levelDbFS, fLow, std::min (fHigh, 0.45 * fs), 0x5eed + (juce::uint32) t);

    if (t == ListeningTrack::panSweep)
    {
        const double positions[] = { 0.0, 0.25, 0.5, 0.75, 1.0, 0.75, 0.5, 0.25 };
        constexpr int numSteps = (int) std::size (positions);
        const int seg = (int) std::round (2.0 * fs);
        const int fade = (int) std::round (0.03 * fs);
        const int total = seg * numSteps;
        out.left.resize ((size_t) total);
        out.right.resize ((size_t) total);

        for (int i = 0; i < total; ++i)
        {
            const int s = i / seg, inSeg = i % seg;
            double pos = positions[s];
            if (inSeg < fade) // short equal-power glide from the previous position
            {
                const double prev = positions[(s + numSteps - 1) % numSteps];
                pos = prev + (pos - prev) * (0.5 - 0.5 * std::cos (pi * inSeg / fade));
            }
            const auto [gl, gr] = panGains (pos);
            const float x = noise[(size_t) (i % period)];
            out.left[(size_t) i]  = (float) (x * gl);
            out.right[(size_t) i] = (float) (x * gr);
        }
        return out;
    }

    const double pan = panPosition (t);
    const auto [gl, gr] = pan >= 0.0 ? panGains (pan) : std::pair<double, double> { 1.0, 1.0 };
    out.left.resize (noise.size());
    out.right.resize (noise.size());
    for (size_t i = 0; i < noise.size(); ++i)
    {
        out.left[i]  = (float) (noise[i] * gl);
        out.right[i] = (float) (noise[i] * gr);
    }
    return out;
}
} // namespace fa
