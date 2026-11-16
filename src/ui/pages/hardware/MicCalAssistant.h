#pragma once

#include "../system/PageKit.h"
#include "../../../core/measure/CalibrationDownloader.h"
#include "../../../core/Session.h"

namespace fa::ui
{
/** Compact "get my mic calibration" helper: serial number -> official miniDSP page (serial on the clipboard),
    import from Downloads / a picked file, 0 or 90 degree choice, and the generic uncalibrated ECM8000 option. */
class MicCalAssistant : public juce::Component
{
public:
    using D = CalibrationDownloader;

    explicit MicCalAssistant (Session& s) : session (s)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &serialCaption, &serialEd, &angle, &micBox, &openBtn,
                                                                 &importBtn, &fileBtn, &genericBtn, &status })
            addAndMakeVisible (c);

        serialEd.setFont (theme::mono (14.0f));
        serialEd.setTextToShowWhenEmpty ("e.g. 700-1870", theme::textMuted);
        serialEd.setInputRestrictions (14, "0123456789-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz ");
        serialEd.setColour (juce::TextEditor::backgroundColourId, theme::bg1);
        serialEd.setColour (juce::TextEditor::textColourId, theme::text);
        serialEd.setColour (juce::TextEditor::outlineColourId, theme::border);
        serialEd.setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
        kit::styleCombo (micBox);
        angle.setOptions ({ "90 deg (car)", "0 deg" });
        angle.setTooltip ("90 deg: mic points at the roof (recommended in cars). 0 deg: mic points at the speakers.");

        openBtn.onClick = [this]
        {
            const auto s = D::normaliseSerial (serialEd.getText());
            if (! D::isValidSerial (s)) { say ("Enter the serial number printed on the mic (UMIK-1: 7xx-xxxx).", false); return; }
            juce::SystemClipboard::copyTextToClipboard (s);
            juce::URL (D::productPageUrl (D::modelForSerial (s))).launchInDefaultBrowser();
            say ("miniDSP page opened, serial copied. Paste it in 'Unique Calibration File Download', submit, then press Import.", true);
        };
        importBtn.onClick = [this] { importDownloads(); };
        fileBtn.onClick = [this] { pickFile(); };
        genericBtn.onClick = [this]
        {
            const auto f = D::writeGenericFile (D::defaultCalibrationDir());
            apply (f, "Generic ECM8000 / UCM8000 loaded (flat, uncalibrated). " + D::genericAccuracyNote());
        };
        genericBtn.setTooltip (D::genericAccuracyNote());
        say ("Type the serial number: the app opens miniDSP's official page and files the downloaded 0/90 deg files for you.", true);
    }

    void setMicCount (int n)
    {
        const int sel = juce::jmax (0, micBox.getSelectedItemIndex());
        micBox.clear (juce::dontSendNotification);
        for (int i = 0; i < n; ++i) micBox.addItem ("Mic " + juce::String (i + 1), i + 1);
        micBox.setSelectedItemIndex (juce::jmin (sel, juce::jmax (0, n - 1)), juce::dontSendNotification);
    }

    std::function<void (const juce::String&, bool good)> onMessage;
    std::function<void()> onApplied;

    static int heightFor (int width) { return width >= 760 ? 118 : 196; }

    void resized() override
    {
        auto b = getLocalBounds().reduced (14, 12);
        const bool wide = getWidth() >= 760;
        const int h = kit::btnH;

        auto top = b.removeFromTop (h + 18);
        auto cap = top.removeFromTop (18);
        serialCaption.setBounds (cap.removeFromLeft (170));
        auto row = top;
        serialEd.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (kit::gap);
        angle.setBounds (row.removeFromLeft (200));
        row.removeFromLeft (kit::gap);
        micBox.setBounds (row.removeFromLeft (110));

        if (wide)
        {
            row.removeFromLeft (kit::gap);
            openBtn.setBounds (row.removeFromLeft (170).withHeight (h));
            row.removeFromLeft (kit::gap);
            importBtn.setBounds (row.removeFromLeft (150).withHeight (h));
            row.removeFromLeft (kit::gap);
            fileBtn.setBounds (row.removeFromLeft (110).withHeight (h));
            row.removeFromLeft (kit::gap);
            genericBtn.setBounds (row.removeFromLeft (170).withHeight (h));
        }
        else
        {
            b.removeFromTop (kit::gap);
            auto r2 = b.removeFromTop (h);
            openBtn.setBounds (r2.removeFromLeft (170));
            r2.removeFromLeft (kit::gap);
            importBtn.setBounds (r2.removeFromLeft (150));
            b.removeFromTop (kit::gap);
            auto r3 = b.removeFromTop (h);
            fileBtn.setBounds (r3.removeFromLeft (110));
            r3.removeFromLeft (kit::gap);
            genericBtn.setBounds (r3.removeFromLeft (170));
        }
        b.removeFromTop (kit::gap);
        status.setBounds (b);
    }

    void paint (juce::Graphics& g) override
    {
        theme::fillCard (g, getLocalBounds().toFloat().reduced (0.5f), theme::bg1.withAlpha (0.6f), theme::border, theme::radiusSm);
    }

private:
    void say (const juce::String& m, bool good)
    {
        status.setText (m);
        status.setTextColour (good ? theme::textDim : theme::warn);
        if (onMessage) onMessage (m, good);
    }

    D::Angle wantedAngle() const { return angle.getSelected() == 0 ? D::Angle::deg90 : D::Angle::deg0; }

    void apply (const juce::File& f, const juce::String& okMsg)
    {
        juce::String err;
        if (session.loadCalibration (juce::jmax (0, micBox.getSelectedItemIndex()), f, err))
        {
            say (okMsg, true);
            if (onApplied) onApplied();
        }
        else
            say (err, false);
    }

    void importDownloads()
    {
        const auto s = D::normaliseSerial (serialEd.getText());
        if (! D::isValidSerial (s)) { say ("Enter the serial number first.", false); return; }
        auto res = D::importFromFolder (D::defaultDownloadsDir(), s, D::defaultCalibrationDir());
        if (res.empty())
        {
            // maybe imported before
            const auto stored = D::findStored (D::defaultCalibrationDir(), s, wantedAngle());
            if (stored.existsAsFile())
                apply (stored, "Using stored calibration for " + s + ".");
            else
                say ("No " + D::miniDspFileName (s, D::Angle::deg0) + " / " + D::miniDspFileName (s, D::Angle::deg90)
                     + " found in Downloads. Use 'Open miniDSP page' first, or 'Import file'.", false);
            return;
        }
        const auto f = D::findStored (D::defaultCalibrationDir(), s, wantedAngle());
        if (f.existsAsFile())
        {
            const auto cal = fa::MicCalibration::load (f);
            apply (f, "UMIK " + s + (wantedAngle() == D::Angle::deg90 ? " 90 deg" : " 0 deg") + " loaded, Sens "
                          + theme::formatDb (cal.sensitivityDb, 2) + " (" + juce::String ((int) res.size()) + " file(s) saved).");
        }
        else
            say ("Imported, but the " + juce::String (wantedAngle() == D::Angle::deg90 ? "90" : "0") + " deg file was not among them.", false);
    }

    void pickFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import calibration file", D::defaultDownloadsDir(), "*.txt;*.cal;*.csv");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile()) return;
            auto r = D::importFile (file, serialEd.getText(), D::defaultCalibrationDir());
            if (r.ok)
            {
                serialEd.setText (r.serial, false);
                apply (r.saved, "Imported " + r.cal.name + ", Sens " + theme::formatDb (r.cal.sensitivityDb, 2) + ".");
            }
            else
                say (r.error, false);
        });
    }

    Session& session;
    kit::Caption serialCaption { "Mic serial number" };
    juce::TextEditor serialEd;
    SegmentedControl angle;
    juce::ComboBox micBox;
    FaButton openBtn { "Open miniDSP page", FaButton::Style::primary, icons::Id::external };
    FaButton importBtn { "Import downloads", FaButton::Style::secondary, icons::Id::download };
    FaButton fileBtn { "Import file", FaButton::Style::ghost, icons::Id::folder };
    FaButton genericBtn { "Generic ECM8000 (uncalibrated)", FaButton::Style::ghost, icons::Id::warning };
    kit::Note status;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace fa::ui
