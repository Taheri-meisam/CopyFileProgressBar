#include "fileHeader.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kBufferSize = 1024 * 1024; // copy in 1 MB chunks
constexpr int kBarWidth = 40;
constexpr auto kRedrawInterval = std::chrono::milliseconds(200);

// ANSI color codes
const char* const kReset = "\033[0m";
const char* const kBold = "\033[1m";
const char* const kRed = "\033[1;31m";
const char* const kGreen = "\033[32m";
const char* const kBoldGreen = "\033[1;32m";
const char* const kYellow = "\033[33m";
const char* const kBoldYellow = "\033[1;33m";
const char* const kCyan = "\033[36m";
const char* const kGray = "\033[90m";

// Set in main(): whether stdout / stderr are terminals that understand colors.
bool g_colorOut = false;
bool g_colorErr = false;

// https://no-color.org: a non-empty NO_COLOR variable turns colors off.
bool noColorRequested()
{
#ifdef _WIN32
    return GetEnvironmentVariableA("NO_COLOR", nullptr, 0) > 1; // size includes the terminating null
#else
    const char* value = std::getenv("NO_COLOR");
    return value != nullptr && value[0] != '\0';
#endif
}

// Colors are only used on a real terminal, so redirecting output to a file
// doesn't fill it with escape codes.
bool supportsColor(bool forStderr)
{
    if (noColorRequested())
        return false;
#ifdef _WIN32
    // Windows 10+ consoles understand ANSI codes once virtual terminal processing is enabled.
    HANDLE handle = GetStdHandle(forStderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode))
        return false;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    if (!isatty(forStderr ? STDERR_FILENO : STDOUT_FILENO))
        return false;
    const char* term = std::getenv("TERM");
    return term == nullptr || std::string(term) != "dumb";
#endif
}

std::string paint(const std::string& text, const char* style, bool enabled = g_colorOut)
{
    return enabled ? style + text + kReset : text;
}

std::ostream& errorOut()
{
    return std::cerr << paint("Error:", kRed, g_colorErr) << ' ';
}

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string formatSpeed(std::uintmax_t bytes, double seconds)
{
    return formatBytes(static_cast<std::uintmax_t>(static_cast<double>(bytes) / seconds)) + "/s";
}

// Reads a whole line so paths containing spaces work, and strips the quotes
// Windows adds when you use "Copy as path".
std::string readPath(const std::string& prompt)
{
    std::cout << paint(prompt, kBold);
    std::string line;
    std::getline(std::cin, line);

    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = line.find_last_not_of(" \t\r\n");
    line = line.substr(first, last - first + 1);

    if (line.size() >= 2 && line.front() == '"' && line.back() == '"')
        line = line.substr(1, line.size() - 2);
    return line;
}

bool askYesNo(const std::string& question)
{
    std::cout << paint(question, kYellow) << " [y/N]: ";
    std::string answer;
    std::getline(std::cin, answer);
    return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

void drawProgress(std::uintmax_t copied, std::uintmax_t total, double elapsedSeconds)
{
    const int percent = total == 0 ? 100 : static_cast<int>(copied * 100 / total);
    const int filled = percent * kBarWidth / 100;

    std::ostringstream percentText;
    percentText << std::setw(3) << percent << '%';

    std::ostringstream line;
    line << '\r' << '['
         << paint(std::string(static_cast<std::size_t>(filled), '#'), kGreen)
         << paint(std::string(static_cast<std::size_t>(kBarWidth - filled), '-'), kGray)
         << "] " << paint(percentText.str(), kBold) << "  "
         << formatBytes(copied) << " / " << formatBytes(total);
    if (elapsedSeconds > 0.0)
        line << "  " << paint(formatSpeed(copied, elapsedSeconds), kCyan);
    line << "    "; // overwrite leftovers from a previously longer line

    std::cout << line.str() << std::flush;
}

// Deletes a partially written copy so a failed run doesn't leave a corrupt file behind.
bool failCopy(std::ofstream& out, const fs::path& destination, const std::string& reason)
{
    out.close();
    std::error_code ec;
    fs::remove(destination, ec);
    std::cerr << '\n';
    errorOut() << reason << '\n';
    return false;
}

// Checks the source, resolves the final destination path and confirms overwrites.
// Returns false if the copy should not go ahead.
bool preparePaths(const fs::path& source, fs::path& destination)
{
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        errorOut() << '"' << source.string() << "\" does not exist or is not a file.\n";
        return false;
    }

    // Copying into a folder keeps the original file name.
    if (fs::is_directory(destination, ec))
        destination /= source.filename();

    if (fs::exists(destination, ec)) {
        // Opening the destination for writing would empty the source if they are the same file.
        if (fs::equivalent(source, destination, ec)) {
            errorOut() << "source and destination are the same file.\n";
            return false;
        }
        if (!askYesNo("\"" + destination.string() + "\" already exists. Overwrite?")) {
            std::cout << paint("Copy cancelled.", kYellow) << '\n';
            return false;
        }
    }
    else if (destination.has_parent_path() && !fs::is_directory(destination.parent_path(), ec)) {
        errorOut() << "folder \"" << destination.parent_path().string() << "\" does not exist.\n";
        return false;
    }
    return true;
}

} // namespace

std::string formatBytes(std::uintmax_t bytes)
{
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
    return out.str();
}

std::string formatDuration(double seconds)
{
    const auto whole = static_cast<long long>(seconds);
    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(2) << whole / 3600 << ':'
        << std::setw(2) << (whole / 60) % 60 << ':'
        << std::setw(5) << std::fixed << std::setprecision(2) << std::fmod(seconds, 60.0);
    return out.str();
}

bool copyFileWithProgress(const fs::path& source, const fs::path& destination)
{
    std::error_code ec;
    const std::uintmax_t total = fs::file_size(source, ec);
    if (ec) {
        errorOut() << "cannot read the size of \"" << source.string() << "\": " << ec.message() << '\n';
        return false;
    }

    std::ifstream in(source, std::ios::binary);
    if (!in) {
        errorOut() << "cannot open \"" << source.string() << "\" for reading.\n";
        return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        errorOut() << "cannot create \"" << destination.string() << "\".\n";
        return false;
    }

    std::cout << paint("Copying", kBold) << " \"" << source.string() << "\" -> \"" << destination.string()
              << "\" " << paint("(" + formatBytes(total) + ")", kGray) << '\n';

    std::vector<char> buffer(kBufferSize);
    std::uintmax_t copied = 0;
    const auto start = Clock::now();
    auto lastDraw = start;
    int lastPercent = -1;

    drawProgress(0, total, 0.0);

    // read() fails on the last, partial chunk, so the loop runs on gcount()
    // rather than on the stream state - otherwise the end of the file is lost.
    while (true) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0)
            break;

        if (!out.write(buffer.data(), got))
            return failCopy(out, destination, "writing to \"" + destination.string() + "\" failed (disk full?).");
        copied += static_cast<std::uintmax_t>(got);

        // Console output is slow: only redraw when the percentage changes or now and then for the speed.
        const int percent = total == 0 ? 100 : static_cast<int>(copied * 100 / total);
        const auto now = Clock::now();
        if (percent != lastPercent || now - lastDraw >= kRedrawInterval) {
            drawProgress(copied, total, secondsSince(start));
            lastPercent = percent;
            lastDraw = now;
        }
    }

    if (in.bad())
        return failCopy(out, destination, "reading \"" + source.string() + "\" failed.");

    out.close(); // flushes buffered data; a full disk may only show up here
    if (!out)
        return failCopy(out, destination, "writing to \"" + destination.string() + "\" failed (disk full?).");

    const double elapsed = secondsSince(start);
    drawProgress(copied, copied, elapsed);
    std::cout << '\n' << paint("Copy finished", kBoldGreen) << " in " << formatDuration(elapsed);
    if (elapsed > 0.0)
        std::cout << " (average " << paint(formatSpeed(copied, elapsed), kCyan) << ')';
    std::cout << '\n';

    if (copied != total)
        std::cerr << paint("Warning:", kBoldYellow, g_colorErr) << " expected " << total << " bytes but copied "
                  << copied << ". The source may have changed during the copy.\n";
    return true;
}

int main(int argc, char* argv[])
{
    g_colorOut = supportsColor(false);
    g_colorErr = supportsColor(true);

    fs::path source;
    fs::path destination;

    if (argc == 3) {
        source = argv[1];
        destination = argv[2];
    }
    else if (argc == 1) {
        source = readPath("Source file (e.g. E:\\folder\\file.txt): ");
        destination = readPath("Destination file or folder: ");
    }
    else {
        std::cerr << "Usage: " << fs::path(argv[0]).filename().string() << " [<source> <destination>]\n"
                  << "Run without arguments to be asked for the paths.\n";
        return 2;
    }

    if (source.empty() || destination.empty()) {
        errorOut() << "both a source and a destination are required.\n";
        return 2;
    }

    if (!preparePaths(source, destination))
        return 1;
    return copyFileWithProgress(source, destination) ? 0 : 1;
}
