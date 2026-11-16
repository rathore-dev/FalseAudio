#include "HeaderBar.h"

namespace fa::ui
{
using namespace juce;

HeaderBar::HeaderBar (Session& s) : session (s)
{
    addAndMakeVisible (modePill);
    addAndMakeVisible (audioChip);
    addAndMakeVisible (dspChip);
    addChildComponent (tuneChip);
    addChildComponent (tuneBar);

    modePill.setTooltip ("Demo tunes a simulated car; Live drives your real interface and DSP");
    modePill.onChange = [this] (int index)
    {
        if (onModeChange)
            onModeChange (index == 1 ? Session::Mode::live : Session::Mode::demo);
    };

    refresh();
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::setPage (const String& t, const String& sub)
{
    title = t;
    subtitle = sub;
    repaint();
}

void HeaderBar::refresh()
{
    modePill.setSelected (session.getMode() == Session::Mode::live ? 1 : 0, dontSendNotification);

    // audio device
    {
        String name = "No audio device";
        Colour c = theme::bad;
        if (auto* dev = session.getAudio().getDeviceManager().getCurrentAudioDevice())
        {
            name = dev->getName();
            String why;
            c = session.getAudio().isReady (why) ? theme::good : theme::warn;
        }
        if (name.length() > 28)
            name = name.substring (0, 26).trimEnd() + juce::String (juce::CharPointer_UTF8 ("…"));
        audioChip.set (name, c);
        audioChip.setTooltip ("Audio interface - change it on the Rig page");
    }

    // DSP link
    {
        String name;
        Colour c = theme::good;
        switch (session.getDspKind())
        {
            case Session::DspKind::virtualDemo:  name = "Virtual DSP"; c = theme::roleFullrange; break;
            case Session::DspKind::manual:       name = "Manual DSP";  c = theme::warn; break;
            case Session::DspKind::uiAutomation: name = session.getDsp().getName(); c = session.getDsp().isConnected() ? theme::good : theme::warn; break;
        }
        if (name.isEmpty())
            name = "DSP";
        if (name.length() > 24)
            name = name.substring (0, 22).trimEnd() + juce::String (juce::CharPointer_UTF8 ("…"));
        dspChip.set (name, c);
        dspChip.setTooltip ("How tuning results reach your DSP - set up on the DSP page");
    }

    resized();
    repaint();
}

void HeaderBar::setTuneStatus (const String& text, double progress)
{
    tuning = text.isNotEmpty();
    tuneChip.set (text, theme::accent);
    tuneChip.setVisible (tuning);
    tuneBar.setVisible (tuning);
    tuneBar.setProgress (progress);
    resized();
    repaint();
}

void HeaderBar::resized()
{
    auto r = getLocalBounds();
    tuneBar.setBounds (r.removeFromBottom (2));

    auto right = r.reduced (theme::padLg, 0);
    const int chipH = 24;
    const int cy = (r.getHeight() - chipH) / 2;

    modePill.setBounds (right.removeFromRight (152).withSizeKeepingCentre (152, 30));
    right.removeFromRight (16);

    auto place = [&] (Chip& chip, int maxW)
    {
        const int w = jmin (maxW, chip.getIdealWidth());
        chip.setBounds (right.removeFromRight (w).withY (cy).withHeight (chipH));
        right.removeFromRight (8);
    };

    place (dspChip, 200);
    place (audioChip, 240);
    if (tuning)
        place (tuneChip, 260);
}

void HeaderBar::paint (Graphics& g)
{
    auto r = getLocalBounds();
    g.fillAll (theme::bg0);
    g.setColour (theme::border);
    g.fillRect (r.removeFromBottom (1));

    auto left = r.reduced (theme::padLg, 0);
    const int rightEdge = tuning ? tuneChip.getX() : audioChip.getX();
    left.setRight (jmax (left.getX() + 80, rightEdge - 16));

    const auto titleFont = theme::display (19.0f);
    const int titleW = (int) std::ceil (theme::textWidth (titleFont, title)) + 2;
    g.setColour (theme::text);
    g.setFont (titleFont);
    auto titleArea = left.removeFromLeft (jmin (titleW, left.getWidth()));
    g.drawText (title, titleArea, Justification::centredLeft, true);

    if (subtitle.isNotEmpty() && left.getWidth() > 60)
    {
        left.removeFromLeft (14);
        g.setColour (theme::border);
        g.fillRect (left.removeFromLeft (1).reduced (0, 16));
        left.removeFromLeft (14);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (12.5f));
        g.drawText (subtitle, left, Justification::centredLeft, true);
    }

    // separator between chips and the mode pill
    g.setColour (theme::border);
    g.fillRect (modePill.getX() - 9, r.getCentreY() - 10, 1, 20);
}
} // namespace fa::ui
