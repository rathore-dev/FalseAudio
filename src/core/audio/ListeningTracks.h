#pragma once

#include "../Types.h"

namespace fa
{
/** The listening checks of the Audiofrog process (steps 8-10), which a measurement can't do:
    band-limited mono pink noise to judge where the image sits per band, the L / LC / C / RC / R
    pan positions to check the stage width and centre, and a silent track for the noise check. */
enum class ListeningTrack
{
    pinkFull,          // mono pink noise, 20 Hz - 20 kHz
    pinkBelow200,      // < 200 Hz: the bass should come from the front, centred
    pink200to1k,
    pink1kto4k,
    pinkAbove4k,
    panLeft,           // equal-power pan positions of mono pink noise
    panLeftCentre,
    panCentre,
    panRightCentre,
    panRight,
    panSweep,          // L -> LC -> C -> RC -> R -> RC -> C -> LC, 2 s each, loops
    noiseCheck         // digital silence: listen for hiss, whine and alternator noise
};

struct StereoTrack
{
    std::vector<float> left, right;
    double sampleRate = 48000.0;
    bool loop = true;
    juce::String name;
    juce::String instruction;    // what to listen for
};

struct ListeningTracks
{
    static const std::vector<ListeningTrack>& all();

    static juce::String name (ListeningTrack t);
    static juce::String instruction (ListeningTrack t);

    /** Pass band of the noise in Hz ({0, 0} for the silent track). */
    static std::pair<double, double> bandLimits (ListeningTrack t);

    /** Pan position 0 = hard left, 0.5 = centre, 1 = hard right, or -1 for non-pan tracks. */
    static double panPosition (ListeningTrack t);

    /** Equal-power pan law: {left gain, right gain}, cos/sin, centre = -3 dB each. */
    static std::pair<double, double> panGains (double position);

    /** Generates the track. Noise is periodic (loops seamlessly); levelDbFS is the RMS of the
        mono signal before panning (centre-panned tracks are 3 dB lower per side). */
    static StereoTrack make (ListeningTrack t, double sampleRate, double levelDbFS = -20.0);
};
} // namespace fa
