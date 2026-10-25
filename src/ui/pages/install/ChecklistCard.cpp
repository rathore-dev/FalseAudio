#include "ChecklistCard.h"

namespace fa::ui
{
namespace
{
    constexpr int stripW = 3, tickZone = 44, valueW = 116, chevronW = 28, badgeW = 104;
    constexpr int innerPadX = 12, expandPadY = 10;

    const juce::Font detailFont() { return theme::font (13.0f); }
    const juce::Font whyFont()    { return theme::font (12.5f); }

    int textLeft (int rowX) { return rowX + stripW + tickZone + valueW; }
}

ChecklistCard::ChecklistCard (juce::File tickFile)
    : Card ("Step-by-step instructions", "Every manual setting, exactly. Tick each one off as you go."),
      ticks (std::move (tickFile))
{
    addAndMakeVisible (view);
    addAndMakeVisible (copyBtn);
    addAndMakeVisible (exportBtn);
    addAndMakeVisible (resetBtn);

    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (asText (false));
        copyBtn.setButtonText ("Copied");
        juce::Timer::callAfterDelay (1400, [sp = juce::Component::SafePointer<ChecklistCard> (this)]
        {
            if (sp != nullptr) sp->copyBtn.setButtonText ("Copy all");
        });
    };
    exportBtn.onClick = [this] { exportChecklist(); };
    resetBtn.onClick = [this]
    {
        juce::StringArray keys;
        for (auto& r : rows) keys.add (r.key);
        ticks.clearAll (keys);
        updateProgressText();
        view.repaint();
        if (onProgressChanged) onProgressChanged();
    };
}

void ChecklistCard::setInstructions (const std::vector<install::Instruction>& list)
{
    rows.clear();
    groupsInOrder.clear();
    for (auto& ins : list)
    {
        Row r;
        r.ins = ins;
        r.area = ikit::classifyArea (ins.area);
        r.key = ikit::instructionKey (ins);
        r.expanded = ins.severity == ikit::Severity::danger && ! ticks.isDone (r.key);
        rows.push_back (std::move (r));
    }
    for (int a = 0; a < ikit::numAreas; ++a)
        for (auto& r : rows)
            if ((int) r.area == a) { groupsInOrder.push_back ((ikit::Area) a); break; }

    layoutWidth = -1;
    updateProgressText();
    view.repaint();
}

int ChecklistCard::countDone() const
{
    int n = 0;
    for (auto& r : rows) if (ticks.isDone (r.key)) ++n;
    return n;
}

int ChecklistCard::countOpen (ikit::Severity s) const
{
    int n = 0;
    for (auto& r : rows) if (r.ins.severity == s && ! ticks.isDone (r.key)) ++n;
    return n;
}

void ChecklistCard::updateProgressText()
{
    const int total = countTotal(), done = countDone();
    if (total == 0)
        setTitle ("Step-by-step instructions", "Every manual setting, exactly. Tick each one off as you go.");
    else if (done == total)
        setTitle ("Step-by-step instructions", "All " + juce::String (total) + " steps done - your install matches the plan.");
    else
        setTitle ("Step-by-step instructions", juce::String (done) + " of " + juce::String (total)
                      + " steps done. Click a row for the how and the why; tick it when it's set.");
}

// ---- layout --------------------------------------------------------------------------------------
void ChecklistCard::rebuildLayout (int width) const
{
    items.clear();
    int y = 0;
    const int textW = juce::jmax (120, width - textLeft (0) - innerPadX - chevronW - 8);

    for (size_t gi = 0; gi < groupsInOrder.size(); ++gi)
    {
        Item head;
        head.group = (int) gi;
        head.bounds = { 0, y, width, groupH };
        items.push_back (head);
        y += groupH;

        if (collapsedGroups.count ((int) gi))
            continue;

        for (size_t ri = 0; ri < rows.size(); ++ri)
        {
            auto& r = rows[ri];
            if (r.area != groupsInOrder[gi])
                continue;
            Item it;
            it.group = (int) gi;
            it.row = (int) ri;
            int h = rowCollapsedH;
            if (r.expanded)
            {
                it.detailH = ikit::textHeight (detailFont(), r.ins.detail, textW);
                it.whyH = r.ins.why.isNotEmpty() ? ikit::textHeight (whyFont(), r.ins.why, textW - 2 * innerPadX) + 18 + 2 * expandPadY : 0;
                h += (it.detailH > 0 ? it.detailH + expandPadY : 0) + (it.whyH > 0 ? it.whyH + expandPadY : 0) + expandPadY;
            }
            it.bounds = { 0, y, width, h };
            items.push_back (it);
            y += h;
        }
    }
    layoutWidth = width;
}

int ChecklistCard::viewHeight (int width) const
{
    if (layoutWidth != width)
        rebuildLayout (width);
    if (rows.empty())
        return 56;
    return items.empty() ? 0 : items.back().bounds.getBottom();
}

int ChecklistCard::heightFor (int width) const
{
    return ikit::cardHead + viewHeight (width - 2 * ikit::gap2) + ikit::gap2;
}

void ChecklistCard::resized()
{
    auto actions = getHeaderActionArea();
    const int bw = 96;
    exportBtn.setBounds (actions.removeFromRight (bw).withSizeKeepingCentre (bw, ikit::btnH));
    actions.removeFromRight (ikit::gap);
    copyBtn.setBounds (actions.removeFromRight (bw).withSizeKeepingCentre (bw, ikit::btnH));
    actions.removeFromRight (ikit::gap);
    resetBtn.setBounds (actions.removeFromRight (bw + 12).withSizeKeepingCentre (bw + 12, ikit::btnH));

    auto r = getContentArea();
    view.setBounds (r.withHeight (viewHeight (r.getWidth())));
}

void ChecklistCard::paint (juce::Graphics& g)
{
    Card::paint (g);
}

juce::Rectangle<int> ChecklistCard::tickBoxFor (juce::Rectangle<int> rowBounds)
{
    return juce::Rectangle<int> (rowBounds.getX() + stripW + (tickZone - 22) / 2, rowBounds.getY() + (rowCollapsedH - 22) / 2, 22, 22);
}

// ---- text export -----------------------------------------------------------------------------
juce::String ChecklistCard::asText (bool markdown) const
{
    juce::String out;
    out << (markdown ? "# " : "") << "FalseAudio - Install & Gains checklist" << juce::newLine
        << juce::Time::getCurrentTime().toString (true, true, false, true) << juce::newLine << juce::newLine;

    for (auto area : groupsInOrder)
    {
        out << (markdown ? "## " : "== ") << ikit::areaName (area).toUpperCase() << (markdown ? "" : " ==") << juce::newLine;
        for (auto& r : rows)
        {
            if (r.area != area) continue;
            const bool done = ticks.isDone (r.key);
            juce::String value = r.ins.unit.isNotEmpty() || r.ins.value != 0.0
                                     ? "  [" + ikit::fmtValue (r.ins.value, r.ins.unit) + (r.ins.unit.isNotEmpty() ? " " + r.ins.unit : "") + "]"
                                     : juce::String();
            out << (done ? "[x] " : "[ ] ") << r.ins.title << value
                << "  (" << ikit::severityName (r.ins.severity) << ", " << r.ins.area << ")" << juce::newLine;
            if (r.ins.detail.isNotEmpty())
                out << "    How: " << r.ins.detail.replace ("\n", "\n         ") << juce::newLine;
            if (r.ins.why.isNotEmpty())
                out << "    Why: " << r.ins.why.replace ("\n", "\n         ") << juce::newLine;
            out << juce::newLine;
        }
    }
    return out;
}

void ChecklistCard::exportChecklist()
{
    auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FalseAudio install checklist.txt");
    chooser = std::make_unique<juce::FileChooser> ("Export install checklist", start, "*.txt;*.md");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              const bool md = f.hasFileExtension ("md");
                              if (! f.replaceWithText (asText (md)))
                                  juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                               "Export failed", "Could not write " + f.getFullPathName());
                          });
}

// ---- view ---------------------------------------------------------------------------------------
void ChecklistCard::View::paint (juce::Graphics& g)
{
    auto& o = owner;
    if (o.layoutWidth != getWidth())
        o.rebuildLayout (getWidth());

    if (o.rows.empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (13.0f));
        g.drawText ("Nothing to do yet - the plan has no manual steps for this equipment.", getLocalBounds(),
                    juce::Justification::centredLeft, true);
        return;
    }

    const bool wide = getWidth() >= 620;

    for (size_t idx = 0; idx < o.items.size(); ++idx)
    {
        auto& it = o.items[idx];
        auto b = it.bounds;

        if (it.row < 0)
        {
            // ---- group header
            const auto area = o.groupsInOrder[(size_t) it.group];
            int total = 0, done = 0;
            for (auto& r : o.rows)
                if (r.area == area) { ++total; if (o.ticks.isDone (r.key)) ++done; }
            const bool collapsed = o.collapsedGroups.count (it.group) > 0;
            const bool hover = hoverGroup == it.group && hoverRow < 0;

            auto hb = b.withTrimmedTop (idx == 0 ? 0 : 8);
            if (hover)
            {
                g.setColour (theme::bg3.withAlpha (0.5f));
                g.fillRoundedRectangle (hb.toFloat(), theme::radiusSm);
            }
            auto row = hb.reduced (6, 0);
            auto iconBox = row.removeFromLeft (28).toFloat().withSizeKeepingCentre (24.0f, 24.0f);
            const auto tint = done == total ? theme::good : theme::accent;
            g.setColour (tint.withAlpha (0.14f));
            g.fillRoundedRectangle (iconBox, 6.0f);
            icons::draw (g, ikit::areaIcon (area), iconBox.reduced (5.0f), tint);
            row.removeFromLeft (ikit::gap + 2);

            auto chev = row.removeFromRight (chevronW).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            icons::draw (g, collapsed ? icons::Id::chevronRight : icons::Id::chevronDown, chev, theme::textMuted);

            auto progress = row.removeFromRight (wide ? 160 : 90);
            auto pt = progress.removeFromRight (wide ? 76 : 44);
            g.setColour (done == total ? theme::good : theme::textDim);
            g.setFont (theme::mono (11.5f));
            g.drawText (wide ? juce::String (done) + " of " + juce::String (total) + " done" : juce::String (done) + "/" + juce::String (total),
                        pt, juce::Justification::centredRight, false);
            ikit::drawGauge (g, progress.withTrimmedRight (ikit::gap).toFloat().withSizeKeepingCentre ((float) progress.getWidth() - ikit::gap, 4.0f),
                             total > 0 ? (float) done / (float) total : 0.0f, tint);

            g.setColour (theme::text);
            g.setFont (theme::fontBold (12.5f));
            g.drawText (ikit::areaName (area).toUpperCase(), row, juce::Justification::centredLeft, false);
            continue;
        }

        // ---- instruction row
        auto& r = o.rows[(size_t) it.row];
        const bool done = o.ticks.isDone (r.key);
        const bool hover = hoverRow == it.row;
        const auto sev = done ? theme::good : ikit::severityColour (r.ins.severity);

        auto card = b.reduced (0, 2);
        if (r.expanded || hover)
        {
            g.setColour (r.expanded ? theme::bg3.withAlpha (0.55f) : theme::bg3.withAlpha (0.35f));
            g.fillRoundedRectangle (card.toFloat(), theme::radiusSm);
            if (r.expanded)
            {
                g.setColour (theme::borderHi.withAlpha (0.7f));
                g.drawRoundedRectangle (card.toFloat().reduced (0.5f), theme::radiusSm, 1.0f);
            }
        }
        // separator
        g.setColour (theme::border.withAlpha (0.5f));
        g.fillRect (b.getX() + stripW + tickZone, b.getBottom() - 1, b.getWidth() - stripW - tickZone, 1);

        // severity strip
        g.setColour (sev);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) b.getX(), (float) b.getY() + 14.0f, (float) stripW, (float) rowCollapsedH - 28.0f), 1.5f);

        // tick
        const bool tickHover = hover && hoverTick;
        ikit::drawTickBox (g, tickBoxFor (b).toFloat(), done, tickHover);

        // value
        auto head = b.withHeight (rowCollapsedH);
        auto valueArea = head.withX (b.getX() + stripW + tickZone).withWidth (valueW).reduced (4, 0);
        const bool hasValue = r.ins.unit.isNotEmpty() || r.ins.value != 0.0;
        if (hasValue)
        {
            const auto vs = ikit::fmtValue (r.ins.value, r.ins.unit);
            const auto vf = theme::display (21.0f);
            g.setFont (vf);
            g.setColour (done ? theme::textDim : theme::text);
            const int vw = juce::jmin (valueArea.getWidth(), (int) theme::textWidth (vf, vs) + 2);
            auto va = valueArea;
            g.drawText (vs, va.removeFromLeft (vw), juce::Justification::centredLeft, false);
            if (r.ins.unit.isNotEmpty())
            {
                g.setFont (theme::fontMedium (11.5f));
                g.setColour (sev);
                g.drawText (r.ins.unit, va.withTrimmedLeft (3).withTrimmedTop (6), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            icons::draw (g, ikit::areaIcon (r.area), valueArea.toFloat().withSizeKeepingCentre (18.0f, 18.0f).withX ((float) valueArea.getX() + 4.0f),
                         theme::textMuted);
        }

        // title + area
        auto text = head.withX (textLeft (b.getX())).withRight (b.getRight() - chevronW - 8);
        auto chev = head.removeFromRight (chevronW).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        icons::draw (g, r.expanded ? icons::Id::chevronDown : icons::Id::chevronRight, chev, theme::textMuted);
        if (wide)
        {
            auto badge = text.removeFromRight (badgeW);
            ikit::drawBadge (g, badge, done ? "Done" : ikit::severityName (r.ins.severity), sev, juce::Justification::centredRight);
        }
        auto titleRow = text.withTrimmedTop (12).withHeight (20);
        g.setFont (theme::fontMedium (14.0f));
        g.setColour (done ? theme::textDim : theme::text);
        g.drawText (r.ins.title, titleRow, juce::Justification::centredLeft, true);
        if (done)
        {
            const int tw = juce::jmin (titleRow.getWidth(), (int) theme::textWidth (theme::fontMedium (14.0f), r.ins.title));
            g.setColour (theme::textDim.withAlpha (0.7f));
            g.fillRect (titleRow.getX(), titleRow.getCentreY(), tw, 1);
        }
        g.setFont (theme::font (11.5f));
        g.setColour (theme::textMuted);
        g.drawText (r.ins.area, text.withTrimmedTop (33).withHeight (16), juce::Justification::centredLeft, true);

        if (! r.expanded)
            continue;

        // ---- expanded body
        const int textX = textLeft (b.getX());
        const int textW = b.getRight() - chevronW - 8 - textX;
        int y = b.getY() + rowCollapsedH;
        if (it.detailH > 0)
        {
            ikit::drawMultiline (g, detailFont(), theme::text.withAlpha (0.92f), r.ins.detail, { textX, y, textW, it.detailH });
            y += it.detailH + expandPadY;
        }
        if (it.whyH > 0)
        {
            auto box = juce::Rectangle<int> (textX, y, textW, it.whyH);
            g.setColour (theme::bg1.withAlpha (0.8f));
            g.fillRoundedRectangle (box.toFloat(), theme::radiusSm);
            g.setColour (theme::border);
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), theme::radiusSm, 1.0f);
            auto inner = box.reduced (innerPadX, expandPadY);
            theme::drawEyebrow (g, "Why", inner.removeFromTop (16), sev.withAlpha (0.9f));
            inner.removeFromTop (2);
            ikit::drawMultiline (g, whyFont(), theme::textDim, r.ins.why, inner);
        }
    }
}

void ChecklistCard::View::mouseMove (const juce::MouseEvent& e)
{
    int newRow = -1, newGroup = -1;
    bool tick = false;
    for (auto& it : owner.items)
    {
        if (! it.bounds.contains (e.getPosition()))
            continue;
        newGroup = it.group;
        newRow = it.row;
        tick = it.row >= 0 && tickBoxFor (it.bounds).expanded (8).contains (e.getPosition());
        break;
    }
    if (newRow != hoverRow || newGroup != hoverGroup || tick != hoverTick)
    {
        hoverRow = newRow; hoverGroup = newGroup; hoverTick = tick;
        setMouseCursor (newGroup >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ChecklistCard::View::mouseExit (const juce::MouseEvent&)
{
    hoverRow = hoverGroup = -1;
    hoverTick = false;
    repaint();
}

void ChecklistCard::View::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    for (auto it : owner.items)   // copy: the vector is rebuilt below
    {
        if (! it.bounds.contains (e.getPosition()))
            continue;
        if (it.row < 0)
        {
            if (owner.collapsedGroups.count (it.group)) owner.collapsedGroups.erase (it.group);
            else                                        owner.collapsedGroups.insert (it.group);
            owner.layoutWidth = -1;
            if (owner.onLayoutChanged) owner.onLayoutChanged();
            repaint();
            return;
        }
        auto& r = owner.rows[(size_t) it.row];
        if (tickBoxFor (it.bounds).expanded (8).contains (e.getPosition()))
        {
            const bool nowDone = ! owner.ticks.isDone (r.key);
            owner.ticks.setDone (r.key, nowDone);
            if (nowDone && r.expanded)
            {
                r.expanded = false;
                owner.layoutWidth = -1;
                if (owner.onLayoutChanged) owner.onLayoutChanged();
            }
            owner.updateProgressText();
            if (owner.onProgressChanged) owner.onProgressChanged();
            repaint();
            return;
        }
        r.expanded = ! r.expanded;
        owner.layoutWidth = -1;
        if (owner.onLayoutChanged) owner.onLayoutChanged();
        repaint();
        return;
    }
}
} // namespace fa::ui
