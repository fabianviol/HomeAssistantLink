#include <Windows.h>       // For MAX_PATH, HMODULE, GetModuleHandleExA, GetModuleFileNameA
#include "RE/Skyrim.h"
#include "SKSE/API.h"
#include "SKSE/SKSE.h"
#include <spdlog/sinks/basic_file_sink.h> 
#include <spdlog/spdlog.h>
#include "Logger.h"
#include "ConfigLoader.h"
#include "LightManager.h"
#include "GameState.h"

const std::string PLUGIN_NAME_STR = "HomeAssistantLink";  // Use a string constant for the plugin name
const std::string LOG_FILE_NAME = PLUGIN_NAME_STR + ".log";  // New dedicated log file name

// Global atomic flag to track if the data export thread is already running
std::atomic<bool> g_threadRunning = false;

void PeriodicGameDataExportThread() {
    if (g_threadRunning.exchange(true)) {
        LogToFile_Warn("PeriodicGameDataExportThread attempted to start but was already running. Skipping.");
        LogToConsole("WARNING: PeriodicGameDataExportThread attempted to start but was already running. Skipping.");
        return;
    }

    LogToFile_Info("Periodic Game Data Export Thread started.");
    LogToConsole("HomeAssistantLink: Periodic Game Data Export Thread started.");  // This one should always appear

    std::this_thread::sleep_for(std::chrono::seconds(5));  // Initial delay

    while (true) {
        ExportGameData();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // Adjust as needed
    }
    g_threadRunning.store(false);
}

// This is the SKSE plugin load function.
SKSEPluginLoad(const SKSE::LoadInterface *skse) {
    // 1. Initialize SKSE API first. This is still necessary for other SKSE services.
    SKSE::Init(skse);
    //SKSE::GetPapyrusInterface()->Register(HALPapyrus::Register);

    // 2. Set up our custom spdlog logger
    try {
        auto logsFolder = SKSE::log::log_directory();  // Get default SKSE log directory path
        if (logsFolder) {
            std::filesystem::path logFilePath = logsFolder->string();
            logFilePath /= LOG_FILE_NAME;  // Append our custom log file name

            // Create a file sink for our specific log file (true to truncate on start)
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);

            // Create our logger with the file sink. Use PLUGIN_NAME_STR as the logger name.
            // Set the log level. spdlog::level::trace will show everything.
            // You can change this to spdlog::level::debug, spdlog::level::info, etc.
            g_plugin_logger = std::make_shared<spdlog::logger>(PLUGIN_NAME_STR, file_sink);
            g_plugin_logger->set_level(spdlog::level::trace);

            // Flush messages immediately for easier debugging. Remove this in release builds for performance.
            g_plugin_logger->flush_on(spdlog::level::trace);

            // Optionally register it with spdlog's global registry (not strictly necessary if only you use it)
            spdlog::register_logger(g_plugin_logger);

            LogToConsole("HomeAssistantLink: Dedicated file logger initialized at: " + logFilePath.string());
        } else {
            // Fallback if log_directory isn't available
            LogToConsole(
                "HomeAssistantLink: WARNING: Could not determine SKSE log directory. Dedicated file logging disabled.");
            OutputDebugStringA(
                "HomeAssistantLink: WARNING - Could not determine SKSE log directory. Dedicated file logging "
                "disabled.\n");
        }
    } catch (const spdlog::spdlog_ex &ex) {
        LogToConsole("HomeAssistantLink: ERROR: Failed to set up custom file logger: " + std::string(ex.what()));
        OutputDebugStringA(
            ("HomeAssistantLink: ERROR - Failed to set up custom file logger: " + std::string(ex.what()) + "\n")
                .c_str());
    }

    LogToFile_Info("Plugin loading...");
    LogToFile_Info("SKSE API initialized.");

    // 3. Load Configuration
    if (!LoadConfiguration()) {
        LogToFile_Error("Failed to load configuration. Plugin will not function correctly.");
    }

    std::string lightsJsonPath = "SKSE/Plugins/lights.json";  // Adjust as needed!
    if (!LoadSkyrimLightsDatabase(lightsJsonPath)) {
        LogToFile_Error("Failed to load Skyrim light definitions database. Proximity triggers will not work.");
    }


    LogToFile_Info("Registering messaging listener.");

    // Register your data export thread to start when a save game is loaded (kPostLoadGame)
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message *message) {
        if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            if (!g_threadRunning.load()) {  // Check if thread is NOT already running
                LogToFile_Info("Game loaded. Starting Home Assistant communication thread.");
                LogToConsole("HomeAssistantLink: Game loaded. Starting Home Assistant communication thread.");
                std::thread exportThread(PeriodicGameDataExportThread);
                exportThread.detach();  // Detach the thread so it runs independently
            } else {
                LogToFile_Info("Game loaded, but Home Assistant communication thread is already running.");
                LogToConsole(
                    "HomeAssistantLink: Game loaded, but HomeAssistantLink communication thread is already running.");
            }
        }
    });

    LogToFile_Info("Plugin loaded successfully.");
    return true;
}