#include "AiOperatorLive.h"
#include "core/DspSoftware.h"
#include "core/ai/ApiKeyStore.h"
#include "core/ai/AiProfileStore.h"
#include "core/ai/ClaudeClient.h"

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace fa::ui::dsp
{
namespace
{
    constexpr const char* kOpus = "claude-opus-5-5";
    constexpr const char* kSonnet = "claude-sonnet-5-5";

    juce::String kindFor (const juce::String& s)
    {
        const auto t = s.toLowerCase();
        if (t.contains ("denied") || t.contains ("refus") || t.contains ("safety") || t.contains ("killed") || t.contains ("limit")) return "safety";
        if (t.contains ("error") || t.contains ("fail")) return "error";
        if (t.contains ("screenshot")) return "see";
        if (t.contains ("click") || t.contains ("invoke") || t.contains ("toggle") || t.contains ("select")) return "click";
        if (t.contains ("type") || t.contains ("set_value") || t.contains ("write") || t.contains ("set ")) return "type";
        if (t.contains ("read")) return "read";
        return "think";
    }

#if JUCE_WINDOWS
    juce::Image captureWindow (void* hwndPtr)
    {
        auto hwnd = (HWND) hwndPtr;
        RECT rc;
        if (hwnd == nullptr || ! IsWindow (hwnd) || ! GetWindowRect (hwnd, &rc))
            return {};
        const int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w < 16 || h < 16)
            return {};

        HDC screen = GetDC (nullptr);
        HDC mem = CreateCompatibleDC (screen);
        HBITMAP bmp = CreateCompatibleBitmap (screen, w, h);
        auto old = SelectObject (mem, bmp);
        PrintWindow (hwnd, mem, 0x2 /* PW_RENDERFULLCONTENT */);

        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        juce::Image img (juce::Image::ARGB, w, h, false);
        {
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            GetDIBits (mem, bmp, 0, (UINT) h, bd.data, &bi, DIB_RGB_COLORS);
            for (int y = 0; y < h; ++y)
            {
                auto* p = (juce::uint8*) bd.getLinePointer (y);
                for (int x = 0; x < w; ++x) p[x * 4 + 3] = 255;
            }
        }
        SelectObject (mem, old);
        DeleteObject (bmp);
        DeleteDC (mem);
        ReleaseDC (nullptr, screen);
        return img.getWidth() > 400 ? img.rescaled (400, juce::roundToInt (400.0 * h / w), juce::Graphics::mediumResamplingQuality) : img;
    }
#else
    juce::Image captureWindow (void*) { return {}; }
#endif

    class LiveAiOperator : public AiOperatorFacade, private juce::Timer
    {
    public:
        LiveAiOperator (Session& s, std::function<void*()> handle) : session (s), windowHandle (std::move (handle)) { ps = keys.loadProviderSettings(); }
        ~LiveAiOperator() override
        {
            stopTimer();
            *alive = false;
            if (auto* ai = session.getAiOperator())
                if (state == State::running) ai->kill ("Panel closed");
        }

        // ---- key
        bool hasKey() const override { return keys.resolve().isNotEmpty(); }
        juce::String keyHint() const override { return ApiKeyStore::maskedHint (keys.resolve()); }
        bool saveKey (const juce::String& key, juce::String& error) override { return keys.save (key, error); }
        void removeKey() override { keys.clear(); }
        void testKey (std::function<void (bool, juce::String)> done) override
        {
            auto live = alive;
            const auto k = keys.resolve();
            if (k.isEmpty()) { if (done) done (false, "No key saved."); return; }
            juce::Thread::launch ([live, k, done]
            {
                ClaudeClient::Config cfg;
                cfg.model = kSonnet;
                cfg.maxTokens = 16;
                cfg.maxRetries = 0;
                cfg.timeoutMs = 20000;
                ClaudeClient client (cfg, nullptr, [k] { return k; });

                auto* msg = new juce::DynamicObject();
                msg->setProperty ("role", "user");
                msg->setProperty ("content", "Reply with the single word: ok");
                juce::Array<juce::var> messages { juce::var (msg) };
                const auto r = client.createMessage ("You are a connectivity check.", juce::var (juce::Array<juce::var>()), juce::var (messages));
                const bool ok = r.ok;
                const auto text = ok ? juce::String ("Key works.") : (r.httpStatus == 401 ? juce::String ("Anthropic rejected this key (401).")
                                                                                      : "Test failed: " + r.error);
                juce::MessageManager::callAsync ([live, done, ok, text] { if (*live && done) done (ok, text); });
            });
        }

        // ---- provider
        juce::String getProvider() const override { return ps.provider; }
        void setProvider (const juce::String& p) override { ps.provider = p; keys.saveProviderSettings (ps); }
        juce::String getOllamaUrl() const override { return ps.ollamaUrl; }
        juce::String getOllamaModel() const override { return ps.ollamaModel; }
        void setOllama (const juce::String& url, const juce::String& m) override
        {
            ps.ollamaUrl = url.trim().isEmpty() ? juce::String ("http://localhost:11434") : url.trim();
            ps.ollamaModel = m.trim().isEmpty() ? juce::String ("qwen3-vl:4b") : m.trim();
            keys.saveProviderSettings (ps);
        }
        void testOllama (std::function<void (bool, juce::String)> done) override
        {
            auto live = alive;
            const auto base = ps.ollamaUrl.trimCharactersAtEnd ("/");
            const auto wanted = ps.ollamaModel;
            juce::Thread::launch ([live, base, wanted, done]
            {
                bool ok = false;
                juce::String msg;
                auto stream = juce::URL (base + "/api/tags").createInputStream (
                    juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (4000));
                if (stream == nullptr)
                    msg = "Ollama is not running at " + base + ". Start it (ollama serve) and try again.";
                else
                {
                    const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
                    bool found = false;
                    if (auto* arr = json.getProperty ("models", {}).getArray())
                        for (auto& m : *arr)
                        {
                            const auto n = m.getProperty ("name", {}).toString();
                            if (n.equalsIgnoreCase (wanted) || n.upToFirstOccurrenceOf (":", false, false).equalsIgnoreCase (wanted)) found = true;
                        }
                    ok = found;
                    msg = found ? "Ollama is running and " + wanted + " is installed."
                                : "Ollama is running, but " + wanted + " is not installed. Run: ollama pull " + wanted;
                }
                juce::MessageManager::callAsync ([live, done, ok, msg] { if (*live && done) done (ok, msg); });
            });
        }

        // ---- settings
        std::vector<ModelOption> models() const override
        {
            return { { kOpus, "Opus 5.5", "most reliable at reading unfamiliar software" },
                     { kSonnet, "Sonnet 5.5", "cheaper, good for software you have used before" } };
        }
        void setModel (const juce::String& id) override { model = id; }
        juce::String getModel() const override { return model; }
        void setBudgetUsd (double usd) override { budget = usd; }
        double getBudgetUsd() const override { return budget; }

        void setTarget (const juce::String& p, const juce::String& t) override { proc = p; title = t; }

        SoftwareInfo softwareInfo() const override
        {
            SoftwareInfo si;
            const auto stem = juce::File (proc).getFileNameWithoutExtension();
            for (auto& v : vendors())
                for (auto& exe : v.exeNames)
                    if (stem.isNotEmpty() && stem.equalsIgnoreCase (juce::File (exe).getFileNameWithoutExtension()))
                        return { v.vendor, v.software, v.officialDownloadPage, true };
            return si;     // unknown software: no vendor button
        }
        bool hasVendorList() const override { return true; }
        std::vector<SoftwareInfo> vendorList() const override
        {
            std::vector<SoftwareInfo> out;
            for (auto& v : vendors())
                if (v.officialDownloadPage.isNotEmpty())
                    out.push_back ({ v.vendor, v.software, v.officialDownloadPage, DspSoftware::detect (v).installed });
            return out;
        }

        void setConsent (bool allowed) override { consent = allowed; keys.setConsent (allowed); }
        bool hasConsent() const override { return consent; }

        // ---- run
        bool start (bool forceRelearn, juce::String& error) override
        {
            if (state == State::running) return true;
            const bool local = ps.provider == "ollama";
            if (! local && ! hasKey())  { error = "Save your Anthropic API key first."; return false; }
            if (proc.isEmpty() || windowHandle == nullptr || windowHandle() == nullptr)
                { error = "Attach or launch your DSP software first."; return false; }
            if (! consent)   { error = "Allow the AI to control the window first."; return false; }
            if (session.getMode() != Session::Mode::live) { error = "Switch to Live mode to operate real software."; return false; }
            if (session.getAutoTune().isRunning()) { error = "Auto Tune is running."; return false; }

            // Same resolver the controller loads with: the map is found by the exact title OR by the fragment the AI saved it under.
            if (forceRelearn) AiProfileStore().removeFor (title);

            AiDspController::Options o;
            o.windowProvider = windowHandle;
            o.client.model = model.isEmpty() ? juce::String (kOpus) : model;
            if (o.client.model == kSonnet) { o.client.pricing.inputPerMTok = 2.0; o.client.pricing.outputPerMTok = 10.0; }
            o.limits.maxUsd = budget;
            o.provider = llmProviderFromString (ps.provider);
            o.ollama.baseUrl = ps.ollamaUrl;
            o.ollama.model = ps.ollamaModel;
            o.agent.consentGiven = consent;        // always the user's toggle, also for the local model
            o.agent.vendorHint = proc;
            const int run = ++runId;
            auto live = alive;
            o.agent.log = [this, live, run] (const juce::String& s)
            {
                juce::MessageManager::callAsync ([this, live, run, s]
                {
                    if (*live && run == runId && onLog)
                        onLog ({ juce::Time::getCurrentTime().formatted ("%H:%M:%S"), kindFor (s), s, {} });
                });
            };
            o.agent.progress = [this, live, run] (const DspOperatorAgent::Progress& p)
            {
                juce::MessageManager::callAsync ([this, live, run, p]
                {
                    if (! *live || run != runId) return;
                    cost.usd = p.usd;
                    cost.tokensIn = (int) p.tokens;      // total tokens (in+out) while learning
                    cost.tokensOut = 0;
                    cost.calls = p.turn;
                    cost.budgetUsd = budget;
                    if (onCost) onCost (cost);
                    if (p.bindings > 0 && ! mapStepOn) { mapStepOn = true; step (Step::learn, StepState::done); step (Step::map, StepState::active); }
                    if (p.usd >= budget * 0.98)
                        if (auto* ai = session.getAiOperator()) ai->kill ("Budget cap reached");
                });
            };
            // agent.verify is left empty here: Session fills it (verify_channel handler).

            if (! session.useAiOperator (std::move (o), error))
                return false;
            auto* ai = session.getAiOperator();
            if (ai == nullptr) { error = "The AI operator could not be created."; return false; }

            state = State::running;
            mapStepOn = false;
            cost = {}; cost.budgetUsd = budget;
            if (onState) onState (state, "AI operator started");
            if (onCost) onCost (cost);
            startTimer (1500);                      // thumbnails
            const bool needLearn = ! ai->hasMap();
            step (Step::learn, StepState::active);

            // Learn (if there is no map), then verify (if the map is not verified yet), then push the tune.
            // Nothing is pushed through a map that did not pass a per-output test sound.
            const bool needVerify = needLearn || ! ai->isMapVerified();
            if (! needLearn)
            {
                if (onLog) onLog ({ juce::Time::getCurrentTime().formatted ("%H:%M:%S"), "read", "Loaded the saved map for this software",
                                    needVerify ? "not verified yet: testing each output" : "verified, no AI call needed" });
                step (Step::learn, StepState::done);
                step (Step::map, StepState::done);
                if (! needVerify)
                {
                    step (Step::verify, StepState::done);
                    beginApply (run);
                    return true;
                }
                step (Step::verify, StepState::active);
            }

            juce::Thread::launch ([this, live, run, ai, needLearn]
            {
                juce::String err;
                bool ok = true;
                if (needLearn)
                    ok = ai->learn (err);
                if (ok && ! ai->isMapVerified())
                    ok = ai->verifyMap (err);
                juce::MessageManager::callAsync ([this, live, run, ok, err]
                {
                    if (! *live || run != runId || state != State::running) return;
                    if (! ok) { finish (State::error, err.isNotEmpty() ? err : "Learning failed."); return; }
                    for (auto s : { Step::learn, Step::map, Step::verify }) step (s, StepState::done);
                    if (onMapChanged) onMapChanged();
                    beginApply (run);
                });
            });
            return true;
        }

        void stop() override
        {
            if (state != State::running) return;
            if (auto* ai = session.getAiOperator()) ai->kill ("Stopped by user");
            ++runId;
            finish (State::stopped, "Stopped by you. Nothing further was written.");
        }
        State getState() const override { return state; }

        // ---- map
        MapSummary getMap() const override
        {
            MapSummary m;
            DspProfile p;
            if (title.isEmpty() || ! AiProfileStore().loadFor (title, p) || p.bindings.empty())
                return m;
            m.exists = true;
            m.software = title;
            m.learnedOn = p.learnedAt.substring (0, 10);
            static const std::pair<const char*, const char*> controls[] = {
                { "Gain", "gain" }, { "Delay", "delay" }, { "Polarity", "polarity" }, { "HP", "hp.freq" }, { "LP", "lp.freq" },
                { "Slope", "hp.type" }, { "PEQ", "peq.freq" }, { "Mute", "mute" } };
            for (auto& ch : session.getConfig().channels)
            {
                MapOutput o;
                o.output = "Out " + juce::String (ch.dspOutput + 1);
                o.driver = ch.label.isNotEmpty() ? ch.label : "(unassigned)";
                for (auto& c : controls)
                {
                    bool has = p.find (c.second, ch.dspOutput) != nullptr;
                    if (! has && juce::String (c.second) == "hp.type") has = p.find ("lp.type", ch.dspOutput) != nullptr;
                    (has ? o.found : o.missing).add (c.first);
                }
                m.outputs.push_back (std::move (o));
            }
            return m;
        }
        void deleteMap() override { AiProfileStore().removeFor (title); if (onMapChanged) onMapChanged(); }

    private:
        static const std::vector<DspVendorSoftware>& vendors()
        {
            static const auto v = DspSoftware::loadVendors();
            return v;
        }

        void step (Step s, StepState st) { if (onStep) onStep (s, st); }

        void beginApply (int run)
        {
            step (Step::apply, StepState::active);
            bool automatic = false;
            session.pushConfigToDsp (automatic);
            lastWrites = -1; stableTicks = 0; applying = true;
            if (! automatic) { finish (State::error, "The tune could not be sent (Auto Tune running?)."); return; }
            (void) run;
        }

        void finish (State s, const juce::String& msg)
        {
            stopTimer();
            applying = false;
            state = s;
            if (s != State::done)
                for (auto st : { Step::learn, Step::map, Step::verify, Step::apply, Step::readback })
                    if (s == State::error) step (st, StepState::pending);   // panel marks the active one failed
            if (onState) onState (s, msg);
        }

        void timerCallback() override
        {
            if (state != State::running) { stopTimer(); return; }
            if (onThumbnail && windowHandle != nullptr)
                if (auto img = captureWindow (windowHandle()); img.isValid()) onThumbnail (img);

            auto* ai = session.getAiOperator();
            if (ai == nullptr) return;
            const auto st = ai->getStats();
            cost.usd = st.costUsd; cost.budgetUsd = budget;
            cost.tokensIn = (int) st.usage.inputTokens; cost.tokensOut = (int) st.usage.outputTokens; cost.calls = st.learnRuns;
            if (onCost) onCost (cost);

            if (! applying) return;
            for (auto& n : ai->getLastSafetyNotes())
                if (n != lastNote && onLog) { lastNote = n; onLog ({ juce::Time::getCurrentTime().formatted ("%H:%M:%S"), "safety", n, "limit enforced in code" }); }

            if (st.writes != lastWrites) { lastWrites = st.writes; stableTicks = 0; return; }
            if (++stableTicks < 2) return;               // no new writes for 3 s: the push is finished

            step (Step::apply, StepState::done);
            step (Step::readback, StepState::active);
            const bool clean = st.mismatches == 0 && st.unverifiedWrites == 0;
            step (Step::readback, clean ? StepState::done : StepState::failed);
            finish (clean ? State::done : State::error,
                    clean ? juce::String ("Tune applied and verified (") + juce::String (st.verifiedWrites) + " writes read back)."
                          : juce::String (st.mismatches + st.unverifiedWrites) + " value(s) could not be verified. Check the software window.");
        }

        Session& session;
        std::function<void*()> windowHandle;
        ApiKeyStore keys;
        ApiKeyStore::ProviderSettings ps;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
        juce::String model = kOpus, proc, title, lastNote;
        double budget = 2.0;
        bool consent = false, mapStepOn = false, applying = false;
        State state = State::idle;
        int runId = 0, lastWrites = -1, stableTicks = 0;
        Cost cost;
    };
}

std::unique_ptr<AiOperatorFacade> createLiveAiOperator (Session& s, std::function<void*()> h)
{
    return std::make_unique<LiveAiOperator> (s, std::move (h));
}
} // namespace fa::ui::dsp
