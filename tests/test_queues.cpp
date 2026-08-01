#include <doctest/doctest.h>

#include "core/queues.hpp"

using kstudio::BoundedQueue;
using kstudio::LatestSlot;

TEST_CASE("latest slot keeps only the freshest value") {
  LatestSlot<int> slot;
  CHECK_FALSE(slot.take().has_value());
  slot.publish(1);
  slot.publish(2);
  auto v = slot.take();
  REQUIRE(v.has_value());
  CHECK(*v == 2);
  CHECK_FALSE(slot.take().has_value());  // consumed; nothing new
}

TEST_CASE("bounded queue refuses pushes beyond capacity") {
  BoundedQueue<int> q(2);
  CHECK(q.push(1));
  CHECK(q.push(2));
  CHECK_FALSE(q.push(3));  // caller must record this as a loss event
  CHECK(q.high_water() == 2);
  CHECK(*q.pop() == 1);
  CHECK(q.push(3));
  CHECK(*q.pop() == 2);
  CHECK(*q.pop() == 3);
  CHECK_FALSE(q.pop().has_value());
}
