#pragma once
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Loom{
enum class TerminalLevel{ Info, Running, Debug, Error };
struct TerminalEntry{
    std::string text;
    TerminalLevel level = TerminalLevel::Info;
    std::chrono::system_clock::time_point at = std::chrono::system_clock::now();
};
struct TerminalState{
    bool visible = false;
    bool unreadError = false;
    bool follow = true;
    int scrollRows = 0;
    std::uint64_t jobNextLineIndex = 0;
    std::filesystem::path liveLogPath;
    std::uintmax_t liveLogOffset = 0;
    std::string lastMessage;
    std::string lastLiveMessage;
    std::vector<TerminalEntry> entries;

    void add(std::string line, TerminalLevel level = TerminalLevel::Info){
        if(line.empty()) return;
        if(level == TerminalLevel::Error && !visible) unreadError = true;
        entries.push_back({std::move(line), level});
        if(entries.size() > 1600){
            entries.erase(entries.begin(), entries.begin() + 400);
            scrollRows = std::max(0, scrollRows - 400);
        }
        if(follow) scrollRows = 0;
    }
    static TerminalLevel classify(const std::string& line){
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return char(std::tolower(c)); });
        if(lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos ||
           lower.find("traceback") != std::string::npos || lower.find("exception") != std::string::npos)
            return TerminalLevel::Error;
        if(lower.find("debug") != std::string::npos || lower.find("[validation]") != std::string::npos)
            return TerminalLevel::Debug;
        return TerminalLevel::Running;
    }
};
}
