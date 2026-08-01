#include "track/pose_protocol.hpp"

#include <bit>
#include <cmath>
#include <type_traits>

namespace kstudio::pose_wire {

namespace {

class Writer {
 public:
  explicit Writer(std::span<std::byte> bytes) : bytes_(bytes) {}

  template <typename T>
  void integer(T value) {
    static_assert(std::is_unsigned_v<T>);
    for (size_t i = 0; i < sizeof(T); ++i)
      bytes_[offset_++] = std::byte((value >> (i * 8u)) & T{0xff});
  }

  void floating(float value) { integer(std::bit_cast<uint32_t>(value)); }

 private:
  std::span<std::byte> bytes_;
  size_t offset_ = 0;
};

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename T>
  T integer() {
    static_assert(std::is_unsigned_v<T>);
    T value = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
      value |= T(std::to_integer<uint8_t>(bytes_[offset_++])) << (i * 8u);
    return value;
  }

  float floating() { return std::bit_cast<float>(integer<uint32_t>()); }

 private:
  std::span<const std::byte> bytes_;
  size_t offset_ = 0;
};

bool validateSize(std::span<const std::byte> bytes, size_t expected, std::string& error) {
  if (bytes.size() == expected) return true;
  error = "wrong message size: expected " + std::to_string(expected) + ", got " +
          std::to_string(bytes.size());
  return false;
}

bool finiteLandmark(const PoseLandmark& landmark) {
  return finite(landmark.image) && finite(landmark.model) && std::isfinite(landmark.visibility) &&
         std::isfinite(landmark.presence);
}

}  // namespace

RequestHeaderBytes encodeRequestHeader(const RequestHeader& header) {
  RequestHeaderBytes bytes{};
  Writer writer(bytes);
  writer.integer(kRequestMagic);
  writer.integer(kVersion);
  writer.integer(header.frame_id);
  writer.integer(header.depth_seq);
  writer.integer(header.color_seq);
  writer.integer(header.capture_ns);
  writer.integer(header.payload_bytes);
  return bytes;
}

ResultHeaderBytes encodeResultHeader(const ResultHeader& header) {
  ResultHeaderBytes bytes{};
  Writer writer(bytes);
  writer.integer(kResultMagic);
  writer.integer(kVersion);
  writer.integer(header.frame_id);
  writer.integer(header.depth_seq);
  writer.integer(header.color_seq);
  writer.integer(header.capture_ns);
  writer.integer(header.result_ns);
  writer.floating(header.inference_ms);
  writer.integer(uint32_t(header.detected));
  writer.integer(header.joint_count);
  writer.integer(uint32_t{0});
  return bytes;
}

LandmarkBytes encodeLandmarks(const std::array<PoseLandmark, kBodyJointCount>& landmarks) {
  LandmarkBytes bytes{};
  Writer writer(bytes);
  for (const PoseLandmark& landmark : landmarks) {
    writer.floating(landmark.image.x);
    writer.floating(landmark.image.y);
    writer.floating(landmark.image.z);
    writer.floating(landmark.model.x);
    writer.floating(landmark.model.y);
    writer.floating(landmark.model.z);
    writer.floating(landmark.visibility);
    writer.floating(landmark.presence);
  }
  return bytes;
}

bool decodeRequestHeader(std::span<const std::byte> bytes, RequestHeader& header,
                         std::string& error) {
  if (!validateSize(bytes, kRequestHeaderBytes, error)) return false;
  Reader reader(bytes);
  if (reader.integer<uint32_t>() != kRequestMagic) {
    error = "wrong request magic";
    return false;
  }
  if (reader.integer<uint32_t>() != kVersion) {
    error = "unsupported request version";
    return false;
  }
  header.frame_id = reader.integer<uint64_t>();
  header.depth_seq = reader.integer<uint64_t>();
  header.color_seq = reader.integer<uint64_t>();
  header.capture_ns = reader.integer<uint64_t>();
  header.payload_bytes = reader.integer<uint64_t>();
  if (header.payload_bytes == 0 || header.payload_bytes > kMaximumJpegBytes) {
    error = "request JPEG size is outside protocol bounds";
    return false;
  }
  return true;
}

bool decodeResultHeader(std::span<const std::byte> bytes, ResultHeader& header,
                        std::string& error) {
  if (!validateSize(bytes, kResultHeaderBytes, error)) return false;
  Reader reader(bytes);
  if (reader.integer<uint32_t>() != kResultMagic) {
    error = "wrong result magic";
    return false;
  }
  if (reader.integer<uint32_t>() != kVersion) {
    error = "unsupported result version";
    return false;
  }
  header.frame_id = reader.integer<uint64_t>();
  header.depth_seq = reader.integer<uint64_t>();
  header.color_seq = reader.integer<uint64_t>();
  header.capture_ns = reader.integer<uint64_t>();
  header.result_ns = reader.integer<uint64_t>();
  header.inference_ms = reader.floating();
  const uint32_t detected = reader.integer<uint32_t>();
  header.joint_count = reader.integer<uint32_t>();
  const uint32_t reserved = reader.integer<uint32_t>();
  if (detected > 1) {
    error = "invalid detected flag";
    return false;
  }
  if (header.joint_count != kBodyJointCount) {
    error = "result does not contain exactly 33 joints";
    return false;
  }
  if (reserved != 0) {
    error = "non-zero reserved result field";
    return false;
  }
  if (!std::isfinite(header.inference_ms) || header.inference_ms < 0.0f ||
      header.inference_ms > 60'000.0f) {
    error = "invalid inference duration";
    return false;
  }
  header.detected = detected != 0;
  return true;
}

bool decodeLandmarks(std::span<const std::byte> bytes,
                     std::array<PoseLandmark, kBodyJointCount>& landmarks, std::string& error) {
  if (!validateSize(bytes, kResultLandmarkBytes, error)) return false;
  Reader reader(bytes);
  for (PoseLandmark& landmark : landmarks) {
    landmark.image = {reader.floating(), reader.floating(), reader.floating()};
    landmark.model = {reader.floating(), reader.floating(), reader.floating()};
    landmark.visibility = reader.floating();
    landmark.presence = reader.floating();
    if (!finiteLandmark(landmark)) {
      error = "result contains a non-finite landmark";
      return false;
    }
    if (landmark.visibility < 0.0f || landmark.visibility > 1.0f || landmark.presence < 0.0f ||
        landmark.presence > 1.0f) {
      error = "result landmark confidence is outside [0, 1]";
      return false;
    }
  }
  return true;
}

}  // namespace kstudio::pose_wire
