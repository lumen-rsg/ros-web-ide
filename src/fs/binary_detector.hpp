#pragma once

#include <string>
#include <string_view>

namespace rosweb::fs {

class BinaryDetector {
public:
    auto is_binary(const std::string& content) const -> bool;

    auto has_binary_extension(const std::string& path) const -> bool;

    static auto base64_encode(const std::string& data) -> std::string;

    static auto base64_decode(const std::string& encoded) -> std::string;
};

}  // namespace rosweb::fs
