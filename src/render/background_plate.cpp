#include "render/background_plate.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <new>

namespace kstudio {

namespace {

constexpr std::array<char, 8> kMagic = {'K', 'S', 'T', 'P', 'L', 'T', '1', '\0'};
constexpr uint32_t kFormatVersion = 1;
constexpr uint32_t kHeaderBytes = 40;
constexpr uint32_t kUnitsPerMm = 10;
constexpr size_t kMaxPixels = size_t{16} * 1024 * 1024;

bool checkedPixelCount(uint32_t width, uint32_t height, size_t& count) {
  if (width == 0 || height == 0 || size_t(height) > std::numeric_limits<size_t>::max() / width)
    return false;
  count = size_t(width) * height;
  return count <= kMaxPixels;
}

void putU32(std::array<uint8_t, kHeaderBytes>& bytes, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
}

void putU64(std::array<uint8_t, kHeaderBytes>& bytes, size_t offset, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) bytes[offset + i] = uint8_t(value >> (i * 8));
}

uint32_t getU32(const std::array<uint8_t, kHeaderBytes>& bytes, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes[offset + i]) << (i * 8);
  return value;
}

uint64_t getU64(const std::array<uint8_t, kHeaderBytes>& bytes, size_t offset) {
  uint64_t value = 0;
  for (size_t i = 0; i < 8; ++i) value |= uint64_t(bytes[offset + i]) << (i * 8);
  return value;
}

}  // namespace

bool BackgroundPlate::valid() const {
  size_t count = 0;
  return checkedPixelCount(width, height, count) && depth_dmm.size() == count;
}

size_t BackgroundPlate::knownPixelCount() const {
  return size_t(
      std::count_if(depth_dmm.begin(), depth_dmm.end(), [](uint16_t d) { return d != 0; }));
}

bool BackgroundPlateAccumulator::begin(Config config, std::string& error) {
  cancel();
  error.clear();

  size_t pixel_count = 0;
  if (!checkedPixelCount(config.width, config.height, pixel_count)) {
    error = "invalid plate dimensions";
    return false;
  }
  if (config.frame_count == 0 ||
      config.frame_count > std::numeric_limits<size_t>::max() / pixel_count) {
    error = "invalid plate frame count";
    return false;
  }
  if (!std::isfinite(config.min_valid_fraction) || config.min_valid_fraction <= 0.0 ||
      config.min_valid_fraction > 1.0) {
    error = "valid fraction must be in (0, 1]";
    return false;
  }

  config_ = config;
  pixel_count_ = pixel_count;
  const double required = double(config_.frame_count) * config_.min_valid_fraction;
  // Fractions such as 0.2 are not exactly representable. Step one ULP toward
  // zero so an exact decimal threshold (60 * 0.2 = 12) cannot round up to 13.
  required_valid_frames_ = std::max<size_t>(
      1, size_t(std::ceil(std::nextafter(required, -std::numeric_limits<double>::infinity()))));
  captured_frames_ = 0;
  try {
    samples_.assign(pixel_count_ * config_.frame_count, 0);
  } catch (const std::bad_alloc&) {
    cancel();
    error = "not enough memory for background capture";
    return false;
  }
  return true;
}

bool BackgroundPlateAccumulator::addFrame(std::span<const uint16_t> depth_dmm, std::string& error) {
  error.clear();
  if (!active()) {
    error = ready() ? "plate capture is already complete" : "plate capture has not started";
    return false;
  }
  if (depth_dmm.size() != pixel_count_) {
    error = "depth frame dimensions do not match plate capture";
    return false;
  }

  std::copy(depth_dmm.begin(), depth_dmm.end(),
            samples_.begin() + std::ptrdiff_t(captured_frames_ * pixel_count_));
  ++captured_frames_;
  return true;
}

std::optional<BackgroundPlate> BackgroundPlateAccumulator::finish(uint64_t captured_unix_seconds,
                                                                  std::string& error) {
  error.clear();
  if (!ready()) {
    error = "plate capture is not complete";
    return std::nullopt;
  }

  try {
    BackgroundPlate plate;
    plate.width = config_.width;
    plate.height = config_.height;
    plate.captured_unix_seconds = captured_unix_seconds;
    plate.depth_dmm.assign(pixel_count_, 0);

    // One reusable capture-time scratch buffer: no allocation per pixel.
    std::vector<uint16_t> valid(config_.frame_count);
    for (size_t pixel = 0; pixel < pixel_count_; ++pixel) {
      size_t valid_count = 0;
      for (size_t frame = 0; frame < config_.frame_count; ++frame) {
        const uint16_t sample = samples_[frame * pixel_count_ + pixel];
        if (sample != 0) valid[valid_count++] = sample;
      }
      if (valid_count < required_valid_frames_) continue;  // unknown pixel

      std::sort(valid.begin(), valid.begin() + std::ptrdiff_t(valid_count));
      if ((valid_count & 1u) != 0) {
        plate.depth_dmm[pixel] = valid[valid_count / 2];
      } else {
        const uint32_t lower = valid[valid_count / 2 - 1];
        const uint32_t upper = valid[valid_count / 2];
        plate.depth_dmm[pixel] = uint16_t((lower + upper + 1) / 2);  // round half up
      }
    }

    cancel();
    return plate;
  } catch (const std::bad_alloc&) {
    cancel();
    error = "not enough memory to finalize background plate";
    return std::nullopt;
  }
}

void BackgroundPlateAccumulator::cancel() {
  config_ = {};
  pixel_count_ = 0;
  required_valid_frames_ = 0;
  captured_frames_ = 0;
  std::vector<uint16_t>().swap(samples_);
}

bool shouldSubtractBackground(uint16_t depth_dmm, uint16_t plate_dmm, float epsilon_mm) {
  if (depth_dmm == 0 || plate_dmm == 0 || !std::isfinite(epsilon_mm) || epsilon_mm <= 0.0f)
    return false;
  const int delta = std::abs(int(depth_dmm) - int(plate_dmm));
  return float(delta) < epsilon_mm * float(kUnitsPerMm);
}

bool saveBackgroundPlate(const std::filesystem::path& path, const BackgroundPlate& plate,
                         std::string& error) {
  error.clear();
  if (!plate.valid()) {
    error = "cannot save an invalid background plate";
    return false;
  }
  if (path.empty() || path.filename().empty()) {
    error = "plate path must name a file";
    return false;
  }

  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "cannot create plate directory: " + ec.message();
      return false;
    }
  }

  const std::filesystem::path temporary = path.string() + ".tmp";
  std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "cannot open temporary plate file for writing";
    return false;
  }

  std::array<uint8_t, kHeaderBytes> header{};
  std::copy(kMagic.begin(), kMagic.end(), reinterpret_cast<char*>(header.data()));
  putU32(header, 8, kFormatVersion);
  putU32(header, 12, kHeaderBytes);
  putU32(header, 16, plate.width);
  putU32(header, 20, plate.height);
  putU32(header, 24, kUnitsPerMm);
  putU32(header, 28, 0);  // reserved
  putU64(header, 32, plate.captured_unix_seconds);
  out.write(reinterpret_cast<const char*>(header.data()), std::streamsize(header.size()));
  try {
    std::vector<uint8_t> payload(plate.depth_dmm.size() * 2);
    for (size_t i = 0; i < plate.depth_dmm.size(); ++i) {
      payload[i * 2] = uint8_t(plate.depth_dmm[i] & 0xff);
      payload[i * 2 + 1] = uint8_t(plate.depth_dmm[i] >> 8);
    }
    out.write(reinterpret_cast<const char*>(payload.data()), std::streamsize(payload.size()));
  } catch (const std::bad_alloc&) {
    out.close();
    std::filesystem::remove(temporary, ec);
    error = "not enough memory to save background plate";
    return false;
  }
  out.flush();
  if (!out) {
    out.close();
    std::filesystem::remove(temporary, ec);
    error = "failed while writing background plate";
    return false;
  }
  out.close();

  std::filesystem::rename(temporary, path, ec);
  if (ec) {
    const std::error_code rename_error = ec;
    std::filesystem::remove(temporary, ec);
    error = "cannot publish background plate: " + rename_error.message();
    return false;
  }
  return true;
}

std::optional<BackgroundPlate> loadBackgroundPlate(const std::filesystem::path& path,
                                                   std::string& error) {
  error.clear();
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot open background plate";
    return std::nullopt;
  }

  std::array<uint8_t, kHeaderBytes> header{};
  in.read(reinterpret_cast<char*>(header.data()), std::streamsize(header.size()));
  if (!in) {
    error = "background plate header is truncated";
    return std::nullopt;
  }
  if (!std::equal(kMagic.begin(), kMagic.end(), reinterpret_cast<const char*>(header.data()))) {
    error = "background plate magic mismatch";
    return std::nullopt;
  }
  if (getU32(header, 8) != kFormatVersion || getU32(header, 12) != kHeaderBytes) {
    error = "unsupported background plate version";
    return std::nullopt;
  }
  if (getU32(header, 24) != kUnitsPerMm) {
    error = "unsupported background plate depth units";
    return std::nullopt;
  }

  BackgroundPlate plate;
  plate.width = getU32(header, 16);
  plate.height = getU32(header, 20);
  plate.captured_unix_seconds = getU64(header, 32);
  size_t pixel_count = 0;
  if (!checkedPixelCount(plate.width, plate.height, pixel_count)) {
    error = "invalid background plate dimensions";
    return std::nullopt;
  }

  std::error_code ec;
  const uintmax_t file_bytes = std::filesystem::file_size(path, ec);
  const uintmax_t expected_bytes = uintmax_t(kHeaderBytes) + uintmax_t(pixel_count) * 2;
  if (ec || file_bytes != expected_bytes) {
    error = "background plate payload size mismatch";
    return std::nullopt;
  }

  try {
    std::vector<uint8_t> payload(pixel_count * 2);
    in.read(reinterpret_cast<char*>(payload.data()), std::streamsize(payload.size()));
    if (!in) {
      error = "background plate payload is truncated";
      return std::nullopt;
    }
    plate.depth_dmm.resize(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i)
      plate.depth_dmm[i] = uint16_t(payload[i * 2]) | uint16_t(uint16_t(payload[i * 2 + 1]) << 8);
  } catch (const std::bad_alloc&) {
    error = "not enough memory to load background plate";
    return std::nullopt;
  }
  return plate;
}

}  // namespace kstudio
