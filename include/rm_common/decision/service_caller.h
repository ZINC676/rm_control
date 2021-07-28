//
// Created by qiayuan on 5/22/21.
//

#ifndef RM_COMMON_SERVICE_CALLER_H_
#define RM_COMMON_SERVICE_CALLER_H_

#include <chrono>
#include <mutex>
#include <thread>
#include <utility>
#include <ros/ros.h>
#include <ros/service.h>
#include <controller_manager_msgs/SwitchController.h>
#include <control_msgs/QueryCalibrationState.h>
#include <rm_msgs/StatusChange.h>

namespace rm_common {
template<class ServiceType>
class ServiceCallerBase {
 public:
  explicit ServiceCallerBase(ros::NodeHandle &nh, const std::string &service_name = "")
      : fail_count_(0), fail_limit_(0) {
    nh.param("fail_limit", fail_limit_, 0);
    if (!nh.param("service_name", service_name_, service_name) && service_name.empty()) {
      ROS_ERROR("Service name no defined (namespace: %s)", nh.getNamespace().c_str());
      return;
    }
    client_ = nh.serviceClient<ServiceType>(service_name_);
  }
  ServiceCallerBase(XmlRpc::XmlRpcValue &controllers, ros::NodeHandle &nh,
                    const std::string &service_name = "") : fail_count_(0), fail_limit_(0) {
    if (controllers.hasMember("service_name"))
      service_name_ = static_cast<std::string>(controllers["service_name"]);
    else {
      service_name_ = service_name;
      if (service_name.empty()) {
        ROS_ERROR("Service name no defined (namespace: %s)", nh.getNamespace().c_str());
        return;
      }
    }
    client_ = nh.serviceClient<ServiceType>(service_name_);
  }
  ~ServiceCallerBase() { delete thread_; }
  void callService() {
    if (isCalling())
      return;
    thread_ = new std::thread(&ServiceCallerBase::callingThread, this);
    thread_->detach();
  }
  ServiceType &getService() { return service_; }
  bool isCalling() {
    std::unique_lock<std::mutex> guard(mutex_, std::try_to_lock);
    return !guard.owns_lock();
  }

 protected:
  void callingThread() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!client_.call(service_)) {
      ROS_INFO_ONCE("Failed to call service %s on %s. Retrying now ...",
                    typeid(ServiceType).name(), service_name_.c_str());
      if (fail_limit_ != 0) {
        fail_count_++;
        if (fail_count_ >= fail_limit_) {
          ROS_ERROR_ONCE("Failed to call service %s on %s", typeid(ServiceType).name(), service_name_.c_str());
          fail_count_ = 0;
        }
      }
//      ros::WallDuration(0.2).sleep();
    }
  }
  std::string service_name_;
  ros::ServiceClient client_;
  ServiceType service_;
  std::thread *thread_{};
  std::mutex mutex_;
  int fail_count_, fail_limit_;
};

class SwitchControllersServiceCaller : public ServiceCallerBase<controller_manager_msgs::SwitchController> {
 public:
  explicit SwitchControllersServiceCaller(ros::NodeHandle &nh) :
      ServiceCallerBase<controller_manager_msgs::SwitchController>(nh, "/controller_manager/switch_controller") {
    service_.request.strictness = service_.request.BEST_EFFORT;
    service_.request.start_asap = true;
  }
  void startControllers(const std::vector<std::string> &controllers) {
    service_.request.start_controllers = controllers;
  }
  void stopControllers(const std::vector<std::string> &controllers) {
    service_.request.stop_controllers = controllers;
  }
  bool getOk() {
    if (isCalling()) return false;
    return service_.response.ok;
  }
};

class QueryCalibrationServiceCaller : public ServiceCallerBase<control_msgs::QueryCalibrationState> {
 public:
  explicit QueryCalibrationServiceCaller(ros::NodeHandle &nh) : ServiceCallerBase<control_msgs::QueryCalibrationState>(
      nh) {}
  QueryCalibrationServiceCaller(XmlRpc::XmlRpcValue &controllers, ros::NodeHandle &nh)
      : ServiceCallerBase<control_msgs::QueryCalibrationState>(controllers, nh) {}
  bool isCalibrated() {
    if (isCalling()) return false;
    return service_.response.is_calibrated;
  }
};

class SwitchDetectionCaller : public ServiceCallerBase<rm_msgs::StatusChange> {
 public:
  explicit SwitchDetectionCaller(ros::NodeHandle &nh) : ServiceCallerBase<rm_msgs::StatusChange>(
      nh, "/detection/status_switch") {
    service_.request.target = rm_msgs::StatusChangeRequest::ARMOR;
    service_.request.exposure = rm_msgs::StatusChangeRequest::EXPOSURE_LEVEL_0;
    service_.request.armor_target = rm_msgs::StatusChangeRequest::ARMOR_ALL;
    callService();
  }
  void setEnemyColor(const RefereeData &referee_data) {
    if (referee_data.robot_id_ != 0 && !is_set_) {
      service_.request.color =
          referee_data.robot_color_ == "blue" ? rm_msgs::StatusChangeRequest::RED : rm_msgs::StatusChangeRequest::BLUE;
      callService();
      if (getIsSwitch())
        is_set_ = true;
    }
  }
  void switchEnemyColor() {
    service_.request.color = service_.request.color == rm_msgs::StatusChangeRequest::RED;
  }
  void switchTargetType() {
    service_.request.target = service_.request.target == rm_msgs::StatusChangeRequest::ARMOR;
  }
  void switchArmorTargetType() {
    service_.request.armor_target = service_.request.armor_target == rm_msgs::StatusChangeRequest::ARMOR_ALL;
  }
  void switchExposureLevel() {
    service_.request.exposure = service_.request.exposure == rm_msgs::StatusChangeRequest::EXPOSURE_LEVEL_4 ?
                                rm_msgs::StatusChangeRequest::EXPOSURE_LEVEL_0 : service_.request.exposure + 1;
  }
  int getColor() {
    return service_.request.color;
  }
  int getTarget() {
    return service_.request.target;
  }
  int getArmorTarget() {
    return service_.request.armor_target;
  }
  bool getIsSwitch() {
    if (isCalling()) return false;
    return service_.response.switch_is_success;
  }

 private:
  bool is_set_{};
};
}

#endif //RM_COMMON_SERVICE_CALLER_H_
