#include "ApiKeyStore.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
 #include <wincrypt.h>
 #pragma comment (lib, "crypt32.lib")
#endif

namespace fa
{
namespace
{
    const char* const entropyText = "FalseAudio.AnthropicKey.v1";
}

ApiKeyStore::ApiKeyStore (juce::File folder) : dir (std::move (folder))
{
    if (dir == juce::File())
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FalseAudio");
}

bool ApiKeyStore::protect (const juce::MemoryBlock& plain, juce::MemoryBlock& cipher)
{
#if JUCE_WINDOWS
    DATA_BLOB in { (DWORD) plain.getSize(), (BYTE*) plain.getData() };
    DATA_BLOB entropy { (DWORD) std::strlen (entropyText), (BYTE*) entropyText };
    DATA_BLOB out {};
    if (! CryptProtectData (&in, L"FalseAudio API key", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    cipher.replaceAll (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return true;
#else
    juce::ignoreUnused (plain, cipher);
    return false;
#endif
}

bool ApiKeyStore::unprotect (const juce::MemoryBlock& cipher, juce::MemoryBlock& plain)
{
#if JUCE_WINDOWS
    DATA_BLOB in { (DWORD) cipher.getSize(), (BYTE*) cipher.getData() };
    DATA_BLOB entropy { (DWORD) std::strlen (entropyText), (BYTE*) entropyText };
    DATA_BLOB out {};
    if (! CryptUnprotectData (&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    plain.replaceAll (out.pbData, out.cbData);
    SecureZeroMemory (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return true;
#else
    juce::ignoreUnused (cipher, plain);
    return false;
#endif
}

juce::String ApiKeyStore::resolve() const
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).trim();
    if (env.isNotEmpty())
        return env;

    juce::MemoryBlock cipher;
    if (! keyFile().existsAsFile() || ! keyFile().loadFileAsData (cipher))
        return {};
    juce::MemoryBlock plain;
    if (! unprotect (cipher, plain))
        return {};
    return juce::String::fromUTF8 ((const char*) plain.getData(), (int) plain.getSize()).trim();
}

ApiKeyStore::Source ApiKeyStore::getSource() const
{
    if (juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).trim().isNotEmpty())
        return Source::environment;
    return hasStoredKey() ? Source::stored : Source::none;
}

bool ApiKeyStore::save (const juce::String& key, juce::String& error)
{
    const auto k = key.trim();
    if (k.length() < 20 || k.containsAnyOf (" \t\r\n"))
    {
        error = "That does not look like an API key.";
        return false;
    }
    juce::MemoryBlock plain (k.toRawUTF8(), k.getNumBytesAsUTF8()), cipher;
    if (! protect (plain, cipher))
    {
        error = "Could not encrypt the key (Windows DPAPI unavailable).";
        return false;
    }
    dir.createDirectory();
    if (! keyFile().replaceWithData (cipher.getData(), cipher.getSize()))
    {
        error = "Could not write the key file.";
        return false;
    }
    return true;
}

void ApiKeyStore::clear()
{
    keyFile().deleteFile();
}

bool ApiKeyStore::hasStoredKey() const
{
    return keyFile().existsAsFile();
}

juce::String ApiKeyStore::maskedHint (const juce::String& key)
{
    if (key.length() < 12)
        return key.isEmpty() ? juce::String() : juce::String ("****");
    return key.substring (0, 7) + "..." + key.getLastCharacters (4);
}

ApiKeyStore::ProviderSettings ApiKeyStore::loadProviderSettings() const
{
    ProviderSettings ps;
    const auto v = juce::JSON::parse (providerFile());
    if (v.isObject())
    {
        const auto prov = v.getProperty ("provider", {}).toString().trim().toLowerCase();
        if (prov == "ollama" || prov == "claude")
            ps.provider = prov;
        if (v.getProperty ("ollamaUrl", {}).toString().trim().isNotEmpty())
            ps.ollamaUrl = v.getProperty ("ollamaUrl", {}).toString().trim();
        if (v.getProperty ("ollamaModel", {}).toString().trim().isNotEmpty())
            ps.ollamaModel = v.getProperty ("ollamaModel", {}).toString().trim();
    }
    return ps;
}

bool ApiKeyStore::saveProviderSettings (const ProviderSettings& s) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("provider", s.provider);
    o->setProperty ("ollamaUrl", s.ollamaUrl);
    o->setProperty ("ollamaModel", s.ollamaModel);
    dir.createDirectory();
    return providerFile().replaceWithText (juce::JSON::toString (juce::var (o), false));
}

bool ApiKeyStore::hasConsent() const
{
    return consentFile().existsAsFile();
}

void ApiKeyStore::setConsent (bool granted)
{
    if (granted)
    {
        dir.createDirectory();
        consentFile().replaceWithText ("consented " + juce::Time::getCurrentTime().toISO8601 (true));
    }
    else
        consentFile().deleteFile();
}
} // namespace fa
