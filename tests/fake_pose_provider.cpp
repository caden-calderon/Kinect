#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/clock.hpp"
#include "track/pose_protocol.hpp"

namespace {

bool readAll(void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  size_t offset = 0;
  while (offset < size) {
    const ssize_t count = ::read(STDIN_FILENO, bytes + offset, size - offset);
    if (count == 0) return false;
    if (count < 0) return false;
    offset += size_t(count);
  }
  return true;
}

bool writeAll(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  size_t offset = 0;
  while (offset < size) {
    const ssize_t count = ::write(STDOUT_FILENO, bytes + offset, size - offset);
    if (count <= 0) return false;
    offset += size_t(count);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace kstudio;
  const bool mismatch_identity = argc > 1 && std::string(argv[1]) == "mismatch";
  while (true) {
    pose_wire::RequestHeaderBytes request_bytes{};
    if (!readAll(request_bytes.data(), request_bytes.size())) return 0;
    pose_wire::RequestHeader request;
    std::string error;
    if (!pose_wire::decodeRequestHeader(request_bytes, request, error)) return 2;
    std::array<std::byte, 4096> chunk{};
    uint64_t unread = request.payload_bytes;
    while (unread > 0) {
      const size_t count = size_t(std::min<uint64_t>(unread, chunk.size()));
      if (!readAll(chunk.data(), count)) return 3;
      unread -= count;
    }

    pose_wire::ResultHeader result;
    result.frame_id = request.frame_id;
    result.depth_seq = request.depth_seq;
    result.color_seq = request.color_seq;
    result.capture_ns = request.capture_ns;
    result.result_ns = mono_now_ns();
    result.inference_ms = 4.25f;
    result.detected = true;
    result.joint_count = kBodyJointCount;
    if (mismatch_identity) ++result.frame_id;
    std::array<PoseLandmark, kBodyJointCount> landmarks{};
    for (size_t i = 0; i < landmarks.size(); ++i) {
      landmarks[i].image = {float(i) / 100.0f, 0.5f, -0.1f};
      landmarks[i].model = {float(i) / 10.0f, 1.0f, -0.2f};
      landmarks[i].visibility = 0.9f;
      landmarks[i].presence = 0.8f;
    }
    const auto result_bytes = pose_wire::encodeResultHeader(result);
    const auto landmark_bytes = pose_wire::encodeLandmarks(landmarks);
    if (!writeAll(result_bytes.data(), result_bytes.size()) ||
        !writeAll(landmark_bytes.data(), landmark_bytes.size()))
      return 0;
  }
}
