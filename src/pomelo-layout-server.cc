//======================================================================
// pomelo-layout-server - Lay strings out with a baked 3d font, on demand.
//
// A long lived companion to pomelo-layout-3d-text. Where that tool loads
// a 3d font, lays one string out and exits, this one loads the font once
// and then answers layout requests for as long as it is kept alive. It
// speaks line delimited json on stdin and stdout: one request object per
// line in, one response object per line out.
//
// It returns placements rather than geometry. The geometry is already in
// the glb that the caller loaded, so a host that keeps the glyph meshes
// around only needs the transforms, and those are small enough to ship on
// every keystroke. This is what the 3dtext-creator web app is built on:
// its python backend spawns one of these per font and pipes to it.
//
// Everything diagnostic goes to stderr, so that stdout stays a clean
// stream of response objects.
//----------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>
#include <vector>

#include <fmt/core.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "font3d.h"
#include "font3d-layout.h"

using namespace std;
using json = nlohmann::json;

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

static void usage()
{
  fmt::print(
    "pomelo-layout-server - answer 3d text layout requests on stdin\n"
    "\n"
    "Syntax:\n"
    "    pomelo-layout-server --font3d font3d.glb --font face.ttf\n"
    "\n"
    "Loads the 3d font and the face it was baked from, then reads one\n"
    "json request per line from stdin and writes one json response per\n"
    "line to stdout. The first line written is a ready object, so a\n"
    "caller can wait for the font to finish loading.\n"
    "\n"
    "Request fields, all optional except text:\n"
    "   id              Echoed back on the response\n"
    "   op              \"layout\" (default) or \"quit\"\n"
    "   text            The utf-8 to lay out; \\n separates lines\n"
    "   direction       auto (default), ltr or rtl\n"
    "   align           start (default), center or end\n"
    "   language        OpenType language tag, e.g. he or en\n"
    "   line_height     Baseline distance; 0 takes the font's own\n"
    "   letter_spacing  Extra advance after every glyph\n"
    "   features        Array of harfbuzz feature strings, e.g. [\"-liga\"]\n"
    "\n"
    "Options:\n"
    "   --font3d font3d.glb    The baked 3d font from pomelo-build-3d-font\n"
    "   --font face.ttf        The face the 3d font was baked from\n"
    "   --allow-font-mismatch  Warn instead of failing on a sha256 mismatch\n"
    "   --verbose              Log progress to stderr\n"
    );
}

// Pull a LayoutOptions out of a request, throwing on anything malformed so
// that the caller gets a diagnostic rather than a silently wrong layout.
static LayoutOptions options_from_json(const json& req)
{
  LayoutOptions options;

  if (req.contains("direction") && !req["direction"].is_null())
  {
    string d = req["direction"].get<string>();
    if (d == "auto")      options.direction = TextDirection::Auto;
    else if (d == "ltr")  options.direction = TextDirection::LTR;
    else if (d == "rtl")  options.direction = TextDirection::RTL;
    else throw runtime_error(fmt::format(
      "direction must be auto, ltr or rtl, got '{}'", d));
  }

  if (req.contains("align") && !req["align"].is_null())
  {
    string a = req["align"].get<string>();
    if (a == "start")       options.align = TextAlign::Start;
    else if (a == "center") options.align = TextAlign::Center;
    else if (a == "end")    options.align = TextAlign::End;
    else throw runtime_error(fmt::format(
      "align must be start, center or end, got '{}'", a));
  }

  if (req.contains("language") && !req["language"].is_null())
    options.language = req["language"].get<string>();
  if (req.contains("line_height") && !req["line_height"].is_null())
    options.line_height = req["line_height"].get<double>();
  if (req.contains("letter_spacing") && !req["letter_spacing"].is_null())
    options.letter_spacing = req["letter_spacing"].get<double>();
  if (req.contains("features") && !req["features"].is_null())
    for (const json& f : req["features"])
      options.features.push_back(f.get<string>());

  return options;
}

static json layout_to_json(const LayoutResult& result)
{
  json placements = json::array();
  for (const PlacedGlyph& p : result.glyphs)
    placements.push_back({
      {"glyph_id", p.glyph_id},
      {"pen", {p.pen.x, p.pen.y, p.pen.z}},
      {"cluster", p.cluster},
      {"line", p.line},
    });

  return {
    {"placements", placements},
    {"num_lines", result.num_lines},
    {"min", {result.min.x, result.min.y}},
    {"max", {result.max.x, result.max.y}},
    {"missing_codepoints", result.missing_codepoints},
    {"unbaked_glyphs", result.unbaked_glyphs},
  };
}

// One line of json out, flushed, because the caller is blocked reading it.
static void respond(const json& response)
{
  fmt::print("{}\n", response.dump());
  fflush(stdout);
}

int main(int argc, char **argv)
{
  int argp = 1;
  vector<string> positional;

  string font3d_filename;
  string font_filename;
  bool allow_mismatch = false;
  bool verbose = false;

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
    CASE("--allow-font-mismatch") { allow_mismatch = true; continue; }
    CASE("--verbose") { verbose = true; continue; }

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

  // stdout belongs to the protocol, so the log has to go to stderr.
  auto color_sink = std::make_shared<spdlog::sinks::ansicolor_stderr_sink_mt>();
  auto logger = std::make_shared<spdlog::logger>("pomelo", color_sink);
  logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
  logger->set_level(verbose ? spdlog::level::info : spdlog::level::warn);
  spdlog::set_default_logger(logger);

  Font3D font;
  unique_ptr<Font3DLayout> engine;
  try
  {
    font = Font3D::load_glb(font3d_filename);
    engine = make_unique<Font3DLayout>(font, font_filename, !allow_mismatch);
  }
  catch (const std::exception& e)
  {
    // Report the failure on the protocol stream too. A caller that is
    // waiting for the ready line would otherwise just see the pipe close.
    respond({{"ok", false}, {"ready", false}, {"error", e.what()}});
    die("{}", e.what());
  }

  // The glyph ids that actually carry geometry. A caller can use this to
  // tell "the face has no such glyph" from "it was outside the subset".
  json baked = json::array();
  for (const Glyph3D *g : font.glyphs_sorted_for_manifest())
    if (g->has_geometry())
      baked.push_back(g->glyph_id);

  respond({
    {"ok", true},
    {"ready", true},
    {"font3d", font3d_filename},
    {"font", font_filename},
    {"family", font.meta.family},
    {"style", font.meta.style},
    {"postscript_name", font.meta.postscript_name},
    {"em_size", font.meta.em_size},
    {"ascender", font.meta.ascender},
    {"descender", font.meta.descender},
    {"line_height", font.meta.line_height},
    {"num_glyphs", font.glyphs.size()},
    {"baked_glyph_ids", baked},
  });

  string line;
  while (std::getline(std::cin, line))
  {
    // Blank keepalive lines are not an error.
    if (line.find_first_not_of(" \t\r\n") == string::npos)
      continue;

    json id;   // null unless the request carried one
    try
    {
      json req = json::parse(line);
      if (!req.is_object())
        throw runtime_error("a request must be a json object");
      if (req.contains("id"))
        id = req["id"];

      string op = req.value("op", "layout");
      if (op == "quit")
      {
        respond({{"id", id}, {"ok", true}, {"bye", true}});
        break;
      }
      if (op != "layout")
        throw runtime_error(fmt::format("unknown op '{}'", op));

      if (!req.contains("text") || req["text"].is_null())
        throw runtime_error("a layout request needs a text field");

      LayoutResult result = engine->layout(req["text"].get<string>(),
                                           options_from_json(req));
      json response = layout_to_json(result);
      response["id"] = id;
      response["ok"] = true;
      respond(response);
    }
    catch (const std::exception& e)
    {
      // A bad request kills the request, not the server.
      respond({{"id", id}, {"ok", false}, {"error", e.what()}});
    }
  }

  return 0;
}
