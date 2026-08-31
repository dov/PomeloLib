//======================================================================
//  font3d.h - A "3d font": one baked mesh per glyph of a ttf/otf face.
//
//  A 3d font is distributed as a single binary glb. Each glyph becomes one
//  glTF mesh, with one primitive per pomelo profile layer. The glyph
//  metadata and the full bake manifest live in asset.extras, so the glb is
//  self describing and there is no sidecar file.
//
//  Vertices are stored centred on the glyph bounding box so that the
//  float32 encoding is symmetric around zero. Glyph3D::offset carries the
//  shift needed to put the glyph back into glyph space, whose origin is
//  the pen position on the baseline.
//----------------------------------------------------------------------
#ifndef FONT3D_H
#define FONT3D_H

#include <string>
#include <vector>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include "mesh.h"
#include "material.h"

// How a glyph came out of the bake.
enum class GlyphStatus {
  Ok,       // has geometry
  Empty,    // no outline, e.g. the space character
  Failed    // the algorithm threw; see Glyph3D::error
};

std::string to_string(GlyphStatus status);

class Glyph3D {
  public:
  uint32_t glyph_id = 0;
  std::string name;                 // from the post table, may be empty
  GlyphStatus status = GlyphStatus::Empty;
  std::string error;                // set when status == Failed

  double advance = 0;               // horizontal advance in mesh units

  // The bounding box in glyph space, and the box centre that has been
  // subtracted from every stored vertex.
  glm::dvec3 bbox_min {0,0,0};
  glm::dvec3 bbox_max {0,0,0};
  glm::dvec3 offset {0,0,0};

  // One Mesh per profile layer, already centred by offset.
  MultiMesh layers;

  bool has_geometry() const;

  // Subtract the bounding box centre from every vertex and record it in
  // offset. Also fills in bbox_min/bbox_max. A no-op without geometry.
  void center_on_bbox();
};

// Everything needed to know how the font was baked and what it came from.
class Font3DMeta {
  public:
  std::string source_font;          // basename of the ttf/otf
  std::string source_sha256;        // hash of the exact file that was baked
  std::string postscript_name;
  std::string family;
  std::string style;
  int face_index = 0;
  int units_per_em = 0;
  int num_glyphs_in_face = 0;

  // The scale of the bake: how many mesh units one em spans. All lengths
  // in the glyph records and in the bevel parameters are in mesh units.
  double em_size = 0;

  double ascender = 0;              // mesh units
  double descender = 0;             // mesh units, negative
  double line_height = 0;           // mesh units

  // Right handed, x to the right, y up, z out of the glyph face. Note
  // that this is a flip of cairo's y-down outline space.
  std::string axis_convention = "x-right,y-up,z-out";

  std::string generator;
  std::string commit_id;
  std::string version;

  // The BevelParams the bake was run with, as json.
  nlohmann::json bake_params;
};

// A glyph placed in layout space. pen is where the glyph's own origin -
// the pen position on its baseline - lands. The stored meshes are centred
// on their bounding box, so the translation to apply to a glyph mesh is
// pen + Glyph3D::offset; use mesh_translation() rather than open coding it.
class PlacedGlyph {
  public:
  uint32_t glyph_id = 0;
  glm::dvec3 pen {0,0,0};
  double angle = 0;             // radians, ccw about +z; 0 is flat text
  int cluster = 0;              // byte offset into the source utf-8
  int line = 0;
};

// One profile level, and how it looks by default.
//
// The slot index is the position in Glyph3D::layers, and it is the only
// thing tying geometry to appearance: primitive n of a glyph is level n.
// The name is what a person calls the level and what a saved colour scheme
// keys off, so it survives a re-bake that changes the level count in a way
// a bare index would not.
class MaterialSlot {
  public:
  int index = 0;
  std::string name;         // "base", "icing", ... may be empty
  Material material;

  // The name to show when the profile did not give one.
  std::string display_name() const;
};

class Font3D {
  public:
  Font3DMeta meta;

  // One entry per profile level, in layer order. A round bevel bake has
  // exactly one. Never shorter than the widest glyph's layer count once
  // the font has been through save_glb()/load_glb().
  std::vector<MaterialSlot> slots;

  std::vector<Glyph3D> glyphs;

  // slots[i], filling in a default when the font predates the slot table
  // or was built without one.
  MaterialSlot slot(size_t index) const;

  // The number of levels any glyph has, which is what the slot table has
  // to cover.
  size_t num_layers() const;

  // The slot table with a default invented for every level that has no
  // entry, so it always has num_layers() entries. This is what gets
  // written, so a font always describes its own levels even when it was
  // baked with no material library in sight.
  std::vector<MaterialSlot> resolved_slots() const;


  // Null when the font has no such glyph.
  const Glyph3D *find(uint32_t glyph_id) const;

  // Read back a font written by save_glb().
  static Font3D load_glb(const std::string& filename);

  // Where a placed glyph's stored mesh has to be translated to. Only
  // correct when placed.angle is 0; use mesh_placement() otherwise.
  glm::dvec3 mesh_translation(const PlacedGlyph& placed) const;

  // The rigid transform - rotate by placed.angle about z, then translate -
  // that places a glyph's stored (centred) mesh in layout space. A stored
  // vertex v is placed at rotate_z(angle, v) + translation.
  struct GlyphPlacement { double angle = 0; glm::dvec3 translation {0,0,0}; };
  GlyphPlacement mesh_placement(const PlacedGlyph& placed) const;

  // Write the font as a single binary glb. The default scene lays the
  // glyphs out in a grid so that the file previews as a specimen sheet;
  // that layout lives purely in the node transforms and consumers should
  // ignore it and use the mesh extras instead.
  void save_glb(const std::string& filename) const;

  // Write a laid out string. Each distinct glyph contributes one mesh and
  // every placement becomes a node referencing it, so repeated letters
  // cost one transform rather than a second copy of the geometry.
  void save_layout_glb(const std::vector<PlacedGlyph>& placements,
                       const std::string& filename) const;

  // The manifest that gets embedded in asset.extras.
  nlohmann::json manifest() const;

  // The glyphs in ascending glyph id order, which is the order both the
  // manifest and the glb meshes are written in.
  std::vector<const Glyph3D*> glyphs_sorted_for_manifest() const;
};

// Bake the placements down into concrete geometry, one Mesh per profile
// layer with every placement's triangles merged in. This is what an STL
// export needs; a glTF export should use save_layout_glb() instead so that
// repeated glyphs stay instanced.
MultiMesh instantiate(const Font3D& font,
                      const std::vector<PlacedGlyph>& placements);

#endif /* FONT3D */
