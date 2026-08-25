//======================================================================
//  font3d-layout.h - Lay a string out into placed 3d glyphs.
//
//  The 3d font is keyed by glyph id, so turning a string into glyphs needs
//  the same shaping the original face would get. That is done with
//  harfbuzz against the very font file the 3d font was baked from - the
//  manifest's sha256 is checked so that a mismatched face cannot silently
//  produce nonsense - plus fribidi for the bidirectional ordering that
//  mixed hebrew and latin needs.
//
//  Layout produces placements, not geometry. A host application is free to
//  take the transforms and do its own thing with them: bend them onto an
//  arc, colour them individually, or instance them itself.
//----------------------------------------------------------------------
#ifndef FONT3D_LAYOUT_H
#define FONT3D_LAYOUT_H

#include <memory>
#include <string>
#include <vector>
#include <glm/vec2.hpp>
#include "font3d.h"

enum class TextDirection { Auto, LTR, RTL };
enum class TextAlign { Start, Center, End };

struct LayoutOptions {
  // Auto resolves the base direction from the text itself, which is what
  // mixed hebrew and latin wants.
  TextDirection direction = TextDirection::Auto;
  TextAlign align = TextAlign::Start;

  // An OpenType language tag such as "he" or "en". Empty leaves it unset.
  std::string language;

  // Distance between baselines, in mesh units. 0 takes the value the font
  // was baked with.
  double line_height = 0;

  // Extra advance after every glyph, in mesh units.
  double letter_spacing = 0;

  // harfbuzz feature strings, e.g. "-liga" to switch ligatures off.
  std::vector<std::string> features;
};

struct LayoutResult {
  std::vector<PlacedGlyph> glyphs;
  int num_lines = 0;

  // Extents of the advance boxes, not of the geometry: y runs from the
  // last baseline plus descender to the first baseline plus ascender.
  glm::dvec2 min {0,0};
  glm::dvec2 max {0,0};

  // Codepoints the face has no glyph for.
  std::vector<uint32_t> missing_codepoints;

  // Glyphs the face shaped to but which carry no geometry in the 3d font,
  // because they were outside the baked subset or failed to bake. Spaces
  // are not reported here.
  std::vector<uint32_t> unbaked_glyphs;
};

class Font3DLayout {
  public:
  // font_filename must be the face the 3d font was baked from. When
  // require_matching_hash is set a sha256 mismatch is an error rather than
  // a warning.
  Font3DLayout(const Font3D& font,
               const std::string& font_filename,
               bool require_matching_hash = true);
  ~Font3DLayout();

  Font3DLayout(const Font3DLayout&) = delete;
  Font3DLayout& operator=(const Font3DLayout&) = delete;

  LayoutResult layout(const std::string& utf8,
                      const LayoutOptions& options) const;

  private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

#endif /* FONT3D_LAYOUT */
