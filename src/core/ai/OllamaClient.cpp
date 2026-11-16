#include "OllamaClient.h"
#include <atomic>
#include <map>

namespace fa
{
namespace
{
    juce::var mk (std::initializer_list<std::pair<const char*, juce::var>> items)
    {
        auto* o = new juce::DynamicObject();
        for (auto& [k, v] : items)
            o->setProperty (k, v);
        return juce::var (o);
    }

    std::atomic<int> callCounter { 0 };

    /** Our input_schema is already plain JSON schema, which is what Ollama expects as `parameters`. */
    juce::var toOllamaTool (const juce::var& t)
    {
        return mk ({ { "type", "function" },
                     { "function", mk ({ { "name", t.getProperty ("name", {}) },
                                         { "description", t.getProperty ("description", {}) },
                                         { "parameters", t.getProperty ("input_schema", mk ({ { "type", "object" } })) } }) } });
    }

    juce::String textOnly (const juce::var& content)
    {
        if (content.isString())
            return content.toString();
        juce::String s;
        if (auto* a = content.getArray())
            for (auto& b : *a)
                if (b.getProperty ("type", {}).toString() == "text")
                    s << (s.isEmpty() ? "" : "\n") << b.getProperty ("text", {}).toString();
        return s;
    }

    void collectImages (const juce::var& content, juce::Array<juce::var>& out)
    {
        if (auto* a = content.getArray())
            for (auto& b : *a)
                if (b.getProperty ("type", {}).toString() == "image")
                    out.add (b.getProperty ("source", {}).getProperty ("data", {}));
    }
}

OllamaClient::OllamaClient (Config c, std::shared_ptr<ClaudeTransport> t)
    : config (std::move (c)), transport (std::move (t))
{
    if (transport == nullptr)
        transport = makeWinHttpTransport();
}

juce::String OllamaClient::chatUrl (const juce::String& baseUrl)
{
    auto b = baseUrl.trim();
    if (b.isEmpty()) b = "http://localhost:11434";
    if (! b.containsIgnoreCase ("://")) b = "http://" + b;
    while (b.endsWithChar ('/')) b = b.dropLastCharacters (1);
    return b + "/api/chat";
}

juce::String OllamaClient::buildRequestBody (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages) const
{
    juce::Array<juce::var> out;
    out.add (mk ({ { "role", "system" }, { "content", systemPrompt } }));

    std::map<juce::String, juce::String> idToName;      // tool_use id -> tool name (for tool_name on results)

    if (auto* arr = messages.getArray())
        for (auto& m : *arr)
        {
            const auto role = m.getProperty ("role", {}).toString();
            const auto content = m.getProperty ("content", {});

            if (role == "assistant")
            {
                juce::Array<juce::var> calls;
                if (auto* blocks = content.getArray())
                    for (auto& b : *blocks)
                        if (b.getProperty ("type", {}).toString() == "tool_use")
                        {
                            idToName[b.getProperty ("id", {}).toString()] = b.getProperty ("name", {}).toString();
                            auto args = b.getProperty ("input", {});
                            if (! args.isObject() || args.hasProperty ("_malformed")) args = mk ({});
                            calls.add (mk ({ { "function", mk ({ { "name", b.getProperty ("name", {}) }, { "arguments", args } }) } }));
                        }
                auto msg = mk ({ { "role", "assistant" }, { "content", textOnly (content) } });
                if (! calls.isEmpty())
                    msg.getDynamicObject()->setProperty ("tool_calls", calls);
                out.add (msg);
                continue;
            }

            // user turn: plain text, text/image blocks, or tool_result blocks
            juce::Array<juce::var> images;
            juce::String text;
            bool anyResult = false;
            if (auto* blocks = content.getArray())
            {
                for (auto& b : *blocks)
                {
                    const auto type = b.getProperty ("type", {}).toString();
                    if (type == "tool_result")
                    {
                        anyResult = true;
                        const auto rc = b.getProperty ("content", {});
                        auto t = textOnly (rc);
                        if ((bool) b.getProperty ("is_error", false)) t = "ERROR: " + t;
                        const auto name = idToName[b.getProperty ("tool_use_id", {}).toString()];
                        auto msg = mk ({ { "role", "tool" }, { "content", t.isEmpty() ? juce::String ("ok") : t } });
                        if (name.isNotEmpty()) msg.getDynamicObject()->setProperty ("tool_name", name);
                        out.add (msg);
                        collectImages (rc, images);
                    }
                    else if (type == "text")
                        text << (text.isEmpty() ? "" : "\n") << b.getProperty ("text", {}).toString();
                    else if (type == "image")
                        images.add (b.getProperty ("source", {}).getProperty ("data", {}));
                }
            }
            else
                text = content.toString();

            if (text.isNotEmpty() || ! images.isEmpty())
            {
                auto msg = mk ({ { "role", "user" }, { "content", text.isEmpty() ? juce::String (anyResult ? "Screenshot from the tool result above." : "(image)") : text } });
                if (! images.isEmpty())
                    msg.getDynamicObject()->setProperty ("images", images);
                out.add (msg);
            }
        }

    juce::Array<juce::var> toolList;
    if (auto* a = tools.getArray())
        for (auto& t : *a)
            toolList.add (toOllamaTool (t));

    auto* root = new juce::DynamicObject();
    root->setProperty ("model", config.model);
    root->setProperty ("messages", out);
    if (! toolList.isEmpty())
        root->setProperty ("tools", toolList);
    root->setProperty ("stream", false);
    root->setProperty ("think", config.think);
    root->setProperty ("keep_alive", config.keepAlive);
    root->setProperty ("options", mk ({ { "num_ctx", config.numCtx }, { "num_predict", config.numPredict },
                                        { "temperature", config.temperature } }));
    return juce::JSON::toString (juce::var (root), true);
}

bool OllamaClient::parseResponse (const juce::String& body, LlmResponse& out)
{
    const auto v = juce::JSON::parse (body);
    if (! v.isObject() || ! v.getProperty ("message", {}).isObject())
    {
        out.error = "Malformed response from Ollama.";
        return false;
    }
    const auto msg = v["message"];
    juce::Array<juce::var> content;
    const auto text = msg.getProperty ("content", {}).toString();
    if (text.trim().isNotEmpty())
        content.add (mk ({ { "type", "text" }, { "text", text } }));

    bool anyCall = false;
    if (auto* calls = msg.getProperty ("tool_calls", {}).getArray())
        for (auto& c : *calls)
        {
            const auto fn = c.getProperty ("function", {});
            auto args = fn.getProperty ("arguments", {});
            if (args.isString())
            {
                const auto raw = args.toString();
                const auto parsed = juce::JSON::parse (raw);
                args = parsed.isObject() ? parsed : mk ({ { "_malformed", raw } });
            }
            else if (! args.isObject())
                args = args.isVoid() ? mk ({}) : mk ({ { "_malformed", juce::JSON::toString (args, true) } });
            content.add (mk ({ { "type", "tool_use" }, { "id", "ollama_" + juce::String (++callCounter) },
                               { "name", fn.getProperty ("name", {}).toString() }, { "input", args } }));
            anyCall = true;
        }

    const auto done = v.getProperty ("done_reason", {}).toString();
    out.stopReason = anyCall ? "tool_use" : (done == "length" ? "max_tokens" : "end_turn");
    out.content = content;
    out.usage.inputTokens = (juce::int64) v.getProperty ("prompt_eval_count", 0);
    out.usage.outputTokens = (juce::int64) v.getProperty ("eval_count", 0);
    out.costUsd = 0.0;
    return true;
}

LlmResponse OllamaClient::createMessage (const juce::String& systemPrompt, const juce::var& tools, const juce::var& messages)
{
    LlmResponse r;
    if (transport == nullptr)
    {
        r.error = "No HTTP transport available.";
        return r;
    }
    juce::StringPairArray headers;
    headers.set ("Content-Type", "application/json");
    const auto http = transport->post (chatUrl (config.baseUrl), headers, buildRequestBody (systemPrompt, tools, messages), config.timeoutMs);
    r.attempts = 1;
    r.httpStatus = http.status;

    if (http.status == 0)
    {
        r.error = "Ollama not running or not reachable at " + config.baseUrl + " (start it with \"ollama serve\"). " + http.networkError;
        return r;
    }
    if (http.status != 200)
    {
        const auto m = juce::JSON::parse (http.body).getProperty ("error", {}).toString();
        r.error = "HTTP " + juce::String (http.status) + (m.isNotEmpty() ? ": " + m : juce::String());
        if (http.status == 404 || m.containsIgnoreCase ("not found"))
            r.error += " (pull the model first: ollama pull " + config.model + ")";
        return r;
    }
    LlmResponse parsed;
    if (! parseResponse (http.body, parsed))
    {
        r.error = parsed.error;
        return r;
    }
    parsed.ok = true;
    parsed.httpStatus = 200;
    parsed.attempts = 1;
    return parsed;
}
} // namespace fa
