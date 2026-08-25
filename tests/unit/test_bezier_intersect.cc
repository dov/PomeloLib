#include <doctest/doctest.h>
#include "bezier-intersect.h"

#include <cmath>
#include <limits>

using namespace bezier_intersect;

namespace {

Vec2 eval(const Bezier& bz, double t)
{
  double mt = 1 - t;
  double b0 = mt * mt * mt, b1 = 3 * mt * mt * t, b2 = 3 * mt * t * t, b3 = t * t * t;
  return {b0 * bz.xy0.x + b1 * bz.cp0.x + b2 * bz.cp1.x + b3 * bz.xy1.x,
          b0 * bz.xy0.y + b1 * bz.cp0.y + b2 * bz.cp1.y + b3 * bz.xy1.y};
}

// How far p is from the closest densely-sampled point on the curve, i.e.
// an independent (non-analytic) check that a reported hit really does
// lie on the bezier, without having to hand-derive its parameter.
double distance_to_curve(const Bezier& bz, Vec2 p, int samples = 20000)
{
  double best = std::numeric_limits<double>::infinity();
  for (int i = 0; i <= samples; i++)
  {
    Vec2 q = eval(bz, double(i) / samples);
    double d = std::hypot(q.x - p.x, q.y - p.y);
    if (d < best)
      best = d;
  }
  return best;
}

}

TEST_CASE("find_bezier_line_intersection: single crossing of an x-monotone curve")
{
  // Control x's 0,3,7,10 are non-decreasing, which makes the cubic x(t)
  // itself non-decreasing, so a vertical probe within [0,10] must cross
  // exactly once.
  Bezier bz{{0, 0}, {3, 8}, {7, -3}, {10, 5}};
  Line probe{{6, -100}, {6, 100}};

  auto hits = find_bezier_line_intersection(bz, probe);

  REQUIRE(hits.size() == 1);
  CHECK(hits[0].x == doctest::Approx(6.0));
  CHECK(distance_to_curve(bz, hits[0]) < 2e-3);
}

TEST_CASE("find_bezier_line_intersection: no hit when the probe misses the curve's range")
{
  Bezier bz{{0, 0}, {3, 8}, {7, -3}, {10, 5}};
  Line probe{{50, -100}, {50, 100}};

  auto hits = find_bezier_line_intersection(bz, probe);

  CHECK(hits.empty());
}

TEST_CASE("find_bezier_line_intersection: a hump can cross a horizontal line twice")
{
  // Rises from 0 up past y=5 and back down to 0, asymmetrically so
  // neither of the curve's own coefficients accidentally vanish.
  Bezier bz{{0, 0}, {2, 15}, {8, 12}, {10, 0}};
  Line probe{{-100, 5}, {100, 5}};

  auto hits = find_bezier_line_intersection(bz, probe);

  REQUIRE(hits.size() == 2);
  for (const auto& hit : hits)
  {
    CHECK(hit.y == doctest::Approx(5.0));
    CHECK(distance_to_curve(bz, hit) < 2e-3);
  }
  CHECK(hits[0].x != doctest::Approx(hits[1].x));
}
