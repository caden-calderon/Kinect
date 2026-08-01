#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "render/background_plate.hpp"

using namespace kstudio;

TEST_CASE("background plate uses valid-sample median and marks sparse pixels unknown") {
  BackgroundPlateAccumulator capture;
  BackgroundPlateAccumulator::Config config;
  config.width = 3;
  config.height = 1;
  config.frame_count = 5;
  config.min_valid_fraction = 0.4;  // at least two valid samples

  std::string error;
  REQUIRE(capture.begin(config, error));
  const std::array<std::array<uint16_t, 3>, 5> frames = {std::array<uint16_t, 3>{100, 0, 300},
                                                         {102, 0, 302},
                                                         {98, 200, 0},
                                                         {104, 0, 304},
                                                         {0, 0, 306}};
  for (const auto& frame : frames) REQUIRE(capture.addFrame(frame, error));

  CHECK(capture.ready());
  auto plate = capture.finish(123456, error);
  REQUIRE(plate);
  CHECK(plate->width == 3);
  CHECK(plate->height == 1);
  CHECK(plate->captured_unix_seconds == 123456);
  REQUIRE(plate->depth_dmm.size() == 3);
  CHECK(plate->depth_dmm[0] == 101);  // even median, rounded half up
  CHECK(plate->depth_dmm[1] == 0);    // one valid sample: unknown
  CHECK(plate->depth_dmm[2] == 303);
  CHECK(plate->knownPixelCount() == 2);
  CHECK_FALSE(capture.active());
  CHECK_FALSE(capture.ready());
}

TEST_CASE("background plate accepts the exact valid-fraction threshold") {
  BackgroundPlateAccumulator capture;
  BackgroundPlateAccumulator::Config config{1, 1, 60, 0.2};
  std::string error;
  REQUIRE(capture.begin(config, error));
  for (size_t frame_index = 0; frame_index < 60; ++frame_index) {
    const uint16_t value = frame_index < 12 ? uint16_t(200 + frame_index) : 0;
    const std::array<uint16_t, 1> frame{value};
    REQUIRE(capture.addFrame(frame, error));
  }
  auto plate = capture.finish(1, error);
  REQUIRE(plate);
  CHECK(plate->depth_dmm[0] == 206);
}

TEST_CASE("background plate capture rejects invalid state and frame shape") {
  BackgroundPlateAccumulator capture;
  std::string error;
  const std::array<uint16_t, 1> one{100};
  CHECK_FALSE(capture.addFrame(one, error));
  CHECK_FALSE(error.empty());

  BackgroundPlateAccumulator::Config bad{0, 1, 5, 0.2};
  CHECK_FALSE(capture.begin(bad, error));

  BackgroundPlateAccumulator::Config good{2, 1, 1, 0.2};
  REQUIRE(capture.begin(good, error));
  CHECK_FALSE(capture.addFrame(one, error));
  const std::array<uint16_t, 2> two{100, 200};
  REQUIRE(capture.addFrame(two, error));
  CHECK_FALSE(capture.addFrame(two, error));
}

TEST_CASE("background epsilon comparison is strict and preserves unknown pixels") {
  CHECK(shouldSubtractBackground(15000, 15010, 2.0f));
  CHECK_FALSE(shouldSubtractBackground(15000, 15020, 2.0f));  // exactly epsilon
  CHECK_FALSE(shouldSubtractBackground(0, 15000, 60.0f));
  CHECK_FALSE(shouldSubtractBackground(15000, 0, 60.0f));
  CHECK_FALSE(shouldSubtractBackground(15000, 15000, 0.0f));
}

TEST_CASE("background plate file round-trips metadata and little-endian depth") {
  BackgroundPlate plate;
  plate.width = 3;
  plate.height = 2;
  plate.captured_unix_seconds = 987654321;
  plate.depth_dmm = {0, 1, 255, 256, 32000, 65535};

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("kstudio-background-plate-" + std::to_string(nonce) + ".plate");
  std::string error;
  REQUIRE(saveBackgroundPlate(path, plate, error));
  auto loaded = loadBackgroundPlate(path, error);
  REQUIRE(loaded);
  CHECK(loaded->width == plate.width);
  CHECK(loaded->height == plate.height);
  CHECK(loaded->captured_unix_seconds == plate.captured_unix_seconds);
  CHECK(loaded->depth_dmm == plate.depth_dmm);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST_CASE("background plate loader refuses a truncated payload") {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto path = std::filesystem::temp_directory_path() /
                    ("kstudio-background-plate-bad-" + std::to_string(nonce) + ".plate");
  {
    std::ofstream out(path, std::ios::binary);
    out << "KSTPLT1";
  }
  std::string error;
  CHECK_FALSE(loadBackgroundPlate(path, error));
  CHECK_FALSE(error.empty());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
