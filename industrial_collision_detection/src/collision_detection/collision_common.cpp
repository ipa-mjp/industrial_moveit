/**
 * @file collision_common.cpp
 * @brief This contains common data used during collision checking
 *
 * @author Levi Armstrong
 * @date May 4, 2016
 * @version TODO
 * @bug No known bugs
 *
 * @copyright Copyright (c) 2016, Southwest Research Institute
 *
 * @license Software License Agreement (Apache License)\n
 * \n
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at\n
 * \n
 * http://www.apache.org/licenses/LICENSE-2.0\n
 * \n
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <industrial_collision_detection/collision_detection/collision_common.h>
#include <moveit/collision_detection_fcl/collision_common.h>
#include <ros/ros.h>

namespace industrial_collision_detection
{
using namespace collision_detection;

  bool getDistanceInfo(const DistanceMap &distance_detailed, DistanceInfoMap &distance_info_map)
  {
    Eigen::Affine3d tf;
    tf.setIdentity();
    return getDistanceInfo(distance_detailed, distance_info_map, tf);
  }

  bool getDistanceInfo(const DistanceMap &distance_detailed, DistanceInfoMap &distance_info_map, const Eigen::Affine3d &tf)
  {
    bool status = true;
    for (DistanceMap::const_iterator it = distance_detailed.begin(); it != distance_detailed.end(); ++it)
    {
      DistanceInfo dist_info;
      DistanceResultsData dist = static_cast<const DistanceResultsData>(it->second);
      if (dist.link_name[0] == it->first)
      {
        dist_info.nearest_obsticle = dist.link_name[1];
        dist_info.link_point = tf * dist.nearest_points[0];
        dist_info.obsticle_point = tf * dist.nearest_points[1];
        dist_info.avoidance_vector = dist_info.link_point - dist_info.obsticle_point;
        dist_info.avoidance_vector.normalize();
        dist_info.distance = dist.min_distance;
      }
      else if (dist.link_name[1] == it->first)
      {
        dist_info.nearest_obsticle = dist.link_name[0];
        dist_info.link_point = tf * dist.nearest_points[1];
        dist_info.obsticle_point = tf * dist.nearest_points[0];
        dist_info.avoidance_vector = dist_info.link_point - dist_info.obsticle_point;
        dist_info.avoidance_vector.normalize();
        dist_info.distance = dist.min_distance;
      }
      else
      {
        ROS_ERROR("getDistanceInfo was unable to find link after match!");
        status &= false;
      }

      distance_info_map.insert(std::make_pair(it->first, dist_info));
    }

    return status;
  }

  void DistanceRequest::enableGroup(const robot_model::RobotModelConstPtr &kmodel)
  {
    if (kmodel->hasJointModelGroup(group_name))
      active_components_only = &kmodel->getJointModelGroup(group_name)->getUpdatedLinkModelsWithGeometrySet();
    else
      active_components_only = NULL;
  }

  bool distanceDetailedCallback(fcl::CollisionObject* o1, fcl::CollisionObject* o2, void* data, double& min_dist)
  {
    DistanceData* cdata = reinterpret_cast<DistanceData*>(data);

    const CollisionGeometryData* cd1 = static_cast<const CollisionGeometryData*>(o1->collisionGeometry()->getUserData());
    const CollisionGeometryData* cd2 = static_cast<const CollisionGeometryData*>(o2->collisionGeometry()->getUserData());
    bool active1 = true, active2 = true;

    // do not distance check geoms part of the same object / link / attached body
    if (cd1->sameObject(*cd2))
      return false;

    // If active components are specified
    if (cdata->req->active_components_only)
    {
      const robot_model::LinkModel *l1 = cd1->type == BodyTypes::ROBOT_LINK ? cd1->ptr.link : (cd1->type == BodyTypes::ROBOT_ATTACHED ? cd1->ptr.ab->getAttachedLink() : NULL);
      const robot_model::LinkModel *l2 = cd2->type == BodyTypes::ROBOT_LINK ? cd2->ptr.link : (cd2->type == BodyTypes::ROBOT_ATTACHED ? cd2->ptr.ab->getAttachedLink() : NULL);

      // If neither of the involved components is active
      if ((!l1 || cdata->req->active_components_only->find(l1) == cdata->req->active_components_only->end()) &&
          (!l2 || cdata->req->active_components_only->find(l2) == cdata->req->active_components_only->end()))
      {
        return false;
      }
      else
      {
        if (!l1 || cdata->req->active_components_only->find(l1) == cdata->req->active_components_only->end())
          active1 = false;

        if (!l2 || cdata->req->active_components_only->find(l2) == cdata->req->active_components_only->end())
          active2 = false;
      }
    }

    // use the collision matrix (if any) to avoid certain distance checks
    bool always_allow_collision = false;
    if (cdata->req->acm)
    {
      AllowedCollision::Type type;

      bool found = cdata->req->acm->getAllowedCollision(cd1->getID(), cd2->getID(), type);
      if (found)
      {
        // if we have an entry in the collision matrix, we read it
        if (type == AllowedCollision::ALWAYS)
        {
          always_allow_collision = true;
          if (!cdata->req->verbose)
            CONSOLE_BRIDGE_logDebug("Collision between '%s' and '%s' is always allowed. No contacts are computed.",
                     cd1->getID().c_str(), cd2->getID().c_str());
        }
      }
    }

    // check if a link is touching an attached object
    if (cd1->type == BodyTypes::ROBOT_LINK && cd2->type == BodyTypes::ROBOT_ATTACHED)
    {
      const std::set<std::string> &tl = cd2->ptr.ab->getTouchLinks();
      if (tl.find(cd1->getID()) != tl.end())
      {
        always_allow_collision = true;
        if (!cdata->req->verbose)
          CONSOLE_BRIDGE_logDebug("Robot link '%s' is allowed to touch attached object '%s'. No contacts are computed.",
                   cd1->getID().c_str(), cd2->getID().c_str());
      }
    }
    else
    {
      if (cd2->type == BodyTypes::ROBOT_LINK && cd1->type == BodyTypes::ROBOT_ATTACHED)
      {
        const std::set<std::string> &tl = cd1->ptr.ab->getTouchLinks();
        if (tl.find(cd2->getID()) != tl.end())
        {
          always_allow_collision = true;
          if (!cdata->req->verbose)
            CONSOLE_BRIDGE_logDebug("Robot link '%s' is allowed to touch attached object '%s'. No contacts are computed.",
                     cd2->getID().c_str(), cd1->getID().c_str());
        }
      }
    }

    if(always_allow_collision)
    {
      return false;
    }

    if (!cdata->req->verbose)
      CONSOLE_BRIDGE_logDebug("Actually checking collisions between %s and %s", cd1->getID().c_str(), cd2->getID().c_str());


    fcl::DistanceResult fcl_result;
    DistanceResultsData dist_result;
    double dist_threshold = cdata->req->distance_threshold;
    std::map<std::string, DistanceResultsData>::iterator it1, it2;

    if (!cdata->req->global)
    {
      it1 = cdata->res->distance.find(cd1->ptr.obj->id_);
      it2 = cdata->res->distance.find(cd2->ptr.obj->id_);

      if (cdata->req->active_components_only)
      {
        if (active1 && active2)
        {
          if (it1 != cdata->res->distance.end() && it2 != cdata->res->distance.end())
            dist_threshold = std::max(it1->second.min_distance, it2->second.min_distance);
        }
        else if (active1 && !active2)
        {
          if (it1 != cdata->res->distance.end())
            dist_threshold = it1->second.min_distance;
        }
        else if (!active1 && active2)
        {
          if (it2 != cdata->res->distance.end())
            dist_threshold = it2->second.min_distance;
        }
      }
      else
      {
        if (it1 != cdata->res->distance.end() && it2 != cdata->res->distance.end())
          dist_threshold = std::max(it1->second.min_distance, it2->second.min_distance);
      }
    }
    else
    {
        dist_threshold = cdata->res->minimum_distance.min_distance;
    }

    fcl_result.min_distance = dist_threshold;
    double d = fcl::distance(o1, o2, fcl::DistanceRequest(cdata->req->detailed), fcl_result);

    // Check if either object is already in the map. If not add it or if present
    // check to see if the new distance is closer. If closer remove the existing
    // one and add the new distance information.
    if (d < dist_threshold)
    {
      dist_result.min_distance = fcl_result.min_distance;
      dist_result.nearest_points[0] = Eigen::Vector3d(fcl_result.nearest_points[0].data.vs);
      dist_result.nearest_points[1] = Eigen::Vector3d(fcl_result.nearest_points[1].data.vs);
      dist_result.link_name[0] = cd1->ptr.obj->id_;
      dist_result.link_name[1] = cd2->ptr.obj->id_;

      cdata->res->minimum_distance.update(dist_result);

      if (!cdata->req->global)
      {
        if (d <= 0 && !cdata->res->collision)
        {
          cdata->res->collision = true;
        }

        if (active1)
        {
          if (it1 == cdata->res->distance.end())
          {
            cdata->res->distance.insert(std::make_pair(cd1->ptr.obj->id_, dist_result));
          }
          else
          {
            it1->second.update(dist_result);
          }
        }

        if (active2)
        {
          if (it2 == cdata->res->distance.end())
          {
            cdata->res->distance.insert(std::make_pair(cd2->ptr.obj->id_, dist_result));
          }
          else
          {
            it2->second.update(dist_result);
          }
        }
      }
      else
      {
        if (d <= 0)
        {
          cdata->res->collision = true;
          cdata->done = true;
        }
      }
    }

    return cdata->done;
  }
}
