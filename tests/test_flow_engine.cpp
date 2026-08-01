#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <opencv2/imgproc.hpp>
#include <thread>
#include <vector>

#include "motion/flow_engine.hpp"

using namespace kstudio;

namespace {

/// Textured square on a textured background at (x, y), 512x424. The
/// background needs gradients too — DIS extrapolates arbitrary vectors on
/// featureless regions (observed while writing this test).
cv::Mat sceneWithSquare(int x, int y) {
  cv::Mat img(FlowField::kH, FlowField::kW, CV_8UC1);
  for (int j = 0; j < FlowField::kH; ++j)
    for (int i = 0; i < FlowField::kW; ++i)
      img.at<uint8_t>(j, i) = uint8_t(40 + 18 * std::sin(i * 0.21) * std::cos(j * 0.17));
  for (int j = 0; j < 96; ++j)
    for (int i = 0; i < 96; ++i) {
      // deterministic texture so DIS has gradients to match on
      const uint8_t v = uint8_t(100 + 77 * std::sin(i * 0.55) * std::sin(j * 0.4));
      const int px = x + i, py = y + j;
      if (px >= 0 && px < FlowField::kW && py >= 0 && py < FlowField::kH)
        img.at<uint8_t>(py, px) = v;
    }
  cv::GaussianBlur(img, img, cv::Size(5, 5), 1.2);
  return img;
}

/// Median flow / confidence inside a rect of the field.
struct RegionStats {
  float med_fx, med_fy, med_conf;
};
RegionStats regionStats(const FlowField& f, int x0, int y0, int w, int h) {
  std::vector<float> fx, fy, conf;
  for (int y = y0; y < y0 + h; ++y)
    for (int x = x0; x < x0 + w; ++x) {
      const float* t = f.texels.data() + (size_t(y) * FlowField::kW + x) * 4;
      fx.push_back(t[0]);
      fy.push_back(t[1]);
      conf.push_back(t[2]);
    }
  auto med = [](std::vector<float>& v) {
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
  };
  return {med(fx), med(fy), med(conf)};
}

}  // namespace

TEST_CASE("computePair recovers a known translation with high confidence") {
  auto fwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
  auto bwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
  FlowField field;
  FlowEngine::computePair(fwd, bwd, sceneWithSquare(200, 160), sceneWithSquare(206, 157), field);

  // interior of the moved square (avoid its edges)
  const auto s = regionStats(field, 206 + 24, 157 + 24, 48, 48);
  CHECK(s.med_fx == doctest::Approx(6.f).epsilon(0.25));
  CHECK(s.med_fy == doctest::Approx(-3.f).epsilon(0.25));
  CHECK(s.med_conf > 0.7f);

  // far background: static and consistent
  const auto b = regionStats(field, 8, 8, 64, 64);
  CHECK(std::abs(b.med_fx) < 0.5f);
  CHECK(std::abs(b.med_fy) < 0.5f);
  CHECK(b.med_conf > 0.7f);
}

TEST_CASE("computePair output is deterministic across repeat runs") {
  const cv::Mat a = sceneWithSquare(180, 140), b = sceneWithSquare(188, 146);
  FlowField f1, f2;
  {
    auto fwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
    auto bwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
    FlowEngine::computePair(fwd, bwd, a, b, f1);
  }
  {
    auto fwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
    auto bwd = FlowEngine::makeDis(FlowEngine::Preset::Fast);
    FlowEngine::computePair(fwd, bwd, a, b, f2);
  }
  CHECK(f1.texels == f2.texels);  // E7 rule: replay must be reproducible
}

TEST_CASE("grayFromBgrx downscales 1080p BGRX to the flow raster") {
  // left half pure blue, right half pure red (BGRX byte order: B,G,R,X)
  std::vector<uint8_t> bgrx(size_t(kColorWidth) * kColorHeight * 4, 0);
  for (int y = 0; y < kColorHeight; ++y)
    for (int x = 0; x < kColorWidth; ++x) {
      uint8_t* p = bgrx.data() + (size_t(y) * kColorWidth + x) * 4;
      p[x < kColorWidth / 2 ? 0 : 2] = 255;
    }
  cv::Mat gray;
  FlowEngine::grayFromBgrx(bgrx.data(), gray);
  REQUIRE(gray.cols == FlowField::kW);
  REQUIRE(gray.rows == FlowField::kH);
  // Rec.601: blue ~0.114*255=29, red ~0.299*255=76 — proves channel order
  CHECK(int(gray.at<uint8_t>(200, 64)) == doctest::Approx(29).epsilon(0.15));
  CHECK(int(gray.at<uint8_t>(200, 448)) == doctest::Approx(76).epsilon(0.15));
}

TEST_CASE("engine thread: distinct colors produce fields, repeats do not") {
  Telemetry telemetry;
  bool enabled = true;
  int preset = int(FlowEngine::Preset::Ultrafast);  // cheap for the unit test
  FlowEngine engine({&enabled, &preset}, telemetry);
  engine.start();

  struct TestColor final : ColorProduct {
    explicit TestColor(uint32_t seq) : seq_(seq), bgrx_(size_t(kColorWidth) * kColorHeight * 4) {
      // moving vertical stripe so consecutive frames differ
      for (int y = 0; y < kColorHeight; ++y)
        for (int x = 0; x < kColorWidth; ++x) {
          uint8_t v = uint8_t(60 + 60 * std::sin((x - int(seq_) * 12) * 0.05));
          uint8_t* p = bgrx_.data() + (size_t(y) * kColorWidth + x) * 4;
          p[0] = p[1] = p[2] = v;
        }
    }
    const uint8_t* jpeg() const override { return nullptr; }
    size_t jpegSize() const override { return 0; }
    const uint8_t* bgrx() const override { return bgrx_.data(); }
    uint32_t sequence() const override { return seq_; }
    uint32_t deviceTimestamp() const override { return seq_ * 267; }
    uint64_t hostReceiveNs() const override { return 0; }
    uint32_t seq_;
    std::vector<uint8_t> bgrx_;
  };

  auto frameWithColor = [](uint32_t color_seq, uint64_t frame_id) {
    RgbdFrame f;
    f.frame_id = frame_id;
    f.color = std::make_shared<TestColor>(color_seq);
    f.color_seq = color_seq;
    return f;
  };

  engine.submit(frameWithColor(10, 0));
  engine.submit(frameWithColor(10, 1));  // same color again: no new field
  engine.submit(frameWithColor(11, 2));

  std::shared_ptr<const FlowField> field;
  for (int i = 0; i < 200 && !field; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    field = engine.latest();
  }
  REQUIRE(field);
  CHECK(field->from_color_seq == 10);
  CHECK(field->to_color_seq == 11);
  CHECK(field->color_seq_gap == 0);
  CHECK(field->frame_id == 2);
  engine.stop();
}
