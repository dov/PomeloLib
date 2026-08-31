//======================================================================
//  svg-path-flatten.cc - Flatten a single svg path into a polyline that
//  supports arc-length queries.
//----------------------------------------------------------------------
#include "svg-path-flatten.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <fmt/core.h>
#include <glm/geometric.hpp>
#include "nanosvg.h"

using namespace std;

namespace {

glm::dvec2 to_vec2(const float *p) { return {p[0], p[1]}; }

glm::dvec2 eval_cubic(glm::dvec2 p0, glm::dvec2 c1, glm::dvec2 c2,
                      glm::dvec2 p1, double t)
{
  double mt = 1-t;
  double b0 = mt*mt*mt, b1 = 3*mt*mt*t, b2 = 3*mt*t*t, b3 = t*t*t;
  return p0*b0 + c1*b1 + c2*b2 + p1*b3;
}

// Samples per bezier segment. Fixed rather than adaptive: text-on-path is
// engraving/sign scale, not a zoomed-in vector illustration, so a coarse
// but simple flattening is plenty.
constexpr int kStepsPerSegment = 24;

void append_flattened_cubic(vector<glm::dvec2>& points, glm::dvec2 p0,
                            glm::dvec2 c1, glm::dvec2 c2, glm::dvec2 p1)
{
  for (int i=1; i<=kStepsPerSegment; i++)
    points.push_back(eval_cubic(p0, c1, c2, p1, (double)i/kStepsPerSegment));
}

const NSVGpath *find_nth_path(const NSVGimage *image, int index)
{
  int i = 0;
  for (NSVGshape *shape = image->shapes; shape; shape = shape->next)
    for (NSVGpath *path = shape->paths; path; path = path->next)
    {
      if (i == index)
        return path;
      i++;
    }
  return nullptr;
}

vector<glm::dvec2> flatten_points(const NSVGpath *path)
{
  if (!path || path->npts < 4)
    throw runtime_error("The svg path has no drawable segments");

  vector<glm::dvec2> points;
  points.push_back(to_vec2(path->pts));

  int segments = (path->npts - 1) / 3;
  for (int j=0; j<segments; j++)
  {
    const float *p = &path->pts[j*3*2];
    append_flattened_cubic(points, to_vec2(p), to_vec2(p+2), to_vec2(p+4),
                           to_vec2(p+6));
  }
  return points;
}

} // anonymous namespace

// --- FlattenedPath -----------------------------------------------------

void FlattenedPath::build_cumulative_lengths()
{
  m_cumulative_len.assign(m_points.size(), 0.0);
  for (size_t i=1; i<m_points.size(); i++)
    m_cumulative_len[i] = m_cumulative_len[i-1] +
                         glm::length(m_points[i] - m_points[i-1]);
}

FlattenedPath FlattenedPath::from_svg_file(const string& filename,
                                           int path_index)
{
  NSVGimage *image = nsvgParseFromFile(filename.c_str(), "px", 96);
  if (!image)
    throw runtime_error(fmt::format("Failed to parse svg file {}", filename));

  FlattenedPath fp;
  try
  {
    const NSVGpath *path = find_nth_path(image, path_index);
    if (!path)
      throw runtime_error(fmt::format(
        "{} has no path at index {}", filename, path_index));
    fp.m_points = flatten_points(path);
  }
  catch (...)
  {
    nsvgDelete(image);
    throw;
  }
  nsvgDelete(image);

  fp.build_cumulative_lengths();
  return fp;
}

FlattenedPath FlattenedPath::from_svg_d(const string& d)
{
  string doc = fmt::format("<svg><path d=\"{}\"/></svg>", d);
  NSVGimage *image = nsvgParse(doc.data(), "px", 96);
  if (!image)
    throw runtime_error(fmt::format("Failed to parse svg path '{}'", d));

  FlattenedPath fp;
  try
  {
    const NSVGpath *path = find_nth_path(image, 0);
    if (!path)
      throw runtime_error(fmt::format("'{}' has no drawable path", d));
    fp.m_points = flatten_points(path);
  }
  catch (...)
  {
    nsvgDelete(image);
    throw;
  }
  nsvgDelete(image);

  fp.build_cumulative_lengths();
  return fp;
}

FlattenedPath FlattenedPath::circular_arc(glm::dvec2 center, double radius,
                                          double start_deg, double end_deg)
{
  double start = start_deg*M_PI/180.0, end = end_deg*M_PI/180.0;
  double span = end - start;
  int steps = max(8, (int)ceil(fabs(span)/(2*M_PI)*360));

  FlattenedPath fp;
  for (int i=0; i<=steps; i++)
  {
    double a = start + span*i/steps;
    fp.m_points.push_back(center + radius*glm::dvec2(cos(a), sin(a)));
  }

  fp.build_cumulative_lengths();
  return fp;
}

double FlattenedPath::length() const
{
  return m_cumulative_len.empty() ? 0.0 : m_cumulative_len.back();
}

FlattenedPath::PointTangent FlattenedPath::at(double s) const
{
  if (m_points.size() < 2)
    throw runtime_error("The path has no length to place text on");

  s = clamp(s, 0.0, length());

  size_t i = 1;
  while (i+1 < m_cumulative_len.size() && m_cumulative_len[i] < s)
    i++;

  glm::dvec2 a = m_points[i-1], b = m_points[i];
  double seg_len = m_cumulative_len[i] - m_cumulative_len[i-1];
  double t = seg_len > 0 ? (s - m_cumulative_len[i-1])/seg_len : 0;

  return { a + (b-a)*t, atan2(b.y-a.y, b.x-a.x) };
}
