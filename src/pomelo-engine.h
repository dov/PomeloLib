//======================================================================
//  pomelo-engine.h - The font agnostic core of the pomelo algorithm.
//
//  This is the "flat outlines in, beveled mesh out" half of pomelo, with
//  the pango and svg front ends factored out. Both pomelo-cli and the 3d
//  font baker drive the algorithm through this interface.
//
//  Deliberately free of pango, gtk and CGAL so that it can be included by
//  tools that only care about geometry.
//----------------------------------------------------------------------
#ifndef POMELO_ENGINE_H
#define POMELO_ENGINE_H

#include <cmath>
#include <memory>
#include <string>
#include <cairomm/context.h>
#include <cairomm/surface.h>
#include "mesh.h"
#include "profile.h"

class Updater;

// Everything needed to turn a flat outline into a beveled 3d mesh. All
// lengths are in the same units as the incoming cairo paths.
struct BevelParams {
  double zdepth = 10;
  double profile_radius = 3;
  double profile_round_max_angle = M_PI/2;
  int    profile_num_radius_steps = 5;
  double linear_limit = 500;

  // When use_profile_data is set the bezier profile is used instead of
  // the simple round profile described by the parameters above.
  bool use_profile_data = false;
  ProfileData profile_data;

  // Tolerance the bezier profile is flattened to. Absolute, so it has to
  // be scaled alongside a scaled profile_data.
  double profile_linear_limit = 0.01;

  // Distance under which coincident vertices are welded at the end. Also
  // absolute: if it is tighter than the algorithm's own numerical noise,
  // vertices that ought to be one stay separate and the surface cracks.
  double merge_distance = 1e-4;

  // The paths are rasterized and retraced before skeletonizing, which is
  // what resolves self overlapping outlines. resolution is in pixels per
  // path unit; max_image_width caps the raster (-1 leaves it uncapped).
  double resolution = 100;
  int    max_image_width = -1;

  // Replace sharp corners of the outline with a fillet before
  // skeletonizing. A straight skeleton emits long bisector spikes from
  // sharp vertices, which surface as creases and notches in the roof;
  // rounding the corners first tames them, at the cost of a good many more
  // skeleton regions.
  //
  // A vertex is filleted when its interior angle is below
  // smooth_max_angle, so a larger value smooths more. Note that the
  // outline reaching this point has been retraced from a raster and so is
  // already a dense polyline: much above 135 degrees and nearly every
  // traced vertex qualifies, which multiplies the triangle count for no
  // real gain.
  bool   smooth_corners = false;
  double smooth_radius = 0.5;          // in path units
  double smooth_max_angle = 135*M_PI/180;
  int    smooth_num_points = 16;
};

// Turn the filled paths held by a cairo (recording) surface into a
// beveled mesh - one Mesh per profile layer.
//
// Note that this consumes the paths through a rasterize-and-retrace step,
// so the surface should hold the outlines of a single logical object,
// e.g. one glyph or one run of text.
MultiMesh paths_to_mesh(Cairo::RefPtr<Cairo::Surface> surface,
                        const BevelParams& params,
                        std::shared_ptr<Updater> updater = nullptr,
                        const std::string& debug_dir = std::string(),
                        std::string *giv_string = nullptr);

#endif /* POMELO_ENGINE */
