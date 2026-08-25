//======================================================================
//  font3d-bake.h - Bake the glyphs of a ttf/otf face into a 3d font.
//
//  The baker needs no text shaper: it walks glyph ids and converts glyph
//  outlines. Only a layout engine, which turns a string into a sequence of
//  glyph ids, needs harfbuzz. The one exception is subset selection, where
//  harfbuzz is used to take the GSUB closure of a character set so that
//  reachable ligature glyphs (which carry no cmap entry) come along.
//----------------------------------------------------------------------
#ifndef FONT3D_BAKE_H
#define FONT3D_BAKE_H

#include <functional>
#include <string>
#include <vector>
#include "font3d.h"
#include "pomelo-engine.h"

struct Font3DBakeParams {
  std::string font_filename;
  int face_index = 0;

  // How many mesh units one em spans. Every other length below, and every
  // length in the resulting font, is in these units.
  //
  // The algorithm is not scale invariant: CGAL's straight skeleton runs on
  // an inexact construction kernel and the mesh is welded with an absolute
  // tolerance, so very small coordinates lose detail and eventually throw.
  // Measured on DejaVu Sans Bold, em_size 1 keeps only ~54% of the
  // triangles and fails two glyphs outright, em_size 5 keeps ~82%, and
  // from em_size 10 upwards the output is stable.
  //
  // The upper end is bounded by viewers rather than by the algorithm: a
  // glTF unit is a metre, so an em of 100 makes a full specimen sheet
  // roughly a kilometre across and it falls outside the default far clip
  // plane in blender. 10 sits comfortably between the two limits.
  double em_size = 10;

  // The bevel. Note that bevel.resolution is derived from trace_em_pixels
  // and does not need to be set by the caller.
  BevelParams bevel;

  // A bezier profile is authored in the gui against text that pango lays
  // out at the gui's default font size of 48, which reaches cairo at
  // 48 * 96/72 * 2 = 128 user units per em. Profiles therefore carry
  // absolute coordinates on that scale, and have to be normalised to this
  // bake's em or they come out wildly too wide and too deep. These two
  // multiply that normalisation, so 1.0 reproduces the gui and 0.5 gives
  // half the bevel width or depth.
  double profile_x_scale = 1.0;
  double profile_z_scale = 1.0;

  // A name for the profile, recorded in the manifest so that a consumer can
  // tell one profile bake from another - "ogee" and "wedge30" produce very
  // different edges and are otherwise indistinguishable in the metadata.
  // pomelo-build-3d-font fills this in from the profile's filename.
  std::string profile_name;

  // The levels of the profile and how each one looks, already resolved
  // against a material library. Copied straight into Font3D::slots, so a
  // caller that leaves this empty gets the default palette. Entries past
  // the profile's layer count are harmless and are kept, so a slot table
  // describes the profile rather than this particular subset's geometry.
  std::vector<MaterialSlot> layer_slots;

  // Fillet the sharp corners of each glyph before skeletonizing, which is
  // what the gui does by default and the cli path never did. It removes
  // the creases and notches the straight skeleton throws off sharp
  // vertices, at the cost of a good many more triangles.
  //
  // The radius is relative to the em: the gui hardcodes 0.5 against its em
  // of 128, so 0 here means em_size/256, the same thing at this bake's
  // scale. A vertex is filleted when its interior angle falls below
  // smooth_max_angle.
  bool smooth_corners = true;
  double smooth_radius = 0;                       // 0 = em_size/256
  double smooth_max_angle = 135*M_PI/180;

  // Distance under which coincident vertices are welded. 0 keeps the
  // engine default. Delicate: too tight leaves cracks where vertices that
  // ought to be one stay apart, too loose collapses triangles into
  // degenerates and tears the surface open.
  double merge_distance = 0;

  // The glyph outline is rasterized and retraced before skeletonizing.
  // This is the width of that raster for a full em, and so sets the
  // fidelity of the outline independently of em_size.
  double trace_em_pixels = 1024;

  // 0 means one worker per hardware thread.
  int num_jobs = 0;

  // Follow GSUB when expanding a character subset, so that e.g. ffi comes
  // along with f and i.
  bool ligature_closure = true;

  std::string debug_dir;
};

class Font3DBaker {
  public:
  explicit Font3DBaker(const Font3DBakeParams& params);
  ~Font3DBaker();

  Font3DBaker(const Font3DBaker&) = delete;
  Font3DBaker& operator=(const Font3DBaker&) = delete;

  // Every glyph in the face.
  std::vector<uint32_t> all_glyph_ids() const;

  // The glyphs that render the given utf-8 characters, plus - when
  // ligature_closure is set - the glyphs GSUB can substitute in for them.
  // Characters with no cmap entry are reported through missing.
  std::vector<uint32_t> glyph_ids_for_characters(
    const std::string& utf8,
    // output
    std::vector<uint32_t>& missing_codepoints) const;

  // Bake the given glyphs. Failures are recorded per glyph rather than
  // thrown. progress is called from the worker threads, so it must be
  // thread safe; it may be null.
  Font3D bake(const std::vector<uint32_t>& glyph_ids,
              std::function<void(size_t done, size_t total,
                                 uint32_t glyph_id)> progress = nullptr);

  private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

#endif /* FONT3D_BAKE */
