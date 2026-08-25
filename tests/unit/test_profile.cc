#include <doctest/doctest.h>
#include "profile.h"

namespace {
NodeData make_node(int node_type, Vec2 xy, Vec2 before, Vec2 after)
{
  NodeData n;
  n.node_type = node_type;
  n.xy = xy;
  n.control_before_xy = before;
  n.control_after_xy = after;
  return n;
}
}

TEST_CASE("Vec2 as_json / from_json round trip")
{
  Vec2 v(1.5, -2.25);
  nlohmann::json j = as_json(v);
  Vec2 back = from_json(j);

  CHECK(back.x == doctest::Approx(v.x));
  CHECK(back.y == doctest::Approx(v.y));
}

TEST_CASE("NodeData::is_positive_directional")
{
  NodeData forward = make_node(0, {5, 0}, {4, 0}, {6, 0});
  CHECK(forward.is_positive_directional());

  NodeData backward = make_node(0, {5, 0}, {6, 0}, {4, 0});
  CHECK_FALSE(backward.is_positive_directional());
}

TEST_CASE("LayerData::get_end_dir")
{
  LayerData layer;
  CHECK(layer.get_end_dir() == Vec2(0, 0));

  layer.push_back(make_node(0, {0, 0}, {0, 0}, {0, 0}));
  CHECK(layer.get_end_dir() == Vec2(0, 0));

  // control_before_xy differs from the node's own xy: the tangent is
  // node - control_before.
  layer.push_back(make_node(0, {10, 4}, {8, 0}, {10, 4}));
  Vec2 dir = layer.get_end_dir();
  CHECK(dir.x == doctest::Approx(2.0));
  CHECK(dir.y == doctest::Approx(4.0));
}

TEST_CASE("LayerData::is_positive_monotone")
{
  LayerData monotone;
  monotone.push_back(make_node(0, {0, 0}, {-1, 0}, {1, 0}));
  monotone.push_back(make_node(0, {10, 0}, {9, 0}, {11, 0}));
  CHECK(monotone.is_positive_monotone());

  LayerData not_monotone;
  not_monotone.push_back(make_node(0, {0, 0}, {-1, 0}, {5, 0}));
  not_monotone.push_back(make_node(0, {2, 0}, {1, 0}, {3, 0}));
  CHECK_FALSE(not_monotone.is_positive_monotone());
}

TEST_CASE("LayerData as_json/from_json: bare array form when there is no appearance")
{
  LayerData layer;
  layer.push_back(make_node(0, {0, 0}, {0, 0}, {1, 0}));
  layer.push_back(make_node(0, {10, 0}, {9, 0}, {10, 0}));

  CHECK_FALSE(layer.has_appearance());
  nlohmann::json j = layer.as_json();
  CHECK(j.is_array());

  LayerData round_tripped;
  round_tripped.from_json(j);
  REQUIRE(round_tripped.size() == layer.size());
  CHECK(round_tripped[1].xy == layer[1].xy);
}

TEST_CASE("LayerData as_json/from_json: object form once named or materialed")
{
  LayerData layer;
  layer.name = "icing";
  layer.material = "chocolate";
  layer.push_back(make_node(0, {0, 0}, {0, 0}, {1, 0}));

  CHECK(layer.has_appearance());
  nlohmann::json j = layer.as_json();
  CHECK(j.is_object());
  CHECK(j["name"] == "icing");
  CHECK(j["material"] == "chocolate");

  LayerData round_tripped;
  round_tripped.from_json(j);
  CHECK(round_tripped.name == "icing");
  CHECK(round_tripped.material == "chocolate");
  REQUIRE(round_tripped.size() == 1);
  CHECK(round_tripped[0].xy == layer[0].xy);
}

TEST_CASE("LayerData::from_json rejects a nodes-less object")
{
  LayerData layer;
  CHECK_THROWS_AS(layer.from_json(nlohmann::json::object({{"name", "x"}})),
                  std::runtime_error);
}

TEST_CASE("ProfileData::scale")
{
  ProfileData profile;
  LayerData layer;
  layer.push_back(make_node(0, {2, 3}, {1, 3}, {3, 3}));
  profile.push_back(layer);

  profile.scale(2.0, 5.0);

  CHECK(profile[0][0].xy.x == doctest::Approx(4.0));
  CHECK(profile[0][0].xy.y == doctest::Approx(15.0));
  CHECK(profile[0][0].control_before_xy.x == doctest::Approx(2.0));
  CHECK(profile[0][0].control_after_xy.x == doctest::Approx(6.0));
}

TEST_CASE("ProfileData::export_string / load_from_string round trip")
{
  ProfileData profile;

  LayerData base;
  base.push_back(make_node(0, {0, 0}, {0, 0}, {1, 0}));
  base.push_back(make_node(0, {10, 2}, {9, 2}, {10, 2}));
  profile.push_back(base);

  LayerData icing;
  icing.name = "icing";
  icing.push_back(make_node(1, {0, 1}, {0, 1}, {1, 1}));
  profile.push_back(icing);

  std::string dumped = profile.export_string();

  ProfileData reloaded;
  reloaded.load_from_string(dumped);

  REQUIRE(reloaded.size() == 2);
  REQUIRE(reloaded[0].size() == 2);
  CHECK(reloaded[0][1].xy == base[1].xy);
  CHECK(reloaded[1].name == "icing");
  REQUIRE(reloaded[1].size() == 1);
  CHECK(reloaded[1][0].node_type == 1);
}

TEST_CASE("ProfileData::load_from_string rejects a profile with no layers")
{
  ProfileData profile;
  CHECK_THROWS_AS(profile.load_from_string("{}"), std::runtime_error);
}

TEST_CASE("LayerData::set_linear_limit / get_flat_list on a straight segment")
{
  LayerData layer;
  layer.push_back(make_node(0, {0, 0}, {0, 0}, {0, 0}));
  layer.push_back(make_node(0, {10, 0}, {10, 0}, {10, 0}));

  layer.set_linear_limit(0.01);
  std::vector<Vec2> flat = layer.get_flat_list();

  REQUIRE(flat.size() >= 2);
  CHECK(flat.front().x == doctest::Approx(0.0));
  CHECK(flat.back().x == doctest::Approx(10.0));
  for (const auto& p : flat)
    CHECK(p.y == doctest::Approx(0.0));
}
