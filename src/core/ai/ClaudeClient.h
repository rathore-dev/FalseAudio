#pragma once

#include <juce_core/juce_core.h>
#include "LlmClient.h"
#include <functional>
#include <memory>

namespace fa
{
/** One HTTP response (or a transport-level failure when status == 0). */
struct HttpResponse
{
    int status = 0;
    juce::String body;
    juce::String retryAfter;     // Retry-After header, seconds or empty
    juce::String networkError;   // set when status == 0
};

/** Transport abstraction: the real one is WinHTTP, tests inject recorded responses. */
class ClaudeTransport
{
public:
    virtual ~ClaudeTransport() = default;
    virtual HttpResponse post (const juce::String& url, const juce::StringPairArray& headers,
                               const juce::String& body, int timeoutMs) = 0;
};

/** WinHTTP transport (nullptr on non-Windows). */
std::shared_ptr<ClaudeTransport> makeWinHttpTransport();

/** USD per million tokens. Defaults are Claude Opus 5.5 list prices (4 in / 20 out). */
struct ClaudePricing
{
    double inputPerMTok = 4.0, outputPerMTok = 20.0;
    double cacheWriteMultiplier = 1.25, cacheReadMultiplier = 0.10;
    double cost (const ClaudeUsage& u) const noexcept
    {
        return ((double) u.inputTokens * inputPerMTok + (double) u.outputTokens * outputPerMTok
                + (double) u.cacheCreationTokens * inputPerMTok * cacheWriteMultiplier
                + (double) u.cacheReadTokens * inputPerMTok * cacheReadMultiplier) / 1.0e6;
    }
};

/** Raw client for POST https://api.anthropic.com/v1/messages (no C++ SDK exists).

    Request rules for claude-opus-5-5: adaptive thinking (the thinking field is omitted - it cannot
    be disabled and budget_tokens is rejected), output_config.effort is always set, tool_choice
    is always {"type":"auto"} (forced any/tool returns 400), no assistant prefill, the stable
    system prompt and the tool list carry cache_control. 429, 5xx, 529 and network failures are
    retried with exponential backoff (honouring Retry-After). The API key is read per call from
    the provider and never logged. Blocking: call from a worker thread. */
class ClaudeClient : public LlmClient
{
public:
    struct Config
    {
        juce::String model = "claude-opus-5-5";
        juce::String effort = "medium";            // low | medium | high | xhigh | max
        int maxTokens = 16000;                     // thinking counts toward this
        juce::String apiVersion = "2026-10-21";
        juce::String url = "https://api.anthropic.com/v1/messages";
        int maxRetries = 5;                        // retries after the first attempt
        int baseBackoffMs = 1000, maxBackoffMs = 30000;
        int timeoutMs = 300000;
        ClaudePricing pricing;
    };

    using Response = LlmResponse;

    using ApiKeyProvider = std::function<juce::String()>;
    using SleepFn = std::function<void (int ms)>;

    ClaudeClient (Config config, std::shared_ptr<ClaudeTransport> transport, ApiKeyProvider key, SleepFn sleep = {});

    const Config& getConfig() const noexcept { return config; }
    void setConfig (const Config& c) { config = c; }

    /** `tools` is an array of tool definitions; `messages` an array of {role, content}.
        Stable parts (system, tools) are marked for caching. */
    Response createMessage (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) override;
    juce::String providerName() const override { return "claude"; }

    /** The exact JSON body that would be sent (exposed for tests). */
    juce::String buildRequestBody (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) const;

    /** Parses a 200 body into out. Returns false and fills out.error on malformed JSON. */
    static bool parseResponse (const juce::String& body, Response& out);

    static bool isRetryableStatus (int status) noexcept;

    /** Backoff before retry number `attempt` (1-based), honouring Retry-After seconds when given. */
    int backoffMs (int attempt, const juce::String& retryAfter) const;

private:
    Config config;
    std::shared_ptr<ClaudeTransport> transport;
    ApiKeyProvider keyProvider;
    SleepFn sleepFn;
};
} // namespace fa
