#include <doctest/doctest.h>

#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "capture/rgbd_frame.hpp"
#include "offline/body_bundle.hpp"
#include "replay/golden.hpp"
#include "replay/take_reader.hpp"

using namespace kstudio;

namespace {

class BodyBundleFixture {
 public:
  BodyBundleFixture() {
    static std::atomic<unsigned> serial{0};
    root = std::filesystem::temp_directory_path() /
           ("kstudio-body-bundle-" + std::to_string(::getpid()) + "-" +
            std::to_string(serial.fetch_add(1)));
    std::filesystem::create_directory(root);
    take = root / "source.mcap";
    REQUIRE(writeGoldenTake(take, 6));
  }

  ~BodyBundleFixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  std::filesystem::path root;
  std::filesystem::path take;
};

nlohmann::json readJson(const std::filesystem::path& path) {
  std::ifstream input(path);
  return nlohmann::json::parse(input);
}

}  // namespace

TEST_CASE("body bundle dry run reproduces replay pairing and writes nothing") {
  BodyBundleFixture fixture;
  const auto output = fixture.root / "dry-run-output";
  const BodyBundleResult result =
      extractBodyInputBundle({.take_path = fixture.take, .output_dir = output, .write = false});

  REQUIRE(result);
  CHECK_FALSE(result.report.wrote_bundle);
  CHECK_FALSE(std::filesystem::exists(output));
  CHECK(result.report.source_depth_frames == 6);
  CHECK(result.report.frame_count == 6);
  CHECK(result.report.missing_color_frames == 1);
  CHECK(result.report.unique_jpeg_count == 3);
  CHECK(result.report.depth_payload_bytes ==
        uint64_t(6) * kDepthWidth * kDepthHeight * sizeof(uint16_t));
  CHECK(result.report.rgb_payload_bytes > result.report.unique_rgb_payload_bytes);
  CHECK(result.report.portable_bundle_bytes > result.report.depth_payload_bytes);
  CHECK(result.report.source_take_sha256.size() == 64);
  CHECK(result.report.calibration_sha256.size() == 64);
  CHECK(result.report.manifest_sha256.size() == 64);
  CHECK(result.report.calibration_content_hash == 0x601dE2E2601dE2E2ull);
}

TEST_CASE("body bundle write is exact, validated, and immutable") {
  BodyBundleFixture fixture;
  const auto output = fixture.root / "source.body-input-v1";
  const BodyBundleOptions options{.take_path = fixture.take, .output_dir = output, .write = true};
  const BodyBundleResult written = extractBodyInputBundle(options);
  REQUIRE_MESSAGE(written, written.error);
  CHECK(written.report.wrote_bundle);
  CHECK(std::filesystem::is_directory(output));

  const BodyBundleValidation validation = validateBodyInputBundle(output);
  REQUIRE_MESSAGE(validation, validation.error);
  CHECK(validation.frame_count == written.report.frame_count);
  CHECK(validation.missing_color_frames == written.report.missing_color_frames);
  CHECK(validation.unique_jpeg_count == written.report.unique_jpeg_count);
  CHECK(validation.portable_bundle_bytes == written.report.portable_bundle_bytes);
  CHECK(validation.manifest_sha256 == written.report.manifest_sha256);

  const nlohmann::json manifest = readJson(output / "manifest.json");
  REQUIRE(manifest.at("frames").size() == 6);
  CHECK(manifest.at("frames").at(0).at("rgb_file").is_null());
  CHECK(manifest.at("frames").at(1).at("rgb_sha256") ==
        manifest.at("frames").at(2).at("rgb_sha256"));
  CHECK(manifest.at("frames").at(1).at("rgb_file") == manifest.at("frames").at(2).at("rgb_file"));
  CHECK(manifest.at("calibration_content_hash") == "601de2e2601de2e2");

  // The little-endian depth payload must be a bit-exact copy of take dmm.
  TakeReader reader;
  REQUIRE(reader.open(fixture.take));
  CHECK(reader.calibration()->content_hash == 0x601dE2E2601dE2E2ull);
  CHECK_FALSE(reader.calibrationJson().empty());
  std::vector<uint16_t> source_depth;
  TakeReader::Callbacks callbacks;
  callbacks.on_depth = [&](const TakeReader::DepthMsg& message) {
    source_depth.assign(message.dmm, message.dmm + size_t(kDepthWidth) * kDepthHeight);
  };
  REQUIRE(reader.pump(callbacks).has_value());
  std::ifstream depth_input(output / "frames/0.depth.u16le", std::ios::binary);
  const std::vector<uint8_t> depth_bytes((std::istreambuf_iterator<char>(depth_input)), {});
  REQUIRE(depth_bytes.size() == source_depth.size() * 2);
  for (size_t i = 0; i < source_depth.size(); ++i) {
    const uint16_t decoded =
        uint16_t(depth_bytes[i * 2]) | (uint16_t(depth_bytes[i * 2 + 1]) << 8u);
    REQUIRE(decoded == source_depth[i]);
  }

  const BodyBundleResult overwrite = extractBodyInputBundle(options);
  CHECK_FALSE(overwrite);
  CHECK(overwrite.error.find("immutable") != std::string::npos);
}

TEST_CASE("body bundle validator detects payload tampering") {
  BodyBundleFixture fixture;
  const auto output = fixture.root / "source.body-input-v1";
  const BodyBundleResult written =
      extractBodyInputBundle({.take_path = fixture.take, .output_dir = output, .write = true});
  REQUIRE_MESSAGE(written, written.error);

  const auto depth_path = output / "frames/0.depth.u16le";
  std::fstream depth(depth_path, std::ios::binary | std::ios::in | std::ios::out);
  REQUIRE(depth);
  char first = 0;
  depth.read(&first, 1);
  first ^= 0x1;
  depth.seekp(0);
  depth.write(&first, 1);
  depth.close();

  const BodyBundleValidation validation = validateBodyInputBundle(output);
  CHECK_FALSE(validation);
  CHECK(validation.error.find("SHA-256 mismatch") != std::string::npos);
}
