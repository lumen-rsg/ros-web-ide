#include <doctest.h>
#include <string>

#include "fs/binary_detector.hpp"

using namespace rosweb::fs;

TEST_CASE("BinaryDetector: detects binary content") {
    BinaryDetector detector;

    std::string text = "Hello, world! This is plain text.";
    CHECK_FALSE(detector.is_binary(text));

    std::string binary = std::string("Hello") + '\0' + "World";
    CHECK(detector.is_binary(binary));
}

TEST_CASE("BinaryDetector: detects binary extensions") {
    BinaryDetector detector;

    CHECK(detector.has_binary_extension("image.png"));
    CHECK(detector.has_binary_extension("archive.zip"));
    CHECK(detector.has_binary_extension("library.so"));
    CHECK(detector.has_binary_extension("data.db"));

    CHECK_FALSE(detector.has_binary_extension("source.cpp"));
    CHECK_FALSE(detector.has_binary_extension("header.hpp"));
    CHECK_FALSE(detector.has_binary_extension("README.md"));
}

TEST_CASE("BinaryDetector: base64 encode/decode roundtrip") {
    std::string original = "Hello, World! Testing base64 encoding with special chars: \xff\xfe\xfd";

    auto encoded = BinaryDetector::base64_encode(original);
    auto decoded = BinaryDetector::base64_decode(encoded);

    CHECK(decoded == original);
}

TEST_CASE("BinaryDetector: base64 handles empty input") {
    auto encoded = BinaryDetector::base64_encode("");
    CHECK(encoded.empty());

    auto decoded = BinaryDetector::base64_decode("");
    CHECK(decoded.empty());
}
