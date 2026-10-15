#include "fileHeader.h"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kBufferSize = 1024 * 1024; // copy in 1 MB chunks
constexpr int kBarWidth = 40;
constexpr auto kRedrawInterval = std::chrono::milliseconds(200);

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Reads a whole line so paths containing spaces work, and strips the quotes
// Windows adds when you use "Copy as path".
std::string readPath(const std::string& prompt)
{
    std::cout << prompt;
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
    std::cout << question << " [y/N]: ";
    std::string answer;
    std::getline(std::cin, answer);
    return !answer.empty() && (answer[0] == 'y' || answer[0] == 'Y');
}

void drawProgress(std::uintmax_t copied, std::uintmax_t total, double elapsedSeconds)
{
    const int percent = total == 0 ? 100 : static_cast<int>(copied * 100 / total);
    const int filled = percent * kBarWidth / 100;

    std::ostringstream line;
    line << '\r' << '['
         << std::string(static_cast<std::size_t>(filled), '#')
         << std::string(static_cast<std::size_t>(kBarWidth - filled), '-')
         << "] " << std::setw(3) << percent << "%  "
         << formatBytes(copied) << " / " << formatBytes(total);
    if (elapsedSeconds > 0.0)
        line << "  " << formatBytes(static_cast<std::uintmax_t>(static_cast<double>(copied) / elapsedSeconds)) << "/s";
    line << "    "; // overwrite leftovers from a previously longer line

    std::cout << line.str() << std::flush;
}

// Deletes a partially written copy so a failed run doesn't leave a corrupt file behind.
bool failCopy(std::ofstream& out, const fs::path& destination, const std::string& reason)
{
    out.close();
    std::error_code ec;
    fs::remove(destination, ec);
    std::cerr << "\nError: " << reason << '\n';
    return false;
}

// Checks the source, resolves the final destination path and confirms overwrites.
// Returns false if the copy should not go ahead.
bool preparePaths(const fs::path& source, fs::path& destination)
{
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        std::cerr << "Error: \"" << source.string() << "\" does not exist or is not a file.\n";
        return false;
    }

    // Copying into a folder keeps the original file name.
    if (fs::is_directory(destination, ec))
        destination /= source.filename();

    if (fs::exists(destination, ec)) {
        // Opening the destination for writing would empty the source if they are the same file.
        if (fs::equivalent(source, destination, ec)) {
            std::cerr << "Error: source and destination are the same file.\n";
            return false;
        }
        if (!askYesNo("\"" + destination.string() + "\" already exists. Overwrite?")) {
            std::cout << "Copy cancelled.\n";
            return false;
        }
    }
    else if (destination.has_parent_path() && !fs::is_directory(destination.parent_path(), ec)) {
        std::cerr << "Error: folder \"" << destination.parent_path().string() << "\" does not exist.\n";
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
        std::cerr << "Error: cannot read the size of \"" << source.string() << "\": " << ec.message() << '\n';
        return false;
    }

    std::ifstream in(source, std::ios::binary);
    if (!in) {
        std::cerr << "Error: cannot open \"" << source.string() << "\" for reading.\n";
        return false;
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "Error: cannot create \"" << destination.string() << "\".\n";
        return false;
    }

    std::cout << "Copying \"" << source.string() << "\" -> \"" << destination.string()
              << "\" (" << formatBytes(total) << ")\n";

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
    std::cout << "\nCopy finished in " << formatDuration(elapsed);
    if (elapsed > 0.0)
        std::cout << " (average " << formatBytes(static_cast<std::uintmax_t>(static_cast<double>(copied) / elapsed)) << "/s)";
    std::cout << '\n';

    if (copied != total)
        std::cerr << "Warning: expected " << total << " bytes but copied " << copied
                  << ". The source may have changed during the copy.\n";
    return true;
}

int main(int argc, char* argv[])
{
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
        std::cerr << "Error: both a source and a destination are required.\n";
        return 2;
    }

    if (!preparePaths(source, destination))
        return 1;
    return copyFileWithProgress(source, destination) ? 0 : 1;
}
