#include "CSVReader.h"
#include "MappedFile.h"
#include <charconv>
#include <iostream>
#include <cstring>

namespace Aquila {

CSVReader::CSVReader(std::string_view filepath)
    : m_filepath(filepath) {}

std::vector<MarketData> CSVReader::read_all() {
    std::vector<MarketData> data;
    data.reserve(10'000);

    // ── Memory-map the file ───────────────────────────────
    // MappedFile maps the file directly into our virtual address space.
    // All subsequent reads operate on mapped memory — zero extra copies.
    MappedFile file(m_filepath);
    if (file.empty()) {
        std::cerr << "[CSVReader] File is empty or failed to map: "
                  << m_filepath << "\n";
        return data;
    }

    std::string_view content = file.view();

    // Skip header line (timestamp,open,high,low,close,volume)
    auto nl = content.find('\n');
    if (nl == std::string_view::npos) return data;
    content = content.substr(nl + 1);

    // ── Zero-copy line parsing ────────────────────────────
    // We never copy the file contents — parse_line() operates on
    // string_view slices of the mapped memory directly.
    while (!content.empty()) {
        auto end  = content.find('\n');
        auto line = content.substr(0, end);

        // Strip Windows-style \r\n line endings
        if (!line.empty() && line.back() == '\r')
            line = line.substr(0, line.size() - 1);

        if (!line.empty()) {
            auto md = parse_line(line);
            if (md) data.push_back(*md);
        }

        if (end == std::string_view::npos) break;
        content = content.substr(end + 1);
    }

    std::cout << "[CSVReader] Parsed " << data.size()
              << " bars (mmap, zero-copy)\n";
    return data;
}

std::optional<MarketData> CSVReader::parse_line(std::string_view line) {
    MarketData md{};
    md.instrument_id = 1;
    md.symbol[0]     = '\0';

    // std::from_chars: locale-independent, no allocation, C++17+.
    // Faster than sscanf/strtod because it operates on string_view directly.
    std::size_t pos = 0;
    int col = 0;

    while (col < 6) {
        auto comma = line.find(',', pos);
        const auto end_pos = (comma == std::string_view::npos)
            ? line.size() : comma;
        const std::string_view token = line.substr(pos, end_pos - pos);

        const char* b = token.data();
        const char* e = b + token.size();

        switch (col) {
        case 0: std::from_chars(b, e, md.timestamp); break;
        case 1: std::from_chars(b, e, md.open);      break;
        case 2: std::from_chars(b, e, md.high);      break;
        case 3: std::from_chars(b, e, md.low);       break;
        case 4: std::from_chars(b, e, md.close);     break;
        case 5: std::from_chars(b, e, md.volume);    break;
        }

        ++col;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }

    return (col >= 6) ? std::optional<MarketData>(md) : std::nullopt;
}

} // namespace Aquila
