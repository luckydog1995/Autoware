/*
 *  Copyright (c) 2015, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <geometry_msgs/PoseStamped.h>
#include <waypoint_follower/lane.h>
#include <visualization_msgs/MarkerArray.h>
#include <vector_map/vector_map.h>

#include <vector_map_server/GetRoadEdge.h>
#include <vector_map_server/GetGutter.h>
#include <vector_map_server/GetCurb.h>
#include <vector_map_server/GetWhiteLine.h>
#include <vector_map_server/GetStopLine.h>
#include <vector_map_server/GetZebraZone.h>
#include <vector_map_server/GetCrossWalk.h>
#include <vector_map_server/GetRoadMark.h>
#include <vector_map_server/GetRoadPole.h>
#include <vector_map_server/GetRoadSign.h>
#include <vector_map_server/GetSignal.h>
#include <vector_map_server/GetStreetLight.h>
#include <vector_map_server/GetUtilityPole.h>
#include <vector_map_server/GetGuardRail.h>
#include <vector_map_server/GetSideWalk.h>
#include <vector_map_server/GetDriveOnPortion.h>
#include <vector_map_server/GetCrossRoad.h>
#include <vector_map_server/GetSideStrip.h>
#include <vector_map_server/GetCurveMirror.h>
#include <vector_map_server/GetWall.h>
#include <vector_map_server/GetFence.h>
#include <vector_map_server/GetRailCrossing.h>

using vector_map::VectorMap;
using vector_map::Category;
using vector_map::Color;
using vector_map::Filter;
using vector_map::Key;

using vector_map::Point;
using vector_map::Vector;
using vector_map::Line;
using vector_map::Area;
using vector_map::Pole;
using vector_map::Box;
using vector_map::DTLane;
using vector_map::Node;
using vector_map::Lane;
using vector_map::WayArea;
using vector_map::RoadEdge;
using vector_map::Gutter;
using vector_map::Curb;
using vector_map::WhiteLine;
using vector_map::StopLine;
using vector_map::ZebraZone;
using vector_map::CrossWalk;
using vector_map::RoadMark;
using vector_map::RoadPole;
using vector_map::RoadSign;
using vector_map::Signal;
using vector_map::StreetLight;
using vector_map::UtilityPole;
using vector_map::GuardRail;
using vector_map::SideWalk;
using vector_map::DriveOnPortion;
using vector_map::CrossRoad;
using vector_map::SideStrip;
using vector_map::CurveMirror;
using vector_map::Wall;
using vector_map::Fence;
using vector_map::RailCrossing;

using vector_map::convertGeomPointToPoint;
using vector_map::isValidMarker;

namespace
{
bool isBranchingLane(const Lane& lane)
{
  return lane.jct == Lane::LEFT_BRANCHING || lane.jct == Lane::RIGHT_BRANCHING || lane.jct == Lane::COMPOSITION;
}

bool isMergingLane(const Lane& lane)
{
  return lane.jct == Lane::LEFT_MERGING || lane.jct == Lane::RIGHT_MERGING || lane.jct == Lane::COMPOSITION;
}

double computeDistance(const Point& p1, const Point& p2)
{
  return std::hypot(p2.bx - p1.bx, p2.ly - p1.ly); // XXX: don't consider z axis
}

double computeAngle(const Point& p1, const Point& p2)
{
  return std::atan2(p2.ly - p1.ly, p2.bx - p1.bx); // XXX: don't consider z axis
}

double computeScore(const Point& bp1, const Point& bp2, const Point& p1, const Point& p2, double radius)
{
  double distance_score = computeDistance(bp1, p1);
  distance_score = 50 * (radius - distance_score) / radius;
  double angle_score = computeAngle(p1, p2) - computeAngle(bp1, bp2);
  angle_score = 50 * (M_PI - std::fabs(angle_score)) / M_PI;
  return distance_score + angle_score;
}

Point findStartPoint(const VectorMap& vmap, const Lane& lane)
{
  Point start_point;
  Node node = vmap.findByKey(Key<Node>(lane.bnid));
  if (node.nid == 0)
    return start_point;
  return vmap.findByKey(Key<Point>(node.pid));
}

Point findEndPoint(const VectorMap& vmap, const Lane& lane)
{
  Point end_point;
  Node node = vmap.findByKey(Key<Node>(lane.fnid));
  if (node.nid == 0)
    return end_point;
  return vmap.findByKey(Key<Point>(node.pid));
}

std::vector<Point> findStartPoints(const VectorMap& vmap)
{
  std::vector<Point> start_points;
  for (const auto& lane : vmap.findByFilter([](const Lane& lane){return true;}))
  {
    if (lane.lnid == 0)
      continue;
    Node node = vmap.findByKey(Key<Node>(lane.bnid));
    if (node.nid == 0)
      continue;
    Point point = vmap.findByKey(Key<Point>(node.pid));
    if (point.pid == 0)
      continue;
    start_points.push_back(point);
  }
  return start_points;
}

std::vector<Point> findEndPoints(const VectorMap& vmap)
{
  std::vector<Point> end_points;
  for (const auto& lane : vmap.findByFilter([](const Lane& lane){return true;}))
  {
    if (lane.lnid == 0)
      continue;
    Node node = vmap.findByKey(Key<Node>(lane.fnid));
    if (node.nid == 0)
      continue;
    Point point = vmap.findByKey(Key<Point>(node.pid));
    if (point.pid == 0)
      continue;
    end_points.push_back(point);
  }
  return end_points;
}

Point findNearestPoint(const std::vector<Point>& points, const Point& base_point)
{
  Point nearest_point;
  double min_distance = DBL_MAX;
  for (const auto& point : points)
  {
    double distance = computeDistance(base_point, point);
    if (distance <= min_distance)
    {
      nearest_point = point;
      min_distance = distance;
    }
  }
  return nearest_point;
}

std::vector<Point> findNearPoints(const std::vector<Point>& points, const Point& base_point, double radius)
{
  std::vector<Point> near_points;
  for (const auto& point : points)
  {
    if (computeDistance(base_point, point) <= radius)
      near_points.push_back(point);
  }
  return near_points;
}

std::vector<Lane> findLanesByStartPoint(const VectorMap& vmap, const Point& start_point)
{
  std::vector<Lane> lanes;
  for (const auto& node : vmap.findByFilter([&start_point](const Node& node){return node.pid == start_point.pid;}))
  {
    if (node.nid == 0)
      continue;
    for (const auto& lane : vmap.findByFilter([&node](const Lane& lane){return lane.bnid == node.nid;}))
    {
      if (lane.lnid == 0)
        continue;
      lanes.push_back(lane);
    }
  }
  return lanes;
}

std::vector<Lane> findLanesByEndPoint(const VectorMap& vmap, const Point& end_point)
{
  std::vector<Lane> lanes;
  for (const auto& node : vmap.findByFilter([&end_point](const Node& node){return node.pid == end_point.pid;}))
  {
    if (node.nid == 0)
      continue;
    for (const auto& lane : vmap.findByFilter([&node](const Lane& lane){return lane.fnid == node.nid;}))
    {
      if (lane.lnid == 0)
        continue;
      lanes.push_back(lane);
    }
  }
  return lanes;
}

Lane findStartLane(const VectorMap& vmap, const std::vector<Point>& points, double radius)
{
  Lane start_lane;
  if (points.size() < 2)
    return start_lane;

  Point bp1 = points[0];
  Point bp2 = points[1];
  double max_score = -DBL_MAX;
  for (const auto& p1 : findNearPoints(findStartPoints(vmap), bp1, radius))
  {
    for (const auto& lane : findLanesByStartPoint(vmap, p1))
    {
      if (lane.lnid == 0)
        continue;
      Point p2 = findEndPoint(vmap, lane);
      if (p2.pid == 0)
        continue;
      double score = computeScore(bp1, bp2, p1, p2, radius);
      if (score >= max_score)
      {
        start_lane = lane;
        max_score = score;
      }
    }
  }
  return start_lane;
}

Lane findEndLane(const VectorMap& vmap, const std::vector<Point>& points, double radius)
{
  Lane end_lane;
  if (points.size() < 2)
    return end_lane;

  Point bp1 = points[points.size() - 2];
  Point bp2 = points[points.size() - 1];
  double max_score = -DBL_MAX;
  for (const auto& p2 : findNearPoints(findEndPoints(vmap), bp2, radius))
  {
    for (const auto& lane : findLanesByEndPoint(vmap, p2))
    {
      if (lane.lnid == 0)
        continue;
      Point p1 = findStartPoint(vmap, lane);
      if (p1.pid == 0)
        continue;
      double score = computeScore(bp2, bp1, p2, p1, radius);
      if (score >= max_score)
      {
        end_lane = lane;
        max_score = score;
      }
    }
  }
  return end_lane;
}

Lane findNearestLane(const VectorMap& vmap, const std::vector<Lane>& lanes, const Point& base_point)
{
  Lane nearest_lane;
  double min_distance = DBL_MAX;
  for (const auto& lane : lanes)
  {
    Point start_point = findStartPoint(vmap, lane);
    if (start_point.pid == 0)
      continue;
    double distance = computeDistance(base_point, start_point);
    if (distance <= min_distance)
    {
      nearest_lane = lane;
      min_distance = distance;
    }
  }
  return nearest_lane;
}

std::vector<Lane> findNearLanes(const VectorMap& vmap, const std::vector<Lane>& lanes, const Point& base_point,
                                double radius)
{
  std::vector<Lane> near_lanes;
  for (const auto& lane : lanes)
  {
    Point start_point = findStartPoint(vmap, lane);
    if (start_point.pid == 0)
      continue;
    if (computeDistance(base_point, start_point) <= radius)
      near_lanes.push_back(lane);
  }
  return near_lanes;
}

std::vector<Lane> createFineLanes(const VectorMap& vmap, const waypoint_follower::lane& waypoints, double radius,
                                  int loops)
{
  std::vector<Lane> null_lanes;

  std::vector<Point> coarse_points;
  for (const auto& waypoint : waypoints.waypoints)
    coarse_points.push_back(convertGeomPointToPoint(waypoint.pose.pose.position));

  Lane start_lane = findStartLane(vmap, coarse_points, radius);
  if (start_lane.lnid == 0)
    return null_lanes;

  Lane end_lane = findEndLane(vmap, coarse_points, radius);
  if (end_lane.lnid == 0)
    return null_lanes;

  std::vector<Lane> fine_lanes;
  Lane current_lane = start_lane;
  for (int i = 0; i < loops; ++i)
  {
    fine_lanes.push_back(current_lane);
    if (current_lane.lnid == end_lane.lnid)
      return fine_lanes;

    if (isBranchingLane(current_lane))
    {
      Point fine_p1 = findEndPoint(vmap, current_lane);
      if (fine_p1.pid == 0)
        return null_lanes;

      Point coarse_p1 = findNearestPoint(coarse_points, fine_p1); // certainly succeed

      if (computeDistance(fine_p1, coarse_p1) > radius)
        return null_lanes;

      Point coarse_p2;
      double distance = -DBL_MAX;
      for (const auto& coarse_point : coarse_points)
      {
        if (distance == -DBL_MAX)
        {
          if (coarse_point.bx == coarse_p1.bx && coarse_point.ly == coarse_p1.ly) // XXX: don't consider z axis
            distance = 0;
          continue;
        }
        coarse_p2 = coarse_point;
        distance = computeDistance(coarse_p2, coarse_p1);
        if (distance > radius)
          break;
      }
      if (distance <= 0)
        return null_lanes;

      double max_score = -DBL_MAX;
      Filter<Lane> is_next_lane = [&current_lane](const Lane& lane)
        {
          return lane.lnid == current_lane.flid || lane.lnid == current_lane.flid2 ||
                 lane.lnid == current_lane.flid3 || lane.lnid == current_lane.flid4;
        };
      for (const auto& lane : vmap.findByFilter(is_next_lane))
      {
        Lane next_lane = lane;
        if (next_lane.lnid == 0)
          continue;
        Point next_point = findEndPoint(vmap, next_lane);
        if (next_point.pid == 0)
          continue;
        Point fine_p2 = next_point;
        while (computeDistance(fine_p2, fine_p1) <= radius && !isBranchingLane(next_lane) && next_lane.flid != 0)
        {
          next_lane = vmap.findByKey(Key<Lane>(next_lane.flid));;
          if (next_lane.lnid == 0)
            break;
          next_point = findEndPoint(vmap, next_lane);
          if (next_point.pid == 0)
            break;
          fine_p2 = next_point;
        }
        double score = computeScore(fine_p1, fine_p2, coarse_p1, coarse_p2, radius);
        if (score >= max_score)
        {
          current_lane = lane;
          max_score = score;
        }
      }
      if (max_score == -DBL_MAX)
        return null_lanes;
    }
    else
      current_lane = vmap.findByKey(Key<Lane>(current_lane.flid));;
    if (current_lane.lnid == 0)
      return null_lanes;
  }

  return null_lanes;
}

class VectorMapServer
{
private:
  VectorMap vmap_;
  double radius_;
  int loops_;

  bool debug_;
  visualization_msgs::MarkerArray marker_array_;
  ros::Publisher marker_array_pub_;

  std::vector<Lane> createTravelingRoute(const geometry_msgs::PoseStamped& pose,
                                         const waypoint_follower::lane& waypoints)
  {
    std::vector<Lane> null_lanes;

    std::vector<Lane> fine_lanes = createFineLanes(vmap_, waypoints, radius_, loops_);
    if (fine_lanes.empty())
      return null_lanes;

    Lane nearest_lane = findNearestLane(vmap_, fine_lanes, convertGeomPointToPoint(pose.pose.position));
    if (nearest_lane.lnid == 0)
      return null_lanes;

    std::vector<Lane> traveling_route;
    visualization_msgs::MarkerArray marker_array_buffer;
    int id = 0;
    bool future = false;
    for (const auto& fine_lane : fine_lanes)
    {
      if (fine_lane.lnid == nearest_lane.lnid)
        future = true;
      if (future)
      {
        if (debug_)
        {
          Point start_point = findStartPoint(vmap_, fine_lane);
          if (start_point.pid != 0)
          {
            visualization_msgs::Marker marker = createPointMarker("traveling_route", id++, Color::YELLOW,
                                                                  start_point);
            if (isValidMarker(marker))
              marker_array_buffer.markers.push_back(marker);
          }
          Point end_point = findEndPoint(vmap_, fine_lane);
          if (end_point.pid != 0)
          {
            visualization_msgs::Marker marker = createPointMarker("traveling_route", id++, Color::YELLOW,
                                                                  end_point);
            if (isValidMarker(marker))
              marker_array_buffer.markers.push_back(marker);
          }
        }
        traveling_route.push_back(fine_lane);
      }
    }

    if (debug_)
    {
      if (!marker_array_.markers.empty())
      {
        for (auto& marker : marker_array_.markers)
          marker.action = visualization_msgs::Marker::DELETE;
        marker_array_pub_.publish(marker_array_); // clear previous marker
      }
      marker_array_ = marker_array_buffer;
      marker_array_pub_.publish(marker_array_);
    }

    return traveling_route;
  }

public:
  explicit VectorMapServer(ros::NodeHandle& nh)
  {
    vmap_.subscribe(nh, Category::ALL, ros::Duration(0));
    nh.param<double>("vector_map_server/radius", radius_, 10);
    nh.param<int>("vector_map_server/loops", loops_, 10000);
    nh.param<bool>("vector_map_server/debug", debug_, false);
    if (debug_)
      marker_array_pub_ = nh.advertise<visualization_msgs::MarkerArray>("vector_map_server", 10, true);
  }

  bool getRoadEdge(vector_map_server::GetRoadEdge::Request& request,
                   vector_map_server::GetRoadEdge::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& road_edge : vmap_.findByFilter(
           [&lane](const RoadEdge& road_edge){return road_edge.linkid == lane.lnid;}))
        response.objects.data.push_back(road_edge);
    }
    return true;
  }

  bool getGutter(vector_map_server::GetGutter::Request& request,
                 vector_map_server::GetGutter::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& gutter : vmap_.findByFilter(
           [&lane](const Gutter& gutter){return gutter.linkid == lane.lnid;}))
        response.objects.data.push_back(gutter);
    }
    return true;
  }

  bool getCurb(vector_map_server::GetCurb::Request& request,
               vector_map_server::GetCurb::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& curb : vmap_.findByFilter(
           [&lane](const Curb& curb){return curb.linkid == lane.lnid;}))
        response.objects.data.push_back(curb);
    }
    return true;
  }

  bool getWhiteLine(vector_map_server::GetWhiteLine::Request& request,
                    vector_map_server::GetWhiteLine::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& white_line : vmap_.findByFilter(
           [&lane](const WhiteLine& white_line){return white_line.linkid == lane.lnid;}))
        response.objects.data.push_back(white_line);
    }
    return true;
  }

  bool getStopLine(vector_map_server::GetStopLine::Request& request,
                   vector_map_server::GetStopLine::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& stop_line : vmap_.findByFilter(
           [&lane](const StopLine& stop_line){return stop_line.linkid == lane.lnid;}))
        response.objects.data.push_back(stop_line);
    }
    return true;
  }

  bool getZebraZone(vector_map_server::GetZebraZone::Request& request,
                    vector_map_server::GetZebraZone::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& zebra_zone : vmap_.findByFilter(
           [&lane](const ZebraZone& zebra_zone){return zebra_zone.linkid == lane.lnid;}))
        response.objects.data.push_back(zebra_zone);
    }
    return true;
  }

  bool getCrossWalk(vector_map_server::GetCrossWalk::Request& request,
                    vector_map_server::GetCrossWalk::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& cross_walk : vmap_.findByFilter(
           [&lane](const CrossWalk& cross_walk){return cross_walk.linkid == lane.lnid;}))
        response.objects.data.push_back(cross_walk);
    }
    return true;
  }

  bool getRoadMark(vector_map_server::GetRoadMark::Request& request,
                   vector_map_server::GetRoadMark::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& road_mark : vmap_.findByFilter(
           [&lane](const RoadMark& road_mark){return road_mark.linkid == lane.lnid;}))
        response.objects.data.push_back(road_mark);
    }
    return true;
  }

  bool getRoadPole(vector_map_server::GetRoadPole::Request& request,
                   vector_map_server::GetRoadPole::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& road_pole : vmap_.findByFilter(
           [&lane](const RoadPole& road_pole){return road_pole.linkid == lane.lnid;}))
        response.objects.data.push_back(road_pole);
    }
    return true;
  }

  bool getRoadSign(vector_map_server::GetRoadSign::Request& request,
                   vector_map_server::GetRoadSign::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& road_sign : vmap_.findByFilter(
           [&lane](const RoadSign& road_sign){return road_sign.linkid == lane.lnid;}))
        response.objects.data.push_back(road_sign);
    }
    return true;
  }

  bool getSignal(vector_map_server::GetSignal::Request& request,
                 vector_map_server::GetSignal::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& signal : vmap_.findByFilter(
           [&lane](const Signal& signal){return signal.linkid == lane.lnid;}))
        response.objects.data.push_back(signal);
    }
    return true;
  }

  bool getStreetLight(vector_map_server::GetStreetLight::Request& request,
                      vector_map_server::GetStreetLight::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& street_light : vmap_.findByFilter(
           [&lane](const StreetLight& street_light){return street_light.linkid == lane.lnid;}))
        response.objects.data.push_back(street_light);
    }
    return true;
  }

  bool getUtilityPole(vector_map_server::GetUtilityPole::Request& request,
                      vector_map_server::GetUtilityPole::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& utility_pole : vmap_.findByFilter(
           [&lane](const UtilityPole& utility_pole){return utility_pole.linkid == lane.lnid;}))
        response.objects.data.push_back(utility_pole);
    }
    return true;
  }

  bool getGuardRail(vector_map_server::GetGuardRail::Request& request,
                    vector_map_server::GetGuardRail::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& guard_rail : vmap_.findByFilter(
           [&lane](const GuardRail& guard_rail){return guard_rail.linkid == lane.lnid;}))
        response.objects.data.push_back(guard_rail);
    }
    return true;
  }

  bool getSideWalk(vector_map_server::GetSideWalk::Request& request,
                   vector_map_server::GetSideWalk::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& side_walk : vmap_.findByFilter(
           [&lane](const SideWalk& side_walk){return side_walk.linkid == lane.lnid;}))
        response.objects.data.push_back(side_walk);
    }
    return true;
  }

  bool getDriveOnPortion(vector_map_server::GetDriveOnPortion::Request& request,
                         vector_map_server::GetDriveOnPortion::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& drive_on_portion : vmap_.findByFilter(
           [&lane](const DriveOnPortion& drive_on_portion){return drive_on_portion.linkid == lane.lnid;}))
        response.objects.data.push_back(drive_on_portion);
    }
    return true;
  }

  bool getCrossRoad(vector_map_server::GetCrossRoad::Request& request,
                    vector_map_server::GetCrossRoad::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& cross_road : vmap_.findByFilter(
           [&lane](const CrossRoad& cross_road){return cross_road.linkid == lane.lnid;}))
        response.objects.data.push_back(cross_road);
    }
    return true;
  }

  bool getSideStrip(vector_map_server::GetSideStrip::Request& request,
                    vector_map_server::GetSideStrip::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& side_strip : vmap_.findByFilter(
           [&lane](const SideStrip& side_strip){return side_strip.linkid == lane.lnid;}))
        response.objects.data.push_back(side_strip);
    }
    return true;
  }

  bool getCurveMirror(vector_map_server::GetCurveMirror::Request& request,
                      vector_map_server::GetCurveMirror::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& curve_mirror : vmap_.findByFilter(
           [&lane](const CurveMirror& curve_mirror){return curve_mirror.linkid == lane.lnid;}))
        response.objects.data.push_back(curve_mirror);
    }
    return true;
  }

  bool getWall(vector_map_server::GetWall::Request& request,
               vector_map_server::GetWall::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& wall : vmap_.findByFilter(
           [&lane](const Wall& wall){return wall.linkid == lane.lnid;}))
        response.objects.data.push_back(wall);
    }
    return true;
  }

  bool getFence(vector_map_server::GetFence::Request& request,
                vector_map_server::GetFence::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& fence : vmap_.findByFilter(
           [&lane](const Fence& fence){return fence.linkid == lane.lnid;}))
        response.objects.data.push_back(fence);
    }
    return true;
  }

  bool getRailCrossing(vector_map_server::GetRailCrossing::Request& request,
                       vector_map_server::GetRailCrossing::Response& response)
  {
    std::vector<Lane> traveling_route = createTravelingRoute(request.pose, request.waypoints);
    if (traveling_route.empty())
      return false;
    response.objects.header.frame_id = "map";
    for (const auto& lane : traveling_route)
    {
      for (const auto& rail_crossing : vmap_.findByFilter(
           [&lane](const RailCrossing& rail_crossing){return rail_crossing.linkid == lane.lnid;}))
        response.objects.data.push_back(rail_crossing);
    }
    return true;
  }
};
} // namespace

int main(int argc, char **argv)
{
  ros::init(argc, argv, "vector_map_server");

  ros::NodeHandle nh;
  VectorMapServer vms(nh);

  ros::ServiceServer get_road_edge_srv = nh.advertiseService("vector_map_server/get_road_edge",
                                                             &VectorMapServer::getRoadEdge, &vms);
  ros::ServiceServer get_gutter_srv = nh.advertiseService("vector_map_server/get_gutter",
                                                          &VectorMapServer::getGutter, &vms);
  ros::ServiceServer get_curb_srv = nh.advertiseService("vector_map_server/get_curb",
                                                        &VectorMapServer::getCurb, &vms);
  ros::ServiceServer get_white_line_srv = nh.advertiseService("vector_map_server/get_white_line",
                                                              &VectorMapServer::getWhiteLine, &vms);
  ros::ServiceServer get_stop_line_srv = nh.advertiseService("vector_map_server/get_stop_line",
                                                             &VectorMapServer::getStopLine, &vms);
  ros::ServiceServer get_zebra_zone_srv = nh.advertiseService("vector_map_server/get_zebra_zone",
                                                              &VectorMapServer::getZebraZone, &vms);
  ros::ServiceServer get_cross_walk_srv = nh.advertiseService("vector_map_server/get_cross_walk",
                                                              &VectorMapServer::getCrossWalk, &vms);
  ros::ServiceServer get_road_mark_srv = nh.advertiseService("vector_map_server/get_road_mark",
                                                             &VectorMapServer::getRoadMark, &vms);
  ros::ServiceServer get_road_pole_srv = nh.advertiseService("vector_map_server/get_road_pole",
                                                             &VectorMapServer::getRoadPole, &vms);
  ros::ServiceServer get_road_sign_srv = nh.advertiseService("vector_map_server/get_road_sign",
                                                             &VectorMapServer::getRoadSign, &vms);
  ros::ServiceServer get_signal_srv = nh.advertiseService("vector_map_server/get_signal",
                                                          &VectorMapServer::getSignal, &vms);
  ros::ServiceServer get_street_light_srv = nh.advertiseService("vector_map_server/get_street_light",
                                                                &VectorMapServer::getStreetLight, &vms);
  ros::ServiceServer get_utility_pole_srv = nh.advertiseService("vector_map_server/get_utility_pole",
                                                                &VectorMapServer::getUtilityPole, &vms);
  ros::ServiceServer get_guard_rail_srv = nh.advertiseService("vector_map_server/get_guard_rail",
                                                              &VectorMapServer::getGuardRail, &vms);
  ros::ServiceServer get_side_walk_srv = nh.advertiseService("vector_map_server/get_side_walk",
                                                             &VectorMapServer::getSideWalk, &vms);
  ros::ServiceServer get_drive_on_portion_srv = nh.advertiseService("vector_map_server/get_drive_on_portion",
                                                                    &VectorMapServer::getDriveOnPortion, &vms);
  ros::ServiceServer get_cross_road_srv = nh.advertiseService("vector_map_server/get_cross_road",
                                                              &VectorMapServer::getCrossRoad, &vms);
  ros::ServiceServer get_side_strip_srv = nh.advertiseService("vector_map_server/get_side_strip",
                                                              &VectorMapServer::getSideStrip, &vms);
  ros::ServiceServer get_curve_mirror_srv = nh.advertiseService("vector_map_server/get_curve_mirror",
                                                                &VectorMapServer::getCurveMirror, &vms);
  ros::ServiceServer get_wall_srv = nh.advertiseService("vector_map_server/get_wall",
                                                        &VectorMapServer::getWall, &vms);
  ros::ServiceServer get_fence_srv = nh.advertiseService("vector_map_server/get_fence",
                                                         &VectorMapServer::getFence, &vms);
  ros::ServiceServer get_rail_crossing_srv = nh.advertiseService("vector_map_server/get_rail_crossing",
                                                                 &VectorMapServer::getRailCrossing, &vms);

  ros::spin();

  return EXIT_SUCCESS;
}