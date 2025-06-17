#pragma once
#include <spdlog/logger.h>

#include <memory>
#include <string>

extern std::shared_ptr<spdlog::logger> g_plugin_logger;

void LogToFile_Info(const std::string& message);
void LogToFile_Warn(const std::string& message);
void LogToFile_Error(const std::string& message);
void LogToFile_Debug(const std::string& message);
void LogToConsole(const std::string& message);