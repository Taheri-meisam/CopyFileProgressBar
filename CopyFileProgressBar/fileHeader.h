#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// Copies `source` to `destination` in chunks while drawing a progress bar.
// Returns true on success. On failure it prints the reason and deletes the
// partially written destination file.
bool copyFileWithProgress(const std::filesystem::path& source,
                          const std::filesystem::path& destination);

// 1536 -> "1.5 KB"
std::string formatBytes(std::uintmax_t bytes);

// 3725.4 -> "01:02:05.40"
std::string formatDuration(double seconds);
