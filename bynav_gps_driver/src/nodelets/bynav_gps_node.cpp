#include <exception>
#include <string>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/max.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/min.hpp>
#include <boost/accumulators/statistics/rolling_mean.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/circular_buffer.hpp>

#include <rclcpp/rclcpp.hpp>

#include <bynav_gps_driver/bynav_nmea.h>
#include <bynav_gps_msgs/BynavConfig.h>
#include <bynav_gps_msgs/BynavCorrectedImuData.h>
#include <bynav_gps_msgs/BynavFRESET.h>
#include <bynav_gps_msgs/BynavMessageHeader.h>
#include <bynav_gps_msgs/BynavPosition.h>
#include <bynav_gps_msgs/Gpdop.h>
#include <bynav_gps_msgs/Gpgga.h>
#include <bynav_gps_msgs/Gprmc.h>
#include <bynav_gps_msgs/Heading.h>
#include <bynav_gps_msgs/Inspva.h>
#include <bynav_gps_msgs/Inspvax.h>
#include <bynav_gps_msgs/Psrvel.h>
#include <bynav_gps_msgs/Time.h>

#include <builtin_interfaces/msg/time.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.h>
#include <diagnostic_updater/msg/diagnostic_updater.h>
#include <diagnostic_updater/msg/publisher.h>
#include <gps_msgs/msg/gps_fix.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <swri_math_util/math_util.h>

namespace stats = boost::accumulators;

namespace bynav_gps_driver {

class BynavGpsNode : public rclcpp::Node {
public:
  BynavGpsNode()
      : device_(""), connection_type_("serial"), serial_baud_(115200),
        polling_period_(0.05), publish_gpgsv_(false), publish_gphdt_(false),
        imu_rate_(100.0), imu_sample_rate_(-1), span_frame_to_ros_frame_(false),
        publish_clock_steering_(false), publish_imu_messages_(false),
        publish_obs_messages_(false), publish_nav_messages_(false),
        publish_bynav_positions_(false), publish_bynav_gnss_positions_(false),
        publish_bynav_pjk_positions_(false), publish_bynav_velocity_(false),
        publish_bynav_heading_(false), publish_bynav_gpdop_(false),
        publish_nmea_messages_(false), publish_diagnostics_(true),
        publish_sync_diagnostic_(true), publish_invalid_gpsfix_(false),
        reconnect_delay_s_(0.5), use_binary_messages_(false),
        connection_(BynavNmea::SERIAL), last_sync_(ros::TIME_MIN),
        rolling_offset_(stats::tag::rolling_window::window_size = 10),
        expected_rate_(20), device_timeouts_(0), device_interrupts_(0),
        device_errors_(0), gps_parse_failures_(0),
        gps_insufficient_data_warnings_(0), publish_rate_warnings_(0),
        measurement_count_(0), last_published_(ros::TIME_MIN),
        imu_frame_id_(""), frame_id_("") {}

  ~BynavGpsNode() override { gps_.Disconnect(); }

  void onInit() override {
    ros::NodeHandle &node = getNodeHandle();
    ros::NodeHandle &priv = getPrivateNodeHandle();

    swri::param(priv, "device", device_, device_);
    swri::param(priv, "imu_rate", imu_rate_, imu_rate_);
    swri::param(priv, "imu_sample_rate", imu_sample_rate_, imu_sample_rate_);
    swri::param(priv, "publish_gpgsv", publish_gpgsv_, publish_gpgsv_);
    swri::param(priv, "publish_gphdt", publish_gphdt_, publish_gphdt_);
    swri::param(priv, "publish_imu_messages", publish_imu_messages_,
                publish_imu_messages_);
    swri::param(priv, "publish_obs_messages", publish_obs_messages_,
                publish_obs_messages_);
    swri::param(priv, "publish_nav_messages", publish_nav_messages_,
                publish_nav_messages_);
    swri::param(priv, "publish_bynav_positions", publish_bynav_positions_,
                publish_bynav_positions_);
    swri::param(priv, "publish_bynav_gnss_positions",
                publish_bynav_gnss_positions_, publish_bynav_gnss_positions_);
    swri::param(priv, "publish_bynav_pjk_positions",
                publish_bynav_pjk_positions_, publish_bynav_pjk_positions_);
    swri::param(priv, "publish_bynav_velocity", publish_bynav_velocity_,
                publish_bynav_velocity_);
    swri::param(priv, "publish_bynav_heading2", publish_bynav_heading_,
                publish_bynav_heading_);
    swri::param(priv, "publish_bynav_gpdop", publish_bynav_gpdop_,
                publish_bynav_gpdop_);
    swri::param(priv, "publish_nmea_messages", publish_nmea_messages_,
                publish_nmea_messages_);
    swri::param(priv, "publish_diagnostics", publish_diagnostics_,
                publish_diagnostics_);
    swri::param(priv, "publish_sync_diagnostic", publish_sync_diagnostic_,
                publish_sync_diagnostic_);
    swri::param(priv, "polling_period", polling_period_, polling_period_);
    swri::param(priv, "reconnect_delay_s", reconnect_delay_s_,
                reconnect_delay_s_);
    swri::param(priv, "use_binary_messages", use_binary_messages_,
                use_binary_messages_);
    swri::param(priv, "span_frame_to_ros_frame", span_frame_to_ros_frame_,
                span_frame_to_ros_frame_);

    swri::param(priv, "connection_type", connection_type_, connection_type_);
    connection_ = BynavNmea::ParseConnection(connection_type_);
    swri::param(priv, "serial_baud", serial_baud_, serial_baud_);

    swri::param(priv, "imu_frame_id", imu_frame_id_, std::string(""));
    swri::param(priv, "frame_id", frame_id_, std::string(""));

    swri::param(priv, "gpsfix_sync_tol", gps_.gpsfix_sync_tol_, 0.01);
    swri::param(priv, "wait_for_sync", gps_.wait_for_sync_, true);

    swri::param(priv, "publish_invalid_gpsfix", publish_invalid_gpsfix_,
                publish_invalid_gpsfix_);

    reset_service_ =
        priv.advertiseService("freset", &BynavGpsNode::resetService, this);

    sync_sub_ =
        swri::Subscriber(node, "gps_sync", 100, &BynavGpsNode::SyncCallback, this);

    std::string gps_topic = node.resolveName("gps");
    gps_pub_ = swri::advertise<gps_msgs::msg::GPSFix>(node, gps_topic, 100);
    fix_pub_ = swri::advertise<sensor_msgs::msg::NavSatFix>(node, "fix", 100);

    if (publish_nmea_messages_) {
      gpgga_pub_ = swri::advertise<bynav_gps_msgs::Gpgga>(node, "gpgga", 100);
      gprmc_pub_ = swri::advertise<bynav_gps_msgs::Gprmc>(node, "gprmc", 100);
    }

    if (publish_imu_messages_) {
      imu_pub_ = swri::advertise<sensor_msgs::msg::Imu>(node, "imu", 100);
      bynav_imu_pub_ = swri::advertise<bynav_gps_msgs::BynavCorrectedImuData>(
          node, "corrimudata", 100);
      insstdev_pub_ =
          swri::advertise<bynav_gps_msgs::Insstdev>(node, "insstdev", 100);
      inspva_pub_ =
          swri::advertise<bynav_gps_msgs::Inspva>(node, "inspva", 100);
      inspvax_pub_ =
          swri::advertise<bynav_gps_msgs::Inspvax>(node, "inspvax", 100);
    }

    if (publish_obs_messages_) {
    }

    if (publish_nav_messages_) {
    }

    if (publish_gpgsv_) {
      gpgsv_pub_ = swri::advertise<bynav_gps_msgs::Gpgsv>(node, "gpgsv", 100);
    }

    if (publish_gphdt_) {
      gphdt_pub_ = swri::advertise<bynav_gps_msgs::Gphdt>(node, "gphdt", 100);
    }

    if (publish_bynav_positions_) {
      bynav_position_pub_ =
          swri::advertise<bynav_gps_msgs::BynavPosition>(node, "bestpos", 100);
    }

    if (publish_bynav_gnss_positions_) {
      bynav_gnss_position_pub_ = swri::advertise<bynav_gps_msgs::BynavPosition>(
          node, "bestgnsspos", 100);
    }

    if (publish_bynav_pjk_positions_) {
      bynav_pjk_position_pub_ =
          swri::advertise<bynav_gps_msgs::PtnlPJK>(node, "ptnlpjk", 100);
    }

    if (publish_bynav_velocity_) {
      bynav_velocity_pub_ =
          swri::advertise<bynav_gps_msgs::Psrvel>(node, "bestvel", 100);
    } else {
      gps_.wait_for_sync_ = false;
    }

    if (publish_bynav_heading_) {
      bynav_heading_pub_ =
          swri::advertise<bynav_gps_msgs::Heading>(node, "heading2", 100);
    }

    if (publish_bynav_gpdop_) {
      gpdop_pub_ =
          swri::advertise<bynav_gps_msgs::Gpdop>(node, "gpdop", 100, true);
    }

    hw_id_ = "Bynav GPS (" + device_ + ")";
    if (publish_diagnostics_) {
      diagnostic_updater_.setHardwareID(hw_id_);
      diagnostic_updater_.add("Connection", this, &BynavGpsNode::DeviceDiagnostic);
      diagnostic_updater_.add("Hardware", this, &BynavGpsNode::GpsDiagnostic);
      diagnostic_updater_.add("Data", this, &BynavGpsNode::DataDiagnostic);
      diagnostic_updater_.add("Rate", this, &BynavGpsNode::RateDiagnostic);
      if (publish_sync_diagnostic_) {
        diagnostic_updater_.add("Sync", this, &BynavGpsNode::SyncDiagnostic);
      }
    }

    bynav_config_sub_ =
        node.subscribe("bynav/config", 10u, &BynavGpsNode::ConfigCallback, this);

    thread_ = boost::thread(&BynavGpsNode::Spin, this);
    NODELET_INFO("%s initialized", hw_id_.c_str());
  }

  void
  SyncCallback(const std::shared_ptr<builtin_interfaces::msg::Time> &sync) {
    boost::unique_lock<boost::mutex> lock(mutex_);
    sync_times_.push_back(sync->data);
  }

  void ConfigCallback(const bynav_gps_msgs::BynavConfig &conf) {
    boost::unique_lock<boost::mutex> lock(config_mutex_);
    gps_.SetupConfig(conf);
  }

  void Spin() {
    std::string format_suffix;
    if (use_binary_messages_) {
      format_suffix = "b";
    } else {
      format_suffix = "a";
    }

    BynavMessageOpts opts;
    opts["gpgga"] = polling_period_;
    opts["bestpos" + format_suffix] = polling_period_; // Best position
    opts["bestvel" + format_suffix] = polling_period_; // Best velocity
    if (publish_nmea_messages_) {
      opts["gprmc"] = polling_period_;
    }
    if (publish_bynav_pjk_positions_) {
      opts["ptnlpjk" + format_suffix] = polling_period_;
    }
    if (publish_bynav_heading_) {
      opts["heading2" + format_suffix] = polling_period_;
    }
    if (publish_bynav_gpdop_) {
      opts["gpdop" + format_suffix] = -1.0;
    }
    if (publish_gpgsv_) {
      opts["gpgsv"] = 1.0;
    }
    if (publish_gphdt_) {
      opts["gphdt"] = polling_period_;
    }
    if (publish_imu_messages_) {
      double period = 1.0 / imu_rate_;
      opts["corrimudata" + format_suffix] = period;
      opts["inscov" + format_suffix] = 1.0;
      opts["inspva" + format_suffix] = period;
      opts["inspvax" + format_suffix] = period;
      opts["insstdev" + format_suffix] = 1.0;
      if (!use_binary_messages_) {
        NODELET_WARN(
            "Using the ASCII message format with CORRIMUDATA logs is not "
            "recommended.  "
            "A serial link will not be able to keep up with the data rate.");
      }

      if (imu_sample_rate_ > 0) {
        gps_.SetImuRate(imu_sample_rate_, true);
      }
    }
    if (connection_ == BynavNmea::SERIAL) {
      gps_.SetSerialBaud(serial_baud_);
    }
    ros::WallRate rate(1000.0);
    while (ros::ok()) {
      if (gps_.Connect(device_, connection_, opts)) {
        NODELET_INFO("%s connected to device", hw_id_.c_str());
        while (gps_.IsConnected() && ros::ok()) {
          CheckDeviceForData();

          if (publish_diagnostics_) {
            diagnostic_updater_.update();
          }

          ros::spinOnce();
          rate.sleep();
        }
      } else {
        NODELET_ERROR_THROTTLE(1, "Error connecting to device <%s:%s>: %s",
                               connection_type_.c_str(), device_.c_str(),
                               gps_.ErrorMsg().c_str());
        device_errors_++;
        error_msg_ = gps_.ErrorMsg();
      }

      if (ros::ok()) {
        ros::WallDuration(reconnect_delay_s_).sleep();
      }

      if (publish_diagnostics_) {
        diagnostic_updater_.update();
      }

      ros::spinOnce();
      rate.sleep();
    }

    gps_.Disconnect();
    NODELET_INFO("%s disconnected and shut down", hw_id_.c_str());
  }

private:
  std::string device_;
  std::string connection_type_;
  int32_t serial_baud_;
  double polling_period_;
  bool publish_gpgsv_;
  bool publish_gphdt_;
  double imu_rate_;
  double imu_sample_rate_;
  bool span_frame_to_ros_frame_;
  bool publish_clock_steering_;
  bool publish_nav_messages_;
  bool publish_obs_messages_;
  bool publish_imu_messages_;
  bool publish_bynav_positions_;
  bool publish_bynav_gnss_positions_;
  bool publish_bynav_pjk_positions_;
  bool publish_bynav_velocity_;
  bool publish_bynav_heading_;
  bool publish_bynav_gpdop_;
  bool publish_nmea_messages_;
  bool publish_diagnostics_;
  bool publish_sync_diagnostic_;
  bool publish_invalid_gpsfix_;
  double reconnect_delay_s_;
  bool use_binary_messages_;

  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr fix_pub_;
  rclcpp::Publisher<gps_msgs::msg::GPSFix>::SharedPtr gps_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Inspva>::SharedPtr inspva_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Inspvax>::SharedPtr inspvax_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Insstdev>::SharedPtr insstdev_pub_;
  rclcpp::Publisher<bynav_gps_msgs::BynavCorrectedImuData>::SharedPtr
      bynav_imu_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Insstdev>::SharedPtr bynav_position_pub_;
  rclcpp::Publisher<bynav_gps_msgs::BynavPosition>::SharedPtr
      bynav_gnss_position_pub_;
  rclcpp::Publisher<bynav_gps_msgs::PtnlPJK>::SharedPtr bynav_pjk_position_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Psrvel>::SharedPtr bynav_velocity_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Heading>::SharedPtr bynav_heading_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Gpdop>::SharedPtr gpdop_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Gpgga>::SharedPtr gpgga_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Gpgsv>::SharedPtr gpgsv_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Gphdt>::SharedPtr gphdt_pub_;
  rclcpp::Publisher<bynav_gps_msgs::Gprmc>::SharedPtr gprmc_pub_;

  rclcpp::Publisher<bynav_gps_msgs::GnssEphemMsgPtr>::SharedPtr
      bdsephemerisb_pub_;
  rclcpp::Publisher<bynav_gps_msgs::GnssEphemMsgPtr>::SharedPtr
      galephemerisb_pub_;
  rclcpp::Publisher<bynav_gps_msgs::GnssEphemMsgPtr>::SharedPtr gpsephemb_pub_;
  rclcpp::Publisher<bynav_gps_msgs::GnssGloEphemMsgPtr>::SharedPtr
      gloephemerisb_pub_;
  rclcpp::Publisher<bynav_gps_msgs::GnssEphemMsgPtr>::SharedPtr
      qzssephemerisb_pub_;

  rclcpp::Subscription<bynav_gps_msgs::BynavConfig>::SharedPtr
      bynav_config_sub_;

  ros::ServiceServer reset_service_;

  BynavNmea::ConnectionType connection_;
  BynavNmea gps_;

  boost::thread thread_;
  boost::mutex mutex_;

  boost::mutex config_mutex_;

  rclcpp::Subscription<builtin_interfaces::msg::Time>::SharedPtr sync_sub_;
  rclcpp::Time last_sync_;

  boost::circular_buffer<rclcpp::Time> sync_times_;

  boost::circular_buffer<rclcpp::Time> msg_times_;

  stats::accumulator_set<float,
                         stats::stats<stats::tag::max, stats::tag::min,
                                      stats::tag::mean, stats::tag::variance>>
      offset_stats_;

  stats::accumulator_set<float, stats::stats<stats::tag::rolling_mean>>
      rolling_offset_;

  std::string error_msg_;
  diagnostic_updater::Updater diagnostic_updater_;
  std::string hw_id_;
  double expected_rate_;
  int32_t device_timeouts_;
  int32_t device_interrupts_;
  int32_t device_errors_;
  int32_t gps_parse_failures_;
  int32_t gps_insufficient_data_warnings_;
  int32_t publish_rate_warnings_;
  int32_t measurement_count_;
  rclcpp::Time last_published_;
  bynav_gps_msgs::BynavPositionPtr last_bynav_position_;

  std::string imu_frame_id_;
  std::string frame_id_;

  bool resetService(bynav_gps_msgs::BynavFRESET::Request &req,
                    bynav_gps_msgs::BynavFRESET::Response &res) {
    if (!gps_.IsConnected()) {
      res.success = false;
    }

    std::string command = "FRESET ";
    command += req.target.length() ? "STANDARD" : req.target;
    command += "\r\n";
    gps_.Write(command);

    if (req.target.length() == 0) {
      ROS_WARN("No FRESET target specified. Doing FRESET STANDARD. This may be "
               "undesired behavior.");
    }

    res.success = true;
    return true;
  }

  void CheckDeviceForData() {
    std::vector<gps_msgs::msg::GPSFixPtr> fix_msgs;
    std::vector<bynav_gps_msgs::BynavPositionPtr> position_msgs;
    std::vector<bynav_gps_msgs::GpggaPtr> gpgga_msgs;

    BynavNmea::ReadResult result = gps_.ProcessData();
    if (result == BynavNmea::READ_ERROR) {
      NODELET_ERROR_THROTTLE(1, "Error reading from device <%s:%s>: %s",
                             connection_type_.c_str(), device_.c_str(),
                             gps_.ErrorMsg().c_str());
      device_errors_++;
    } else if (result == BynavNmea::READ_TIMEOUT) {
      device_timeouts_++;
    } else if (result == BynavNmea::READ_INTERRUPTED) {
      device_interrupts_++;
    } else if (result == BynavNmea::READ_PARSE_FAILED) {
      NODELET_ERROR("Error reading from device <%s:%s>: %s",
                    connection_type_.c_str(), device_.c_str(),
                    gps_.ErrorMsg().c_str());
      gps_parse_failures_++;
    } else if (result == BynavNmea::READ_INSUFFICIENT_DATA) {
      gps_insufficient_data_warnings_++;
    }

    gps_.GetFixMessages(fix_msgs);
    gps_.GetGpggaMessages(gpgga_msgs);
    gps_.GetBynavPositions(position_msgs);

    measurement_count_ += position_msgs.size();

    if (!position_msgs.empty()) {
      last_bynav_position_ = position_msgs.back();
    }

    for (const auto &msg : gpgga_msgs) {
      if (msg->utc_seconds != 0) {
        auto second =
            static_cast<int64_t>(swri_math_util::Round(msg->utc_seconds));
        double difference = std::fabs(msg->utc_seconds - second);

        if (difference < 0.02) {
          msg_times_.push_back(msg->header.stamp);
        }
      }
    }

    CalculateTimeSync();

    ros::Duration sync_offset(0);
    if (last_sync_ != ros::TIME_MIN) {
      sync_offset = ros::Duration(stats::rolling_mean(rolling_offset_));
    }
    NODELET_DEBUG_STREAM("GPS TimeSync offset is " << sync_offset);

    if (publish_nmea_messages_) {
      for (const auto &msg : gpgga_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        gpgga_pub_.publish(msg);
      }

      std::vector<bynav_gps_msgs::GprmcPtr> gprmc_msgs;
      gps_.GetGprmcMessages(gprmc_msgs);
      for (const auto &msg : gprmc_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        gprmc_pub_.publish(msg);
      }
    }

    if (publish_gpgsv_) {
      std::vector<bynav_gps_msgs::GpgsvPtr> gpgsv_msgs;
      gps_.GetGpgsvMessages(gpgsv_msgs);
      for (const auto &msg : gpgsv_msgs) {
        msg->header.stamp = ros::Time::now();
        msg->header.frame_id = frame_id_;
        gpgsv_pub_.publish(msg);
      }
    }

    if (publish_gphdt_) {
      std::vector<bynav_gps_msgs::GphdtPtr> gphdt_msgs;
      gps_.GetGphdtMessages(gphdt_msgs);
      for (const auto &msg : gphdt_msgs) {
        msg->header.stamp = ros::Time::now();
        msg->header.frame_id = frame_id_;
        gphdt_pub_.publish(msg);
      }
    }

    if (publish_bynav_positions_) {
      for (const auto &msg : position_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        bynav_position_pub_.publish(msg);
      }
    }

    if (publish_bynav_gnss_positions_) {
      std::vector<bynav_gps_msgs::BynavPositionPtr> gnss_position_msgs;
      gps_.GetBynavGnssPositions(gnss_position_msgs);
      for (const auto &msg : gnss_position_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        bynav_gnss_position_pub_.publish(msg);
      }
    }

    if (publish_bynav_pjk_positions_) {
      std::vector<bynav_gps_msgs::PtnlPJKPtr> xyz_position_msgs;
      gps_.GetBynavPJKPositions(xyz_position_msgs);
      for (const auto &msg : xyz_position_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        bynav_pjk_position_pub_.publish(msg);
      }
    }

    if (publish_bynav_heading_) {
      std::vector<bynav_gps_msgs::HeadingPtr> heading_msgs;
      gps_.GetHeadingMessages(heading_msgs);
      for (const auto &msg : heading_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        bynav_heading_pub_.publish(msg);
      }
    }

    if (publish_bynav_gpdop_) {
      std::vector<bynav_gps_msgs::GpdopPtr> gpdop_msgs;
      gps_.GetGpdopMessages(gpdop_msgs);
      for (const auto &msg : gpdop_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        gpdop_pub_.publish(msg);
      }
    }

    if (publish_bynav_velocity_) {
      std::vector<bynav_gps_msgs::BynavVelocityPtr> velocity_msgs;
      gps_.GetBynavVelocities(velocity_msgs);
      for (const auto &msg : velocity_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = frame_id_;
        bynav_velocity_pub_.publish(msg);
      }
    }

    if (publish_imu_messages_) {
      std::vector<bynav_gps_msgs::BynavCorrectedImuDataPtr> bynav_imu_msgs;
      gps_.GetBynavCorrectedImuData(bynav_imu_msgs);
      for (const auto &msg : bynav_imu_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = imu_frame_id_;
        bynav_imu_pub_.publish(msg);
      }

      std::vector<sensor_msgs::msg::ImuPtr> imu_msgs;
      gps_.GetImuMessages(imu_msgs);
      for (const auto &msg : imu_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = imu_frame_id_;
        imu_pub_.publish(msg);
      }

      std::vector<bynav_gps_msgs::InspvaPtr> inspva_msgs;
      gps_.GetInspvaMessages(inspva_msgs);
      for (const auto &msg : inspva_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = imu_frame_id_;
        inspva_pub_.publish(msg);
      }

      std::vector<bynav_gps_msgs::InspvaxPtr> inspvax_msgs;
      gps_.GetInspvaxMessages(inspvax_msgs);
      for (const auto &msg : inspvax_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = imu_frame_id_;
        inspvax_pub_.publish(msg);
      }

      std::vector<bynav_gps_msgs::InsstdevPtr> insstdev_msgs;
      gps_.GetInsstdevMessages(insstdev_msgs);
      for (const auto &msg : insstdev_msgs) {
        msg->header.stamp += sync_offset;
        msg->header.frame_id = imu_frame_id_;
        insstdev_pub_.publish(msg);
      }
    }

    for (const auto &msg : fix_msgs) {
      msg->header.stamp += sync_offset;
      msg->header.frame_id = frame_id_;
      if (publish_invalid_gpsfix_ ||
          msg->status.status != gps_msgs::msg::GPSStatus::STATUS_NO_FIX) {
        gps_pub_.publish(msg);
      }

      if (fix_pub_.getNumSubscribers() > 0) {
        sensor_msgs::msg::NavSatFixPtr fix_msg = ConvertGpsFixToNavSatFix(msg);

        fix_pub_.publish(fix_msg);
      }

      if (last_published_ != ros::TIME_MIN &&
          (msg->header.stamp - last_published_).toSec() >
              1.5 * (1.0 / expected_rate_)) {
        publish_rate_warnings_++;
      }

      last_published_ = msg->header.stamp;
    }
  }

  sensor_msgs::msg::NavSatFixPtr
  ConvertGpsFixToNavSatFix(const gps_msgs::msg::GPSFixPtr &msg) {
    sensor_msgs::msg::NavSatFixPtr fix_msg =
        boost::make_shared<sensor_msgs::msg::NavSatFix>();
    fix_msg->header = msg->header;
    fix_msg->latitude = msg->latitude;
    fix_msg->longitude = msg->longitude;
    fix_msg->altitude = msg->altitude;
    fix_msg->position_covariance = msg->position_covariance;
    switch (msg->status.status) {
    case gps_msgs::msg::GPSStatus::STATUS_NO_FIX:
      fix_msg->status.status = sensor_msgs::msg::NavSatStatus::STATUS_NO_FIX;
      break;
    case gps_msgs::msg::GPSStatus::STATUS_FIX:
      fix_msg->status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
      break;
    case gps_msgs::msg::GPSStatus::STATUS_SBAS_FIX:
      fix_msg->status.status = sensor_msgs::msg::NavSatStatus::STATUS_SBAS_FIX;
      break;
    case gps_msgs::msg::GPSStatus::STATUS_GBAS_FIX:
      fix_msg->status.status = sensor_msgs::msg::NavSatStatus::STATUS_GBAS_FIX;
      break;
    case gps_msgs::msg::GPSStatus::STATUS_DGPS_FIX:
    case gps_msgs::msg::GPSStatus::STATUS_WAAS_FIX:
    default:
      ROS_WARN_ONCE("Unsupported fix status: %d", msg->status.status);
      fix_msg->status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
      break;
    }
    switch (msg->position_covariance_type) {
    case gps_msgs::msg::GPSFix::COVARIANCE_TYPE_KNOWN:
      fix_msg->position_covariance_type =
          sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_KNOWN;
      break;
    case gps_msgs::msg::GPSFix::COVARIANCE_TYPE_APPROXIMATED:
      fix_msg->position_covariance_type =
          sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_APPROXIMATED;
      break;
    case gps_msgs::msg::GPSFix::COVARIANCE_TYPE_DIAGONAL_KNOWN:
      fix_msg->position_covariance_type =
          sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN;
      break;
    case gps_msgs::msg::GPSFix::COVARIANCE_TYPE_UNKNOWN:
      fix_msg->position_covariance_type =
          sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_UNKNOWN;
      break;
    default:
      ROS_WARN_ONCE("Unsupported covariance type: %d",
                    msg->position_covariance_type);
      fix_msg->position_covariance_type =
          sensor_msgs::msg::NavSatFix::COVARIANCE_TYPE_UNKNOWN;
      break;
    }
    fix_msg->status.service = 0;

    return fix_msg;
  }

  void CalculateTimeSync() {
    boost::unique_lock<boost::mutex> lock(mutex_);
    int32_t synced_i = -1;
    int32_t synced_j = -1;

    for (size_t i = 0; i < sync_times_.size(); i++) {
      for (size_t j = synced_j + 1; j < msg_times_.size(); j++) {
        double offset = (sync_times_[i] - msg_times_[j]).toSec();
        if (std::fabs(offset) < 0.49) {
          synced_i = static_cast<int32_t>(i);
          synced_j = static_cast<int32_t>(j);
          offset_stats_(offset);
          rolling_offset_(offset);
          last_sync_ = sync_times_[i];
          break;
        }
      }
    }

    for (int i = 0; i <= synced_i && !sync_times_.empty(); i++) {
      sync_times_.pop_front();
    }

    for (int j = 0; j <= synced_j && !msg_times_.empty(); j++) {
      msg_times_.pop_front();
    }
  }

  void SyncDiagnostic(diagnostic_updater::DiagnosticStatusWrapper &status) {
    status.summary(diagnostic_msgs::DiagnosticStatus::OK, "Nominal");

    if (last_sync_ == ros::TIME_MIN) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN, "No Sync");
      return;
    } else if (last_sync_ < ros::Time::now() - ros::Duration(10)) {
      status.summary(diagnostic_msgs::DiagnosticStatus::ERROR, "Sync Stale");
      NODELET_ERROR("GPS time synchronization is stale.");
    }

    status.add("Last Sync", last_sync_);
    status.add("Mean Offset", stats::mean(offset_stats_));
    status.add("Mean Offset (rolling)", stats::rolling_mean(rolling_offset_));
    status.add("Offset Variance", stats::variance(offset_stats_));
    status.add("Min Offset", stats::min(offset_stats_));
    status.add("Max Offset", stats::max(offset_stats_));
  }

  void DeviceDiagnostic(diagnostic_updater::DiagnosticStatusWrapper &status) {
    status.summary(diagnostic_msgs::DiagnosticStatus::OK, "Nominal");

    if (device_errors_ > 0) {
      status.summary(diagnostic_msgs::DiagnosticStatus::ERROR, "Device Errors");
    } else if (device_interrupts_ > 0) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN,
                     "Device Interrupts");
      NODELET_WARN("device interrupts detected <%s:%s>: %d",
                   connection_type_.c_str(), device_.c_str(),
                   device_interrupts_);
    } else if (device_timeouts_) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN,
                     "Device Timeouts");
      NODELET_WARN("device timeouts detected <%s:%s>: %d",
                   connection_type_.c_str(), device_.c_str(), device_timeouts_);
    }

    status.add("Errors", device_errors_);
    status.add("Interrupts", device_interrupts_);
    status.add("Timeouts", device_timeouts_);

    device_timeouts_ = 0;
    device_interrupts_ = 0;
    device_errors_ = 0;
  }

  void GpsDiagnostic(diagnostic_updater::DiagnosticStatusWrapper &status) {
    status.summary(diagnostic_msgs::DiagnosticStatus::OK, "Nominal");

    if (gps_parse_failures_ > 0) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN, "Parse Failures");
      NODELET_WARN("gps parse failures detected <%s>: %d", hw_id_.c_str(),
                   gps_parse_failures_);
    }

    status.add("Parse Failures", gps_parse_failures_);
    status.add("Insufficient Data Warnings", gps_insufficient_data_warnings_);

    gps_parse_failures_ = 0;
    gps_insufficient_data_warnings_ = 0;
  }

  void DataDiagnostic(diagnostic_updater::DiagnosticStatusWrapper &status) {
    status.summary(diagnostic_msgs::DiagnosticStatus::OK, "Nominal");

    double period = diagnostic_updater_.getPeriod();
    double measured_rate = measurement_count_ / period;

    if (measured_rate < 0.5 * expected_rate_) {
      status.summary(diagnostic_msgs::DiagnosticStatus::ERROR,
                     "Insufficient Data Rate");
      NODELET_ERROR("insufficient data rate <%s>: %lf < %lf", hw_id_.c_str(),
                    measured_rate, expected_rate_);
    } else if (measured_rate < 0.95 * expected_rate_) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN,
                     "Insufficient Data Rate");
      NODELET_WARN("insufficient data rate <%s>: %lf < %lf", hw_id_.c_str(),
                   measured_rate, expected_rate_);
    }

    status.add("Measurement Rate (Hz)", measured_rate);

    measurement_count_ = 0;
  }

  void RateDiagnostic(diagnostic_updater::DiagnosticStatusWrapper &status) {
    status.summary(diagnostic_msgs::DiagnosticStatus::OK,
                   "Nominal Publish Rate");

    double elapsed = (ros::Time::now() - last_published_).toSec();
    bool gap_detected = false;
    if (elapsed > 2.0 / expected_rate_) {
      publish_rate_warnings_++;
      gap_detected = true;
    }

    if (publish_rate_warnings_ > 1 || gap_detected) {
      status.summary(diagnostic_msgs::DiagnosticStatus::WARN,
                     "Insufficient Publish Rate");
      NODELET_WARN("publish rate failures detected <%s>: %d", hw_id_.c_str(),
                   publish_rate_warnings_);
    }

    status.add("Warnings", publish_rate_warnings_);

    publish_rate_warnings_ = 0;
  }
};
} // namespace bynav_gps_driver

// Register nodelet plugin
#include <swri_nodelet/class_list_macros.h>
SWRI_NODELET_EXPORT_CLASS(bynav_gps_driver, BynavGpsNode)
