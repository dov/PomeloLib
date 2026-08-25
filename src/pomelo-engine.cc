//======================================================================
//  pomelo-engine.cc - The font agnostic core of the pomelo algorithm.
//----------------------------------------------------------------------

#include "pomelo-engine.h"
#include "textrusion.h"
#include "cairo-flatten-by-bitmap.h"
#include "smooth-sharp-angles.h"

using namespace std;

MultiMesh paths_to_mesh(Cairo::RefPtr<Cairo::Surface> surface,
                        const BevelParams& params,
                        std::shared_ptr<Updater> updater,
                        const std::string& debug_dir,
                        std::string *giv_string)
{
  TeXtrusion textrusion(updater);
  textrusion.linear_limit = params.linear_limit;
  textrusion.m_debug_dir = debug_dir;

  // cairo_paths.giv is written to a fixed filename in the cwd, which is
  // both noise and a race when several meshes are built in parallel.
  textrusion.do_save_cairo_paths = false;

  auto cr = Cairo::Context::create(surface);

  // Flatten overlapping subpaths by rasterizing and retracing. The result
  // is written back into cr.
  FlattenByBitmap fb(cr->cobj());
  fb.max_image_width = params.max_image_width;
  fb.set_debug_dir(debug_dir);
  fb.flatten_by_bitmap(surface->cobj(), params.resolution);

  auto polys = textrusion.cairo_path_to_polygons(cr);
  auto polys_with_holes = textrusion.polys_to_polys_with_holes(polys);

  // Fillet the sharp corners before skeletonizing. See BevelParams.
  if (params.smooth_corners)
    polys_with_holes = smooth_acute_angles(params.smooth_radius,
                                           params.smooth_max_angle,
                                           polys_with_holes,
                                           params.smooth_num_points);

  auto phole_infos = textrusion.skeletonize(polys_with_holes);

  textrusion.use_profile_data = params.use_profile_data;
  textrusion.profile_data = params.profile_data;
  textrusion.profile_linear_limit = params.profile_linear_limit;
  textrusion.merge_distance = params.merge_distance;
  textrusion.zdepth = params.zdepth;
  textrusion.profile_radius = params.profile_radius;
  textrusion.profile_round_max_angle = params.profile_round_max_angle;
  textrusion.profile_num_radius_steps = params.profile_num_radius_steps;

  string giv;
  MultiMesh meshes = textrusion.skeleton_to_mesh(phole_infos,
                                                 // output
                                                 giv);
  if (giv_string)
    *giv_string = giv;

  return meshes;
}
