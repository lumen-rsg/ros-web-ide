#include "fs/binary_detector.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace rosweb::fs {

static const std::unordered_set<std::string> binary_extensions = {
    ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".ico", ".webp", ".tiff", ".tif",
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".7z", ".rar",
    ".so", ".dylib", ".dll", ".a", ".o", ".obj", ".lib",
    ".pyc", ".pyo", ".class", ".jar", ".war",
    ".db", ".sqlite", ".sqlite3",
    ".bag", ".pcap",
    ".mp3", ".mp4", ".avi", ".mov", ".wav", ".flac", ".ogg",
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
    ".woff", ".woff2", ".ttf", ".eot", ".otf",
    ".wasm",
};

auto BinaryDetector::is_binary(const std::string& content) const -> bool {
    // Git's heuristic: check first 8000 bytes for NUL
    size_t check_len = std::min(content.size(), size_t(8000));
    for (size_t i = 0; i < check_len; ++i) {
        if (content[i] == '\0') {
            return true;
        }
    }
    return false;
}

auto BinaryDetector::has_binary_extension(const std::string& path) const -> bool {
    namespace fs = std::filesystem;
    std::string ext = fs::path(path).extension().string();
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return binary_extensions.count(ext) > 0;
}

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

auto BinaryDetector::base64_encode(const std::string& data) -> std::string {
    std::string result;
    result.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i < data.size()) {
        uint32_t octet_a = i < data.size() ? static_cast<unsigned char>(data[i]) : 0;
        uint32_t octet_b = i + 1 < data.size() ? static_cast<unsigned char>(data[i + 1]) : 0;
        uint32_t octet_c = i + 2 < data.size() ? static_cast<unsigned char>(data[i + 2]) : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result.push_back(base64_chars[(triple >> 18) & 0x3F]);
        result.push_back(base64_chars[(triple >> 12) & 0x3F]);
        result.push_back(i + 1 < data.size() ? base64_chars[(triple >> 6) & 0x3F] : '=');
        result.push_back(i + 2 < data.size() ? base64_chars[triple & 0x3F] : '=');

        i += 3;
    }

    return result;
}

auto BinaryDetector::base64_decode(const std::string& encoded) -> std::string {
    // Build decode table
    int decode_table[256];
    std::fill_n(decode_table, 256, -1);
    for (int i = 0; i < 64; ++i) {
        decode_table[static_cast<unsigned char>(base64_chars[i])] = i;
    }

    std::string result;
    result.reserve((encoded.size() / 4) * 3);

    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded) {
        if (decode_table[c] == -1) break;
        val = (val << 6) + decode_table[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }

    return result;
}

}  // namespace rosweb::fs
