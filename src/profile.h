//======================================================================
//  profile.h - 
//
//  Dov Grobgeld <dov.grobgeld@gmail.com>
//  Tue Jul 13 22:09:01 2021
//----------------------------------------------------------------------
#ifndef PROFILE_H
#define PROFILE_H

#include <array>
#include <vector>
#include <string>
#include <stdexcept>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>



using Vec2 = glm::dvec2;
using Vec3 = glm::dvec3;

// use for import and output vec2 as json
nlohmann::json as_json(const Vec2& v);
Vec2 from_json(const nlohmann::json& j);

class NodeData {
  public:
  int node_type;
  Vec2 xy;
  Vec2 control_before_xy;
  Vec2 control_after_xy;

  nlohmann::json as_json(){
    nlohmann::json j;
    j["node_type"] = node_type;
    j["xy"] = ::as_json(xy);
    j["control_before_xy"] = ::as_json(control_before_xy);
    j["control_after_xy"] = ::as_json(control_after_xy);
    return j;
  }

  void from_json(const nlohmann::json& j) {
    node_type = j["node_type"];
    xy = ::from_json(j["xy"]);
    control_before_xy = ::from_json(j["control_before_xy"]);
    control_after_xy = ::from_json(j["control_after_xy"]);
  }

  // Test if the node is "positive directional", i.e. it has no
  // negative directional tangents.
  bool is_positive_directional() const;
};

class LayerData : public std::vector<NodeData> {
  public:
  // What this level is called, e.g. "base" or "icing". Optional, and used
  // only to label the level - the bake keys everything off the layer's
  // index, so renaming a layer never moves geometry.
  std::string name;

  // How the level looks: null, a name in the material library, or an
  // inline material object. Resolved against a MaterialLibrary at bake
  // time rather than here, so that profile.cc stays free of any opinion
  // about what a material is.
  nlohmann::json material;

  // Whether this layer says anything beyond its nodes. A profile whose
  // layers all answer false is written back out in the original bare
  // array form, so adding this feature did not change any existing file.
  bool has_appearance() const {
    return !name.empty() || !material.is_null();
  }

  // Set the linear limit and calculate cache
  void set_linear_limit(double linear_limit=0.01);

  // Get a copy of the flat list. if the x_start and x end
  // are specified, then only return a list between these two
  // values.
  std::vector<Vec2> get_flat_list(double x_start = -INFINITY,
                                  double x_end = INFINITY);

  // The direction the curve leaves its last node in, taken from the
  // bezier's own terminal tangent rather than from the flattened polyline.
  // The roof past the end of the profile is extrapolated along this, and a
  // chord of a curve that is levelling out still slopes upwards, so a
  // profile authored to end horizontally would otherwise keep climbing.
  // Returns (0,0) when there is no direction to be had.
  Vec2 get_end_dir() const;

  // Get the intersection of the LayerData at the given x. Returns
  // whether the LayerData intersects.
  bool get_intersect_coord(double x,
                           // output
                           double& y);

  // Just the nodes, as the bare array that has always been written.
  nlohmann::json nodes_as_json() {
    nlohmann::json j = nlohmann::json::array();
    for (auto& v : *this)
        j.push_back(v.as_json());
    return j;
  }

  // The bare array when the layer is nothing but nodes, and
  // {name, material, nodes} when it is not.
  nlohmann::json as_json() {
    if (!has_appearance())
      return nodes_as_json();

    nlohmann::json j;
    if (!name.empty())
      j["name"] = name;
    if (!material.is_null())
      j["material"] = material;
    j["nodes"] = nodes_as_json();
    return j;
  }

  // Accepts both forms. Anything else throws.
  void from_json(const nlohmann::json& j) {
    name.clear();
    material = nlohmann::json();

    const nlohmann::json *nodes = &j;
    if (j.is_object())
    {
      if (auto it = j.find("name"); it != j.end() && it->is_string())
        name = it->get<std::string>();
      if (auto it = j.find("material"); it != j.end())
        material = *it;
      auto it = j.find("nodes");
      if (it == j.end() || !it->is_array())
        throw std::runtime_error(
          "a profile layer object needs a \"nodes\" array");
      nodes = &*it;
    }
    else if (!j.is_array())
      throw std::runtime_error(
        "a profile layer must be an array of nodes or an object with a "
        "\"nodes\" array");

    this->resize(nodes->size());
    for (size_t i=0; i<nodes->size(); i++)
      (*this)[i].from_json((*nodes)[i]);
  }

  // Test if positive monotone
  bool is_positive_monotone() const;

  private:
  std::vector<Vec2> flat_list;
};

class ProfileData : public std::vector<LayerData> {
  public:
  // Scale every node and control point. x is the distance in from the
  // outline and y is the height, so this widens or deepens the profile.
  // Any cached flat list is invalidated, so set_linear_limit() has to be
  // called again afterwards.
  void scale(double sx, double sy);

  void load_from_file(const std::string& filename);
  void save_to_file(const std::string& filename);
  std::string export_string();
  void load_from_string(const std::string& profile_string);

  // for debugging
  void save_flat_to_giv(const std::string& filename);

};

#endif /* PROFILE */
