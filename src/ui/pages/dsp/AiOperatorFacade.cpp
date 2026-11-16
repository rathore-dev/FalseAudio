#include "AiOperatorFacade.h"
#include "../../theme/Theme.h"

namespace fa::ui::dsp
{
namespace
{
    class DemoAiOperator : public AiOperatorFacade, private juce::Timer
    {
    public:
        DemoAiOperator() { budget = 2.0; }
        ~DemoAiOperator() override { stopTimer(); }

        bool hasKey() const override { return keySaved; }
        juce::String keyHint() const override { return keySaved ? "sk-ant-...d3mo" : juce::String(); }
        bool saveKey (const juce::String& key, juce::String& error) override
        {
            if (key.trim().length() < 8) { error = "That does not look like an Anthropic API key."; return false; }
            keySaved = true;
            return true;
        }
        void testKey (std::function<void (bool, juce::String)> done) override
        {
            juce::Timer::callAfterDelay (900, [done, ok = keySaved]
            {
                if (done) done (ok, ok ? "Key works (demo: no request was sent)." : "No key saved.");
            });
        }
        void removeKey() override { keySaved = false; }

        juce::String getProvider() const override { return provider; }
        void setProvider (const juce::String& p) override { provider = p; }
        juce::String getOllamaUrl() const override { return ollamaUrl; }
        juce::String getOllamaModel() const override { return ollamaModel; }
        void setOllama (const juce::String& u, const juce::String& m) override { ollamaUrl = u; ollamaModel = m; }
        void testOllama (std::function<void (bool, juce::String)> done) override
        {
            juce::Timer::callAfterDelay (700, [done, m = ollamaModel]
            {
                if (done) done (true, "Ollama reachable (demo), model " + m + " is installed.");
            });
        }

        std::vector<ModelOption> models() const override
        {
            return { { "opus-5.5", "Opus 5.5", "most reliable, about $0.60 per learn run" },
                     { "sonnet-5.5", "Sonnet 5.5", "cheaper, about $0.15 per learn run" } };
        }
        void setModel (const juce::String& id) override { model = id; }
        juce::String getModel() const override { return model; }
        void setBudgetUsd (double usd) override { budget = usd; }
        double getBudgetUsd() const override { return budget; }

        void setTarget (const juce::String& p, const juce::String& t) override { proc = p; title = t; }
        SoftwareInfo softwareInfo() const override
        {
            return { "miniDSP", "Device Console", "https://www.minidsp.com/products/minidsp-in-a-box", false };
        }

        void setConsent (bool a) override { consent = a; }
        bool hasConsent() const override { return consent; }

        bool start (bool forceRelearn, juce::String& error) override
        {
            if (state == State::running) return true;
            if (provider == "claude" && ! keySaved) { error = "Save your Anthropic API key first."; return false; }
            if (proc.isEmpty()) { error = "Attach or launch your DSP software first."; return false; }
            if (! consent)   { error = "Allow the AI to control the window first."; return false; }
            relearn = forceRelearn || ! map.exists;
            tick = 0; spent = {}; spent.budgetUsd = budget;
            state = State::running;
            if (onState) onState (state, "AI operator started");
            if (onCost) onCost (spent);
            startTimer (550);
            return true;
        }
        void stop() override
        {
            if (state != State::running) return;
            stopTimer();
            state = State::stopped;
            log ("safety", "STOP pressed - all actions halted", "kill switch");
            if (onState) onState (state, "Stopped by you. Nothing further was written.");
        }
        State getState() const override { return state; }

        MapSummary getMap() const override { return map; }
        void deleteMap() override { map = {}; if (onMapChanged) onMapChanged(); }

    private:
        struct Ev { Step step; StepState ss; const char* kind; const char* what; const char* why; int tokIn, tokOut; };

        void timerCallback() override
        {
            static const Ev script[] = {
                { Step::learn, StepState::active, "see",   "Took a screenshot of the DSP window",               "to find the output tabs and channel strips", 2400, 120 },
                { Step::learn, StepState::active, "think", "Found 8 output strips and a mixer matrix",         "layout looks like a 2x8 matrix with per-output processing", 0, 260 },
                { Step::learn, StepState::done,   "click", "Opened the Out 1 tab",                              "to see gain, delay, crossover and PEQ controls", 2600, 90 },
                { Step::map,   StepState::active, "read",  "Read the name field of Out 1..Out 8",               "matching them to your configured channels", 0, 180 },
                { Step::map,   StepState::done,   "think", "Out 1/2 tweeters, 3/4 midbass, 5/6 mids, 7 sub",    "from labels and your system config", 0, 210 },
                { Step::verify, StepState::active, "safety", "Muted all outputs except Out 1",                  "safe state before playing the test tone", 0, 40 },
                { Step::verify, StepState::active, "click", "Set Out 1 gain to -20 dB and played a test tone",  "to confirm the control I found really is Out 1 gain", 2500, 110 },
                { Step::verify, StepState::done,   "read", "Microphone level moved by 20.1 dB",                 "gain control verified", 0, 70 },
                { Step::apply,  StepState::active, "type", "Typed -3.5 into Out 1 gain",                        "tune value for Front L tweeter", 1800, 60 },
                { Step::apply,  StepState::active, "type", "Typed 0.42 ms into Out 1 delay",                    "time alignment from the measurement", 0, 60 },
                { Step::apply,  StepState::active, "click","Clicked Out 1 polarity (normal)",                   "tune says normal polarity", 0, 50 },
                { Step::apply,  StepState::active, "type", "Set Out 1 HP to 2.5 kHz, 24 dB/oct",                "crossover from the planner", 1900, 90 },
                { Step::apply,  StepState::done,   "type", "Applied 7 more outputs the same way",               "every value was inside the safety limits", 0, 400 },
                { Step::readback, StepState::active, "see", "Re-read every field from the screen",             "verify the software accepted each write", 2700, 150 },
                { Step::readback, StepState::done, "read", "46 of 46 values match the tune",                    "read-back check passed", 0, 60 },
            };
            constexpr int n = (int) (sizeof (script) / sizeof (script[0]));

            if (tick >= n)
            {
                stopTimer();
                finishRun();
                return;
            }
            const auto& e = script[tick];

            // when the map is already learned, skip learn/map quickly (no AI calls)
            if (! relearn && (e.step == Step::learn || e.step == Step::map))
            {
                if (onStep) onStep (e.step, e.ss);
                if (e.step == Step::learn && e.ss == StepState::done) log ("read", "Loaded the saved map for this software", "no AI call needed");
                ++tick;
                return;
            }
            if (onStep) onStep (e.step, e.ss);
            log (e.kind, e.what, e.why);
            spent.tokensIn += e.tokIn; spent.tokensOut += e.tokOut;
            if (e.tokIn > 0 || e.tokOut > 0) ++spent.calls;
            const double k = model == "opus-5.5" ? 1.0 : 0.2;
            spent.usd = provider == "ollama" ? 0.0 : spent.tokensIn * 15.0e-6 * k + spent.tokensOut * 75.0e-6 * k;
            if (onCost) onCost (spent);
            if (onThumbnail && (tick % 3 == 0)) onThumbnail (makeThumb (tick));
            ++tick;
        }

        void finishRun()
        {
            if (relearn)
            {
                map.exists = true;
                map.software = title.isNotEmpty() ? title : proc;
                map.learnedOn = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
                map.outputs.clear();
                const char* drv[] = { "Front L tweeter", "Front R tweeter", "Front L midbass", "Front R midbass", "Front L mid", "Front R mid", "Subwoofer", "(unassigned)" };
                for (int i = 0; i < 8; ++i)
                {
                    MapOutput o;
                    o.output = "Out " + juce::String (i + 1);
                    o.driver = drv[i];
                    o.found = { "Gain", "Delay", "Polarity", "HP", "LP", "Slope", "PEQ", "Mute" };
                    if (i == 7) { o.found = { "Gain", "Mute" }; o.missing = { "Delay", "Polarity", "HP", "LP", "Slope", "PEQ" }; }
                    if (i == 6) { o.missing = { "HP" }; o.found.removeString ("HP"); }
                    map.outputs.push_back (std::move (o));
                }
                if (onMapChanged) onMapChanged();
            }
            state = State::done;
            if (onState) onState (state, "Tune applied and verified.");
        }

        void log (const juce::String& kind, const juce::String& what, const juce::String& why)
        {
            if (onLog) onLog ({ juce::Time::getCurrentTime().formatted ("%H:%M:%S"), kind, what, why });
        }

        juce::Image makeThumb (int t) const
        {
            juce::Image img (juce::Image::ARGB, 320, 200, true);
            juce::Graphics g (img);
            g.fillAll (juce::Colour (0xff1B1F27));
            g.setColour (juce::Colour (0xff2A3140)); g.fillRect (0, 0, 320, 18);
            g.setColour (theme::textDim); g.setFont (10.0f);
            g.drawText (proc.isEmpty() ? "DSP software" : proc, 8, 2, 200, 14, juce::Justification::left);
            for (int i = 0; i < 8; ++i)
            {
                const int x = 10 + i * 38;
                const bool hot = i == (t / 3) % 8;
                g.setColour (juce::Colour (0xff232A36)); g.fillRoundedRectangle ((float) x, 28.f, 34.f, 160.f, 3.f);
                g.setColour (hot ? theme::accent : juce::Colour (0xff3A4558));
                g.drawRoundedRectangle ((float) x, 28.f, 34.f, 160.f, 3.f, hot ? 1.6f : 1.0f);
                g.setColour (juce::Colour (0xff4A5870)); g.fillRect (x + 14, 60 + (i * 7) % 40, 6, 90);
                g.setColour (theme::accentDim); g.fillRect (x + 8, 40, 18, 8);
            }
            return img;
        }

        bool keySaved = false, consent = false, relearn = true;
        juce::String model = "opus-5.5", proc, title, provider = "claude", ollamaUrl = "http://localhost:11434", ollamaModel = "qwen3-vl:4b";
        double budget = 2.0;
        State state = State::idle;
        int tick = 0;
        Cost spent;
        MapSummary map;
    };
}

std::unique_ptr<AiOperatorFacade> createDemoAiOperator() { return std::make_unique<DemoAiOperator>(); }
} // namespace fa::ui::dsp
