//======================================================================
// pomelo-layout-3d-text - Lay a string out with a baked 3d font.
//
// Takes a glb written by pomelo-build-3d-font plus the face it was baked
// from, shapes the text against that face, and writes the placed glyph
// meshes out. Repeated letters are instanced in the glb rather than
// duplicated.
//----------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include <glib.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "font3d.h"
#include "font3d-layout.h"
#include "svg-path-flatten.h"
#include "mesh.h"
#include "utils.h"

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

// Splits "r" or "r,start_deg,end_deg" for --path-arc.
static vector<double> parse_comma_numbers(const string& s)
{
  vector<double> out;
  size_t start = 0;
  while (start <= s.size())
  {
    size_t comma = s.find(',', start);
    string field = s.substr(start, comma - start);
    out.push_back(atof(field.c_str()));
    if (comma == string::npos)
      break;
    start = comma + 1;
  }
  return out;
}

static void usage()
{
  fmt::print(
    "pomelo-layout-3d-text - lay a string out with a baked 3d font\n"
    "\n"
    "Syntax:\n"
    "    pomelo-layout-3d-text --font3d font3d.glb --font face.ttf \\\n"
    "                          --text \"hello\" -o out.glb\n"
    "\n"
    "The face is needed because shaping - ligatures, kerning, mark\n"
    "positioning - lives in the font's own tables. Its sha256 is checked\n"
    "against the one recorded when the 3d font was baked.\n"
    "\n"
    "Options:\n"
    "   --font3d font3d.glb    The baked 3d font from pomelo-build-3d-font\n"
    "   --font face.ttf        The face the 3d font was baked from\n"
    "   --text str             The text to lay out; \\n separates lines\n"
    "   --text-file file       Read the text from a file instead\n"
    "   --direction d          auto (default), ltr or rtl\n"
    "   --align a              start (default), center or end\n"
    "   --language lang        OpenType language tag, e.g. he or en\n"
    "   --line-height h        Baseline distance; default is the font's\n"
    "   --letter-spacing s     Extra advance after every glyph\n"
    "   --feature f            harfbuzz feature, e.g. -liga. Repeatable\n"
    "   --allow-font-mismatch  Warn instead of failing on a sha256 mismatch\n"
    "   --verbose              Log progress\n"
    "  -o, --output file       Output .glb or .stl\n"
    "\n"
    "Path options - bend the laid out text onto a curve, svg textPath\n"
    "style (rotate and translate each glyph; the glyph itself is not\n"
    "distorted). At most one of --path-svg, --path or --path-arc.\n"
    "   --path-svg file.svg    Use a path from an svg file\n"
    "   --path-index n         Which path in the file, in document order\n"
    "                          (default 0, the first)\n"
    "   --path d               A literal svg path 'd' expression\n"
    "   --path-arc r[,s,e]     A circular arc of radius r from s to e\n"
    "                          degrees (default 0,360 - a full ring)\n"
    "   --path-offset v        Shift the text along the path's normal,\n"
    "                          e.g. to sit it above or below the path\n"
    );
}

int main(int argc, char **argv)
{
  int argp = 1;
  vector<string> positional;

  string font3d_filename;
  string font_filename;
  string text;
  string text_file;
  string output_filename;
  bool text_given = false;
  bool allow_mismatch = false;
  bool verbose = false;
  LayoutOptions options;

  string path_svg_filename;
  int path_index = 0;
  string path_d;
  bool path_arc_given = false;
  double path_arc_radius = 0, path_arc_start = 0, path_arc_end = 360;
  double path_offset = 0;

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
    CASE("--font3d") { font3d_filename = argv[argp++]; continue; }
    CASE("--font") { font_filename = argv[argp++]; continue; }
    CASE("--text") { text = argv[argp++]; text_given = true; continue; }
    CASE("--text-file") { text_file = argv[argp++]; continue; }
    CASE("--direction")
    {
      string d = argv[argp++];
      if (d == "auto")      options.direction = TextDirection::Auto;
      else if (d == "ltr")  options.direction = TextDirection::LTR;
      else if (d == "rtl")  options.direction = TextDirection::RTL;
      else die("--direction expects auto, ltr or rtl");
      continue;
    }
    CASE("--align")
    {
      string a = argv[argp++];
      if (a == "start")       options.align = TextAlign::Start;
      else if (a == "center") options.align = TextAlign::Center;
      else if (a == "end")    options.align = TextAlign::End;
      else die("--align expects start, center or end");
      continue;
    }
    CASE("--language") { options.language = argv[argp++]; continue; }
    CASE("--line-height") { options.line_height = atof(argv[argp++]); continue; }
    CASE("--letter-spacing")
      { options.letter_spacing = atof(argv[argp++]); continue; }
    CASE("--feature") { options.features.push_back(argv[argp++]); continue; }
    CASE("--allow-font-mismatch") { allow_mismatch = true; continue; }
    CASE("--verbose") { verbose = true; continue; }
    CASE2("-o", "--output") { output_filename = argv[argp++]; continue; }
    CASE("--path-svg") { path_svg_filename = argv[argp++]; continue; }
    CASE("--path-index") { path_index = atoi(argv[argp++]); continue; }
    CASE("--path") { path_d = argv[argp++]; continue; }
    CASE("--path-arc")
    {
      vector<double> v = parse_comma_numbers(argv[argp++]);
      if (v.empty())
        die("--path-arc expects r[,start_deg,end_deg]");
      path_arc_radius = v[0];
      if (v.size() > 1) path_arc_start = v[1];
      if (v.size() > 2) path_arc_end = v[2];
      path_arc_given = true;
      continue;
    }
    CASE("--path-offset") { path_offset = atof(argv[argp++]); continue; }

    die("Unknown option {}!", S_);
  }

  if (!positional.empty())
  {
    usage();
    die("\nUnexpected argument '{}'. Every input is named.", positional[0]);
  }
  if (font3d_filename.empty())
    die("No 3d font given. Use --font3d font3d.glb");
  if (font_filename.empty())
    die("No face given. Use --font face.ttf");
  if (output_filename.empty())
    die("No output file given. Use -o out.glb");
  if ((!path_svg_filename.empty()) + (!path_d.empty()) + path_arc_given > 1)
    die("--path-svg, --path and --path-arc are mutually exclusive");
  if (path_index != 0 && path_svg_filename.empty())
    die("--path-index only makes sense with --path-svg");

  if (!text_file.empty())
  {
    if (text_given)
      die("--text and --text-file are mutually exclusive");
    text = load_string_from_file(text_file);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
      text.pop_back();
  }
  else if (!text_given)
    die("No text given. Use --text or --text-file");

  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("pomelo", color_sink);
  logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
  logger->set_level(verbose ? spdlog::level::info : spdlog::level::warn);
  spdlog::set_default_logger(logger);

  try
  {
    Font3D font = Font3D::load_glb(font3d_filename);
    fmt::print("Loaded {} - {} {} glyphs, em_size {:g}\n",
               font3d_filename,
               font.meta.family.empty() ? font.meta.postscript_name
                                        : font.meta.family + " " + font.meta.style,
               font.glyphs.size(),
               font.meta.em_size);

    Font3DLayout engine(font, font_filename, !allow_mismatch);
    LayoutResult result = engine.layout(text, options);

    if (!path_svg_filename.empty() || !path_d.empty() || path_arc_given)
    {
      FlattenedPath path = !path_svg_filename.empty()
        ? FlattenedPath::from_svg_file(path_svg_filename, path_index)
        : !path_d.empty()
        ? FlattenedPath::from_svg_d(path_d)
        : FlattenedPath::circular_arc({0,0}, path_arc_radius,
                                      path_arc_start, path_arc_end);

      double text_width = result.max.x - result.min.x;
      if (text_width > path.length())
        fmt::print(stderr,
                   "Warning: the text is {:.2f} units wide but the path is\n"
                   "         only {:.2f} long; it will pile up at the end.\n",
                   text_width, path.length());

      bend_onto_path(result.glyphs, path, path_offset);
    }

    fmt::print("Laid out {} glyphs on {} line(s), advance box {:.2f} x {:.2f}\n",
               result.glyphs.size(),
               result.num_lines,
               result.max.x - result.min.x,
               result.max.y - result.min.y);

    if (!result.missing_codepoints.empty())
      fmt::print(stderr,
                 "Warning: the face has no glyph for {} codepoint(s): {:#x}\n",
                 result.missing_codepoints.size(),
                 fmt::join(result.missing_codepoints, " "));
    if (!result.unbaked_glyphs.empty())
      fmt::print(stderr,
                 "Warning: {} glyph(s) are not in the 3d font and will be\n"
                 "         missing from the output: {}\n"
                 "         Re-bake with a subset that covers them.\n",
                 result.unbaked_glyphs.size(),
                 fmt::join(result.unbaked_glyphs, " "));

    if (output_filename.ends_with(".stl"))
    {
      MultiMesh meshes = instantiate(font, result.glyphs);
      if (meshes.size() == 1)
      {
        save_stl(meshes[0], output_filename);
        fmt::print("Wrote {}\n", output_filename);
      }
      else
      {
        // stl carries no materials, so a multi level profile cannot go
        // into one file and still say which level is which. One file per
        // level is what a multi material slicer wants anyway. Report each
        // one: printing the name of the file that was asked for, which in
        // this branch is never written, is how this used to look like a
        // silent failure.
        string base = output_filename.substr(0, output_filename.size()-4);
        for (size_t i=0; i<meshes.size(); i++)
        {
          string name = fmt::format("{}_{}.stl", base, i);
          save_stl(meshes[i], name);
          fmt::print("Wrote {} ({})\n", name, font.slot(i).display_name());
        }
      }
    }
    else
    {
      font.save_layout_glb(result.glyphs, output_filename);
      fmt::print("Wrote {}\n", output_filename);
    }
  }
  catch (const std::exception& e)
  {
    die("{}", e.what());
  }

  return 0;
}
