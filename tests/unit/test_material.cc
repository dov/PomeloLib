#include <doctest/doctest.h>
#include "material.h"

using json = nlohmann::json;

TEST_CASE("parse_color: #rgb and #rrggbb agree")
{
  glm::vec3 short_form = parse_color(json("#f80"));
  glm::vec3 long_form = parse_color(json("#ff8800"));

  CHECK(short_form.r == doctest::Approx(long_form.r));
  CHECK(short_form.g == doctest::Approx(long_form.g));
  CHECK(short_form.b == doctest::Approx(long_form.b));
  CHECK(long_form.r == doctest::Approx(1.0));
  CHECK(long_form.g == doctest::Approx(0x88 / 255.0));
  CHECK(long_form.b == doctest::Approx(0.0));
}

TEST_CASE("parse_color: rgb(...) and [r,g,b] forms")
{
  glm::vec3 from_rgb = parse_color(json("rgb(255,136,0)"));
  glm::vec3 from_array = parse_color(json::array({1.0, 0x88 / 255.0, 0.0}));

  CHECK(from_rgb.r == doctest::Approx(from_array.r));
  CHECK(from_rgb.g == doctest::Approx(from_array.g).epsilon(0.01));
  CHECK(from_rgb.b == doctest::Approx(from_array.b));
}

TEST_CASE("parse_color: rejects garbage")
{
  CHECK_THROWS_AS(parse_color(json("not a colour")), std::runtime_error);
  CHECK_THROWS_AS(parse_color(json(42)), std::runtime_error);
  CHECK_THROWS_AS(parse_color(json::array({1.0, 2.0})), std::runtime_error);
}

TEST_CASE("color_to_hex round trips through parse_color")
{
  glm::vec3 c(1.0f, 0x88 / 255.0f, 0.0f);
  std::string hex = color_to_hex(c);
  CHECK(hex == "#ff8800");
  CHECK(parse_color(json(hex)).r == doctest::Approx(c.r));
}

TEST_CASE("Material::as_json omits zero-valued extensions")
{
  Material m;
  json j = m.as_json();

  CHECK(j.contains("baseColor"));
  CHECK(j.contains("metalness"));
  CHECK(j.contains("roughness"));
  CHECK_FALSE(j.contains("clearcoat"));
  CHECK_FALSE(j.contains("transmission"));
}

TEST_CASE("Material::as_json includes transmission fields once transmissive")
{
  Material m;
  m.transmission = 0.4;
  m.ior = 1.4;
  m.thickness = 2.0;
  m.attenuation_distance = 5.0;

  CHECK(m.is_transmissive());

  json j = m.as_json();
  CHECK(j["transmission"].get<double>() == doctest::Approx(0.4));
  CHECK(j["ior"].get<double>() == doctest::Approx(1.4));
  CHECK(j["thickness"].get<double>() == doctest::Approx(2.0));
  CHECK(j.contains("attenuationColor"));
  CHECK(j["attenuationDistance"].get<double>() == doctest::Approx(5.0));
}

TEST_CASE("Material::from_json / merge_json round trip")
{
  json spec = {
    {"name", "chocolate"},
    {"baseColor", "#3b2415"},
    {"metalness", 0.1},
    {"roughness", 0.6},
  };

  Material m = Material::from_json(spec);
  CHECK(m.name == "chocolate");
  CHECK(m.metalness == doctest::Approx(0.1));
  CHECK(m.roughness == doctest::Approx(0.6));

  // merge_json only overlays what it is given, leaving the rest alone.
  m.merge_json(json{{"roughness", 0.9}});
  CHECK(m.name == "chocolate");
  CHECK(m.metalness == doctest::Approx(0.1));
  CHECK(m.roughness == doctest::Approx(0.9));
}

TEST_CASE("default_material_for_slot: distinguishable and wraps around")
{
  Material a = default_material_for_slot(0);
  Material b = default_material_for_slot(1);
  CHECK(a.base_color != b.base_color);

  Material wrapped = default_material_for_slot(0 + 4);
  CHECK(wrapped.base_color == a.base_color);
}
