//======================================================================
//  font3d-bake.cc - Bake the glyphs of a ttf/otf face into a 3d font.
//----------------------------------------------------------------------

#include "font3d-bake.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_TRUETYPE_IDS_H
#include <cairo-ft.h>
#include <cairomm/context.h>
#include <glib.h>
#include <hb.h>
#include <hb-ot.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

using namespace std;

namespace {

// The em, in cairo user units, that the gui's pango path produces at its
// default font size of 48: 48 points * 96/72 dpi * the hardcoded
// cairo_scale(2,2) in pangomarkup_to_cairo(). Measured rather than
// assumed - a cap height of 46.86 units at size 24 over DejaVu Sans
// Bold's 0.729 cap ratio gives 64.3 units per em, so 128 at size 48.
// Bezier profiles are authored against this scale.
constexpr double PROFILE_REFERENCE_EM = 128.0;

// A freetype library plus face. Freetype faces are not thread safe, so
// every worker owns one.
class FtFace {
  public:
  FtFace(const string& filename, int face_index)
  {
    if (FT_Init_FreeType(&library))
      throw runtime_error("Failed to initialize freetype");
    FT_Error err = FT_New_Face(library, filename.c_str(), face_index, &face);
    if (err)
    {
      FT_Done_FreeType(library);
      throw runtime_error(fmt::format("Failed to open font {} (freetype error {})",
                                      filename, (int)err));
    }
  }

  ~FtFace()
  {
    if (face)
      FT_Done_Face(face);
    if (library)
      FT_Done_FreeType(library);
  }

  FtFace(const FtFace&) = delete;
  FtFace& operator=(const FtFace&) = delete;

  FT_Library library = nullptr;
  FT_Face face = nullptr;
};

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

string glyph_name(FT_Face face, uint32_t glyph_id)
{
  if (!FT_HAS_GLYPH_NAMES(face))
    return "";
  char buf[256] = {0};
  if (FT_Get_Glyph_Name(face, glyph_id, buf, sizeof(buf)))
    return "";
  return string(buf);
}

} // anonymous namespace

//----------------------------------------------------------------------

class Font3DBaker::Impl {
  public:
  explicit Impl(const Font3DBakeParams& params)
    : params(params),
      main_face(params.font_filename, params.face_index)
  {
    FT_Face face = main_face.face;
    if (!FT_IS_SCALABLE(face))
      throw runtime_error(fmt::format("{} is not a scalable outline font",
                                      params.font_filename));

    units_per_em = face->units_per_EM;
    if (units_per_em <= 0)
      throw runtime_error("Font has a bogus units_per_EM");

    font_scale = params.em_size / units_per_em;

    // The weld tolerance is absolute while the noise it has to swallow
    // scales with the geometry, so tie it to the em. There is a real
    // window either side: measured on DejaVu Sans Bold A-Z0-9 at em 10,
    // 1e-4 leaves 84 boundary edges, 1.5e-4 through 3e-4 give a watertight
    // mesh, and 5e-4 and up collapse triangles into degenerates and tear
    // it open again (710 edges at 5e-4, 6178 at 1e-3). em_size*2e-5 sits
    // in the middle of that window.
    this->params.bevel.merge_distance =
      params.merge_distance > 0 ? params.merge_distance
                                : params.em_size * 2e-5;

    // Corner smoothing, scaled to this bake's em the same way the profile
    // is. See Font3DBakeParams::smooth_corners.
    this->params.bevel.smooth_corners = params.smooth_corners;
    this->params.bevel.smooth_max_angle = params.smooth_max_angle;
    this->params.bevel.smooth_radius =
      params.smooth_radius > 0
        ? params.smooth_radius
        : params.em_size * (0.5/PROFILE_REFERENCE_EM);

    // Bring an authored bezier profile onto this bake's em. See the
    // comment on Font3DBakeParams::profile_x_scale.
    if (this->params.bevel.use_profile_data)
    {
      double norm = params.em_size / PROFILE_REFERENCE_EM;
      double sx = norm * params.profile_x_scale;
      double sz = norm * params.profile_z_scale;
      this->params.bevel.profile_data.scale(sx, sz);

      // The flattening tolerance is an absolute length, so shrink it with
      // the profile or the curves come out visibly polygonal.
      this->params.bevel.profile_linear_limit =
        0.01 * min(sx, sz);

      spdlog::info("Scaling the profile by {:.4f} in x and {:.4f} in z",
                   sx, sz);
    }
  }

  // Font units to mesh units.
  double scaled(double font_units) const { return font_units * font_scale; }

  Font3DMeta face_meta() const
  {
    FT_Face face = main_face.face;
    Font3DMeta meta;
    meta.source_font = g_path_get_basename(params.font_filename.c_str());
    meta.source_sha256 = file_sha256(params.font_filename);
    const char *ps = FT_Get_Postscript_Name(face);
    meta.postscript_name = ps ? ps : "";
    meta.family = face->family_name ? face->family_name : "";
    meta.style = face->style_name ? face->style_name : "";
    meta.face_index = params.face_index;
    meta.units_per_em = units_per_em;
    meta.num_glyphs_in_face = (int)face->num_glyphs;
    meta.em_size = params.em_size;
    meta.ascender = scaled(face->ascender);
    meta.descender = scaled(face->descender);
    meta.line_height = scaled(face->height);
    meta.generator = "pomelo-build-3d-font";
#ifdef COMMIT_ID
    meta.commit_id = COMMIT_ID;
#endif
#ifdef VERSION
    meta.version = VERSION;
#endif
    return meta;
  }

  Glyph3D bake_one(FtFace& ft, cairo_font_face_t *font_face, uint32_t glyph_id) const;

  Font3DBakeParams params;
  FtFace main_face;
  int units_per_em = 0;
  double font_scale = 1;
};

Glyph3D Font3DBaker::Impl::bake_one(FtFace& ft,
                                    cairo_font_face_t *font_face,
                                    uint32_t glyph_id) const
{
  Glyph3D glyph;
  glyph.glyph_id = glyph_id;
  glyph.name = glyph_name(ft.face, glyph_id);

  // Metrics come straight from the font in design units, so they are
  // exact rather than a by-product of the rasterized trace.
  if (FT_Load_Glyph(ft.face, glyph_id, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING))
  {
    glyph.status = GlyphStatus::Failed;
    glyph.error = "FT_Load_Glyph failed";
    return glyph;
  }
  glyph.advance = scaled(ft.face->glyph->metrics.horiAdvance);

  // Glyphs with no contours - the space being the obvious one - have an
  // advance and nothing else.
  if (ft.face->glyph->outline.n_contours == 0)
  {
    glyph.status = GlyphStatus::Empty;
    return glyph;
  }

  // Paint the glyph onto a recording surface in its own coordinate frame:
  // the origin is the pen position on the baseline, and one em spans
  // params.em_size units.
  cairo_surface_t *rec_surface =
    cairo_recording_surface_create(CAIRO_CONTENT_ALPHA, nullptr);
  auto surface = Cairo::RefPtr<Cairo::Surface>(new Cairo::Surface(rec_surface));
  cairo_surface_destroy(rec_surface); // cairomm holds its own reference now

  {
    auto cr = Cairo::Context::create(surface);
    cairo_set_font_face(cr->cobj(), font_face);
    cairo_set_font_size(cr->cobj(), params.em_size);

    cairo_glyph_t cairo_glyph;
    cairo_glyph.index = glyph_id;
    cairo_glyph.x = 0;
    cairo_glyph.y = 0;
    cairo_glyph_path(cr->cobj(), &cairo_glyph, 1);
    cr->fill();

    if (cairo_status(cr->cobj()) != CAIRO_STATUS_SUCCESS)
    {
      glyph.status = GlyphStatus::Failed;
      glyph.error = fmt::format("cairo error: {}",
                                cairo_status_to_string(cairo_status(cr->cobj())));
      return glyph;
    }
  }

  BevelParams bevel = params.bevel;
  bevel.resolution = params.trace_em_pixels / params.em_size;

  try
  {
    glyph.layers = paths_to_mesh(surface, bevel, nullptr, params.debug_dir);
  }
  catch (const std::exception& e)
  {
    glyph.status = GlyphStatus::Failed;
    glyph.error = e.what();
    return glyph;
  }

  if (!glyph.has_geometry())
  {
    glyph.status = GlyphStatus::Failed;
    glyph.error = "the algorithm produced no triangles";
    return glyph;
  }

  // textrusion already negates cairo's y when it builds the mesh, so the
  // geometry arrives y-up. Together with the origin preserving flatten
  // step that puts it straight into glyph space: x to the right, y up
  // from the baseline, z the extrusion depth.
  glyph.status = GlyphStatus::Ok;
  glyph.center_on_bbox();

  return glyph;
}

//----------------------------------------------------------------------

Font3DBaker::Font3DBaker(const Font3DBakeParams& params)
  : m_impl(make_unique<Impl>(params))
{
}

Font3DBaker::~Font3DBaker() = default;

vector<uint32_t> Font3DBaker::all_glyph_ids() const
{
  vector<uint32_t> ids;
  long n = m_impl->main_face.face->num_glyphs;
  ids.reserve(n);
  for (long i=0; i<n; i++)
    ids.push_back((uint32_t)i);
  return ids;
}

vector<uint32_t> Font3DBaker::glyph_ids_for_characters(
  const string& utf8,
  vector<uint32_t>& missing_codepoints) const
{
  missing_codepoints.clear();

  glong num_chars = 0;
  gunichar *ucs4 = g_utf8_to_ucs4(utf8.c_str(), -1, nullptr, &num_chars, nullptr);
  if (!ucs4)
    throw runtime_error("The character subset is not valid utf-8");

  // A set keeps the result sorted and deduplicated.
  set<uint32_t> ids;
  for (glong i=0; i<num_chars; i++)
  {
    FT_UInt gid = FT_Get_Char_Index(m_impl->main_face.face, ucs4[i]);
    if (gid == 0)
      missing_codepoints.push_back((uint32_t)ucs4[i]);
    else
      ids.insert((uint32_t)gid);
  }
  g_free(ucs4);

  // Ligature glyphs carry no cmap entry, so a subset selected by
  // character would silently drop them. Follow GSUB to pull them in.
  if (m_impl->params.ligature_closure && !ids.empty())
  {
    hb_blob_t *blob = hb_blob_create_from_file(m_impl->params.font_filename.c_str());
    hb_face_t *hb_face = hb_face_create(blob, m_impl->params.face_index);

    hb_set_t *lookups = hb_set_create();
    const hb_tag_t features[] = {
      HB_TAG('l','i','g','a'),   // standard ligatures
      HB_TAG('c','l','i','g'),   // contextual ligatures
      HB_TAG('r','l','i','g'),   // required ligatures
      HB_TAG('c','c','m','p'),   // glyph composition, used by hebrew niqqud
      HB_TAG_NONE
    };
    hb_ot_layout_collect_lookups(hb_face, HB_OT_TAG_GSUB,
                                 nullptr, nullptr, features, lookups);

    hb_set_t *glyph_set = hb_set_create();
    for (uint32_t gid : ids)
      hb_set_add(glyph_set, gid);

    hb_ot_layout_lookups_substitute_closure(hb_face, lookups, glyph_set);

    hb_codepoint_t gid = HB_SET_VALUE_INVALID;
    while (hb_set_next(glyph_set, &gid))
      ids.insert((uint32_t)gid);

    hb_set_destroy(glyph_set);
    hb_set_destroy(lookups);
    hb_face_destroy(hb_face);
    hb_blob_destroy(blob);
  }

  return vector<uint32_t>(ids.begin(), ids.end());
}

Font3D Font3DBaker::bake(const vector<uint32_t>& glyph_ids,
                         function<void(size_t, size_t, uint32_t)> progress)
{
  Font3D font;
  font.meta = m_impl->face_meta();
  font.slots = m_impl->params.layer_slots;

  const BevelParams& bevel = m_impl->params.bevel;
  nlohmann::json& bp = font.meta.bake_params;
  bp["zdepth"] = bevel.zdepth;
  bp["profile_radius"] = bevel.profile_radius;
  bp["profile_round_max_angle"] = bevel.profile_round_max_angle;
  bp["profile_num_radius_steps"] = bevel.profile_num_radius_steps;
  bp["linear_limit"] = bevel.linear_limit;
  bp["use_profile_data"] = bevel.use_profile_data;
  if (bevel.use_profile_data)
  {
    bp["profile_x_scale"] = m_impl->params.profile_x_scale;
    bp["profile_z_scale"] = m_impl->params.profile_z_scale;
    bp["profile_reference_em"] = PROFILE_REFERENCE_EM;
    if (!m_impl->params.profile_name.empty())
      bp["profile_name"] = m_impl->params.profile_name;
  }
  bp["smooth_corners"] = bevel.smooth_corners;
  if (bevel.smooth_corners)
  {
    bp["smooth_radius"] = bevel.smooth_radius;
    bp["smooth_max_angle_deg"] = bevel.smooth_max_angle*180/M_PI;
  }
  bp["trace_em_pixels"] = m_impl->params.trace_em_pixels;
  bp["max_image_width"] = bevel.max_image_width;

  font.glyphs.resize(glyph_ids.size());

  int num_jobs = m_impl->params.num_jobs;
  if (num_jobs <= 0)
    num_jobs = max(1u, thread::hardware_concurrency());
  num_jobs = min<int>(num_jobs, (int)glyph_ids.size());
  num_jobs = max(num_jobs, 1);

  spdlog::info("Baking {} glyphs with {} worker(s)", glyph_ids.size(), num_jobs);

  atomic<size_t> next_index {0};
  atomic<size_t> num_done {0};
  mutex error_mutex;
  string fatal_error;

  auto worker = [&]() {
    unique_ptr<FtFace> ft;
    cairo_font_face_t *font_face = nullptr;
    try
    {
      // Each worker gets its own freetype face; cairo will lock it for
      // the duration of each glyph path.
      ft = make_unique<FtFace>(m_impl->params.font_filename,
                               m_impl->params.face_index);
      font_face = cairo_ft_font_face_create_for_ft_face(ft->face, 0);
    }
    catch (const std::exception& e)
    {
      lock_guard<mutex> lock(error_mutex);
      if (fatal_error.empty())
        fatal_error = e.what();
      return;
    }

    for (;;)
    {
      size_t i = next_index.fetch_add(1);
      if (i >= glyph_ids.size())
        break;

      uint32_t glyph_id = glyph_ids[i];
      Glyph3D glyph;
      try
      {
        glyph = m_impl->bake_one(*ft, font_face, glyph_id);
      }
      catch (const std::exception& e)
      {
        glyph = Glyph3D();
        glyph.glyph_id = glyph_id;
        glyph.status = GlyphStatus::Failed;
        glyph.error = e.what();
      }
      font.glyphs[i] = std::move(glyph);

      size_t done = num_done.fetch_add(1) + 1;
      if (progress)
        progress(done, glyph_ids.size(), glyph_id);
    }

    cairo_font_face_destroy(font_face);
  };

  if (num_jobs == 1)
  {
    worker();
  }
  else
  {
    vector<thread> threads;
    threads.reserve(num_jobs);
    for (int i=0; i<num_jobs; i++)
      threads.emplace_back(worker);
    for (auto& t : threads)
      t.join();
  }

  if (!fatal_error.empty())
    throw runtime_error(fatal_error);

  return font;
}
