//======================================================================
//  material.h - The appearance of one profile level.
//
//  pomelo does not invent a material model. This is glTF's metallic
//  roughness plus the three KHR extensions that a jelly or a lacquer
//  needs, which is also exactly what three.js MeshPhysicalMaterial takes,
//  so a material survives the trip profile -> glb -> browser -> exported
//  glb without a lossy translation at either end.
//
//  The json field names are the three.js spelling ("metalness",
//  "baseColor") rather than the glTF one ("metallicFactor",
//  "baseColorFactor"). The web app is the consumer that a person actually
//  authors against, and the mapping to glTF happens in one place, in
//  font3d.cc's glb writer.
//
//  A MaterialLibrary is a flat json object of named materials, shared by
//  the profiles that reference them and by the web app's preset list, so
//  that "chocolate" means one thing across the project.
//----------------------------------------------------------------------
#ifndef MATERIAL_H
#define MATERIAL_H

#include <map>
#include <string>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>

class Material {
  public:
  // The library key this came from, empty when it was authored inline.
  // Carried into the glb so that the ui can show a level as "chocolate"
  // rather than as a set of numbers it has to reverse engineer.
  std::string name;

  // Linear rgb, matching glTF's baseColorFactor. Parsed from "#rrggbb"
  // or from a [r,g,b] triple.
  glm::vec3 base_color {0.8f, 0.8f, 0.8f};

  double metalness = 0;
  double roughness = 0.5;

  // KHR_materials_clearcoat. A lacquer over the base colour.
  double clearcoat = 0;
  double clearcoat_roughness = 0.1;

  // KHR_materials_transmission and KHR_materials_ior. Light travelling
  // through the body rather than bouncing off it, which is what separates
  // a jelly from an opaque red plastic.
  double transmission = 0;
  double ior = 1.5;

  // KHR_materials_volume. Only meaningful when transmission > 0: a
  // transmissive surface with no volume behind it takes almost nothing
  // from base_color, and the tint comes from how far light travels
  // inside instead.
  double thickness = 0;
  glm::vec3 attenuation_color {1.0f, 1.0f, 1.0f};
  // 0 means "no attenuation", standing in for glTF's +infinity, which
  // json cannot represent.
  double attenuation_distance = 0;

  bool is_transmissive() const { return transmission > 0; }

  nlohmann::json as_json() const;

  // Overlay whatever j sets on top of this material, leaving the rest
  // alone. Unknown keys are ignored, so a library may carry ui-only
  // annotations without upsetting the bake.
  void merge_json(const nlohmann::json& j);

  static Material from_json(const nlohmann::json& j);
};

// Parse "#rgb", "#rrggbb", "rgb(r,g,b)" or [r,g,b] with components in
// 0..1. Throws std::runtime_error on anything else.
glm::vec3 parse_color(const nlohmann::json& j);
std::string color_to_hex(const glm::vec3& c);

class MaterialLibrary {
  public:
  // Load a flat json object of name -> material. Throws on a missing or
  // malformed file.
  static MaterialLibrary load(const std::string& filename);

  // The library that ships with the app, or an empty one when it cannot
  // be found. Looked for beside `near` first - a profile's materials.json
  // sits next to the profile - then in the installed data directory.
  static MaterialLibrary load_default(const std::string& near = "");

  bool has(const std::string& name) const;

  // Throws std::runtime_error naming the available materials when there
  // is no such entry, which is the error a typo in a profile deserves.
  Material get(const std::string& name) const;

  // Resolve a profile layer's "material" value: a string names a library
  // entry, an object is an inline material, and an object with a "base"
  // key is a library entry with overrides on top. A null or absent value
  // gives back `fallback` unchanged.
  Material resolve(const nlohmann::json& ref, const Material& fallback) const;

  std::vector<std::string> names() const;
  bool empty() const { return m_materials.empty(); }
  const std::string& source() const { return m_source; }

  private:
  std::map<std::string, Material> m_materials;
  std::string m_source;
};

// A distinguishable colour per level for a profile that names no
// materials at all, so that a bake without a library still previews as
// something other than a sheet of grey.
Material default_material_for_slot(size_t slot_index);

#endif /* MATERIAL */
