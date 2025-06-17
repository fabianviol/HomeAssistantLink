#include "Logger.h"
#include "ConfigLoader.h"
#include <RE/Skyrim.h>
#include <spdlog/spdlog.h>

std::shared_ptr<spdlog::logger> g_plugin_logger;


void LogToFile_Info(const std::string& message) {
    if (g_plugin_logger) {
        g_plugin_logger->info("INFO: {}", message);
    }
}

void LogToFile_Warn(const std::string& message) {
    if (g_plugin_logger) {
        g_plugin_logger->warn("WARN: {}", message);
    }
}

void LogToFile_Error(const std::string& message) {
    if (g_plugin_logger) {
        g_plugin_logger->error("ERROR: {}", message);
    }
}

void LogToFile_Debug(const std::string& message) {
    if (g_DebugMode && g_plugin_logger) {
        g_plugin_logger->debug("DEBUG: {}", message);
    }
}

void LogToConsole(const std::string& message) {
    if (g_DebugMode || message.find("ERROR:") != std::string::npos || message.find("WARNING:") != std::string::npos) {
        if (auto console = RE::ConsoleLog::GetSingleton()) {
            console->Print(message.c_str());
        }
    }
}