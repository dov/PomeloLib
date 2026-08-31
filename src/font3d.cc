//======================================================================
//  font3d.cc - Packing baked glyph meshes into a single glb.
//----------------------------------------------------------------------

#include "font3d.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

// See the comment in mesh.cc: pull in the 3.9.1 json before tinygltf so
// that its bundled 3.5.0 copy stays out of the way.
#include <nlohmann/json.hpp>
#define TINYGLTF_NO_INCLUDE_JSON
#include "tiny_gltf.h"

using namespace std;
using json = nlohmann::json;

string to_string(GlyphStatus status)
{
  switch (status)
  {
    case GlyphStatus::Ok:     return "ok";
    case GlyphStatus::Empty:  return "empty";
    case GlyphStatus::Failed: return "failed";
  }
  return "unknown";
}

string MaterialSlot::display_name() const
{
  if (!name.empty())
    return name;
  return index == 0 ? "base" : fmt::format("level {}", index);
}

size_t Font3D::num_layers() const
{
  size_t n = 0;
  for (const auto& g : glyphs)
    n = max(n, g.layers.size());
  return n;
}

MaterialSlot Font3D::slot(size_t index) const
{
  if (index < slots.size())
    return slots[index];

  MaterialSlot s;
  s.index = (int)index;
  s.material = default_material_for_slot(index);
  return s;
}

vector<MaterialSlot> Font3D::resolved_slots() const
{
  // A slot table longer than the geometry is kept: a profile may name
  // four levels and a particular subset of glyphs only ever produce
  // three, and dropping the fourth would make two bakes of the same
  // profile disagree about what its levels are called.
  size_t n = max(num_layers(), slots.size());
  vector<MaterialSlot> out;
  out.reserve(n);
  for (size_t i=0; i<n; i++)
  {
    MaterialSlot s = slot(i);
    s.index = (int)i;
    out.push_back(std::move(s));
  }
  return out;
}

bool Glyph3D::has_geometry() const
{
  for (const auto& layer : layers)
    if (layer.vertices.size() >= 3)
      return true;
  return false;
}

void Glyph3D::center_on_bbox()
{
  if (!has_geometry())
  {
    bbox_min = bbox_max = offset = glm::dvec3(0,0,0);
    return;
  }

  glm::dvec3 lo(numeric_limits<double>::max());
  glm::dvec3 hi(numeric_limits<double>::lowest());
  for (const auto& layer : layers)
    for (const auto& v : layer.vertices)
      for (int i=0; i<3; i++)
      {
        lo[i] = min(lo[i], v[i]);
        hi[i] = max(hi[i], v[i]);
      }

  bbox_min = lo;
  bbox_max = hi;
  offset = 0.5*(lo+hi);

  for (auto& layer : layers)
    for (auto& v : layer.vertices)
      v -= offset;
}

//----------------------------------------------------------------------
// json <-> tinygltf::Value
//----------------------------------------------------------------------
static tinygltf::Value json_to_value(const json& j)
{
  if (j.is_object())
  {
    tinygltf::Value::Object o;
    for (auto it = j.begin(); it != j.end(); ++it)
      o[it.key()] = json_to_value(it.value());
    return tinygltf::Value(o);
  }
  if (j.is_array())
  {
    tinygltf::Value::Array a;
    a.reserve(j.size());
    for (const auto& e : j)
      a.push_back(json_to_value(e));
    return tinygltf::Value(a);
  }
  if (j.is_string())
    return tinygltf::Value(j.get<string>());
  if (j.is_boolean())
    return tinygltf::Value(j.get<bool>());
  if (j.is_number_integer())
    return tinygltf::Value(static_cast<int>(j.get<int64_t>()));
  if (j.is_number())
    return tinygltf::Value(j.get<double>());

  // null and anything exotic
  return tinygltf::Value();
}

static json value_to_json(const tinygltf::Value& v)
{
  switch (v.Type())
  {
    case tinygltf::OBJECT_TYPE:
    {
      json j = json::object();
      for (const string& key : v.Keys())
        j[key] = value_to_json(v.Get(key));
      return j;
    }
    case tinygltf::ARRAY_TYPE:
    {
      json j = json::array();
      for (size_t i=0; i<v.ArrayLen(); i++)
        j.push_back(value_to_json(v.Get((int)i)));
      return j;
    }
    case tinygltf::STRING_TYPE: return json(v.Get<string>());
    case tinygltf::BOOL_TYPE:   return json(v.Get<bool>());
    case tinygltf::INT_TYPE:    return json(v.Get<int>());
    case tinygltf::REAL_TYPE:   return json(v.Get<double>());
    default:                    return json();
  }
}

//----------------------------------------------------------------------
// Material <-> glTF
//----------------------------------------------------------------------

// Everything past metallic-roughness lives in a KHR extension, and a
// reader is only allowed to rely on one that the model declares.
static void declare_extension(tinygltf::Model& model, const string& name)
{
  auto& used = model.extensionsUsed;
  if (find(used.begin(), used.end(), name) == used.end())
    used.push_back(name);
}

// The glTF spelling of one level's material. Note the translation of
// field names: pomelo and three.js say "metalness", glTF says
// "metallicFactor", and this is the single place the two meet.
static tinygltf::Material to_gltf_material(const MaterialSlot& slot,
                                           tinygltf::Model& model)
{
  const Material& m = slot.material;

  tinygltf::Material mat;
  mat.name = m.name.empty() ? fmt::format("layer_{}", slot.index)
                            : fmt::format("{}_{}", slot.index, m.name);
  mat.pbrMetallicRoughness.baseColorFactor = {m.base_color.r,
                                              m.base_color.g,
                                              m.base_color.b,
                                              1.0};
  mat.pbrMetallicRoughness.metallicFactor = m.metalness;
  mat.pbrMetallicRoughness.roughnessFactor = m.roughness;

  // A glyph is a closed solid, so its back faces never show - except
  // through itself, when light passes through the body.
  mat.doubleSided = m.is_transmissive();

  if (m.clearcoat > 0)
  {
    declare_extension(model, "KHR_materials_clearcoat");
    json ext;
    ext["clearcoatFactor"] = m.clearcoat;
    ext["clearcoatRoughnessFactor"] = m.clearcoat_roughness;
    mat.extensions["KHR_materials_clearcoat"] = json_to_value(ext);
  }

  if (m.is_transmissive())
  {
    declare_extension(model, "KHR_materials_transmission");
    json tj;
    tj["transmissionFactor"] = m.transmission;
    mat.extensions["KHR_materials_transmission"] = json_to_value(tj);

    declare_extension(model, "KHR_materials_ior");
    json ij;
    ij["ior"] = m.ior;
    mat.extensions["KHR_materials_ior"] = json_to_value(ij);

    // Transmission alone refracts through an infinitely thin shell and
    // picks up no tint. What makes a jelly red is the volume behind the
    // surface, so write one whenever the material describes it.
    if (m.thickness > 0 || m.attenuation_distance > 0)
    {
      declare_extension(model, "KHR_materials_volume");
      json vj;
      vj["thicknessFactor"] = m.thickness;
      if (m.attenuation_distance > 0)
      {
        vj["attenuationDistance"] = m.attenuation_distance;
        vj["attenuationColor"] = json::array({m.attenuation_color.r,
                                              m.attenuation_color.g,
                                              m.attenuation_color.b});
      }
      mat.extensions["KHR_materials_volume"] = json_to_value(vj);
    }
  }

  return mat;
}

// The inverse, for load_glb(). The manifest is the better source and is
// preferred; this only has to cope with a font written before the
// manifest carried a layer table.
static Material from_gltf_material(const tinygltf::Material& mat)
{
  Material m;
  m.name = mat.name;

  const auto& c = mat.pbrMetallicRoughness.baseColorFactor;
  if (c.size() >= 3)
    m.base_color = glm::vec3((float)c[0], (float)c[1], (float)c[2]);
  m.metalness = mat.pbrMetallicRoughness.metallicFactor;
  m.roughness = mat.pbrMetallicRoughness.roughnessFactor;

  auto ext = [&](const char *name) -> json {
    auto it = mat.extensions.find(name);
    return it == mat.extensions.end() ? json() : value_to_json(it->second);
  };

  if (json j = ext("KHR_materials_clearcoat"); j.is_object())
  {
    m.clearcoat = j.value("clearcoatFactor", 0.0);
    m.clearcoat_roughness = j.value("clearcoatRoughnessFactor", 0.0);
  }
  if (json j = ext("KHR_materials_transmission"); j.is_object())
    m.transmission = j.value("transmissionFactor", 0.0);
  if (json j = ext("KHR_materials_ior"); j.is_object())
    m.ior = j.value("ior", 1.5);
  if (json j = ext("KHR_materials_volume"); j.is_object())
  {
    m.thickness = j.value("thicknessFactor", 0.0);
    m.attenuation_distance = j.value("attenuationDistance", 0.0);
    if (j.contains("attenuationColor"))
      m.attenuation_color = parse_color(j["attenuationColor"]);
  }
  return m;
}

static json as_json(const glm::dvec3& v)
{
  return json::array({v.x, v.y, v.z});
}

static glm::dvec3 dvec3_from_json(const json& j, const glm::dvec3& fallback)
{
  if (!j.is_array() || j.size() != 3)
    return fallback;
  return glm::dvec3(j[0].get<double>(), j[1].get<double>(), j[2].get<double>());
}

//----------------------------------------------------------------------
// The manifest
//----------------------------------------------------------------------
json Font3D::manifest() const
{
  json j;

  j["format"] = "pomelo-font3d";
  j["format_version"] = 1;

  json& src = j["source_font"];
  src["filename"] = meta.source_font;
  src["sha256"] = meta.source_sha256;
  src["postscript_name"] = meta.postscript_name;
  src["family"] = meta.family;
  src["style"] = meta.style;
  src["face_index"] = meta.face_index;
  src["units_per_em"] = meta.units_per_em;
  src["num_glyphs"] = meta.num_glyphs_in_face;

  json& scale = j["scale"];
  scale["em_size"] = meta.em_size;
  scale["ascender"] = meta.ascender;
  scale["descender"] = meta.descender;
  scale["line_height"] = meta.line_height;
  scale["axis_convention"] = meta.axis_convention;

  j["bake_params"] = meta.bake_params;

  // The levels of the profile this was baked with, and how each one looks
  // by default. A consumer reads this rather than guessing appearance
  // from the glTF materials, which carry the same colours but say nothing
  // about which library material they came from.
  json layers = json::array();
  for (const auto& s : resolved_slots())
  {
    json lj;
    lj["index"] = s.index;
    lj["name"] = s.display_name();
    lj["material"] = s.material.as_json();
    layers.push_back(lj);
  }
  j["layers"] = layers;

  json& gen = j["generator"];
  gen["name"] = meta.generator;
  gen["commit_id"] = meta.commit_id;
  gen["version"] = meta.version;

  // The glyph table covers every requested glyph, including the ones with
  // no outline and the ones that failed, so a consumer can tell the
  // difference between "not in this font" and "did not bake".
  json glyphs = json::array();
  for (const auto& g : glyphs_sorted_for_manifest())
  {
    json gj;
    gj["glyph_id"] = g->glyph_id;
    gj["name"] = g->name;
    gj["status"] = to_string(g->status);
    gj["advance"] = g->advance;
    if (g->status == GlyphStatus::Ok)
    {
      gj["offset"] = as_json(g->offset);
      gj["bbox_min"] = as_json(g->bbox_min);
      gj["bbox_max"] = as_json(g->bbox_max);
      gj["num_layers"] = g->layers.size();
    }
    if (!g->error.empty())
      gj["error"] = g->error;
    glyphs.push_back(gj);
  }
  j["glyphs"] = glyphs;

  return j;
}

vector<const Glyph3D*> Font3D::glyphs_sorted_for_manifest() const
{
  vector<const Glyph3D*> out;
  out.reserve(glyphs.size());
  for (const auto& g : glyphs)
    out.push_back(&g);
  sort(out.begin(), out.end(),
       [](const Glyph3D* a, const Glyph3D* b) {
         return a->glyph_id < b->glyph_id;
       });
  return out;
}

//----------------------------------------------------------------------
// The glb writer
//----------------------------------------------------------------------

// A welded index buffer for one layer. Vertices are quantized to the
// float32 they will be stored as before being compared, so that welding
// never merges two points that would stay distinct in the file, nor
// leaves duplicates that have collapsed onto each other.
namespace {

struct Vec3f { float x, y, z; };

struct Vec3fHash {
  size_t operator()(const Vec3f& v) const {
    // Hash the bit patterns so that -0.0 and 0.0 do not collide with
    // different keys in the equality test below.
    auto h = [](float f) {
      uint32_t u;
      memcpy(&u, &f, 4);
      return std::hash<uint32_t>()(u);
    };
    return h(v.x) ^ (h(v.y)<<1) ^ (h(v.z)<<2);
  }
};

struct Vec3fEq {
  bool operator()(const Vec3f& a, const Vec3f& b) const {
    return a.x==b.x && a.y==b.y && a.z==b.z;
  }
};

struct IndexedLayer {
  vector<Vec3f> positions;
  vector<uint32_t> indices;
  Vec3f lo {0,0,0}, hi {0,0,0};
};

// Glyph3D already holds y-up glyph space geometry, so storing it is just
// a narrowing to the float32 that glTF accessors hold.
static Vec3f to_gltf_space(const glm::dvec3& v)
{
  return Vec3f { (float)v.x, (float)v.y, (float)v.z };
}

static IndexedLayer index_layer(const Mesh& mesh)
{
  IndexedLayer out;
  const size_t num_triangles = mesh.vertices.size()/3;

  unordered_map<Vec3f, uint32_t, Vec3fHash, Vec3fEq> lookup;
  lookup.reserve(mesh.vertices.size());

  out.indices.reserve(num_triangles*3);

  auto index_of = [&](const glm::dvec3& v) -> uint32_t {
    Vec3f p = to_gltf_space(v);
    auto it = lookup.find(p);
    if (it != lookup.end())
      return it->second;
    uint32_t idx = (uint32_t)out.positions.size();
    lookup.emplace(p, idx);
    out.positions.push_back(p);
    return idx;
  };

  for (size_t t=0; t+2 < mesh.vertices.size(); t += 3)
  {
    uint32_t i0 = index_of(mesh.vertices[t+0]);
    uint32_t i1 = index_of(mesh.vertices[t+1]);
    uint32_t i2 = index_of(mesh.vertices[t+2]);

    // Welding coincident vertices can collapse a triangle onto a line or
    // a point. Those carry no surface, and leaving them in makes the mesh
    // read as non manifold to anything that walks the edges.
    if (i0 == i1 || i1 == i2 || i0 == i2)
      continue;

    out.indices.push_back(i0);
    out.indices.push_back(i1);
    out.indices.push_back(i2);
  }

  if (!out.positions.empty())
  {
    out.lo = out.hi = out.positions[0];
    for (const auto& p : out.positions)
    {
      out.lo.x = min(out.lo.x, p.x); out.hi.x = max(out.hi.x, p.x);
      out.lo.y = min(out.lo.y, p.y); out.hi.y = max(out.hi.y, p.y);
      out.lo.z = min(out.lo.z, p.z); out.hi.z = max(out.hi.z, p.z);
    }
  }

  return out;
}

// Append bytes to the buffer, padded to a four byte boundary as the glTF
// spec requires of accessor aligned buffer views.
static size_t append_to_buffer(vector<unsigned char>& buffer,
                               const void *data,
                               size_t num_bytes)
{
  while (buffer.size() % 4)
    buffer.push_back(0);
  size_t offset = buffer.size();
  const unsigned char *p = (const unsigned char*)data;
  buffer.insert(buffer.end(), p, p+num_bytes);
  return offset;
}

// Fill model with one material per profile layer and one mesh per glyph
// that has geometry, restricted to used when that is non null. Returns the
// glyph id to mesh index mapping.
static map<uint32_t,int> build_glyph_meshes(const Font3D& font,
                                            const set<uint32_t> *used,
                                            tinygltf::Model& model,
                                            tinygltf::Buffer& buffer,
                                            size_t& total_triangles);

} // anonymous namespace

Font3D Font3D::load_glb(const string& filename)
{
  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  string err, warn;

  if (!loader.LoadBinaryFromFile(&model, &err, &warn, filename))
    throw runtime_error(fmt::format("Failed to read {}{}{}",
                                    filename, err.empty() ? "" : ": ", err));
  if (!warn.empty())
    spdlog::warn("{}: {}", filename, warn);

  json manifest = value_to_json(model.asset.extras);
  if (!manifest.contains("format") || manifest["format"] != "pomelo-font3d")
    throw runtime_error(fmt::format(
      "{} is not a pomelo 3d font - asset.extras has no pomelo-font3d manifest",
      filename));

  Font3D font;
  Font3DMeta& m = font.meta;
  const json& src = manifest["source_font"];
  m.source_font = src.value("filename", "");
  m.source_sha256 = src.value("sha256", "");
  m.postscript_name = src.value("postscript_name", "");
  m.family = src.value("family", "");
  m.style = src.value("style", "");
  m.face_index = src.value("face_index", 0);
  m.units_per_em = src.value("units_per_em", 0);
  m.num_glyphs_in_face = src.value("num_glyphs", 0);

  const json& scale = manifest["scale"];
  m.em_size = scale.value("em_size", 0.0);
  m.ascender = scale.value("ascender", 0.0);
  m.descender = scale.value("descender", 0.0);
  m.line_height = scale.value("line_height", 0.0);
  m.axis_convention = scale.value("axis_convention", "");

  m.bake_params = manifest.value("bake_params", json::object());

  // The level table. Fonts baked before it existed have none, so fall
  // back to reconstructing one from the glTF materials, which have
  // always been written one per level in level order.
  if (manifest.contains("layers") && manifest["layers"].is_array())
  {
    for (const json& lj : manifest["layers"])
    {
      MaterialSlot s;
      s.index = lj.value("index", (int)font.slots.size());
      s.name = lj.value("name", "");
      if (lj.contains("material"))
        s.material = Material::from_json(lj["material"]);
      font.slots.push_back(std::move(s));
    }
  }
  else
  {
    for (size_t i=0; i<model.materials.size(); i++)
    {
      MaterialSlot s;
      s.index = (int)i;
      s.material = from_gltf_material(model.materials[i]);
      // The material's glTF name is a label for the level, not a library
      // material this font can claim to be made of.
      s.material.name.clear();
      font.slots.push_back(std::move(s));
    }
  }

  const json& gen = manifest["generator"];
  m.generator = gen.value("name", "");
  m.commit_id = gen.value("commit_id", "");
  m.version = gen.value("version", "");

  // The glyph table in the manifest is authoritative: it covers the empty
  // and failed glyphs too, which have no mesh to hang metadata off.
  map<uint32_t, size_t> index_of_glyph;
  for (const json& gj : manifest["glyphs"])
  {
    Glyph3D g;
    g.glyph_id = gj.value("glyph_id", 0u);
    g.name = gj.value("name", "");
    g.advance = gj.value("advance", 0.0);
    g.error = gj.value("error", "");
    string status = gj.value("status", "empty");
    g.status = status == "ok"     ? GlyphStatus::Ok
             : status == "failed" ? GlyphStatus::Failed
                                  : GlyphStatus::Empty;
    g.offset = dvec3_from_json(gj.value("offset", json()), glm::dvec3(0));
    g.bbox_min = dvec3_from_json(gj.value("bbox_min", json()), glm::dvec3(0));
    g.bbox_max = dvec3_from_json(gj.value("bbox_max", json()), glm::dvec3(0));

    index_of_glyph[g.glyph_id] = font.glyphs.size();
    font.glyphs.push_back(std::move(g));
  }

  // Now attach the geometry. Meshes are matched to glyph records by the
  // glyph_id in their extras rather than by position.
  for (const tinygltf::Mesh& tmesh : model.meshes)
  {
    json mj = value_to_json(tmesh.extras);
    if (!mj.contains("glyph_id"))
      continue;
    auto it = index_of_glyph.find(mj["glyph_id"].get<uint32_t>());
    if (it == index_of_glyph.end())
      continue;
    Glyph3D& g = font.glyphs[it->second];

    for (const tinygltf::Primitive& prim : tmesh.primitives)
    {
      auto pit = prim.attributes.find("POSITION");
      if (pit == prim.attributes.end() || prim.indices < 0)
        continue;

      const tinygltf::Accessor& pa = model.accessors[pit->second];
      const tinygltf::Accessor& ia = model.accessors[prim.indices];
      const tinygltf::BufferView& pv = model.bufferViews[pa.bufferView];
      const tinygltf::BufferView& iv = model.bufferViews[ia.bufferView];
      const unsigned char *pbase =
        model.buffers[pv.buffer].data.data() + pv.byteOffset + pa.byteOffset;
      const unsigned char *ibase =
        model.buffers[iv.buffer].data.data() + iv.byteOffset + ia.byteOffset;

      if (pa.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
          pa.type != TINYGLTF_TYPE_VEC3)
        throw runtime_error("Unexpected POSITION accessor format");

      size_t pstride = pv.byteStride ? pv.byteStride : 3*sizeof(float);

      auto index_at = [&](size_t i) -> uint32_t {
        switch (ia.componentType)
        {
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return ((const uint32_t*)ibase)[i];
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return ((const uint16_t*)ibase)[i];
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return ((const uint8_t*)ibase)[i];
          default:
            throw runtime_error("Unexpected index accessor format");
        }
      };

      Mesh layer;
      layer.vertices.reserve(ia.count);
      for (size_t i=0; i<ia.count; i++)
      {
        const float *p = (const float*)(pbase + pstride*index_at(i));
        layer.vertices.push_back(glm::dvec3(p[0], p[1], p[2]));
      }
      if (prim.material >= 0 &&
          prim.material < (int)model.materials.size())
      {
        const auto& c =
          model.materials[prim.material].pbrMetallicRoughness.baseColorFactor;
        if (c.size() >= 3)
          layer.color = glm::vec3((float)c[0], (float)c[1], (float)c[2]);
      }
      g.layers.push_back(std::move(layer));
    }
  }

  return font;
}

const Glyph3D *Font3D::find(uint32_t glyph_id) const
{
  for (const auto& g : glyphs)
    if (g.glyph_id == glyph_id)
      return &g;
  return nullptr;
}

glm::dvec3 Font3D::mesh_translation(const PlacedGlyph& placed) const
{
  const Glyph3D *g = find(placed.glyph_id);
  return g ? placed.pen + g->offset : placed.pen;
}

namespace {

// Rotation about z by angle radians; z passes through unchanged.
glm::dvec3 rotate_z(double angle, const glm::dvec3& v)
{
  double c = cos(angle), s = sin(angle);
  return { c*v.x - s*v.y, s*v.x + c*v.y, v.z };
}

} // anonymous namespace

Font3D::GlyphPlacement Font3D::mesh_placement(const PlacedGlyph& placed) const
{
  const Glyph3D *g = find(placed.glyph_id);
  glm::dvec3 offset = g ? g->offset : glm::dvec3{0,0,0};
  return { placed.angle, rotate_z(placed.angle, offset) + placed.pen };
}

MultiMesh instantiate(const Font3D& font,
                      const vector<PlacedGlyph>& placements)
{
  size_t max_layers = 0;
  for (const auto& p : placements)
    if (const Glyph3D *g = font.find(p.glyph_id))
      max_layers = max(max_layers, g->layers.size());

  MultiMesh out;
  out.resize(max_layers);

  for (const auto& p : placements)
  {
    const Glyph3D *g = font.find(p.glyph_id);
    if (!g || !g->has_geometry())
      continue;
    Font3D::GlyphPlacement placement = font.mesh_placement(p);
    for (size_t i=0; i<g->layers.size(); i++)
    {
      out[i].color = g->layers[i].color;
      const auto& src = g->layers[i].vertices;
      auto& dst = out[i].vertices;
      dst.reserve(dst.size()+src.size());
      for (const auto& v : src)
        dst.push_back(rotate_z(placement.angle, v) + placement.translation);
    }
  }
  return out;
}

namespace {

map<uint32_t,int> build_glyph_meshes(const Font3D& font,
                                     const set<uint32_t> *used,
                                     tinygltf::Model& model,
                                     tinygltf::Buffer& buffer,
                                     size_t& total_triangles)
{
  map<uint32_t,int> mesh_of_glyph;

  // One glTF material per profile level, shared by every glyph, and the
  // primitive's material index is what says which level it belongs to.
  // The materials are real rather than a preview convenience: opened in
  // blender or a web viewer, an unmodified font already looks like the
  // profile said it should.
  for (const auto& slot : font.resolved_slots())
    model.materials.push_back(to_gltf_material(slot, model));

  // Build one glTF mesh per glyph that has geometry.
  total_triangles = 0;
  for (const Glyph3D *gp : font.glyphs_sorted_for_manifest())
  {
    const Glyph3D& g = *gp;
    if (!g.has_geometry())
      continue;
    if (used && !used->count(g.glyph_id))
      continue;

    tinygltf::Mesh tmesh;
    tmesh.name = g.name.empty() ? fmt::format("glyph_{:05d}", g.glyph_id)
                                : fmt::format("glyph_{:05d}_{}", g.glyph_id, g.name);

    for (size_t layer_idx=0; layer_idx<g.layers.size(); layer_idx++)
    {
      const Mesh& layer = g.layers[layer_idx];
      if (layer.vertices.size() < 3)
        continue;

      IndexedLayer il = index_layer(layer);
      total_triangles += il.indices.size()/3;

      size_t index_offset = append_to_buffer(buffer.data,
                                             il.indices.data(),
                                             il.indices.size()*sizeof(uint32_t));
      tinygltf::BufferView index_view;
      index_view.buffer = 0;
      index_view.byteOffset = index_offset;
      index_view.byteLength = il.indices.size()*sizeof(uint32_t);
      index_view.target = TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER;
      int index_view_idx = (int)model.bufferViews.size();
      model.bufferViews.push_back(index_view);

      size_t pos_offset = append_to_buffer(buffer.data,
                                           il.positions.data(),
                                           il.positions.size()*sizeof(Vec3f));
      tinygltf::BufferView pos_view;
      pos_view.buffer = 0;
      pos_view.byteOffset = pos_offset;
      pos_view.byteLength = il.positions.size()*sizeof(Vec3f);
      pos_view.target = TINYGLTF_TARGET_ARRAY_BUFFER;
      int pos_view_idx = (int)model.bufferViews.size();
      model.bufferViews.push_back(pos_view);

      tinygltf::Accessor index_accessor;
      index_accessor.bufferView = index_view_idx;
      index_accessor.byteOffset = 0;
      index_accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
      index_accessor.count = il.indices.size();
      index_accessor.type = TINYGLTF_TYPE_SCALAR;
      index_accessor.minValues = {0};
      index_accessor.maxValues = {(double)(il.positions.size()-1)};
      int index_accessor_idx = (int)model.accessors.size();
      model.accessors.push_back(index_accessor);

      tinygltf::Accessor pos_accessor;
      pos_accessor.bufferView = pos_view_idx;
      pos_accessor.byteOffset = 0;
      pos_accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
      pos_accessor.count = il.positions.size();
      pos_accessor.type = TINYGLTF_TYPE_VEC3;
      pos_accessor.minValues = {il.lo.x, il.lo.y, il.lo.z};
      pos_accessor.maxValues = {il.hi.x, il.hi.y, il.hi.z};
      int pos_accessor_idx = (int)model.accessors.size();
      model.accessors.push_back(pos_accessor);

      tinygltf::Primitive primitive;
      primitive.indices = index_accessor_idx;
      primitive.attributes["POSITION"] = pos_accessor_idx;
      primitive.material = (int)layer_idx;
      primitive.mode = TINYGLTF_MODE_TRIANGLES;
      tmesh.primitives.push_back(primitive);
    }

    if (tmesh.primitives.empty())
      continue;

    // Per glyph metadata, duplicated from the manifest so that a consumer
    // walking the meshes does not have to cross reference asset.extras.
    json gj;
    gj["glyph_id"] = g.glyph_id;
    gj["name"] = g.name;
    gj["advance"] = g.advance;
    gj["offset"] = as_json(g.offset);
    gj["bbox_min"] = as_json(g.bbox_min);
    gj["bbox_max"] = as_json(g.bbox_max);
    tmesh.extras = json_to_value(gj);

    mesh_of_glyph[g.glyph_id] = (int)model.meshes.size();
    model.meshes.push_back(tmesh);
  }

  return mesh_of_glyph;
}

// Lay the whole thing down with a -90 degree turn about x. The glyphs
// stand upright in the glTF xy plane, which a z-up viewer such as blender
// shows standing in its xz plane; this puts it flat in the viewer's xy
// plane instead. Quaternion is [x,y,z,w].
static tinygltf::Node make_zup_root(const string& name, vector<int> children)
{
  tinygltf::Node root;
  root.name = name;
  root.children = std::move(children);
  root.rotation = { -M_SQRT1_2, 0.0, 0.0, M_SQRT1_2 };
  return root;
}

static void write_glb(tinygltf::Model& model,
                      tinygltf::Buffer& buffer,
                      const Font3D& font,
                      const string& generator,
                      const string& filename)
{
  model.asset.version = "2.0";
  model.asset.generator = fmt::format("{} {}", generator, font.meta.version);
  model.asset.extras = json_to_value(font.manifest());
  model.buffers.push_back(buffer);

  tinygltf::TinyGLTF writer;
  bool ok = writer.WriteGltfSceneToFile(&model,
                                        filename,
                                        true,   // embedImages
                                        true,   // embedBuffers
                                        false,  // prettyPrint (irrelevant for glb)
                                        true);  // writeBinary
  if (!ok)
    throw runtime_error(fmt::format("Failed to write {}", filename));
}

} // anonymous namespace

void Font3D::save_glb(const string& filename) const
{
  tinygltf::Model model;
  tinygltf::Buffer buffer;
  size_t total_triangles = 0;

  auto mesh_of_glyph = build_glyph_meshes(*this, nullptr, model, buffer,
                                          total_triangles);

  // The specimen sheet. Purely a preview: the glyph meshes themselves are
  // centred on the origin and the grid lives only in the node transforms.
  vector<const Glyph3D*> shown;
  for (const Glyph3D *g : glyphs_sorted_for_manifest())
    if (mesh_of_glyph.count(g->glyph_id))
      shown.push_back(g);

  const int columns = max(1, (int)ceil(sqrt((double)shown.size())));
  double cell_w = 0, cell_h = 0;
  for (const Glyph3D *g : shown)
  {
    cell_w = max(cell_w, g->bbox_max.x - g->bbox_min.x);
    cell_h = max(cell_h, g->bbox_max.y - g->bbox_min.y);
  }
  cell_w *= 1.25;
  cell_h *= 1.25;

  vector<int> children;
  for (size_t i=0; i<shown.size(); i++)
  {
    int mesh_index = mesh_of_glyph[shown[i]->glyph_id];

    tinygltf::Node node;
    node.mesh = mesh_index;
    node.name = model.meshes[mesh_index].name;
    node.translation = { ((int)i % columns)*cell_w,
                         -((int)i / columns)*cell_h,
                         0.0 };

    children.push_back((int)model.nodes.size());
    model.nodes.push_back(node);
  }

  tinygltf::Scene scene;
  scene.name = "specimen";
  scene.nodes.push_back((int)model.nodes.size());
  model.nodes.push_back(make_zup_root("specimen", children));
  model.scenes.push_back(scene);
  model.defaultScene = 0;

  write_glb(model, buffer, *this, "pomelo-build-3d-font", filename);

  spdlog::info("Wrote {} - {} glyph meshes, {} triangles, {:.1f} kB buffer",
               filename, shown.size(), total_triangles,
               buffer.data.size()/1024.0);
}

void Font3D::save_layout_glb(const vector<PlacedGlyph>& placements,
                             const string& filename) const
{
  set<uint32_t> used;
  for (const auto& p : placements)
    used.insert(p.glyph_id);

  tinygltf::Model model;
  tinygltf::Buffer buffer;
  size_t total_triangles = 0;

  auto mesh_of_glyph = build_glyph_meshes(*this, &used, model, buffer,
                                          total_triangles);

  vector<int> children;
  for (const auto& p : placements)
  {
    auto it = mesh_of_glyph.find(p.glyph_id);
    if (it == mesh_of_glyph.end())
      continue;   // no outline, e.g. a space

    GlyphPlacement placement = mesh_placement(p);

    tinygltf::Node node;
    node.mesh = it->second;
    node.name = model.meshes[it->second].name;
    node.translation = { placement.translation.x, placement.translation.y,
                         placement.translation.z };
    if (placement.angle != 0)
      // [x,y,z,w], rotation about z - see make_zup_root() above.
      node.rotation = { 0.0, 0.0, sin(placement.angle/2), cos(placement.angle/2) };

    // Keep the tie back to the source string so that a host application
    // can map a node to the character the user typed.
    nlohmann::json nj;
    nj["glyph_id"] = p.glyph_id;
    nj["cluster"] = p.cluster;
    nj["line"] = p.line;
    node.extras = json_to_value(nj);

    children.push_back((int)model.nodes.size());
    model.nodes.push_back(node);
  }

  tinygltf::Scene scene;
  scene.name = "text";
  scene.nodes.push_back((int)model.nodes.size());
  model.nodes.push_back(make_zup_root("text", children));
  model.scenes.push_back(scene);
  model.defaultScene = 0;

  write_glb(model, buffer, *this, "pomelo-layout-3d-text", filename);

  spdlog::info("Wrote {} - {} placements over {} instanced meshes, "
               "{} triangles, {:.1f} kB buffer",
               filename, children.size(), mesh_of_glyph.size(),
               total_triangles, buffer.data.size()/1024.0);
}
