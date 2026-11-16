#pragma once

#include <juce_core/juce_core.h>

namespace fa
{
/** Token usage of one or more responses. */
struct ClaudeUsage
{
    juce::int64 inputTokens = 0, outputTokens = 0, cacheCreationTokens = 0, cacheReadTokens = 0;
    ClaudeUsage& operator+= (const ClaudeUsage& o)
    {
        inputTokens += o.inputTokens; outputTokens += o.outputTokens;
        cacheCreationTokens += o.cacheCreationTokens; cacheReadTokens += o.cacheReadTokens;
        return *this;
    }
    juce::int64 total() const noexcept { return inputTokens + outputTokens + cacheCreationTokens + cacheReadTokens; }
};

/** One model turn, in the agent's lingua franca (Anthropic content blocks): `content` is an array of
    {type:"text",text} and {type:"tool_use",id,name,input} blocks. Other providers translate to this. */
struct LlmResponse
{
    bool ok = false;                 // HTTP 200 and parseable
    int httpStatus = 0;
    juce::String error;              // human-readable, never contains a key
    juce::String stopReason;         // end_turn | tool_use | max_tokens | refusal | ...
    juce::String refusalCategory;    // stop_details.category when stopReason == "refusal"
    juce::var content;               // the content array, replayed verbatim into the history
    ClaudeUsage usage;
    double costUsd = 0.0;
    int attempts = 0;
    bool isRefusal() const noexcept { return stopReason == "refusal"; }
};

/** Provider interface used by DspOperatorAgent. Messages and tools are in Anthropic format
    (messages: {role, content: string | blocks incl. tool_use / tool_result / image};
    tools: {name, description, input_schema}). Blocking: call from a worker thread. */
class LlmClient
{
public:
    virtual ~LlmClient() = default;
    virtual LlmResponse createMessage (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) = 0;
    virtual juce::String providerName() const = 0;     // "claude" | "ollama"
    virtual bool isLocal() const noexcept { return false; }
};

enum class LlmProvider { claude, ollama };
inline juce::String toString (LlmProvider p) { return p == LlmProvider::ollama ? "ollama" : "claude"; }
inline LlmProvider llmProviderFromString (const juce::String& s) { return s.trim().equalsIgnoreCase ("ollama") ? LlmProvider::ollama : LlmProvider::claude; }
} // namespace fa
