//======================================================================
//  material.cc - See material.h.
//----------------------------------------------------------------------

#include "material.h"
#include "utils.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <fmt/core.h>
#include <fmt/ranges.h>

using json = nlohmann::json;
using namespace std;

//----------------------------------------------------------------------
// Colours
//----------------------------------------------------------------------

static float clamp01(double v)
{
  return (float)(v < 0 ? 0 : v > 1 ? 1 : v);
}

static glm::vec3 parse_hex(const string& s)
{
  // "#rgb" and "#rrggbb". The short form repeats each nibble, so #f00 and
  // #ff0000 are the same colour.
  string hex = s.substr(1);
  if (hex.size() == 3)
    hex = {hex[0],hex[0], hex[1],hex[1], hex[2],hex[2]};
  if (hex.size() != 6)
    throw runtime_error(fmt::format("'{}' is not a #rgb or #rrggbb colour", s));

  unsigned int v = 0;
  for (char c : hex)
  {
    int d;
    if (c >= '0' && c <= '9')      d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else throw runtime_error(fmt::format("'{}' is not a hex colour", s));
    v = v*16 + d;
  }
  return glm::vec3(((v >> 16) & 0xff) / 255.0f,
                   ((v >>  8) & 0xff) / 255.0f,
                   ( v        & 0xff) / 255.0f);
}

glm::vec3 parse_color(const json& j)
{
  if (j.is_array())
  {
    if (j.size() < 3)
      throw runtime_error("a colour array needs three components");
    return glm::vec3(clamp01(j[0].get<double>()),
                     clamp01(j[1].get<double>()),
                     clamp01(j[2].get<double>()));
  }
  if (!j.is_string())
    throw runtime_error("a colour must be a string or a [r,g,b] array");

  string s = j.get<string>();
  if (!s.empty() && s[0] == '#')
    return parse_hex(s);

  // "rgb(r,g,b)" with 0..255 components, which is the spelling the gtk
  // colour buttons - and so the .po3d files - already use.
  if (s.rfind("rgb(", 0) == 0 && s.back() == ')')
  {
    int r=0, g=0, b=0;
    if (sscanf(s.c_str(), "rgb(%d,%d,%d)", &r, &g, &b) == 3)
      return glm::vec3(clamp01(r/255.0), clamp01(g/255.0), clamp01(b/255.0));
  }
  throw runtime_error(fmt::format("'{}' is not a colour", s));
}

string color_to_hex(const glm::vec3& c)
{
  auto byte = [](float v) {
    int i = (int)(v*255.0f + 0.5f);
    return i < 0 ? 0 : i > 255 ? 255 : i;
  };
  return fmt::format("#{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b));
}

//----------------------------------------------------------------------
// Material
//----------------------------------------------------------------------

json Material::as_json() const
{
  json j;
  if (!name.empty())
    j["name"] = name;
  j["baseColor"] = color_to_hex(base_color);
  j["metalness"] = metalness;
  j["roughness"] = roughness;
  // Only spell out the extension driven properties when they do
  // something, so that a plain matte material reads as four fields
  // rather than as ten, eight of which are zero.
  if (clearcoat > 0)
  {
    j["clearcoat"] = clearcoat;
    j["clearcoatRoughness"] = clearcoat_roughness;
  }
  if (transmission > 0)
  {
    j["transmission"] = transmission;
    j["ior"] = ior;
    if (thickness > 0)
      j["thickness"] = thickness;
    if (attenuation_distance > 0)
    {
      j["attenuationColor"] = color_to_hex(attenuation_color);
      j["attenuationDistance"] = attenuation_distance;
    }
  }
  return j;
}

void Material::merge_json(const json& j)
{
  if (!j.is_object())
    throw runtime_error("a material must be a json object");

  auto number = [&](const char *key, double& into) {
    if (auto it = j.find(key); it != j.end() && !it->is_null())
      into = it->get<double>();
  };

  if (auto it = j.find("name"); it != j.end() && it->is_string())
    name = it->get<string>();
  if (auto it = j.find("baseColor"); it != j.end() && !it->is_null())
    base_color = parse_color(*it);
  if (auto it = j.find("attenuationColor"); it != j.end() && !it->is_null())
    attenuation_color = parse_color(*it);

  number("metalness", metalness);
  number("roughness", roughness);
  number("clearcoat", clearcoat);
  number("clearcoatRoughness", clearcoat_roughness);
  number("transmission", transmission);
  number("ior", ior);
  number("thickness", thickness);
  number("attenuationDistance", attenuation_distance);
}

Material Material::from_json(const json& j)
{
  Material m;
  m.merge_json(j);
  return m;
}

//----------------------------------------------------------------------
// MaterialLibrary
//----------------------------------------------------------------------

MaterialLibrary MaterialLibrary::load(const string& filename)
{
  json j;
  try {
    j = json::parse(load_string_from_file(filename));
  }
  catch (const json::exception& exc) {
    throw runtime_error(fmt::format("{} is not valid json: {}",
                                    filename, exc.what()));
  }

  // Both a bare object of materials and one wrapped in a "materials" key
  // are accepted, so the file has somewhere to grow a version field.
  const json *entries = &j;
  if (j.is_object() && j.contains("materials"))
    entries = &j["materials"];
  if (!entries->is_object())
    throw runtime_error(fmt::format(
      "{} should hold a json object of name -> material", filename));

  MaterialLibrary lib;
  lib.m_source = filename;
  for (const auto& [name, spec] : entries->items())
  {
    try {
      Material m = Material::from_json(spec);
      // The key wins over any "name" inside the entry: the key is how a
      // profile refers to it, so they must not be able to disagree.
      m.name = name;
      lib.m_materials[name] = m;
    }
    catch (const std::exception& exc) {
      throw runtime_error(fmt::format("{}: material '{}': {}",
                                      filename, name, exc.what()));
    }
  }
  return lib;
}

MaterialLibrary MaterialLibrary::load_default(const string& near)
{
  vector<string> candidates;
  if (const char *env = getenv("POMELO_MATERIAL_LIBRARY"))
    candidates.push_back(env);
  if (!near.empty())
  {
    string dir = near;
    if (auto slash = dir.find_last_of("/\\"); slash != string::npos)
      dir = dir.substr(0, slash);
    else
      dir = ".";
    candidates.push_back(dir + "/materials.json");
  }

  for (const auto& path : candidates)
  {
    if (FILE *fp = fopen(path.c_str(), "rb"))
    {
      fclose(fp);
      return load(path);
    }
  }
  return MaterialLibrary();
}

bool MaterialLibrary::has(const string& name) const
{
  return m_materials.count(name) > 0;
}

Material MaterialLibrary::get(const string& name) const
{
  auto it = m_materials.find(name);
  if (it == m_materials.end())
    throw runtime_error(fmt::format(
      "no material named '{}'{}. Known materials: {}",
      name,
      m_source.empty() ? " (no material library was loaded)"
                       : fmt::format(" in {}", m_source),
      m_materials.empty() ? "none" : fmt::format("{}", fmt::join(names(), ", "))));
  return it->second;
}

Material MaterialLibrary::resolve(const json& ref, const Material& fallback) const
{
  if (ref.is_null())
    return fallback;
  if (ref.is_string())
    return get(ref.get<string>());
  if (!ref.is_object())
    throw runtime_error(
      "a layer's material must be a library name or an inline object");

  // { "base": "chocolate", "roughness": 0.6 } - a library entry with
  // tweaks, which is how a profile says "the usual chocolate, but wetter"
  // without copying the whole material.
  Material m;
  if (auto it = ref.find("base"); it != ref.end() && it->is_string())
    m = get(it->get<string>());
  m.merge_json(ref);
  return m;
}

vector<string> MaterialLibrary::names() const
{
  vector<string> out;
  out.reserve(m_materials.size());
  for (const auto& [name, _] : m_materials)
    out.push_back(name);
  return out;
}

//----------------------------------------------------------------------

Material default_material_for_slot(size_t slot_index)
{
  // Distinguishable rather than pretty. A profile that names its
  // materials never gets here.
  static const glm::vec3 palette[] = {
    glm::vec3(0.80f, 0.16f, 0.12f),
    glm::vec3(0.22f, 0.45f, 0.78f),
    glm::vec3(0.92f, 0.72f, 0.20f),
    glm::vec3(0.30f, 0.62f, 0.36f),
  };
  const size_t n = sizeof(palette)/sizeof(palette[0]);

  Material m;
  m.base_color = palette[slot_index % n];
  m.roughness = 0.5;
  m.metalness = 0;
  return m;
}
