#ifndef BYNAV_NMEA_H_
#define BYNAV_NMEA_H_

#include <map>
#include <queue>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/circular_buffer.hpp>

#include <swri_serial_util/serial_port.h>

#include <gps_msgs/msg/gps_fix.hpp>

#include <bynav_gps_msgs/BynavCorrectedImuData.h>
#include <bynav_gps_msgs/BynavPosition.h>
#include <bynav_gps_msgs/Gpgga.h>
#include <bynav_gps_msgs/Gphdt.h>
#include <bynav_gps_msgs/Gprmc.h>
#include <bynav_gps_msgs/Inspva.h>
#include <bynav_gps_msgs/Inspvax.h>
#include <bynav_gps_msgs/Insstdev.h>
#include <bynav_gps_msgs/Psrvel.h>

#include <bynav_gps_driver/bynav_control.h>
#include <bynav_gps_driver/bynav_message_extractor.h>

#include <bynav_gps_driver/parsers/bdsephemerisb.h>
#include <bynav_gps_driver/parsers/bestgnsspos.h>
#include <bynav_gps_driver/parsers/bestpos.h>
#include <bynav_gps_driver/parsers/bestvel.h>
#include <bynav_gps_driver/parsers/corrimudata.h>
#include <bynav_gps_driver/parsers/corrimudatas.h>
#include <bynav_gps_driver/parsers/galephemerisb.h>
#include <bynav_gps_driver/parsers/gloephemerisb.h>
#include <bynav_gps_driver/parsers/gpdop.h>
#include <bynav_gps_driver/parsers/gpgga.h>
#include <bynav_gps_driver/parsers/gpgsv.h>
#include <bynav_gps_driver/parsers/gphdt.h>
#include <bynav_gps_driver/parsers/gprmc.h>
#include <bynav_gps_driver/parsers/gpsphemb.h>
#include <bynav_gps_driver/parsers/heading.h>
#include <bynav_gps_driver/parsers/inspva.h>
#include <bynav_gps_driver/parsers/inspvax.h>
#include <bynav_gps_driver/parsers/insstdev.h>
#include <bynav_gps_driver/parsers/ptnlpjk.h>
#include <bynav_gps_driver/parsers/qzssephemerisb.h>
#include <bynav_gps_driver/parsers/rangecmpb.h>
#include <bynav_gps_driver/parsers/rawimu.h>
#include <bynav_gps_driver/parsers/rawimus.h>

#include <sensor_msgs/msg/imu.hpp>

namespace bynav_gps_driver {

class BynavNmea : public BynavControl {
public:
  using BynavConnection::ConnectionType;
  using BynavConnection::ReadResult;

  BynavNmea();
  virtual ~BynavNmea() = default;

  bool Connect(const std::string &device, ConnectionType connection,
               BynavMessageOpts const &opts);

  void GetFixMessages(std::vector<gps_msgs::msg::GPSFixPtr> &fix_messages);

  void GetGpggaMessages(std::vector<bynav_gps_msgs::GpggaPtr> &gpgga_messages);

  void GetGpgsvMessages(std::vector<bynav_gps_msgs::GpgsvPtr> &gpgsv_messages);

  void GetGphdtMessages(std::vector<bynav_gps_msgs::GphdtPtr> &gphdt_messages);

  void GetGprmcMessages(std::vector<bynav_gps_msgs::GprmcPtr> &gprmc_messages);

  void GetHeadingMessages(std::vector<bynav_gps_msgs::HeadingPtr> &headings);

  void GetGpdopMessages(std::vector<bynav_gps_msgs::GpdopPtr> &gpdop_messages);

  void GetImuMessages(std::vector<sensor_msgs::msg::ImuPtr> &imu_messages);

  void
  GetInspvaMessages(std::vector<bynav_gps_msgs::InspvaPtr> &inspva_messages);

  void
  GetInspvaxMessages(std::vector<bynav_gps_msgs::InspvaxPtr> &inspvax_messages);

  void GetInsstdevMessages(
      std::vector<bynav_gps_msgs::InsstdevPtr> &insstdev_messages);

  void GetBynavCorrectedImuData(
      std::vector<bynav_gps_msgs::BynavCorrectedImuDataPtr> &imu_messages);

  void GetRawImuData(std::vector<bynav_gps_msgs::RawIMUPtr> &imu_messages);

  void
  GetBynavPositions(std::vector<bynav_gps_msgs::BynavPositionPtr> &positions);

  void GetBynavGnssPositions(
      std::vector<bynav_gps_msgs::BynavPositionPtr> &positions);

  void GetBynavPJKPositions(std::vector<bynav_gps_msgs::PtnlPJKPtr> &positions);

  void
  GetBynavVelocities(std::vector<bynav_gps_msgs::BynavVelocityPtr> &velocities);

  void GetBdsephemerisbMessages(
      std::vector<bynav_gps_msgs::GnssEphemMsgPtr> &messages);

  void GetGaleephemerisbMessages(
      std::vector<bynav_gps_msgs::GnssEphemMsgPtr> &messages);

  void GetGloephemerisbMessages(
      std::vector<bynav_gps_msgs::GnssGloEphemMsgPtr> &messages);

  void
  GetGpsephembMessages(std::vector<bynav_gps_msgs::GnssEphemMsgPtr> &messages);

  void GetQzssephemerisbMessages(
      std::vector<bynav_gps_msgs::GnssEphemMsgPtr> &messages);

  void
  GetRangecmpbMessages(std::vector<bynav_gps_msgs::GnssMeasMsgPtr> &messages);

  ReadResult ProcessData();

  void SetImuRate(double imu_rate, bool force = true);

  double gpsfix_sync_tol_;
  bool wait_for_sync_;

private:
  void GenerateImuMessages();

  BynavNmea::ReadResult
  ParseBinaryMessage(const BinaryMessage &msg,
                     const ros::Time &stamp) noexcept(false);

  BynavNmea::ReadResult
  ParseBinaryMicroMessage(const BinaryMicroMessage &msg,
                          const ros::Time &stamp) noexcept(false);

  BynavNmea::ReadResult
  ParseNmeaSentence(const NmeaSentence &sentence, const ros::Time &stamp,
                    double most_recent_utc_time) noexcept(false);

  BynavNmea::ReadResult
  ParseBynavSentence(const BynavSentence &sentence,
                     const ros::Time &stamp) noexcept(false);

  static constexpr uint32_t SECONDS_PER_WEEK = 604800;
  static constexpr double IMU_TOLERANCE_S = 0.0002;
  static constexpr double DEGREES_TO_RADIANS = M_PI / 180.0;

  bool imu_rate_forced_;
  bool enable_imu_;
  bool use_micro_imu_msg_;

  double utc_offset_;

  std::string nmea_buffer_;

  BynavMessageExtractor extractor_;

  BestposParser bestpos_parser_;
  BestGNSSposParser bestgnsspos_parser_;
  PtnlPJKParser ptnlpjk_parser_;
  BynavVelocityParser bestvel_parser_;
  HeadingParser heading_parser_;
  CorrImuDataParser corrimudata_parser_;
  CorrImuDataSParser corrimudatas_parser_;
  GpggaParser gpgga_parser_;
  GpgsvParser gpgsv_parser_;
  GphdtParser gphdt_parser_;
  GprmcParser gprmc_parser_;
  InspvaParser inspva_parser_;
  InspvaxParser inspvax_parser_;
  InsstdevParser insstdev_parser_;
  GpdopParser gpdop_parser_;

  RawIMUSParser rawimus_parser_;
  RawIMUParser rawimu_parser_;

  BdsephemerisbParser bdsephemerisb_parser_;
  GaleephemerisbParser galephemerisb_parser_;
  GloephemerisbParser gloephemerisb_parser_;
  GpsephembParser gpsephemb_parser_;
  QzssephemerisbParser qzssephemerisb_parser_;
  RangrcmpbParser rangecmpb_parser_;

  boost::circular_buffer<bynav_gps_msgs::RawIMUPtr> rawimu_msgs_;
  boost::circular_buffer<bynav_gps_msgs::BynavCorrectedImuDataPtr>
      corrimudata_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GpggaPtr> gpgga_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GpgsvPtr> gpgsv_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GphdtPtr> gphdt_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GprmcPtr> gprmc_msgs_;
  boost::circular_buffer<sensor_msgs::msg::ImuPtr> imu_msgs_;
  boost::circular_buffer<bynav_gps_msgs::InspvaPtr> inspva_msgs_;
  boost::circular_buffer<bynav_gps_msgs::InspvaxPtr> inspvax_msgs_;
  boost::circular_buffer<bynav_gps_msgs::InsstdevPtr> insstdev_msgs_;
  boost::circular_buffer<bynav_gps_msgs::BynavPositionPtr> bynav_positions_;
  boost::circular_buffer<bynav_gps_msgs::BynavPositionPtr>
      bynav_gnss_positions_;
  boost::circular_buffer<bynav_gps_msgs::PtnlPJKPtr> bynav_pjk_positions_;
  boost::circular_buffer<bynav_gps_msgs::BynavVelocityPtr> bynav_velocities_;
  boost::circular_buffer<bynav_gps_msgs::BynavPositionPtr> bestpos_sync_buffer_;
  boost::circular_buffer<bynav_gps_msgs::BynavVelocityPtr> bestvel_sync_buffer_;
  boost::circular_buffer<bynav_gps_msgs::HeadingPtr> heading_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GpdopPtr> gpdop_msgs_;
  boost::circular_buffer<bynav_gps_msgs::TimePtr> time_msgs_;

  boost::circular_buffer<bynav_gps_msgs::GnssEphemMsgPtr> bdsephemerisb_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GnssEphemMsgPtr> galephemerisb_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GnssGloEphemMsgPtr>
      gloephemerisb_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GnssEphemMsgPtr> gpsephemb_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GnssEphemMsgPtr> qzssephemerisb_msgs_;
  boost::circular_buffer<bynav_gps_msgs::GnssMeasMsgPtr> rangecmpb_msgs_;

  bynav_gps_msgs::GpdopPtr latest_gpdop_;

  std::queue<bynav_gps_msgs::BynavCorrectedImuDataPtr> corrimudata_queue_;
  std::queue<bynav_gps_msgs::InspvaPtr> inspva_queue_;
  bynav_gps_msgs::InsstdevPtr latest_insstdev_;
  double imu_rate_;
};
} // namespace bynav_gps_driver

#endif // BYNAV_NMEA_H_
