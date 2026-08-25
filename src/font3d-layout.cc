//======================================================================
//  font3d-layout.cc - Lay a string out into placed 3d glyphs.
//----------------------------------------------------------------------

#include "font3d-layout.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include <fribidi.h>
#include <glib.h>
#include <hb.h>
#include <hb-ot.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace {

string file_sha256(const string& filename)
{
  gchar *contents = nullptr;
  gsize length = 0;
  GError *error = nullptr;
  if (!g_file_get_contents(filename.c_str(), &contents, &length, &error))
  {
    string msg = error ? error->message : "unknown error";
    if (error)
      g_error_free(error);
    throw runtime_error(fmt::format("Failed to read {}: {}", filename, msg));
  }
  gchar *sum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                           (const guchar*)contents, length);
  string result = sum ? sum : "";
  g_free(sum);
  g_free(contents);
  return result;
}

// One line of the input, as codepoints plus the byte offset each came from
// so that clusters can be reported against the original utf-8.
struct Line {
  vector<uint32_t> text;
  vector<int> byte_offset;
};

vector<Line> split_lines(const string& utf8)
{
  vector<Line> lines(1);
  const char *p = utf8.c_str();
  const char *start = p;
  while (*p)
  {
    gunichar c = g_utf8_get_char_validated(p, -1);
    if (c == (gunichar)-1 || c == (gunichar)-2)
      throw runtime_error("The input text is not valid utf-8");
    int offset = (int)(p - start);
    p = g_utf8_next_char(p);

    if (c == '\n')
    {
      lines.emplace_back();
      continue;
    }
    if (c == '\r')
      continue;
    lines.back().text.push_back((uint32_t)c);
    lines.back().byte_offset.push_back(offset);
  }
  return lines;
}

// A maximal stretch of one line that shares a bidi level and a script, and
// so can be handed to harfbuzz in one piece.
struct Run {
  int start = 0;          // index into Line::text
  int length = 0;
  FriBidiLevel level = 0;
  hb_script_t script = HB_SCRIPT_COMMON;
  bool rtl() const { return level % 2; }
};

// Unicode bidi rule L2: from the highest level down to the lowest odd
// level, reverse any contiguous run of runs at or above that level.
void reorder_runs_visually(vector<Run>& runs)
{
  if (runs.empty())
    return;

  FriBidiLevel highest = 0;
  FriBidiLevel lowest_odd = FRIBIDI_BIDI_MAX_RESOLVED_LEVELS;
  for (const Run& r : runs)
  {
    highest = max(highest, r.level);
    if (r.level % 2)
      lowest_odd = min(lowest_odd, r.level);
  }

  for (FriBidiLevel level = highest; level >= lowest_odd && level > 0; level--)
  {
    for (size_t i=0; i<runs.size(); i++)
    {
      if (runs[i].level < level)
        continue;
      size_t j = i;
      while (j+1 < runs.size() && runs[j+1].level >= level)
        j++;
      reverse(runs.begin()+i, runs.begin()+j+1);
      i = j;
    }
  }
}

vector<Run> split_into_runs(const Line& line,
                            const vector<FriBidiLevel>& levels)
{
  vector<Run> runs;
  const int n = (int)line.text.size();

  hb_unicode_funcs_t *ufuncs = hb_unicode_funcs_get_default();

  hb_script_t current = HB_SCRIPT_COMMON;
  for (int i=0; i<n; i++)
  {
    hb_script_t s = hb_unicode_script(ufuncs, line.text[i]);

    // Punctuation and marks continue whatever script they sit in rather
    // than chopping a word into pieces.
    if (s == HB_SCRIPT_COMMON || s == HB_SCRIPT_INHERITED ||
        s == HB_SCRIPT_UNKNOWN)
      s = current;
    else
      current = s;

    if (!runs.empty() &&
        runs.back().level == levels[i] &&
        runs.back().script == s)
    {
      runs.back().length++;
      continue;
    }
    Run r;
    r.start = i;
    r.length = 1;
    r.level = levels[i];
    r.script = s;
    runs.push_back(r);
  }

  // The script of a leading COMMON stretch is only known once a real
  // script shows up, so fold any such prefix into its neighbour.
  for (size_t i=0; i+1<runs.size(); )
  {
    if (runs[i].script == HB_SCRIPT_COMMON &&
        runs[i].level == runs[i+1].level)
    {
      runs[i+1].start = runs[i].start;
      runs[i+1].length += runs[i].length;
      runs.erase(runs.begin()+i);
      continue;
    }
    i++;
  }

  return runs;
}

} // anonymous namespace

//----------------------------------------------------------------------

class Font3DLayout::Impl {
  public:
  Impl(const Font3D& font, const string& font_filename, bool require_hash)
    : font(font)
  {
    string actual = file_sha256(font_filename);
    if (!font.meta.source_sha256.empty() && actual != font.meta.source_sha256)
    {
      string msg = fmt::format(
        "{} is not the face this 3d font was baked from.\n"
        "  baked from {} sha256 {}\n"
        "  given      {} sha256 {}\n"
        "Glyph ids would not line up, so the output would be wrong.",
        font_filename,
        font.meta.source_font, font.meta.source_sha256,
        font_filename, actual);
      if (require_hash)
        throw runtime_error(msg);
      spdlog::warn("{}", msg);
    }

    blob = hb_blob_create_from_file(font_filename.c_str());
    if (hb_blob_get_length(blob) == 0)
      throw runtime_error(fmt::format("Failed to read {}", font_filename));

    hb_face = hb_face_create(blob, font.meta.face_index);
    upem = hb_face_get_upem(hb_face);
    if (upem == 0)
      throw runtime_error("The font reports a zero units per em");

    hb_font = hb_font_create(hb_face);
    // Shape in font design units and scale to mesh units in double
    // afterwards. Setting the harfbuzz scale to the mesh em directly would
    // quantize every advance to an integer.
    hb_font_set_scale(hb_font, upem, upem);
    hb_ot_font_set_funcs(hb_font);

    to_mesh = font.meta.em_size / upem;
  }

  ~Impl()
  {
    if (hb_font) hb_font_destroy(hb_font);
    if (hb_face) hb_face_destroy(hb_face);
    if (blob) hb_blob_destroy(blob);
  }

  const Font3D& font;
  hb_blob_t *blob = nullptr;
  hb_face_t *hb_face = nullptr;
  hb_font_t *hb_font = nullptr;
  unsigned int upem = 0;
  double to_mesh = 1;
};

Font3DLayout::Font3DLayout(const Font3D& font,
                           const string& font_filename,
                           bool require_matching_hash)
  : m_impl(make_unique<Impl>(font, font_filename, require_matching_hash))
{
}

Font3DLayout::~Font3DLayout() = default;

LayoutResult Font3DLayout::layout(const string& utf8,
                                  const LayoutOptions& options) const
{
  const Font3D& font = m_impl->font;
  LayoutResult result;

  vector<hb_feature_t> features;
  for (const string& f : options.features)
  {
    hb_feature_t feature;
    if (!hb_feature_from_string(f.c_str(), (int)f.size(), &feature))
      throw runtime_error(fmt::format("Cannot parse the feature '{}'", f));
    features.push_back(feature);
  }

  hb_language_t language = options.language.empty()
    ? HB_LANGUAGE_INVALID
    : hb_language_from_string(options.language.c_str(),
                              (int)options.language.size());

  double line_height = options.line_height > 0 ? options.line_height
                                               : font.meta.line_height;

  vector<Line> lines = split_lines(utf8);
  result.num_lines = (int)lines.size();

  set<uint32_t> missing_seen, unbaked_seen;

  // Lay each line out from its own origin first, then align it.
  struct LaidOutLine {
    vector<PlacedGlyph> glyphs;
    double width = 0;
    bool rtl_paragraph = false;
  };
  vector<LaidOutLine> laid_out;

  for (size_t line_idx=0; line_idx<lines.size(); line_idx++)
  {
    const Line& line = lines[line_idx];
    LaidOutLine out;

    if (line.text.empty())
    {
      laid_out.push_back(out);
      continue;
    }

    const FriBidiStrIndex n = (FriBidiStrIndex)line.text.size();
    vector<FriBidiCharType> bidi_types(n);
    vector<FriBidiBracketType> bracket_types(n);
    vector<FriBidiLevel> levels(n);

    const FriBidiChar *ucs4 = (const FriBidiChar*)line.text.data();
    fribidi_get_bidi_types(ucs4, n, bidi_types.data());
    fribidi_get_bracket_types(ucs4, n, bidi_types.data(),
                              bracket_types.data());

    FriBidiParType par_type =
      options.direction == TextDirection::LTR ? FRIBIDI_PAR_LTR :
      options.direction == TextDirection::RTL ? FRIBIDI_PAR_RTL :
                                                FRIBIDI_PAR_ON;

    if (fribidi_get_par_embedding_levels_ex(
          bidi_types.data(), bracket_types.data(), n, &par_type,
          levels.data()) == 0)
      throw runtime_error("fribidi failed to resolve the embedding levels");

    out.rtl_paragraph = FRIBIDI_IS_RTL(par_type);

    vector<Run> runs = split_into_runs(line, levels);
    reorder_runs_visually(runs);

    double pen_x = 0;
    for (const Run& run : runs)
    {
      hb_buffer_t *buffer = hb_buffer_create();
      hb_buffer_add_utf32(buffer,
                          (const uint32_t*)line.text.data(),
                          (int)line.text.size(),
                          run.start,
                          run.length);
      hb_buffer_set_direction(buffer, run.rtl() ? HB_DIRECTION_RTL
                                                : HB_DIRECTION_LTR);
      hb_buffer_set_script(buffer, run.script);
      if (language != HB_LANGUAGE_INVALID)
        hb_buffer_set_language(buffer, language);
      hb_buffer_set_cluster_level(
        buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

      hb_shape(m_impl->hb_font, buffer,
               features.empty() ? nullptr : features.data(),
               (unsigned int)features.size());

      unsigned int num_glyphs = 0;
      hb_glyph_info_t *infos = hb_buffer_get_glyph_infos(buffer, &num_glyphs);
      hb_glyph_position_t *positions =
        hb_buffer_get_glyph_positions(buffer, &num_glyphs);

      for (unsigned int i=0; i<num_glyphs; i++)
      {
        uint32_t gid = infos[i].codepoint;
        int cluster = (int)infos[i].cluster;

        if (gid == 0)
        {
          // .notdef - the face has no glyph for this character.
          if (cluster >= 0 && cluster < (int)line.text.size())
            missing_seen.insert(line.text[cluster]);
        }
        else
        {
          const Glyph3D *g = font.find(gid);
          if (!g || g->status == GlyphStatus::Failed)
            unbaked_seen.insert(gid);
        }

        PlacedGlyph placed;
        placed.glyph_id = gid;
        placed.pen = glm::dvec3(
          pen_x + positions[i].x_offset*m_impl->to_mesh,
          positions[i].y_offset*m_impl->to_mesh,
          0.0);
        placed.cluster = (cluster >= 0 && cluster < (int)line.byte_offset.size())
                       ? line.byte_offset[cluster] : -1;
        placed.line = (int)line_idx;
        out.glyphs.push_back(placed);

        pen_x += positions[i].x_advance*m_impl->to_mesh;
        if (positions[i].x_advance != 0)
          pen_x += options.letter_spacing;
      }

      hb_buffer_destroy(buffer);
    }

    out.width = pen_x;
    laid_out.push_back(out);
  }

  // Align, drop onto successive baselines, and collect.
  double max_width = 0;
  for (const auto& l : laid_out)
    max_width = max(max_width, l.width);

  double min_x = 0, max_x = 0;
  for (size_t i=0; i<laid_out.size(); i++)
  {
    LaidOutLine& l = laid_out[i];

    // Start and End follow the paragraph direction, so a right to left
    // line starts at the right hand edge.
    TextAlign align = options.align;
    bool rtl = l.rtl_paragraph;
    double shift = 0;
    switch (align)
    {
      case TextAlign::Start:  shift = rtl ? max_width - l.width : 0; break;
      case TextAlign::Center: shift = (max_width - l.width)/2;       break;
      case TextAlign::End:    shift = rtl ? 0 : max_width - l.width; break;
    }

    double baseline_y = -(double)i * line_height;
    for (PlacedGlyph& p : l.glyphs)
    {
      p.pen.x += shift;
      p.pen.y += baseline_y;
      result.glyphs.push_back(p);
    }
    min_x = min(min_x, shift);
    max_x = max(max_x, shift + l.width);
  }

  result.min = glm::dvec2(min_x,
                          -(double)(lines.size()-1)*line_height
                          + font.meta.descender);
  result.max = glm::dvec2(max_x, font.meta.ascender);

  result.missing_codepoints.assign(missing_seen.begin(), missing_seen.end());
  result.unbaked_glyphs.assign(unbaked_seen.begin(), unbaked_seen.end());

  return result;
}
