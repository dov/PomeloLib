//======================================================================
//  svg-path-flatten.h - Flatten a single svg path into a polyline that
//  supports arc-length queries.
//
//  Used to lay text out along an arbitrary curve: a FlattenedPath knows
//  nothing about text, and text layout knows nothing about svg - the two
//  meet only through point()/tangent lookups by arc length.
//----------------------------------------------------------------------
#ifndef SVG_PATH_FLATTEN_H
#define SVG_PATH_FLATTEN_H

#include <string>
#include <vector>
#include <glm/vec2.hpp>

class FlattenedPath {
  public:
  // The path_index'th <path> in the document, counting every path of
  // every shape in document order. Throws if the file can't be parsed or
  // has no path at that index.
  static FlattenedPath from_svg_file(const std::string& filename,
                                     int path_index = 0);

  // A literal svg path 'd' expression, e.g. "M0,0 C10,0 20,10 30,10".
  static FlattenedPath from_svg_d(const std::string& d);

  // A circular arc, the common case for ring/badge text. Degrees, 0
  // pointing along +x, increasing counterclockwise; end_deg - start_deg
  // of 360 is a full circle.
  static FlattenedPath circular_arc(glm::dvec2 center, double radius,
                                    double start_deg = 0,
                                    double end_deg = 360);

  double length() const;

  struct PointTangent {
    glm::dvec2 point;
    double angle = 0;   // radians, atan2 of the local tangent direction
  };

  // The point and tangent angle at arc-length distance s along the path.
  // s is clamped to [0, length()]. Throws if the path has no length.
  PointTangent at(double s) const;

  private:
  void build_cumulative_lengths();

  std::vector<glm::dvec2> m_points;       // flattened polyline
  std::vector<double> m_cumulative_len;   // m_cumulative_len[0] == 0
};

#endif /* SVG_PATH_FLATTEN */
