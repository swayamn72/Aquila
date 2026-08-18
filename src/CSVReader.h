#pragma once
// CSVReader.h — Parses OHLCV CSV using memory-mapped I/O (see MappedFile.h).
// Interface is unchanged; implementation switched from ifstream to mmap.
#include "MarketData.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace Aquila {

class CSVReader {
public:
    explicit CSVReader(std::string_view filepath);

    // Returns all parsed bars. Uses mmap under the hood — zero-copy parsing.
    std::vector<MarketData> read_all();

private:
    std::string m_filepath;
    std::optional<MarketData> parse_line(std::string_view line);
};

} // namespace Aquila
