#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/frame_pool.hpp"

using kstudio::FramePool;

namespace {
struct Payload {
  int value = 0;
};
}  // namespace

TEST_CASE("pool exhausts to empty handles, never blocks") {
  auto pool = FramePool<Payload>::create(2);
  auto a = pool->acquire();
  auto b = pool->acquire();
  REQUIRE(a);
  REQUIRE(b);
  auto c = pool->acquire();
  CHECK_FALSE(c);
  CHECK(pool->in_use() == 2);
  CHECK(pool->high_water() == 2);
}

TEST_CASE("slot returns to freelist when last handle drops") {
  auto pool = FramePool<Payload>::create(1);
  {
    auto a = pool->acquire();
    REQUIRE(a);
    auto copy = a;  // refcount 2
    a.reset();
    CHECK(pool->in_use() == 1);  // copy still holds it
  }
  CHECK(pool->in_use() == 0);
  CHECK(pool->acquire());
}

TEST_CASE("handles keep the pool alive after the creator drops it") {
  FramePool<Payload>::Handle survivor;
  {
    auto pool = FramePool<Payload>::create(1);
    survivor = pool->acquire();
    survivor->value = 42;
  }
  CHECK(survivor->value == 42);  // pool storage still valid
}

TEST_CASE("payload is reused across acquire cycles (no reallocation)") {
  auto pool = FramePool<Payload>::create(1);
  Payload* first;
  {
    auto a = pool->acquire();
    first = &*a;
    a->value = 7;
  }
  auto b = pool->acquire();
  CHECK(&*b == first);  // same slot memory
}
