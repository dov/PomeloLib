//======================================================================
// pomelo-build-3d-font - Bake a ttf/otf face into a 3d font glb.
//
// Every glyph of the requested subset is run through the pomelo beveling
// algorithm and the results are packed into a single self describing glb.
//----------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include <glib.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "font3d.h"
#include "font3d-bake.h"
#include "material.h"
#include "profile.h"

using namespace std;

template <typename... Args>
static void die(fmt::format_string<Args...> FormatStr, Args &&... args)
{
  string msg = fmt::format(FormatStr, std::forward<Args>(args)...);
  if (msg.empty() || msg[msg.size()-1] != '\n')
    msg += "\n";
  fmt::print(stderr, "{}", msg);
  exit(-1);
}

#define CASE(s) if (s == S_)
#define CASE2(s,s1) if (s == S_ || s1 == S_)

static const char *DEFAULT_CHARS =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static vector<uint32_t> parse_glyph_id_list(const string& spec)
{
  vector<uint32_t> ids;
  size_t pos = 0;
  while (pos < spec.size())
  {
    size_t comma = spec.find(',', pos);
    if (comma == string::npos)
      comma = spec.size();
    string item = spec.substr(pos, comma-pos);
    if (!item.empty())
    {
      size_t dash = item.find('-');
      if (dash != string::npos && dash > 0)
      {
        long lo = atol(item.substr(0, dash).c_str());
        long hi = atol(item.substr(dash+1).c_str());
        for (long g=lo; g<=hi; g++)
          ids.push_back((uint32_t)g);
      }
      else
        ids.push_back((uint32_t)atol(item.c_str()));
    }
    pos = comma+1;
  }
  return ids;
}

static void usage()
{
  fmt::print(
    "pomelo-build-3d-font - bake the glyphs of a font into a 3d font glb\n"
    "\n"
    "Syntax:\n"
    "    pomelo-build-3d-font [options] font.ttf -o font3d.glb\n"
    "\n"
    "Subset selection:\n"
    "   --chars str            Bake the glyphs for these utf-8 characters\n"
    "                          (default: A-Z 0-9)\n"
    "   --all-glyphs           Bake every glyph in the face\n"
    "   --glyph-ids list       Bake these glyph ids, e.g. 3,7,20-30\n"
    "   --no-ligature-closure  Do not follow GSUB when expanding --chars\n"
    "   --face-index n         Face index within a collection (default 0)\n"
    "\n"
    "Geometry:\n"
    "   --em-size s            Mesh units spanned by one em (default 10).\n"
    "                          A gltf unit is a metre, so this also sets how\n"
    "                          big the font looks in a viewer. Below about 5\n"
    "                          the algorithm starts losing detail.\n"
    "   --zdepth z             Extrusion depth in mesh units (default em/10)\n"
    "   --radius r             Bevel radius in mesh units (default em/20)\n"
    "   --num-radius-steps n   Steps across the bevel (default 5)\n"
    "   --profile-filename pf  Use a bezier profile instead of the round one.\n"
    "                          Profiles are authored in the gui against an em\n"
    "                          of 128 units and are normalised to --em-size\n"
    "                          automatically.\n"
    "   --material-library f   The json library of named materials that the\n"
    "                          profile's levels refer to. Defaults to a\n"
    "                          materials.json sitting beside the profile.\n"
    "   --profile-x-scale s    Widen (>1) or narrow (<1) the profile on top\n"
    "                          of that normalisation. Default 1, which\n"
    "                          reproduces the gui.\n"
    "   --profile-z-scale s    Same for the profile height. Default 1.\n"
    "   --merge-distance d     Weld vertices closer than this. Default 1e-4.\n"
    "   --no-smooth-corners    Do not fillet sharp corners before\n"
    "                          skeletonizing. Smoothing is on by default,\n"
    "                          matching the gui; it removes the creases the\n"
    "                          straight skeleton throws off sharp vertices,\n"
    "                          but costs a good many more triangles.\n"
    "   --smooth-max-angle d   Fillet vertices whose interior angle is below\n"
    "                          this, in degrees. Default 135. Much above that\n"
    "                          and nearly every traced vertex qualifies.\n"
    "   --smooth-radius r      Fillet radius in mesh units. Default em/256,\n"
    "                          which is what the gui uses at its own scale.\n"
    "   --trace-em-pixels n    Raster width of one em when retracing the\n"
    "                          outline (default 1024)\n"
    "   --max-image-width w    Hard cap on the raster width\n"
    "\n"
    "Other:\n"
    "   --jobs n               Worker threads (default: one per cpu)\n"
    "   --debug-dir dir        Write algorithm debug files here\n"
    "   --verbose              Log the engine's progress chatter\n"
    "  -o, --output file       The glb to write\n"
    );
}

int main(int argc, char **argv)
{
  int argp = 1;
  vector<string> positional;

  string output_filename;
  string chars = DEFAULT_CHARS;
  string glyph_id_spec;
  string profile_filename;
  string material_library_filename;
  string debug_dir;
  bool all_glyphs = false;
  bool verbose = false;
  bool chars_given = false;
  bool zdepth_given = false;
  bool radius_given = false;

  Font3DBakeParams params;
  // A tenth and a twentieth of an em, at the default em_size.
  params.bevel.zdepth = 0.1*params.em_size;
  params.bevel.profile_radius = 0.05*params.em_size;
  params.bevel.profile_num_radius_steps = 5;

  while (argp < argc)
  {
    const string S_ = argv[argp];
    if (S_.size() < 2 || S_[0] != '-')
    {
      positional.push_back(S_);
      argp++;
      continue;
    }
    argp++;

    CASE2("-h", "--help") { usage(); exit(0); }
    CASE("--chars") { chars = argv[argp++]; chars_given = true; continue; }
    CASE("--all-glyphs") { all_glyphs = true; continue; }
    CASE("--glyph-ids") { glyph_id_spec = argv[argp++]; continue; }
    CASE("--no-ligature-closure") { params.ligature_closure = false; continue; }
    CASE("--face-index") { params.face_index = atoi(argv[argp++]); continue; }
    CASE("--em-size") { params.em_size = atof(argv[argp++]); continue; }
    CASE("--zdepth")
      { params.bevel.zdepth = atof(argv[argp++]); zdepth_given = true; continue; }
    CASE("--radius")
      { params.bevel.profile_radius = atof(argv[argp++]); radius_given = true; continue; }
    CASE("--num-radius-steps")
      { params.bevel.profile_num_radius_steps = atoi(argv[argp++]); continue; }
    CASE("--profile-filename") { profile_filename = argv[argp++]; continue; }
    CASE("--material-library")
      { material_library_filename = argv[argp++]; continue; }
    CASE("--profile-x-scale")
      { params.profile_x_scale = atof(argv[argp++]); continue; }
    CASE("--profile-z-scale")
      { params.profile_z_scale = atof(argv[argp++]); continue; }
    CASE("--merge-distance")
      { params.merge_distance = atof(argv[argp++]); continue; }
    CASE("--no-smooth-corners") { params.smooth_corners = false; continue; }
    CASE("--smooth-max-angle")
      { params.smooth_max_angle = atof(argv[argp++])*M_PI/180; continue; }
    CASE("--smooth-radius")
      { params.smooth_radius = atof(argv[argp++]); continue; }
    CASE("--trace-em-pixels") { params.trace_em_pixels = atof(argv[argp++]); continue; }
    CASE("--max-image-width")
      { params.bevel.max_image_width = atoi(argv[argp++]); continue; }
    CASE("--jobs") { params.num_jobs = atoi(argv[argp++]); continue; }
    CASE("--debug-dir") { debug_dir = argv[argp++]; continue; }
    CASE("--verbose") { verbose = true; continue; }
    CASE2("-o", "--output") { output_filename = argv[argp++]; continue; }

    die("Unknown option {}!", S_);
  }

  if (positional.size() != 1)
  {
    usage();
    die("\nExactly one font file is expected");
  }
  params.font_filename = positional[0];
  params.debug_dir = debug_dir;

  // Keep the bevel proportional to the em unless it was pinned explicitly.
  if (!zdepth_given)
    params.bevel.zdepth = 0.1*params.em_size;
  if (!radius_given)
    params.bevel.profile_radius = 0.05*params.em_size;

  // See the comment on Font3DBakeParams::em_size.
  if (params.em_size < 5)
    fmt::print(stderr,
               "Warning: an em_size of {} is below the range the algorithm is\n"
               "         stable over. Expect lost detail and failed glyphs.\n",
               params.em_size);

  if (output_filename.empty())
    die("No output file given. Use -o font3d.glb");

  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("pomelo", color_sink);
  logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
  logger->set_level(verbose ? spdlog::level::info : spdlog::level::warn);
  logger->flush_on(spdlog::level::warn);
  spdlog::set_default_logger(logger);

  if (!profile_filename.empty())
  {
    try {
      params.bevel.profile_data.load_from_file(profile_filename);
    }
    catch (const std::exception& exc) {
      die("{}: {}", profile_filename, exc.what());
    }
    params.bevel.use_profile_data = true;

    // Record which profile this was, so that the glb says what shape its
    // edge is rather than only that it has one. The stem of the filename is
    // what a person recognises, e.g. "ogee" out of "profiles/ogee.profile".
    string stem = profile_filename;
    if (auto slash = stem.find_last_of("/\\"); slash != string::npos)
      stem = stem.substr(slash+1);
    if (auto dot = stem.find_last_of('.'); dot != string::npos && dot > 0)
      stem = stem.substr(0, dot);
    params.profile_name = stem;
  }

  // Turn the profile's levels into the slot table the glb carries. A
  // level's "material" is a name in the library, an inline object, or
  // absent, in which case it falls back to a distinguishable default.
  {
    MaterialLibrary library;
    try {
      library = material_library_filename.empty()
        ? MaterialLibrary::load_default(profile_filename)
        : MaterialLibrary::load(material_library_filename);
    }
    catch (const std::exception& exc) {
      die("Cannot read the material library: {}", exc.what());
    }
    if (!library.empty())
      spdlog::info("material library: {} ({} materials)",
                   library.source(), library.names().size());

    const ProfileData& profile = params.bevel.profile_data;
    size_t num_levels = params.bevel.use_profile_data ? profile.size() : 1;
    for (size_t i=0; i<num_levels; i++)
    {
      MaterialSlot slot;
      slot.index = (int)i;
      slot.material = default_material_for_slot(i);
      if (params.bevel.use_profile_data)
      {
        slot.name = profile[i].name;
        try {
          slot.material = library.resolve(profile[i].material, slot.material);
        }
        catch (const std::exception& exc) {
          die("{}: layer {}{}: {}",
              profile_filename, i,
              slot.name.empty() ? "" : fmt::format(" ({})", slot.name),
              exc.what());
        }
      }
      params.layer_slots.push_back(std::move(slot));
    }
  }

  try
  {
    Font3DBaker baker(params);

    vector<uint32_t> glyph_ids;
    if (all_glyphs)
    {
      glyph_ids = baker.all_glyph_ids();
      fmt::print("Baking all {} glyphs\n", glyph_ids.size());
    }
    else if (!glyph_id_spec.empty())
    {
      glyph_ids = parse_glyph_id_list(glyph_id_spec);
      if (chars_given)
        die("--glyph-ids and --chars are mutually exclusive");
      fmt::print("Baking {} explicitly listed glyphs\n", glyph_ids.size());
    }
    else
    {
      vector<uint32_t> missing;
      glyph_ids = baker.glyph_ids_for_characters(chars, missing);
      if (!missing.empty())
        fmt::print(stderr,
                   "Warning: {} requested character(s) are not in the font, "
                   "first few: {}\n",
                   missing.size(),
                   fmt::join(vector<uint32_t>(
                     missing.begin(),
                     missing.begin()+min<size_t>(8, missing.size())), " "));
      fmt::print("Baking {} glyphs for {} characters\n",
                 glyph_ids.size(),
                 g_utf8_strlen(chars.c_str(), -1));
    }

    if (glyph_ids.empty())
      die("Nothing to bake");

    auto start = chrono::steady_clock::now();

    atomic<size_t> last_printed {0};
    auto font = baker.bake(
      glyph_ids,
      [&](size_t done, size_t total, uint32_t /*glyph_id*/) {
        // Only the thread that moves the counter forward prints, so the
        // line stays monotonic without a lock.
        size_t prev = last_printed.exchange(done);
        if (done > prev)
          fmt::print("\r  {}/{} glyphs", done, total);
        fflush(stdout);
      });

    fmt::print("\n");

    auto elapsed = chrono::duration<double>(
      chrono::steady_clock::now() - start).count();

    size_t num_ok = 0, num_empty = 0, num_failed = 0;
    for (const auto& g : font.glyphs)
      switch (g.status)
      {
        case GlyphStatus::Ok:     num_ok++; break;
        case GlyphStatus::Empty:  num_empty++; break;
        case GlyphStatus::Failed: num_failed++; break;
      }

    fmt::print("Baked {} glyphs in {:.1f}s: {} with geometry, {} empty, {} failed\n",
               font.glyphs.size(), elapsed, num_ok, num_empty, num_failed);

    for (const auto& g : font.glyphs)
      if (g.status == GlyphStatus::Failed)
        fmt::print(stderr, "  glyph {} ({}) failed: {}\n",
                   g.glyph_id, g.name.empty() ? "?" : g.name, g.error);

    if (num_ok == 0)
      die("No glyph produced any geometry");

    font.save_glb(output_filename);
    fmt::print("Wrote {}\n", output_filename);
  }
  catch (const std::exception& e)
  {
    die("{}", e.what());
  }

  return 0;
}
