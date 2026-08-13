#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "render/spectral_backfill.hpp"

using namespace kstudio;

static_assert(SpectralBackfillPipeline::layer == Layer::Artistic);
static_assert(SpectralBackfillPipeline::space == Space::DepthCam);
static_assert(SpectralBackfillPipeline::kCapacity == 500'000);

TEST_CASE("spectral backfill lifecycle is absolute-frame and slot deterministic") {
  for (uint32_t slot : {0u, 1u, 17u, 49'999u, 249'999u}) {
    const uint32_t lifetime = backfillLifetimeFrames(slot);
    CHECK(lifetime >= 72u);
    CHECK(lifetime <= 168u);
    CHECK(backfillLifetimeFrames(slot) == lifetime);

    uint64_t boundary = 0;
    while ((boundary % lifetime + slot % lifetime) % lifetime != 0) ++boundary;
    CHECK(backfillRespawnDue(slot, boundary, 1));
    CHECK_FALSE(backfillRespawnDue(slot, boundary + 1, 1));
    CHECK(backfillRespawnDue(slot, boundary + 3, 4));
    CHECK(backfillRespawnDue(slot, boundary + lifetime, lifetime));
  }
  CHECK_FALSE(backfillRespawnDue(5, 100, 0));
}

TEST_CASE("spectral backfill bands remain bounded and preserve a dense near layer") {
  std::array<uint32_t, 3> counts{};
  constexpr uint32_t sample_count = 100'000;
  constexpr float wisp_share = 0.08f;
  for (uint32_t slot = 0; slot < sample_count; ++slot) {
    const auto band = backfillBandForSlot(slot, wisp_share);
    ++counts[size_t(band)];
    const float depth = backfillDepthMeters(slot, band, 0.16f, 0.58f);
    CHECK(depth > 0.0f);
    if (band == BackfillBand::NearSurface) CHECK(depth <= 0.16f);
    if (band == BackfillBand::Wisp) CHECK(depth >= 0.58f * 0.65f);
  }

  CHECK(float(counts[size_t(BackfillBand::Wisp)]) / sample_count ==
        doctest::Approx(wisp_share).epsilon(0.08));
  CHECK(counts[size_t(BackfillBand::NearSurface)] > counts[size_t(BackfillBand::Volume)]);
  CHECK(counts[size_t(BackfillBand::Volume)] > counts[size_t(BackfillBand::Wisp)]);
}

TEST_CASE("spectral backfill lifetime envelope fades both ends") {
  CHECK(backfillLifeEnvelope(0.0f, 120.0f) == doctest::Approx(0.0f));
  CHECK(backfillLifeEnvelope(12.0f, 120.0f) == doctest::Approx(1.0f));
  CHECK(backfillLifeEnvelope(60.0f, 120.0f) == doctest::Approx(1.0f));
  CHECK(backfillLifeEnvelope(119.0f, 120.0f) < 0.01f);
  CHECK(backfillLifeEnvelope(120.0f, 120.0f) == doctest::Approx(0.0f));
  CHECK(backfillLifeEnvelope(1.0f, 0.0f) == doctest::Approx(0.0f));
}
