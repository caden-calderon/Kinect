#include "offline/body_bundle.hpp"

#include <openssl/evp.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "capture/assembler.hpp"
#include "core/frame_pool.hpp"
#include "core/telemetry.hpp"
#include "replay/take_reader.hpp"

namespace kstudio {

namespace {

using nlohmann::json;

constexpr size_t kDepthPixels = size_t(kDepthWidth) * kDepthHeight;
constexpr size_t kDepthPayloadBytes = kDepthPixels * sizeof(uint16_t);
constexpr const char* kSchema = "kstudio.body-input.v1";

struct EvpContextDeleter {
  void operator()(EVP_MD_CTX* context) const { EVP_MD_CTX_free(context); }
};

class Sha256 {
 public:
  Sha256() : context_(EVP_MD_CTX_new()) {
    ok_ = context_ && EVP_DigestInit_ex(context_.get(), EVP_sha256(), nullptr) == 1;
  }

  bool update(const void* data, size_t size) {
    if (!ok_) return false;
    ok_ = EVP_DigestUpdate(context_.get(), data, size) == 1;
    return ok_;
  }

  std::optional<std::string> finish() {
    if (!ok_) return std::nullopt;
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context_.get(), bytes.data(), &size) != 1 || size != 32)
      return std::nullopt;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i) out << std::setw(2) << unsigned(bytes[i]);
    return out.str();
  }

 private:
  std::unique_ptr<EVP_MD_CTX, EvpContextDeleter> context_;
  bool ok_ = false;
};

std::optional<std::string> sha256Bytes(const void* data, size_t size) {
  Sha256 hash;
  if (!hash.update(data, size)) return std::nullopt;
  return hash.finish();
}

std::optional<std::string> sha256File(const std::filesystem::path& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "cannot open for SHA-256: " + path.string();
    return std::nullopt;
  }
  Sha256 hash;
  std::array<char, size_t{1024} * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0 && !hash.update(buffer.data(), size_t(count))) {
      error = "OpenSSL SHA-256 update failed for " + path.string();
      return std::nullopt;
    }
  }
  if (!input.eof()) {
    error = "read failed while hashing " + path.string();
    return std::nullopt;
  }
  auto result = hash.finish();
  if (!result) error = "OpenSSL SHA-256 finalization failed for " + path.string();
  return result;
}

bool writeBytes(const std::filesystem::path& path, const void* data, size_t size,
                std::string& error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "cannot create " + path.string();
    return false;
  }
  output.write(static_cast<const char*>(data), std::streamsize(size));
  output.close();
  if (!output) {
    error = "write failed for " + path.string();
    return false;
  }
  return true;
}

std::string hex64(uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}

bool isLowerHex(const std::string& value, size_t length) {
  return value.size() == length && std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isdigit(c) || (c >= 'a' && c <= 'f');
         });
}

bool isConfinedRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  const auto normalized = path.lexically_normal();
  if (normalized.empty() || normalized == ".") return false;
  for (const auto& component : normalized)
    if (component == ".." || component == ".") return false;
  return normalized == path;
}

bool checkedFileSize(const std::filesystem::path& path, uint64_t& size, std::string& error,
                     bool forbid_symlink = false) {
  std::error_code ec;
  if (forbid_symlink && std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec))) {
    error = "bundle artifacts may not be symlinks: " + path.string();
    return false;
  }
  if (ec) {
    error = "cannot inspect " + path.string() + ": " + ec.message();
    return false;
  }
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    error = "expected regular file: " + path.string();
    return false;
  }
  size = std::filesystem::file_size(path, ec);
  if (ec) {
    error = "cannot determine size of " + path.string() + ": " + ec.message();
    return false;
  }
  return true;
}

std::vector<uint8_t> depthLittleEndian(const DepthPlane& depth) {
  std::vector<uint8_t> bytes(kDepthPayloadBytes);
  for (size_t i = 0; i < kDepthPixels; ++i) {
    bytes[i * 2] = uint8_t(depth.dmm[i] & 0xffu);
    bytes[i * 2 + 1] = uint8_t(depth.dmm[i] >> 8u);
  }
  return bytes;
}

struct BundleColorIdentity {
  uint32_t sequence = 0;
  uint32_t device_timestamp = 0;
  uint64_t host_receive_ns = 0;
};

class BundleColor final : public ColorProduct {
 public:
  BundleColor(std::vector<uint8_t> jpeg, BundleColorIdentity identity)
      : jpeg_(std::move(jpeg)),
        sequence_(identity.sequence),
        device_timestamp_(identity.device_timestamp),
        host_receive_ns_(identity.host_receive_ns) {}

  const uint8_t* jpeg() const override { return jpeg_.data(); }
  size_t jpegSize() const override { return jpeg_.size(); }
  const uint8_t* bgrx() const override { return nullptr; }
  uint32_t sequence() const override { return sequence_; }
  uint32_t deviceTimestamp() const override { return device_timestamp_; }
  uint64_t hostReceiveNs() const override { return host_receive_ns_; }

 private:
  std::vector<uint8_t> jpeg_;
  uint32_t sequence_ = 0;
  uint32_t device_timestamp_ = 0;
  uint64_t host_receive_ns_ = 0;
};

struct CachedJpeg {
  std::string sha256;
  size_t size = 0;
};

class PartialDirectory {
 public:
  explicit PartialDirectory(std::filesystem::path path) : path_(std::move(path)) {}
  ~PartialDirectory() {
    if (!active_) return;
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }
  void release() { active_ = false; }

 private:
  std::filesystem::path path_;
  bool active_ = true;
};

BodyBundleResult failResult(BodyBundleReport report, std::string error) {
  return {false, std::move(report), std::move(error)};
}

}  // namespace

BodyBundleResult extractBodyInputBundle(const BodyBundleOptions& options) {
  BodyBundleReport report;
  report.take_path = options.take_path;
  report.output_dir = options.output_dir.empty()
                          ? std::filesystem::path(options.take_path.string() + ".body-input-v1")
                          : options.output_dir;

  try {
    if (options.take_path.empty()) return failResult(std::move(report), "take path is required");
    uint64_t source_size = 0;
    std::string error;
    if (!checkedFileSize(options.take_path, source_size, error))
      return failResult(std::move(report), std::move(error));
    report.source_take_bytes = source_size;
    auto source_hash = sha256File(options.take_path, error);
    if (!source_hash) return failResult(std::move(report), std::move(error));
    report.source_take_sha256 = *source_hash;

    TakeReader reader;
    if (!reader.open(options.take_path))
      return failResult(std::move(report), "cannot open take: " + options.take_path.string());
    report.source_depth_frames = reader.depthFrameCount();
    const auto calibration = reader.calibration();
    const std::string calibration_json = reader.calibrationJson();
    if (!calibration || calibration_json.empty() || calibration->ir.fx <= 0.0f ||
        calibration->ir.fy <= 0.0f || calibration->color.fx <= 0.0f ||
        calibration->color.fy <= 0.0f)
      return failResult(std::move(report), "take has no calibration; offline alignment is unsafe");
    report.calibration_content_hash = calibration->content_hash;
    auto calibration_hash = sha256Bytes(calibration_json.data(), calibration_json.size());
    if (!calibration_hash)
      return failResult(std::move(report), "OpenSSL SHA-256 failed for calibration");
    report.calibration_sha256 = *calibration_hash;

    std::filesystem::path work_dir;
    std::filesystem::path frames_dir;
    std::filesystem::path rgb_dir;
    std::optional<PartialDirectory> partial_guard;
    if (options.write) {
      if (report.output_dir.empty() || report.output_dir.filename().empty())
        return failResult(std::move(report), "output directory must name one bundle directory");
      std::error_code ec;
      if (std::filesystem::exists(report.output_dir, ec) || ec) {
        const std::string reason =
            ec ? ec.message() : "path already exists (bundles are immutable)";
        const std::string message = "refusing output " + report.output_dir.string() + ": " + reason;
        return failResult(std::move(report), message);
      }
      auto parent = report.output_dir.parent_path();
      if (parent.empty()) parent = ".";
      if (!std::filesystem::is_directory(parent, ec) || ec)
        return failResult(std::move(report),
                          "output parent must already exist: " + parent.string());
      work_dir =
          parent /
          (report.output_dir.filename().string() + ".partial-" + std::to_string(::getpid()) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
      if (std::filesystem::exists(work_dir, ec) || ec)
        return failResult(std::move(report),
                          "temporary output already exists: " + work_dir.string());
      if (!std::filesystem::create_directory(work_dir, ec) || ec)
        return failResult(std::move(report), "cannot create temporary output: " + ec.message());
      partial_guard.emplace(work_dir);
      frames_dir = work_dir / "frames";
      if (!std::filesystem::create_directory(frames_dir, ec) || ec)
        return failResult(std::move(report), "cannot create frames directory: " + ec.message());
      rgb_dir = work_dir / "rgb";
      if (!std::filesystem::create_directory(rgb_dir, ec) || ec)
        return failResult(std::move(report), "cannot create RGB directory: " + ec.message());
      if (!writeBytes(work_dir / "calibration.json", calibration_json.data(),
                      calibration_json.size(), error))
        return failResult(std::move(report), std::move(error));
    }

    json manifest = {
        {"schema", kSchema},
        {"complete", true},
        {"source_take_file", options.take_path.filename().string()},
        {"source_take_sha256", report.source_take_sha256},
        {"source_take_bytes", report.source_take_bytes},
        {"calibration_file", "calibration.json"},
        {"calibration_sha256", report.calibration_sha256},
        {"calibration_content_hash", hex64(report.calibration_content_hash)},
        {"depth_encoding",
         {{"width", kDepthWidth},
          {"height", kDepthHeight},
          {"type", "uint16"},
          {"endianness", "little"},
          {"units_per_mm", DepthPlane::kUnitsPerMm},
          {"invalid_value", 0}}},
        {"pairing",
         {{"implementation", "kstudio.FrameAssembler.v1"}, {"color_staleness_ms", 100.0}}},
        {"frame_count", 0},
        {"frames", json::array()},
    };

    std::vector<uint16_t> pending_depth;
    std::vector<float> depth_mm(kDepthPixels);
    TakeReader::DepthMsg pending_meta{};
    bool have_pending_depth = false;
    std::string extraction_error;
    std::unordered_map<uint64_t, CachedJpeg> jpeg_cache;
    std::set<std::string> unique_jpeg_hashes;

    Telemetry telemetry;
    FrameAssembler::Config assembler_config;
    assembler_config.depth_pool = FramePool<DepthPlane>::create(2);
    assembler_config.calib = calibration;
    assembler_config.color_staleness_ms = 100.0;
    FrameAssembler::Sinks sinks;
    sinks.on_frame = [&](const RgbdFrame& frame) {
      if (!extraction_error.empty()) return;
      if (!frame.depth || pending_depth.size() != kDepthPixels) {
        extraction_error = "assembler emitted a frame without the pending depth plane";
        return;
      }
      if (!std::equal(pending_depth.begin(), pending_depth.end(), frame.depth->dmm.begin())) {
        extraction_error = "depth quantization changed while reproducing replay pairing";
        return;
      }

      const std::string stem = std::to_string(frame.frame_id);
      const std::string depth_relative = "frames/" + stem + ".depth.u16le";
      const std::vector<uint8_t> depth_bytes = depthLittleEndian(*frame.depth);
      auto depth_hash = sha256Bytes(depth_bytes.data(), depth_bytes.size());
      if (!depth_hash) {
        extraction_error = "OpenSSL SHA-256 failed for depth frame " + stem;
        return;
      }
      if (options.write && !writeBytes(work_dir / depth_relative, depth_bytes.data(),
                                       depth_bytes.size(), extraction_error))
        return;
      report.depth_payload_bytes += depth_bytes.size();

      json entry = {
          {"frame_id", frame.frame_id},
          {"depth_seq", frame.depth_seq},
          {"t_device_depth", frame.t_device_depth},
          {"t_host_depth_ns", frame.t_host_depth_ns},
          {"depth_file", depth_relative},
          {"depth_sha256", *depth_hash},
          {"depth_bytes", depth_bytes.size()},
          {"depth_gap_before", frame.health.depth.gap_before},
          {"depth_late", frame.health.depth.late},
      };

      if (frame.color) {
        const uint64_t cache_key = (uint64_t(frame.color_seq) << 32u) | frame.t_device_color;
        auto cached = jpeg_cache.find(cache_key);
        if (cached == jpeg_cache.end()) {
          auto rgb_hash = sha256Bytes(frame.color->jpeg(), frame.color->jpegSize());
          if (!rgb_hash) {
            extraction_error = "OpenSSL SHA-256 failed for RGB frame " + stem;
            return;
          }
          cached =
              jpeg_cache.emplace(cache_key, CachedJpeg{*rgb_hash, frame.color->jpegSize()}).first;
        }
        const std::string rgb_relative = "rgb/" + cached->second.sha256 + ".jpg";
        report.rgb_payload_bytes += frame.color->jpegSize();
        const bool first_unique_jpeg = unique_jpeg_hashes.insert(cached->second.sha256).second;
        if (first_unique_jpeg) {
          report.unique_rgb_payload_bytes += cached->second.size;
          if (options.write && !writeBytes(work_dir / rgb_relative, frame.color->jpeg(),
                                           frame.color->jpegSize(), extraction_error))
            return;
        }
        entry["color_seq"] = frame.color_seq;
        entry["t_device_color"] = frame.t_device_color;
        entry["t_host_color_ns"] = frame.t_host_color_ns;
        entry["rgb_file"] = rgb_relative;
        entry["rgb_sha256"] = cached->second.sha256;
        entry["rgb_bytes"] = frame.color->jpegSize();
        entry["color_age_ms"] = frame.health.color_age_ms;
        entry["skew_ms"] = frame.health.skew_ms;
        entry["color_gap_before"] = frame.health.color.gap_before;
        entry["color_late"] = frame.health.color.late;
      } else {
        ++report.missing_color_frames;
        entry["color_seq"] = nullptr;
        entry["t_device_color"] = nullptr;
        entry["t_host_color_ns"] = nullptr;
        entry["rgb_file"] = nullptr;
        entry["rgb_sha256"] = nullptr;
        entry["rgb_bytes"] = 0;
        entry["color_age_ms"] = nullptr;
        entry["skew_ms"] = nullptr;
        entry["color_gap_before"] = nullptr;
        entry["color_late"] = nullptr;
      }
      manifest["frames"].push_back(std::move(entry));
      ++report.frame_count;
    };

    FrameAssembler assembler(std::move(assembler_config), std::move(sinks), telemetry);
    TakeReader::Callbacks callbacks;
    callbacks.on_depth = [&](const TakeReader::DepthMsg& message) {
      pending_depth.assign(message.dmm, message.dmm + kDepthPixels);
      pending_meta = message;
      have_pending_depth = true;
    };
    callbacks.on_color = [&](const TakeReader::ColorMsg& message) {
      std::vector<uint8_t> jpeg(message.jpeg, message.jpeg + message.jpeg_size);
      auto color = std::make_shared<BundleColor>(
          std::move(jpeg), BundleColorIdentity{message.seq, message.t_device, message.t_host_ns});
      assembler.submitColor(std::move(color), message.seq, message.t_device, message.t_host_ns);
    };

    while (reader.pump(callbacks)) {
      if (!have_pending_depth) {
        extraction_error = "take reader returned a depth index without a depth payload";
        break;
      }
      for (size_t i = 0; i < kDepthPixels; ++i)
        depth_mm[i] = float(pending_depth[i]) / DepthPlane::kUnitsPerMm;
      assembler.submitDepthIr(depth_mm.data(), nullptr, pending_meta.seq, pending_meta.t_device,
                              pending_meta.t_host_ns);
      have_pending_depth = false;
      if (!extraction_error.empty()) break;
    }
    if (!extraction_error.empty())
      return failResult(std::move(report), std::move(extraction_error));
    if (report.frame_count != report.source_depth_frames)
      return failResult(std::move(report),
                        "assembled frame count does not match source depth index");

    // A take still being recorded must never yield a bundle whose manifest
    // hash identifies different bytes than the frames we just read.
    uint64_t final_source_size = 0;
    if (!checkedFileSize(options.take_path, final_source_size, error))
      return failResult(std::move(report), std::move(error));
    auto final_source_hash = sha256File(options.take_path, error);
    if (!final_source_hash) return failResult(std::move(report), std::move(error));
    if (final_source_size != report.source_take_bytes ||
        *final_source_hash != report.source_take_sha256)
      return failResult(std::move(report),
                        "source take changed during extraction; retry after recording stops");

    report.unique_jpeg_count = unique_jpeg_hashes.size();
    manifest["frame_count"] = report.frame_count;
    const std::string manifest_text = manifest.dump(2) + "\n";
    auto manifest_hash = sha256Bytes(manifest_text.data(), manifest_text.size());
    if (!manifest_hash) return failResult(std::move(report), "OpenSSL SHA-256 failed for manifest");
    report.manifest_sha256 = *manifest_hash;
    report.portable_bundle_bytes = report.depth_payload_bytes + report.unique_rgb_payload_bytes +
                                   calibration_json.size() + manifest_text.size();

    if (options.write) {
      if (!writeBytes(work_dir / "manifest.json", manifest_text.data(), manifest_text.size(),
                      error))
        return failResult(std::move(report), std::move(error));
      const BodyBundleValidation validation = validateBodyInputBundle(work_dir);
      if (!validation)
        return failResult(std::move(report),
                          "temporary bundle validation failed: " + validation.error);
      std::error_code ec;
      std::filesystem::rename(work_dir, report.output_dir, ec);
      if (ec) return failResult(std::move(report), "cannot publish bundle: " + ec.message());
      partial_guard->release();
      report.wrote_bundle = true;
    }
    return {true, std::move(report), {}};
  } catch (const std::exception& exception) {
    return failResult(std::move(report), exception.what());
  }
}

BodyBundleValidation validateBodyInputBundle(const std::filesystem::path& bundle_dir) {
  BodyBundleValidation result;
  try {
    std::string error;
    const std::filesystem::path manifest_path = bundle_dir / "manifest.json";
    uint64_t manifest_bytes = 0;
    if (!checkedFileSize(manifest_path, manifest_bytes, error, true)) {
      result.error = std::move(error);
      return result;
    }
    std::ifstream manifest_input(manifest_path, std::ios::binary);
    const std::string manifest_text((std::istreambuf_iterator<char>(manifest_input)), {});
    if (!manifest_input.eof() && manifest_input.fail()) {
      result.error = "cannot read " + manifest_path.string();
      return result;
    }
    const json manifest = json::parse(manifest_text);
    if (manifest.value("schema", "") != kSchema || !manifest.value("complete", false)) {
      result.error = "manifest is not a complete kstudio.body-input.v1 bundle";
      return result;
    }
    if (!isLowerHex(manifest.value("source_take_sha256", ""), 64) ||
        !isLowerHex(manifest.value("calibration_sha256", ""), 64) ||
        !isLowerHex(manifest.value("calibration_content_hash", ""), 16)) {
      result.error = "manifest contains a malformed source or calibration hash";
      return result;
    }
    const auto& encoding = manifest.at("depth_encoding");
    if (encoding.at("width").get<int>() != kDepthWidth ||
        encoding.at("height").get<int>() != kDepthHeight ||
        encoding.at("type").get<std::string>() != "uint16" ||
        encoding.at("endianness").get<std::string>() != "little" ||
        encoding.at("units_per_mm").get<float>() != DepthPlane::kUnitsPerMm ||
        encoding.at("invalid_value").get<int>() != 0) {
      result.error = "unsupported depth encoding";
      return result;
    }
    auto manifest_hash = sha256Bytes(manifest_text.data(), manifest_text.size());
    if (!manifest_hash) {
      result.error = "OpenSSL SHA-256 failed for manifest";
      return result;
    }
    result.manifest_sha256 = *manifest_hash;
    result.portable_bundle_bytes = manifest_bytes;

    const std::filesystem::path calibration_relative =
        manifest.at("calibration_file").get<std::string>();
    if (calibration_relative != "calibration.json" ||
        !isConfinedRelativePath(calibration_relative)) {
      result.error = "calibration path escapes the bundle";
      return result;
    }
    const auto calibration_path = bundle_dir / calibration_relative;
    uint64_t calibration_bytes = 0;
    if (!checkedFileSize(calibration_path, calibration_bytes, error, true)) {
      result.error = std::move(error);
      return result;
    }
    auto calibration_hash = sha256File(calibration_path, error);
    if (!calibration_hash ||
        *calibration_hash != manifest.at("calibration_sha256").get<std::string>()) {
      result.error = error.empty() ? "calibration SHA-256 mismatch" : std::move(error);
      return result;
    }
    result.portable_bundle_bytes += calibration_bytes;

    const auto& frames = manifest.at("frames");
    if (!frames.is_array() || manifest.at("frame_count").get<size_t>() != frames.size()) {
      result.error = "frame_count does not match frames array";
      return result;
    }
    std::set<std::filesystem::path> unique_paths{calibration_relative, "manifest.json"};
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> validated_rgb_files;
    std::set<std::string> unique_jpeg_hashes;
    for (size_t index = 0; index < frames.size(); ++index) {
      const auto& frame = frames[index];
      if (frame.at("frame_id").get<uint64_t>() != index) {
        result.error = "frame_id is not contiguous at manifest index " + std::to_string(index);
        return result;
      }
      const std::filesystem::path depth_relative = frame.at("depth_file").get<std::string>();
      const std::filesystem::path expected_depth_relative =
          std::filesystem::path("frames") / (std::to_string(index) + ".depth.u16le");
      if (depth_relative != expected_depth_relative || !isConfinedRelativePath(depth_relative) ||
          !unique_paths.insert(depth_relative).second) {
        result.error = "invalid or duplicate depth path at frame " + std::to_string(index);
        return result;
      }
      const auto depth_path = bundle_dir / depth_relative;
      uint64_t depth_bytes = 0;
      if (!checkedFileSize(depth_path, depth_bytes, error, true)) {
        result.error = std::move(error);
        return result;
      }
      if (depth_bytes != kDepthPayloadBytes ||
          frame.at("depth_bytes").get<uint64_t>() != depth_bytes) {
        result.error = "depth byte count mismatch at frame " + std::to_string(index);
        return result;
      }
      const std::string expected_depth_hash = frame.at("depth_sha256").get<std::string>();
      if (!isLowerHex(expected_depth_hash, 64)) {
        result.error = "malformed depth SHA-256 at frame " + std::to_string(index);
        return result;
      }
      auto depth_hash = sha256File(depth_path, error);
      if (!depth_hash || *depth_hash != expected_depth_hash) {
        result.error = error.empty() ? "depth SHA-256 mismatch at frame " + std::to_string(index)
                                     : std::move(error);
        return result;
      }
      result.portable_bundle_bytes += depth_bytes;

      if (frame.at("rgb_file").is_null()) {
        if (!frame.at("color_seq").is_null() || !frame.at("rgb_sha256").is_null() ||
            frame.at("rgb_bytes").get<uint64_t>() != 0) {
          result.error = "missing-color frame has contradictory RGB metadata at frame " +
                         std::to_string(index);
          return result;
        }
        ++result.missing_color_frames;
      } else {
        const std::filesystem::path rgb_relative = frame.at("rgb_file").get<std::string>();
        if (!isConfinedRelativePath(rgb_relative)) {
          result.error = "invalid RGB path at frame " + std::to_string(index);
          return result;
        }
        const std::string expected_rgb_hash = frame.at("rgb_sha256").get<std::string>();
        if (!isLowerHex(expected_rgb_hash, 64)) {
          result.error = "malformed RGB SHA-256 at frame " + std::to_string(index);
          return result;
        }
        if (rgb_relative != std::filesystem::path("rgb") / (expected_rgb_hash + ".jpg")) {
          result.error = "RGB path is not content-addressed at frame " + std::to_string(index);
          return result;
        }
        const uint64_t declared_rgb_bytes = frame.at("rgb_bytes").get<uint64_t>();
        const std::string rgb_key = rgb_relative.generic_string();
        const auto already_validated = validated_rgb_files.find(rgb_key);
        if (already_validated != validated_rgb_files.end()) {
          if (already_validated->second.first != expected_rgb_hash ||
              already_validated->second.second != declared_rgb_bytes) {
            result.error =
                "reused RGB path has contradictory metadata at frame " + std::to_string(index);
            return result;
          }
        } else {
          if (!unique_paths.insert(rgb_relative).second) {
            result.error =
                "RGB path collides with another bundle artifact at frame " + std::to_string(index);
            return result;
          }
          const auto rgb_path = bundle_dir / rgb_relative;
          uint64_t rgb_bytes = 0;
          if (!checkedFileSize(rgb_path, rgb_bytes, error, true)) {
            result.error = std::move(error);
            return result;
          }
          if (rgb_bytes == 0 || declared_rgb_bytes != rgb_bytes) {
            result.error = "RGB byte count mismatch at frame " + std::to_string(index);
            return result;
          }
          auto rgb_hash = sha256File(rgb_path, error);
          if (!rgb_hash || *rgb_hash != expected_rgb_hash) {
            result.error = error.empty() ? "RGB SHA-256 mismatch at frame " + std::to_string(index)
                                         : std::move(error);
            return result;
          }
          validated_rgb_files.emplace(rgb_key, std::make_pair(expected_rgb_hash, rgb_bytes));
          result.portable_bundle_bytes += rgb_bytes;
        }
        unique_jpeg_hashes.insert(expected_rgb_hash);
      }
    }
    result.frame_count = frames.size();
    result.unique_jpeg_count = unique_jpeg_hashes.size();
    result.ok = true;
    return result;
  } catch (const std::exception& exception) {
    result.error = exception.what();
    return result;
  }
}

}  // namespace kstudio
