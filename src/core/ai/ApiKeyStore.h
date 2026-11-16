#pragma once

#include <juce_core/juce_core.h>

namespace fa
{
/** Stores the Anthropic API key encrypted with Windows DPAPI (CryptProtectData, current user)
    in %APPDATA%\FalseAudio\anthropic_key.bin. The environment variable ANTHROPIC_API_KEY always
    wins over the stored key. The key is never logged or returned to the UI in clear text:
    use maskedHint() for display. Also remembers the user's cloud-upload consent (screenshots and
    UI text of the DSP window are sent to Anthropic). */
class ApiKeyStore
{
public:
    enum class Source { none, environment, stored };

    /** `folder` defaults to %APPDATA%\FalseAudio (tests pass a temp folder). */
    explicit ApiKeyStore (juce::File folder = {});

    /** Environment variable first, then the stored key; empty if neither exists. */
    juce::String resolve() const;
    Source getSource() const;

    bool save (const juce::String& key, juce::String& error);
    void clear();
    bool hasStoredKey() const;

    /** e.g. "sk-ant-...wxyz" - safe to show in a UI. */
    static juce::String maskedHint (const juce::String& key);

    // Consent to send DSP window screenshots/UI text to the Anthropic API.
    bool hasConsent() const;
    void setConsent (bool granted);

    // Raw DPAPI helpers (exposed for tests). Return false on failure / non-Windows.
    static bool protect (const juce::MemoryBlock& plain, juce::MemoryBlock& cipher);
    static bool unprotect (const juce::MemoryBlock& cipher, juce::MemoryBlock& plain);

    /** Which LLM provider the AI operator uses. Ollama is local and free: no API key and no cloud upload. */
    struct ProviderSettings
    {
        juce::String provider = "claude";                    // claude | ollama
        juce::String ollamaUrl = "http://localhost:11434";
        juce::String ollamaModel = "qwen3-vl:4b";
    };
    ProviderSettings loadProviderSettings() const;
    bool saveProviderSettings (const ProviderSettings& s) const;

    juce::File getFolder() const { return dir; }

private:
    juce::File dir;
    juce::File keyFile() const     { return dir.getChildFile ("anthropic_key.bin"); }
    juce::File consentFile() const { return dir.getChildFile ("ai_consent.flag"); }
    juce::File providerFile() const { return dir.getChildFile ("ai_provider.json"); }
};
} // namespace fa
