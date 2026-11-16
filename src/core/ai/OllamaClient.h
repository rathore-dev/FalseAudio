#pragma once

#include "ClaudeClient.h"

namespace fa
{
/** LlmClient for a local Ollama server (native POST {baseUrl}/api/chat, stream=false, `tools` +
    base64 `images`). Free: cost is always 0, tokens come from prompt_eval_count / eval_count.
    The agent keeps speaking Anthropic content blocks; this class translates both ways:
    tool_use <-> message.tool_calls, tool_result -> role "tool" (+ a follow-up user message carrying
    the screenshot, since tool messages cannot hold images portably).
    Malformed tool arguments (a string that is not JSON) come back as input {"_malformed": raw} so the
    agent can answer with a retry message. Connection failure -> "Ollama not running". Blocking. */
class OllamaClient : public LlmClient
{
public:
    struct Config
    {
        juce::String baseUrl = "http://localhost:11434";
        juce::String model = "qwen3-vl:4b";
        int timeoutMs = 900000;            // CPU inference is slow
        int numCtx = 16384;
        int numPredict = 2048;           // qwen3-vl ignores think:false and reasons first
        double temperature = 0.2;
        juce::String keepAlive = "30m";
        bool think = false;                // reasoning off: far faster on CPU
    };

    explicit OllamaClient (Config config, std::shared_ptr<ClaudeTransport> transport = {});

    const Config& getConfig() const noexcept { return config; }

    LlmResponse createMessage (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) override;
    juce::String providerName() const override { return "ollama"; }
    bool isLocal() const noexcept override { return true; }

    /** The exact JSON body that would be sent (exposed for tests). */
    juce::String buildRequestBody (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) const;
    /** Parses a 200 body into Anthropic-style content. */
    static bool parseResponse (const juce::String& body, LlmResponse& out);
    static juce::String chatUrl (const juce::String& baseUrl);

private:
    Config config;
    std::shared_ptr<ClaudeTransport> transport;
};
} // namespace fa
