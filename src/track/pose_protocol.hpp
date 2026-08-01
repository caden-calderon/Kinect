#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "track/body_frame.hpp"

namespace kstudio::pose_wire {

constexpr uint32_t fourcc(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8u) | (uint32_t(uint8_t(c)) << 16u) |
         (uint32_t(uint8_t(d)) << 24u);
}

constexpr uint32_t kRequestMagic = fourcc('K', 'P', 'R', 'Q');
constexpr uint32_t kResultMagic = fourcc('K', 'P', 'R', 'S');
constexpr uint32_t kVersion = 1;
constexpr size_t kRequestHeaderBytes = 48;
constexpr size_t kResultHeaderBytes = 64;
constexpr size_t kLandmarkBytes = 32;
constexpr size_t kResultLandmarkBytes = kBodyJointCount * kLandmarkBytes;
constexpr uint64_t kMaximumJpegBytes = 16ull * 1024ull * 1024ull;

struct RequestHeader {
  uint64_t frame_id = 0;
  uint64_t depth_seq = 0;
  uint64_t color_seq = 0;
  uint64_t capture_ns = 0;
  uint64_t payload_bytes = 0;
};

struct ResultHeader {
  uint64_t frame_id = 0;
  uint64_t depth_seq = 0;
  uint64_t color_seq = 0;
  uint64_t capture_ns = 0;
  uint64_t result_ns = 0;
  float inference_ms = 0.0f;
  bool detected = false;
  uint32_t joint_count = 0;
};

using RequestHeaderBytes = std::array<std::byte, kRequestHeaderBytes>;
using ResultHeaderBytes = std::array<std::byte, kResultHeaderBytes>;
using LandmarkBytes = std::array<std::byte, kResultLandmarkBytes>;

RequestHeaderBytes encodeRequestHeader(const RequestHeader& header);
ResultHeaderBytes encodeResultHeader(const ResultHeader& header);
LandmarkBytes encodeLandmarks(const std::array<PoseLandmark, kBodyJointCount>& landmarks);

bool decodeRequestHeader(std::span<const std::byte> bytes, RequestHeader& header,
                         std::string& error);
bool decodeResultHeader(std::span<const std::byte> bytes, ResultHeader& header, std::string& error);
bool decodeLandmarks(std::span<const std::byte> bytes,
                     std::array<PoseLandmark, kBodyJointCount>& landmarks, std::string& error);

}  // namespace kstudio::pose_wire
