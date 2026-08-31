#include <doctest/doctest.h>
#include "svg-path-flatten.h"

#include <cmath>

TEST_CASE("FlattenedPath: a straight line has exact length and midpoint")
{
  FlattenedPath path = FlattenedPath::from_svg_d("M0,0 L100,0");

  CHECK(path.length() == doctest::Approx(100.0));

  auto start = path.at(0);
  CHECK(start.point.x == doctest::Approx(0.0));
  CHECK(start.point.y == doctest::Approx(0.0));
  CHECK(start.angle == doctest::Approx(0.0));

  auto mid = path.at(50);
  CHECK(mid.point.x == doctest::Approx(50.0));
  CHECK(mid.point.y == doctest::Approx(0.0));

  auto end = path.at(1000);   // clamped past the end
  CHECK(end.point.x == doctest::Approx(100.0));
  CHECK(end.point.y == doctest::Approx(0.0));
}

TEST_CASE("FlattenedPath: circular_arc has the right length, point and tangent")
{
  const double radius = 10.0;
  FlattenedPath path = FlattenedPath::circular_arc({0,0}, radius, 0, 360);

  CHECK(path.length() == doctest::Approx(2*M_PI*radius).epsilon(0.01));

  // A quarter circle round from the start (radius, 0) is the top of the
  // circle (0, radius), where travelling counterclockwise means the
  // tangent points in -x.
  auto quarter = path.at(path.length()/4);
  CHECK(quarter.point.x == doctest::Approx(0.0).epsilon(0.02));
  CHECK(quarter.point.y == doctest::Approx(radius).epsilon(0.02));
  CHECK(std::cos(quarter.angle) == doctest::Approx(-1.0).epsilon(0.02));
  CHECK(std::sin(quarter.angle) == doctest::Approx(0.0).epsilon(0.02));
}

TEST_CASE("FlattenedPath: a curve's endpoints match what nanosvg parsed")
{
  FlattenedPath path = FlattenedPath::from_svg_d("M0,0 C0,50 100,50 100,0");

  auto start = path.at(0);
  CHECK(start.point.x == doctest::Approx(0.0));
  CHECK(start.point.y == doctest::Approx(0.0));

  auto end = path.at(path.length());
  CHECK(end.point.x == doctest::Approx(100.0));
  CHECK(end.point.y == doctest::Approx(0.0));
}
