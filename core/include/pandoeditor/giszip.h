#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace pandoeditor {
struct GisZipEntry {
    std::string path;
    std::string bytes;
};
struct GisZipArchive {
    std::vector<GisZipEntry> entries;
};

// Extract a local GIS archive without touching a project or the filesystem.
// Throws std::invalid_argument on malformed or unsafe ZIP input.
GisZipArchive readGisZipArchive(std::string_view bytes);
}
