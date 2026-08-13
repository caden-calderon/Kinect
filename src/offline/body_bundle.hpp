#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace kstudio {

/// Local source bundle consumed by an offline body-reconstruction job. Dry
/// run is the default and performs no filesystem writes.
struct BodyBundleOptions {
  std::filesystem::path take_path;
  std::filesystem::path output_dir;
  bool write = false;
};

struct BodyBundleReport {
  std::filesystem::path take_path;
  std::filesystem::path output_dir;
  std::string source_take_sha256;
  std::string calibration_sha256;
  std::string manifest_sha256;
  uint64_t calibration_content_hash = 0;
  size_t source_depth_frames = 0;
  size_t frame_count = 0;
  size_t missing_color_frames = 0;
  size_t unique_jpeg_count = 0;
  uint64_t source_take_bytes = 0;
  uint64_t depth_payload_bytes = 0;
  uint64_t rgb_payload_bytes = 0;
  uint64_t unique_rgb_payload_bytes = 0;
  uint64_t portable_bundle_bytes = 0;
  bool wrote_bundle = false;
};

struct BodyBundleResult {
  bool ok = false;
  BodyBundleReport report;
  std::string error;

  explicit operator bool() const { return ok; }
};

struct BodyBundleValidation {
  bool ok = false;
  size_t frame_count = 0;
  size_t missing_color_frames = 0;
  size_t unique_jpeg_count = 0;
  uint64_t portable_bundle_bytes = 0;
  std::string manifest_sha256;
  std::string error;

  explicit operator bool() const { return ok; }
};

/// Reads the source take through TakeReader + FrameAssembler, producing the
/// exact same RGB/depth pairing as replay. In dry-run mode the full report and
/// hashes are computed but output_dir is never created.
BodyBundleResult extractBodyInputBundle(const BodyBundleOptions& options);

/// Validates schema, path confinement, sizes, hashes, and monotonic identities
/// for an already-written kstudio.body-input.v1 directory.
BodyBundleValidation validateBodyInputBundle(const std::filesystem::path& bundle_dir);

}  // namespace kstudio
