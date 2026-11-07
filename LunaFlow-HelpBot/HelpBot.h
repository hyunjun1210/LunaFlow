#pragma once
#include "Core/Serialization.h"
#include <array>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace LunaFlow {
// One section of the bundled LunaFlow manual ("## Title {#id}" + "키워드: a, b" + body).
struct HelpTopic {
    std::string id,title,body;
    std::vector<std::string> keywords;
};
struct HelpMessage {
    bool user=false;
    std::string text,source;
    float thought=-1;
    std::vector<std::string> suggestions;
};
struct HelpAnswer {
    std::string text;
    std::vector<std::string> suggestions;
    int topic=-1;
};
struct HelpBotState {
    std::vector<HelpMessage> messages;
    std::vector<HelpTopic> knowledge;
    std::array<char,2048> prompt{};
    std::string conversationId,sessionId,stage;
    Json request;
    bool show=false,scroll=false,active=false,focusInput=false,ready=false,configured=false;
    int mode=-1; // -1 = choose on first open, 0 = Copilot Studio agent, 1 = bundled manual
    float elapsed=0;
    float layoutScale=0; // UI scale the floating window was last laid out for
};

std::vector<HelpTopic> ParseHelpKnowledge(const std::string& markdown);
std::vector<HelpTopic> LoadHelpKnowledge(const std::filesystem::path& exeFolder);
// Indices of the best matching topics, best first, paired with their scores.
std::vector<std::pair<int,int>> RankHelpTopics(const std::vector<HelpTopic>&,const std::string& question,size_t limit=3);
constexpr int HelpConfidentScore=3;
HelpAnswer LocalHelpAnswer(const std::vector<HelpTopic>&,const std::string& question,bool korean);
// Request for the dedicated Copilot Studio help agent. Manual excerpts ground the answer.
Json HelpBotRequest(const HelpBotState&,const std::string& question,const Json& appContext,bool korean);
// Copilot replies arrive as Markdown; ImGui shows plain wrapped text.
std::string HelpDisplayText(const std::string& markdown);
std::filesystem::path HelpBotSettings(const std::filesystem::path& projectRoot);
}
