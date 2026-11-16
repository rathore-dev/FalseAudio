#include "AlignmentPage.h"
#include "../components/DspSend.h"

namespace fa::ui
{
namespace
{
    int cardChrome (Card& c)
    {
        const auto saved = c.getBounds();
        c.setSize (400, 300);
        const int chrome = 300 - c.getContentArea().getHeight();
        c.setBounds (saved);
        return chrome;
    }
}

//==============================================================================
void AlignmentPage::TableHeader::paint (juce::Graphics& g)
{
    using R = AlignmentRow;
    auto b = getLocalBounds().reduced (12, 0);
    g.setColour (theme::textMuted);
    g.setFont (theme::fontBold (10.5f));
    auto cell = [&] (int w, const juce::String& t, juce::Justification j = juce::Justification::centredLeft)
    {
        g.drawText (t.toUpperCase(), b.removeFromLeft (w).reduced (j == juce::Justification::centredLeft ? 4 : 0, 0), j, false);
    };
    cell (R::nameW, "Channel");
    cell (R::arrivalW, "Arrival", juce::Justification::centred);
    cell (R::delayW + kit::gap + R::cmW, "Delay");
    cell (R::polW, "Polarity");
    cell (R::gainW, "Gain");
    cell (R::muteW, "Mute");
    g.setColour (theme::borderHi);
    g.fillRect (getLocalBounds().reduced (12, 0).removeFromBottom (1));
}

//==============================================================================
AlignmentPage::AlignmentPage (Session& s) : Page (s)
{
    addAndMakeVisible (body);
    body.layout = [this] (int w) { return layoutContent (w); };
    auto& c = body.content();

    for (auto* card : { &carCard, &tableCard, &timelineCard, &tapeCard, &noteCard })
        c.addAndMakeVisible (card);
    tapeCard.setVisible (false);

    carCard.addAndMakeVisible (car);
    carCard.addAndMakeVisible (carHint);
    car.onChannelClicked = [this] (const juce::String& id) { selectChannel (id); };

    tableCard.addAndMakeVisible (zeroBtn);
    tableCard.addAndMakeVisible (tapeBtn);
    tableCard.addAndMakeVisible (sendBtn);
    sendBtn.setTooltip ("Write the current delay, gain and polarity of every channel to the DSP");
    sendBtn.onClick = [this] { sendConfigToDsp (session, shell); };
    tableCard.addAndMakeVisible (header);
    zeroBtn.onClick = [this]
    {
        editConfig ([] (SystemConfig& cfg) { for (auto& ch : cfg.channels) ch.settings.delayMs = 0.0; });
        toast ("All delays set to 0 ms");
    };
    tapeBtn.onClick = [this]
    {
        tapeVisible = ! tapeVisible;
        if (tapeVisible)
            rebuildTape();
        tapeCard.setVisible (tapeVisible);
        tapeBtn.setStyle (tapeVisible ? FaButton::Style::primary : FaButton::Style::secondary);
        body.relayout();
        if (tapeVisible)
            body.getViewport().setViewPosition (0, juce::jmax (0, tapeCard.getY() - kit::margin));
    };

    timelineCard.addAndMakeVisible (timeline);
    timeline.onRowClicked = [this] (const juce::String& id) { selectChannel (id); };

    tapeCard.addAndMakeVisible (tapeApply);
    tapeCard.addAndMakeVisible (tapeCancel);
    tapeCard.addAndMakeVisible (tapePreview);
    tapeApply.onClick = [this] { applyTape(); };
    tapeCancel.onClick = [this] { tapeBtn.triggerClick(); };

    noteCard.addAndMakeVisible (noteText);

    refreshFromSession();
}

AlignmentPage::~AlignmentPage() = default;

//==============================================================================
void AlignmentPage::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
}

void AlignmentPage::resized()
{
    body.setBounds (getLocalBounds());
}

void AlignmentPage::pageShown()
{
    refreshFromSession();
}

void AlignmentPage::sessionChanged()
{
    refreshFromSession();
}

void AlignmentPage::toast (const juce::String& msg, AppShell::ToastKind kind)
{
    if (shell != nullptr)
        shell->toast (msg, kind);
}

void AlignmentPage::editConfig (std::function<void (SystemConfig&)> edit)
{
    if (updating)
        return;
    auto cfg = session.getConfig();
    edit (cfg);
    session.setConfig (cfg);
}

std::map<juce::String, double> AlignmentPage::arrivals() const
{
    if (session.hasTuneResult())
        return session.getTuneResult().report.arrivalMs;
    return {};
}

void AlignmentPage::selectChannel (const juce::String& id)
{
    selectedId = id;
    car.setHighlight (id);
    timeline.setHighlight (id);
    for (auto& r : rows)
        r->setSelected (r->getChannelId() == id);
}

//==============================================================================
void AlignmentPage::rebuildRows()
{
    const auto& cfg = session.getConfig();
    bool same = rows.size() == cfg.channels.size();
    for (size_t i = 0; same && i < rows.size(); ++i)
        same = rows[i]->getChannelId() == cfg.channels[i].id;

    if (! same)
    {
        rows.clear();
        for (auto& ch : cfg.channels)
        {
            auto row = std::make_unique<AlignmentRow> (ch, cfg.caps,
                [this] (const OutputChannel& edited) { if (! updating) session.updateChannel (edited); },
                [this] (const juce::String& id) { selectChannel (id); });
            tableCard.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        if (cfg.find (selectedId) == nullptr)
            selectedId = cfg.channels.empty() ? juce::String() : cfg.channels.front().id;
        body.relayout();
    }

    const auto arr = arrivals();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        rows[i]->update (cfg.channels[i], cfg.caps);
        auto it = arr.find (cfg.channels[i].id);
        rows[i]->setArrival (it != arr.end() ? std::optional<double> (it->second) : std::nullopt);
    }
}

void AlignmentPage::updateTimeline()
{
    const auto& cfg = session.getConfig();
    const auto arr = arrivals();
    std::vector<ArrivalTimeline::Item> items;
    for (auto& ch : cfg.channels)
    {
        ArrivalTimeline::Item it;
        it.id = ch.id;
        it.label = ch.label;
        it.colour = theme::roleColour (ch.role);
        it.delayMs = ch.settings.delayMs;
        it.muted = ch.settings.muted;
        auto a = arr.find (ch.id);
        if (a != arr.end())
            it.arrivalMs = a->second;
        items.push_back (it);
    }
    timeline.setItems (std::move (items));
    timeline.setHighlight (selectedId);
    car.setArrivals (arr);

    const bool measured = ! arr.empty();
    timelineCard.setTitle ("Arrival timeline", measured
        ? "Hollow = measured arrival, filled = after the DSP delay. Every filled marker should sit on the dashed line."
        : "Delays currently applied. Run Auto Tune to see measured arrivals from each driver.");
    carHint.setText (measured
        ? "Rings grow with arrival time: the farthest driver (largest ring) gets zero delay, nearer drivers wait for it."
        : "No measurement yet - rings appear after Auto Tune (or a measured alignment on the Measure page).");
}

void AlignmentPage::refreshFromSession()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& cfg = session.getConfig();

    car.setRightHandDrive (kit::isRightHandDrive (session));
    car.setConfig (cfg);
    car.setSeat (cfg.listeningSeat);
    rebuildRows();
    updateTimeline();
    selectChannel (selectedId);

    double maxDelay = 0.0;
    int inverted = 0, muted = 0;
    for (auto& ch : cfg.channels)
    {
        maxDelay = juce::jmax (maxDelay, ch.settings.delayMs);
        inverted += ch.settings.inverted ? 1 : 0;
        muted += ch.settings.muted ? 1 : 0;
    }
    juce::String sub = juce::String (cfg.channels.size()) + " channels" + kit::sep() + "longest delay " + theme::formatMs (maxDelay, 2)
                       + "  (" + juce::String (juce::roundToInt (msToMetres (maxDelay) * 100.0)) + " cm)";
    if (inverted > 0) sub << kit::sep() << inverted << " inverted";
    if (muted > 0)    sub << kit::sep() << muted << " muted";
    tableCard.setTitle ("Channels", sub);

    if (tapeVisible && tapeIds.size() != cfg.channels.size())
        rebuildTape();
    body.relayout();
}

//==============================================================================
void AlignmentPage::rebuildTape()
{
    const auto& cfg = session.getConfig();
    tapeFields.clear();
    tapeIds.clear();
    for (auto& ch : cfg.channels)
    {
        // pre-fill from the current delay: farthest driver (0 delay) at the largest distance
        auto f = std::make_unique<ValueField> (kit::channelShortName (ch), "cm", 0.0, 600.0, 0.5, 1);
        f->onChange = [this] (double) { body.relayout(); };
        tapeCard.addAndMakeVisible (*f);
        tapeFields.push_back (std::move (f));
        tapeIds.push_back (ch.id);
    }
    double maxDelay = 0.0;
    for (auto& ch : cfg.channels)
        maxDelay = juce::jmax (maxDelay, ch.settings.delayMs);
    const double farthestCm = 150.0;
    for (size_t i = 0; i < cfg.channels.size(); ++i)
        tapeFields[i]->setValue (farthestCm - msToMetres (maxDelay - cfg.channels[i].settings.delayMs) * 100.0);
    body.relayout();
}

void AlignmentPage::applyTape()
{
    if (tapeFields.empty())
        return;
    double maxD = 0.0;
    for (auto& f : tapeFields)
        maxD = juce::jmax (maxD, f->getValue());

    const double cap = session.getConfig().caps.maxDelayMs;
    int clipped = 0;
    editConfig ([&] (SystemConfig& cfg)
    {
        for (size_t i = 0; i < tapeIds.size(); ++i)
            if (auto* ch = cfg.find (tapeIds[i]))
            {
                const double ms = metresToMs ((maxD - tapeFields[i]->getValue()) / 100.0);
                if (ms > cap) ++clipped;
                ch->settings.delayMs = juce::jlimit (0.0, cap, ms);
            }
    });
    toast (clipped > 0 ? juce::String (clipped) + " delay(s) limited by the DSP's maximum delay"
                       : "Delays set from distances - farthest driver at 0 ms",
           clipped > 0 ? AppShell::ToastKind::warn : AppShell::ToastKind::good);
    tapeBtn.triggerClick();
}

//==============================================================================
int AlignmentPage::layoutContent (int w)
{
    using namespace kit;
    const int innerW = w - 2 * margin;
    const int chrome = cardChrome (tableCard);
    const int tableMinW = 24 + AlignmentRow::nameW + AlignmentRow::arrivalW + AlignmentRow::delayW + gap + AlignmentRow::cmW
                          + AlignmentRow::polW + AlignmentRow::gainW + AlignmentRow::muteW + 2 * gap2;
    const bool twoCols = innerW >= tableMinW + 340 + gap2;
    int y = margin;

    // ---- table card content height
    const int toolbarH = btnH + gap2;
    const int tableContentH = toolbarH + 22 + (int) rows.size() * AlignmentRow::height;
    const int tableH = chrome + tableContentH;

    const int carW = twoCols ? juce::jlimit (340, 520, innerW - tableMinW - gap2) : innerW;
    const int tableW = twoCols ? innerW - carW - gap2 : innerW;
    const int carH = twoCols ? juce::jmax (tableH, 420) : juce::jlimit (380, 520, (int) (innerW * 0.6));

    carCard.setBounds (margin, y, carW, carH);
    {
        auto a = carCard.getContentArea();
        const int hintH = carHint.heightFor (a.getWidth());
        carHint.setBounds (a.removeFromBottom (hintH));
        a.removeFromBottom (gap);
        car.setBounds (a.reduced (gap));
    }

    const int tableX = twoCols ? margin + carW + gap2 : margin;
    const int tableY = twoCols ? y : y + carH + gap2;
    tableCard.setBounds (tableX, tableY, tableW, tableH);
    {
        auto a = tableCard.getContentArea();
        auto tools = a.removeFromTop (btnH);
        zeroBtn.setBounds (tools.removeFromLeft (150));
        tools.removeFromLeft (gap);
        tapeBtn.setBounds (tools.removeFromLeft (230));
        tools.removeFromLeft (gap);
        sendBtn.setBounds (tools.removeFromLeft (124));
        a.removeFromTop (gap2);
        header.setBounds (a.removeFromTop (22));
        for (auto& r : rows)
            r->setBounds (a.removeFromTop (AlignmentRow::height));
    }
    y = twoCols ? y + juce::jmax (carH, tableH) + gap2 : tableY + tableH + gap2;

    // ---- timeline
    timelineCard.setBounds (margin, y, innerW, 200);
    {
        auto a = timelineCard.getContentArea();
        const int h = timeline.idealHeight();
        timeline.setBounds (a.getX(), a.getY(), a.getWidth(), h);
        timelineCard.setSize (innerW, chrome + h);
        y += timelineCard.getHeight() + gap2;
    }

    // ---- tape measure
    if (tapeVisible)
    {
        tapeCard.setBounds (margin, y, innerW, 200);
        auto a = tapeCard.getContentArea();
        std::vector<juce::Component*> items;
        for (auto& f : tapeFields) items.push_back (f.get());
        int yy = a.getY() + gridLayout (a, items, 150, fieldH, gap, gap) + gap2;

        double maxD = 0.0;
        for (auto& f : tapeFields) maxD = juce::jmax (maxD, f->getValue());
        juce::String preview = "Farthest driver at " + juce::String (maxD, 1) + " cm gets 0 ms. ";
        if (! tapeFields.empty())
        {
            double minD = maxD;
            for (auto& f : tapeFields) minD = juce::jmin (minD, f->getValue());
            preview << "Nearest driver (" << juce::String (minD, 1) << " cm) will be delayed by "
                    << theme::formatMs (metresToMs ((maxD - minD) / 100.0), 2) << ". Speed of sound 343 m/s.";
        }
        tapePreview.setText (preview);
        tapeApply.setBounds (a.getX(), yy, 150, btnH);
        tapeCancel.setBounds (a.getX() + 150 + gap, yy, 90, btnH);
        const int pw = a.getWidth() - 240 - 2 * gap - gap2;
        tapePreview.setBounds (a.getX() + 240 + gap + gap2, yy, pw, juce::jmax (btnH, tapePreview.heightFor (pw)));
        yy += juce::jmax (btnH, tapePreview.getHeight());
        tapeCard.setSize (innerW, chrome + (yy - a.getY()));
        y += tapeCard.getHeight() + gap2;
    }

    // ---- process note
    noteCard.setBounds (margin, y, innerW, 200);
    {
        auto a = noteCard.getContentArea();
        const int h = noteText.heightFor (a.getWidth());
        noteText.setBounds (a.getX(), a.getY(), a.getWidth(), h);
        noteCard.setSize (innerW, chrome + h);
        y += noteCard.getHeight();
    }

    return y + margin;
}
} // namespace fa::ui
