#include <doctest/doctest.h>

#include <bit>
#include <limits>
#include <string>

#include "track/pose_protocol.hpp"

using namespace kstudio;

TEST_CASE("pose wire protocol has stable, round-tripping layouts") {
  static_assert(pose_wire::kRequestHeaderBytes == 48);
  static_assert(pose_wire::kResultHeaderBytes == 64);
  static_assert(pose_wire::kLandmarkBytes == 32);

  const pose_wire::RequestHeader input{42, 101, 99, 123'456'789, 72'000};
  const auto bytes = pose_wire::encodeRequestHeader(input);
  CHECK(std::to_integer<char>(bytes[0]) == 'K');
  CHECK(std::to_integer<char>(bytes[1]) == 'P');
  pose_wire::RequestHeader decoded;
  std::string error;
  REQUIRE(pose_wire::decodeRequestHeader(bytes, decoded, error));
  CHECK(decoded.frame_id == input.frame_id);
  CHECK(decoded.depth_seq == input.depth_seq);
  CHECK(decoded.color_seq == input.color_seq);
  CHECK(decoded.capture_ns == input.capture_ns);
  CHECK(decoded.payload_bytes == input.payload_bytes);

  pose_wire::ResultHeader result{42,          101,  99,   123'456'789,
                                 123'999'999, 8.5f, true, kBodyJointCount};
  const auto result_bytes = pose_wire::encodeResultHeader(result);
  pose_wire::ResultHeader decoded_result;
  REQUIRE(pose_wire::decodeResultHeader(result_bytes, decoded_result, error));
  CHECK(decoded_result.detected);
  CHECK(decoded_result.inference_ms == doctest::Approx(8.5f));
  CHECK(decoded_result.joint_count == 33);
}

TEST_CASE("pose wire protocol rejects malformed and unbounded input") {
  std::string error;
  pose_wire::RequestHeader request;
  auto bytes = pose_wire::encodeRequestHeader({1, 2, 3, 4, 20});
  bytes[0] = std::byte{'X'};
  CHECK_FALSE(pose_wire::decodeRequestHeader(bytes, request, error));
  CHECK(error == "wrong request magic");

  auto too_large = pose_wire::encodeRequestHeader({1, 2, 3, 4, pose_wire::kMaximumJpegBytes + 1});
  CHECK_FALSE(pose_wire::decodeRequestHeader(too_large, request, error));

  pose_wire::ResultHeader result;
  auto wrong_count =
      pose_wire::encodeResultHeader({1, 2, 3, 4, 5, 1.0f, true, kBodyJointCount - 1});
  CHECK_FALSE(pose_wire::decodeResultHeader(wrong_count, result, error));
}

TEST_CASE("pose wire landmarks preserve values and reject non-finite data") {
  std::array<PoseLandmark, kBodyJointCount> source{};
  for (size_t i = 0; i < source.size(); ++i) {
    source[i].image = {0.1f * float(i), 0.2f, -0.3f};
    source[i].model = {0.4f, -0.5f, 0.6f};
    source[i].visibility = 0.7f;
    source[i].presence = 0.8f;
  }
  auto bytes = pose_wire::encodeLandmarks(source);
  std::array<PoseLandmark, kBodyJointCount> decoded{};
  std::string error;
  REQUIRE(pose_wire::decodeLandmarks(bytes, decoded, error));
  CHECK(decoded[12].image.x == doctest::Approx(1.2f));
  CHECK(decoded[12].model.y == doctest::Approx(-0.5f));

  source[4].presence = std::numeric_limits<float>::quiet_NaN();
  bytes = pose_wire::encodeLandmarks(source);
  CHECK_FALSE(pose_wire::decodeLandmarks(bytes, decoded, error));
}
