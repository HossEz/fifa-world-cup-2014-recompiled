#pragma once

#include <filesystem>

// Logs the session report: version, hardware and settings (see system_report.cpp).
void LogSystemReport(const std::filesystem::path& game_data_root);
