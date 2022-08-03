cmake_minimum_required(VERSION 2.8.3)
project(bynav_gps_msgs)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 14)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

set(srv_files
  BynavFRESET.srv
)

set(msg_files
  Gpatr.msg
  Gpdop.msg
  Gpfpd.msg
  Gpgga.msg
  Gpgst.msg
  Gpgsv.msg
  Gphdt.msg
  Gphpd.msg
  Gpntr.msg
  Gpori.msg
  Gprmc.msg
  Gpvtg.msg
  Gptra.msg
  Gpzda.msg
  ICOMConfig.msg
  Insatt.msg
  Inspos.msg
  Inspva.msg
  Inspvax.msg
  Insspd.msg
  Insstdev.msg
  Insvel.msg
  Raw.msg
  Rtcm.msg
  InterfaceMode.msg
  MarkTime.msg
  NtripPort.msg
  BynavConfig.msg
  PtnlPJK.msg
  PtnlAvr.msg
  BynavConfigQuery.msg
  BynavCorrectedImuData.msg
  BynavExtendedSolutionStatus.msg
  Heading.msg
  BynavPosition.msg
  Gpdop.msg
  BynavReceiverStatus.msg
  BynavSignalMask.msg
  Psrvel.msg
  PtnlPJK.msg
  Satellite.msg
  SerialConfig.msg
  Time.msg
  Pashr.msg
  FrequencyOut.msg
  RefStation.msg
  BynavVelocity.msg
  RawIMU.msg
  BynavMessageHeader.msg
  GnssEphemMsg.msg
  GnssGloEphemMsg.msg
  GnssMeasMsg.msg
  GnssObsMsg.msg
  GnssTimeMsg.msg
  WorkFREQS.msg
)

rosidl_generate_interfaces( bynav_gps_msgs
  ${msg_files}
  ${srv_files}
  DEPENDENCIES builtin_interfaces std_msgs
  ADD_LINTER_TESTS
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()