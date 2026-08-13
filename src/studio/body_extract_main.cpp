#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "offline/body_bundle.hpp"

namespace {

void usage(const char* executable) {
  std::printf(
      "usage: %s --take <file.mcap> [--output <bundle-dir>] [--write]\n"
      "\n"
      "Default is a read-only dry run: hashes and reports the exact replay-paired\n"
      "bundle without creating any files. --write publishes one immutable bundle\n"
      "transactionally; it refuses to replace an existing path.\n",
      executable);
}

double mib(uint64_t bytes) { return double(bytes) / (1024.0 * 1024.0); }

}  // namespace

int main(int argc, char** argv) {
  kstudio::BodyBundleOptions options;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--take") && i + 1 < argc) {
      options.take_path = argv[++i];
    } else if (!std::strcmp(argv[i], "--output") && i + 1 < argc) {
      options.output_dir = argv[++i];
    } else if (!std::strcmp(argv[i], "--write")) {
      options.write = true;
    } else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
      usage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "unknown or incomplete argument: %s\n", argv[i]);
      usage(argv[0]);
      return 2;
    }
  }
  if (options.take_path.empty()) {
    usage(argv[0]);
    return 2;
  }

  const kstudio::BodyBundleResult result = kstudio::extractBodyInputBundle(options);
  if (!result) {
    std::fprintf(stderr, "body bundle failed: %s\n", result.error.c_str());
    return 1;
  }

  const auto& report = result.report;
  std::printf("kstudio.body-input.v1 %s\n", report.wrote_bundle ? "WRITTEN" : "DRY RUN");
  std::printf("take: %s\n", report.take_path.c_str());
  std::printf("take SHA-256: %s (%.2f MiB)\n", report.source_take_sha256.c_str(),
              mib(report.source_take_bytes));
  std::printf("frames: %zu/%zu depth | %zu missing color | %zu unique JPEGs\n", report.frame_count,
              report.source_depth_frames, report.missing_color_frames, report.unique_jpeg_count);
  std::printf("portable payload: %.2f MiB (depth %.2f + content-addressed RGB %.2f)\n",
              mib(report.portable_bundle_bytes), mib(report.depth_payload_bytes),
              mib(report.unique_rgb_payload_bytes));
  std::printf("logical paired RGB references: %.2f MiB before deduplication\n",
              mib(report.rgb_payload_bytes));
  std::printf("calibration: %016llx | SHA-256 %s\n",
              static_cast<unsigned long long>(report.calibration_content_hash),
              report.calibration_sha256.c_str());
  std::printf("manifest SHA-256: %s\n", report.manifest_sha256.c_str());
  std::printf("output: %s\n", report.output_dir.c_str());
  if (!report.wrote_bundle)
    std::printf("DRY RUN: wrote nothing. Add --write only after reviewing this report.\n");
  return 0;
}
