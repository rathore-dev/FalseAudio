#include "ClaudeClient.h"

#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
 #include <winhttp.h>
 #pragma comment (lib, "winhttp.lib")
#endif

namespace fa
{
//==============================================================================
#if JUCE_WINDOWS
namespace
{
    struct WinHttpTransport : ClaudeTransport
    {
        struct Handle
        {
            HINTERNET h = nullptr;
            ~Handle() { if (h != nullptr) WinHttpCloseHandle (h); }
        };

        HttpResponse post (const juce::String& url, const juce::StringPairArray& headers,
                           const juce::String& body, int timeoutMs) override
        {
            HttpResponse r;
            const auto wurl = url.toWideCharPointer();

            URL_COMPONENTS uc {};
            uc.dwStructSize = sizeof (uc);
            uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD) -1;
            if (! WinHttpCrackUrl (wurl, 0, 0, &uc))
            {
                r.networkError = "Bad URL";
                return r;
            }
            const std::wstring host (uc.lpszHostName, uc.dwHostNameLength);
            std::wstring path (uc.lpszUrlPath, uc.dwUrlPathLength);
            path.append (uc.lpszExtraInfo, uc.dwExtraInfoLength);

            Handle session, connect, request;
            session.h = WinHttpOpen (L"FalseAudio/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (session.h == nullptr) { r.networkError = "WinHttpOpen failed"; return r; }
            WinHttpSetTimeouts (session.h, 15000, 15000, 30000, timeoutMs);

            connect.h = WinHttpConnect (session.h, host.c_str(), uc.nPort, 0);
            if (connect.h == nullptr) { r.networkError = "Connect failed"; return r; }

            const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            request.h = WinHttpOpenRequest (connect.h, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (request.h == nullptr) { r.networkError = "OpenRequest failed"; return r; }

            std::wstring hdr;
            for (int i = 0; i < headers.size(); ++i)
                hdr += (headers.getAllKeys()[i] + ": " + headers.getAllValues()[i] + "\r\n").toWideCharPointer();

            const auto utf8 = body.toUTF8();
            const DWORD len = (DWORD) utf8.sizeInBytes() - 1;
            if (! WinHttpSendRequest (request.h, hdr.c_str(), (DWORD) -1L, (LPVOID) utf8.getAddress(), len, len, 0)
                || ! WinHttpReceiveResponse (request.h, nullptr))
            {
                r.networkError = "Request failed (error " + juce::String ((int) GetLastError()) + ")";
                return r;
            }

            DWORD status = 0, sz = sizeof (status);
            WinHttpQueryHeaders (request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            r.status = (int) status;

            wchar_t ra[64] {};
            DWORD raSize = sizeof (ra);
            if (WinHttpQueryHeaders (request.h, WINHTTP_QUERY_CUSTOM, L"retry-after", ra, &raSize, WINHTTP_NO_HEADER_INDEX))
                r.retryAfter = juce::String (ra);

            juce::MemoryBlock data;
            for (;;)
            {
                DWORD avail = 0;
                if (! WinHttpQueryDataAvailable (request.h, &avail) || avail == 0)
                    break;
                std::vector<char> buf (avail);
                DWORD got = 0;
                if (! WinHttpReadData (request.h, buf.data(), avail, &got) || got == 0)
                    break;
                data.append (buf.data(), got);
            }
            r.body = juce::String::fromUTF8 ((const char*) data.getData(), (int) data.getSize());
            return r;
        }
    };
}

std::shared_ptr<ClaudeTransport> makeWinHttpTransport() { return std::make_shared<WinHttpTransport>(); }
#else
std::shared_ptr<ClaudeTransport> makeWinHttpTransport() { return nullptr; }
#endif

//==============================================================================
ClaudeClient::ClaudeClient (Config c, std::shared_ptr<ClaudeTransport> t, ApiKeyProvider k, SleepFn s)
    : config (std::move (c)), transport (std::move (t)), keyProvider (std::move (k)), sleepFn (std::move (s))
{
    if (transport == nullptr)
        transport = makeWinHttpTransport();
    if (sleepFn == nullptr)
        sleepFn = [] (int ms) { juce::Thread::sleep (ms); };
}

bool ClaudeClient::isRetryableStatus (int status) noexcept
{
    return status == 0 || status == 408 || status == 409 || status == 429 || (status >= 500 && status <= 599);
}

int ClaudeClient::backoffMs (int attempt, const juce::String& retryAfter) const
{
    int ms = config.baseBackoffMs;
    for (int i = 1; i < attempt && ms < config.maxBackoffMs; ++i)
        ms *= 2;
    ms = std::min (ms, config.maxBackoffMs);
    const double ra = retryAfter.getDoubleValue();
    if (ra > 0.0)
        ms = std::max (ms, (int) std::min (ra * 1000.0, (double) config.maxBackoffMs));
    return ms;
}

juce::String ClaudeClient::buildRequestBody (const juce::String& systemPrompt, const juce::var& tools,
                                             const juce::var& messages) const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("model", config.model);
    root->setProperty ("max_tokens", config.maxTokens);

    // Stable system prompt as a block with a cache breakpoint. `thinking` is deliberately omitted:
    // Opus 5.5 always thinks adaptively and rejects "disabled" / budget_tokens.
    juce::Array<juce::var> system;
    {
        auto* b = new juce::DynamicObject();
        b->setProperty ("type", "text");
        b->setProperty ("text", systemPrompt);
        auto* cc = new juce::DynamicObject();
        cc->setProperty ("type", "ephemeral");
        b->setProperty ("cache_control", juce::var (cc));
        system.add (juce::var (b));
    }
    root->setProperty ("system", system);

    juce::Array<juce::var> toolList;
    if (auto* arr = tools.getArray())
    {
        for (int i = 0; i < arr->size(); ++i)
        {
            auto t = (*arr)[i];
            if (i == arr->size() - 1 && t.getDynamicObject() != nullptr)
            {
                auto* copy = t.getDynamicObject()->clone().release();
                auto* cc = new juce::DynamicObject();
                cc->setProperty ("type", "ephemeral");
                copy->setProperty ("cache_control", juce::var (cc));
                t = juce::var (copy);
            }
            toolList.add (t);
        }
    }
    if (! toolList.isEmpty())
    {
        root->setProperty ("tools", toolList);
        auto* tc = new juce::DynamicObject();
        tc->setProperty ("type", "auto");   // forced any/tool returns 400 on this model
        root->setProperty ("tool_choice", juce::var (tc));
    }

    auto* oc = new juce::DynamicObject();
    oc->setProperty ("effort", config.effort);
    root->setProperty ("output_config", juce::var (oc));
    root->setProperty ("messages", messages);
    return juce::JSON::toString (juce::var (root), true);
}

bool ClaudeClient::parseResponse (const juce::String& body, Response& out)
{
    const auto v = juce::JSON::parse (body);
    if (! v.isObject())
    {
        out.error = "Malformed response from the API.";
        return false;
    }
    out.stopReason = v.getProperty ("stop_reason", {}).toString();
    out.content = v.getProperty ("content", juce::var (juce::Array<juce::var>()));
    if (auto* sd = v.getProperty ("stop_details", {}).getDynamicObject())
        out.refusalCategory = sd->getProperty ("category").toString();
    const auto u = v.getProperty ("usage", {});
    out.usage.inputTokens         = (juce::int64) u.getProperty ("input_tokens", 0);
    out.usage.outputTokens        = (juce::int64) u.getProperty ("output_tokens", 0);
    out.usage.cacheCreationTokens = (juce::int64) u.getProperty ("cache_creation_input_tokens", 0);
    out.usage.cacheReadTokens     = (juce::int64) u.getProperty ("cache_read_input_tokens", 0);
    return true;
}

ClaudeClient::Response ClaudeClient::createMessage (const juce::String& systemPrompt, const juce::var& tools,
                                                    const juce::var& messages)
{
    Response r;
    const auto key = keyProvider != nullptr ? keyProvider() : juce::String();
    if (key.isEmpty())
    {
        r.error = "No Anthropic API key configured.";
        return r;
    }
    if (transport == nullptr)
    {
        r.error = "No HTTP transport available.";
        return r;
    }

    juce::StringPairArray headers;
    headers.set ("Content-Type", "application/json");
    headers.set ("x-api-key", key);
    headers.set ("anthropic-version", config.apiVersion);
    const auto body = buildRequestBody (systemPrompt, tools, messages);

    for (int attempt = 1; attempt <= config.maxRetries + 1; ++attempt)
    {
        r.attempts = attempt;
        const auto http = transport->post (config.url, headers, body, config.timeoutMs);
        r.httpStatus = http.status;

        if (http.status == 200)
        {
            Response parsed;
            if (! parseResponse (http.body, parsed))
            {
                r.error = parsed.error;
                return r;
            }
            parsed.ok = true;
            parsed.httpStatus = 200;
            parsed.attempts = attempt;
            parsed.costUsd = config.pricing.cost (parsed.usage);
            return parsed;
        }

        juce::String msg = http.status == 0 ? http.networkError : juce::String();
        if (http.status != 0)
        {
            const auto ev = juce::JSON::parse (http.body);
            msg = ev.getProperty ("error", {}).getProperty ("message", {}).toString();
            msg = msg.isEmpty() ? "HTTP " + juce::String (http.status)
                                : "HTTP " + juce::String (http.status) + ": " + msg;
        }
        r.error = msg;

        if (! isRetryableStatus (http.status) || attempt > config.maxRetries)
            return r;

        sleepFn (backoffMs (attempt, http.retryAfter));
    }
    return r;
}
} // namespace fa
