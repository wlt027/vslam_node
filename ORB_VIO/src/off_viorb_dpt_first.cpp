/*
 * July 19 2017, He Zhang, hxzhang1@ualr.edu  
 * 
 *  offline mode, run viorb using robocane dataset, and the first dpt frame derives scale
 *  
 * */

#include <iostream>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <string>
#include <vector>

#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/Image.h>

#include <opencv2/core/core.hpp>
#include "../../../include/System.h"
#include "MsgSync/MsgSynchronizer.h"
#include "../../../src/IMU/imudata.h"
#include "../../../src/IMU/configparam.h"
#include "imu_vn100.h"

using namespace std; 

bool g_stop = false; // whether to quit the program 

namespace Eigen{
  // typedef Matrix<double, 3, 1> Vector3d; 
  // typedef Matrix<double, 6, 1> Vector6d; 
}

class Msg
{
  public:
    enum MSG_TYPE { IMU_MSG = 1, IMG_MSG, INVALID_MSG=-1} ;
    Msg(string dataDir); // the dataset's directory 
    bool LoadRGBDIr2(const string &strAssociationFilename, vector<string> &vstrImageFilenamesRGB, vector<string> &vstrImageFilenamesD, vector<string> &vstrIr1, vector<string>& strIr2, vector<double> &vTimestamps);

    MSG_TYPE currMsg(cv::Mat& img, cv::Mat& dpt, Eigen::Vector6d& v6, double & timestamp); 
    bool moveNext(MSG_TYPE ); 
    bool moveNextImg(); 
    bool moveNextIMU(); 
    CImuVn100 m_imu_reader;

    // image 
    int m_img_index;  
    vector<double> mvTimeImg; 
    vector<string> mvRgb; 
    vector<string> mvDpt; 
    vector<string> mvIr1;
    vector<string> mvIr2;
    string mDataDir; 
    bool mbDataReady; 
};

bool initMsg(ORB_SLAM2::System& SLAM, sensor_msgs::ImageConstPtr& imageMsg, cv::Mat& dptImg, std::vector<sensor_msgs::ImuConstPtr>& vimuMsg); 

void handleMsg(ORB_SLAM2::System& SLAM, sensor_msgs::ImageConstPtr& imageMsg, std::vector<sensor_msgs::ImuConstPtr>& vimuMsg  );  // SLAM to handle this msg 


int main(int argc, char* argv[])
{
  ros::init(argc, argv, "viorb_dpt_first"); 
  ros::start(); 
  ros::NodeHandle n; 

  // Create SLAM system. It initializes all system threads and gets ready to process frames.
  ORB_SLAM2::System SLAM(argv[1],argv[2],ORB_SLAM2::System::MONOCULAR,true);

  ORB_SLAM2::ConfigParam config(argv[2]);
  
  string dataDir = config._bagfile; // change this bagfile to the directory containing our datasets 
  Msg msg(dataDir); 
  if(!msg.mbDataReady)
  {
    ROS_ERROR("off_viorb.cpp: msg is not ready, fail to access dataset at %s !", dataDir.c_str()); 
    return -1; 
  }
  
  double imageMsgDelaySec = config.GetImageDelayToIMU();
  ORBVIO::MsgSynchronizer msgsync(imageMsgDelaySec);

  sensor_msgs::ImageConstPtr imageMsg;
  std::vector<sensor_msgs::ImuConstPtr> vimuMsg;

  ros::Rate r(1000);

  // handle the information one by one 
  Msg::MSG_TYPE msg_type;
  bool bInitialized = false; 
  cv::Mat dpt; 

  do{
    cv::Mat img; 
    Eigen::Vector6d imu; 
    double timestamp; 
    msg_type = msg.currMsg(img, dpt, imu, timestamp);
    
    if(msg_type == Msg::INVALID_MSG) break; 
    
    if(msg_type == Msg::IMU_MSG) // imu msg 
    {
      sensor_msgs::Imu ss_imu; 
      ss_imu.header.stamp.fromSec(timestamp); 
      ss_imu.angular_velocity.x  = imu(0); 
      ss_imu.angular_velocity.y  = imu(1); 
      ss_imu.angular_velocity.z  = imu(2); 
      ss_imu.linear_acceleration.x = imu(3); 
      ss_imu.linear_acceleration.y = imu(4); 
      ss_imu.linear_acceleration.z = imu(5); 
      sensor_msgs::ImuConstPtr simu(new sensor_msgs::Imu(ss_imu)); 
      msgsync.imuCallback(simu); 
    }else if(msg_type == Msg::IMG_MSG) // image msg 
    {
      cv_bridge::CvImage ss_img;
      ss_img.header.stamp.fromSec(timestamp); 
      ss_img.encoding = sensor_msgs::image_encodings::BGR8; 

      ss_img.image = img.clone(); 
      sensor_msgs::Image ros_img; 
      ss_img.toImageMsg(ros_img);
      sensor_msgs::ImageConstPtr simage(new sensor_msgs::Image(ros_img));
      msgsync.imageCallback(simage); 
    }
    
    bool bdata = msgsync.getRecentMsgs(imageMsg, vimuMsg); 
    if(bdata)
    {
      if(!bInitialized)
      {
        ROS_INFO("before initMsg"); 
        bInitialized = initMsg(SLAM, imageMsg, dpt, vimuMsg); 
      }
      else
        handleMsg(SLAM, imageMsg, vimuMsg); 
    }else
    {
     // ROS_WARN("off_viorb.cpp: failed to getRecentMsg!");
    }

    if(!msg.moveNext(msg_type))
    {
      ROS_WARN("off_viorb.cpp: finish all msgs!"); 
      break; 
    }
    ros::spinOnce(); 
    r.sleep();
    if(!ros::ok())
      break; 

  }while(!g_stop && msg_type != Msg::INVALID_MSG);

  // Save camera trajectory
  //SLAM.SaveKeyFrameTrajectoryTUM("KeyFrameTrajectory.txt");
  SLAM.SaveKeyFrameTrajectoryNavState(config._tmpFilePath+"KeyFrameNavStateTrajectory.txt");

  cout<<endl<<endl<<"press any key to shutdown"<<endl;
  getchar();

  // Stop all threads
  SLAM.Shutdown();

  ros::shutdown();

  return 0; 
}

bool initMsg(ORB_SLAM2::System& SLAM, sensor_msgs::ImageConstPtr& imageMsg, cv::Mat& dptImg, std::vector<sensor_msgs::ImuConstPtr>& vimuMsg)
{
  std::vector<ORB_SLAM2::IMUData> vimuData; 
  for(unsigned int i=0; i<vimuMsg.size(); i++)
  {
    sensor_msgs::ImuConstPtr imuMsg = vimuMsg[i]; 
    double ax = imuMsg->linear_acceleration.x; 
    double ay = imuMsg->linear_acceleration.y; 
    double az = imuMsg->linear_acceleration.z; 
    ORB_SLAM2::IMUData imudata(imuMsg->angular_velocity.x,imuMsg->angular_velocity.y,imuMsg->angular_velocity.z,ax,ay,az,imuMsg->header.stamp.toSec());
    vimuData.push_back(imudata); 
  }
  
  cv_bridge::CvImageConstPtr cv_ptr = cv_bridge::toCvShare(imageMsg); 
  cv::Mat im = cv_ptr->image.clone(); 
  
  // cv::imshow("dpt_img", dptImg);
  // cv::waitKey(3); 
  // cv::imshow("color_img", im);
  // cv::waitKey(0); 

  return SLAM.InitMonoVIWithDpt(im, dptImg, vimuData, imageMsg->header.stamp.toSec()); 
}

void handleMsg(ORB_SLAM2::System& SLAM, sensor_msgs::ImageConstPtr& imageMsg, std::vector<sensor_msgs::ImuConstPtr>& vimuMsg)
{
  std::vector<ORB_SLAM2::IMUData> vimuData; 
  for(unsigned int i=0; i<vimuMsg.size(); i++)
  {
    sensor_msgs::ImuConstPtr imuMsg = vimuMsg[i]; 
    double ax = imuMsg->linear_acceleration.x; 
    double ay = imuMsg->linear_acceleration.y; 
    double az = imuMsg->linear_acceleration.z; 
    ORB_SLAM2::IMUData imudata(imuMsg->angular_velocity.x,imuMsg->angular_velocity.y,imuMsg->angular_velocity.z,ax,ay,az,imuMsg->header.stamp.toSec());
    vimuData.push_back(imudata); 
  }
  
  cv_bridge::CvImageConstPtr cv_ptr = cv_bridge::toCvShare(imageMsg); 
  cv::Mat im = cv_ptr->image.clone(); 
  
  // 
  cv::Mat gray; 
  if(im.type() != CV_8UC1)
  {
    cvtColor(im, gray, CV_RGB2GRAY); 
  }else
    gray = im; 

  // histogram equlization 
  cv::Mat hist_img; 
  // cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8));
  // clahe->apply(gray, hist_img);
  hist_img = gray; 

  // SLAM.TrackMonoVI(im, vimuData, imageMsg->header.stamp.toSec()); 
  SLAM.TrackMonoVI(hist_img, vimuData, imageMsg->header.stamp.toSec()); 

  bool bstop = false; 
  usleep(1000*3); 
  while(!SLAM.bLocalMapAcceptKF())
  {
    if(!ros::ok())
      g_stop = true; 
    usleep(10);
  }
  return ; 
}

Msg::Msg(string dataDir)  : 
  mDataDir(dataDir),
  m_img_index(0),
  mbDataReady(false)
{
  // load imu 
  string imu_file = dataDir + "/imu_vn100.log"; 
  if(!m_imu_reader.readImuData(imu_file))
  {
    ROS_ERROR("off_viorb.cpp: failed to read imu_file %s", imu_file.c_str()); 
    return; 
  }
  
  // load image index 
  string index_file = dataDir + "/timestamp.txt"; 
  if(!LoadRGBDIr2(index_file, mvRgb, mvDpt, mvIr1, mvIr2, mvTimeImg))
  {
    ROS_ERROR("off_viorb.cpp: failed to read img data %s", index_file.c_str()); 
    return; 
  }else
  {
    ROS_INFO("off_viorb.cpp: succeed to read %d images", mvRgb.size()); 
  }
  
  // data is ready 
  mbDataReady = true; 
}

Msg::MSG_TYPE Msg::currMsg(cv::Mat& rgb, cv::Mat& dpt, Eigen::Vector6d& v6, double &timestamp)
{
  MSG_TYPE ret_msg_type = INVALID_MSG; 
  double image_curr_time = mvTimeImg[m_img_index]; 
  double imu_curr_time = m_imu_reader.getCurrTimeStamp(); 

  if(imu_curr_time <= image_curr_time) // output an IMU measurement 
  {
    timestamp = imu_curr_time; 
    m_imu_reader.getCurrMeasurement(v6); 
    ret_msg_type = IMU_MSG; 
  }else // output an image frame
  {
    timestamp = image_curr_time; 
    string rgb_file = mDataDir+"/"+mvRgb[m_img_index];
    string dpt_file = mDataDir+"/"+mvDpt[m_img_index]; 
    rgb = cv::imread(rgb_file.c_str(), -1); 
    if(rgb.empty())
    {
      ROS_ERROR("off_viorb.cpp: failed to load rgb file %s", rgb_file.c_str());
      return ret_msg_type; 
    }
    dpt = cv::imread(dpt_file.c_str(), -1); 
    if(dpt.empty())
    {
      ROS_ERROR("off_viorb_dpt_first.cpp: failed to load dpt file %s", dpt_file.c_str()); 
      return ret_msg_type; 
    }
    ret_msg_type = IMG_MSG; 
  }
  
  return ret_msg_type; 
}

bool Msg::moveNext(Msg::MSG_TYPE type)
{
  if(type == IMU_MSG)
    return moveNextIMU();
  else if(type == IMG_MSG)
    return moveNextImg(); 
  ROS_ERROR("type is neither IMU nor IMG ");
  return false;
}
bool Msg::moveNextImg()
{
  if(++m_img_index >= mvRgb.size())
  {
    ROS_INFO("off_viorb.cpp: rgb image has all handled! m_img_index = %d mvRgb.size = %d", m_img_index, mvRgb.size()); 
    return false; 
  }
  return true; 
}

bool Msg::moveNextIMU()
{
  if(!m_imu_reader.moveNext())
  {
    ROS_INFO("off_viorb.cpp: imu measurement has all handled!"); 
    return false;
  }
  return true;
}

bool Msg::LoadRGBDIr2(const string &strAssociationFilename, vector<string> &vstrImageFilenamesRGB, vector<string> &vstrImageFilenamesD, vector<string> &vstrIr1, vector<string>& vstrIr2, vector<double> &vTimestamps)
{
  ifstream fAssociation;
  fAssociation.open(strAssociationFilename.c_str());
  if(!fAssociation.is_open()) 
    return false; 
  string s; 
  getline(fAssociation, s); // exclude the first line 
  while(!fAssociation.eof())
  {
    string s;
    getline(fAssociation,s);
    if(!s.empty())
    {
      stringstream ss;
      ss << s;
      double t;
      string sRGB, sD, sIr1, sIr2;
      ss >> t;
      // printf("load Images line: %s\n", ss.str().c_str());
      // printf("loadImages t = %.9lf\n", t);
      vTimestamps.push_back(t);
      ss >> sRGB;
      vstrImageFilenamesRGB.push_back(sRGB);
      ss >> t;   ss >> sD;
      vstrImageFilenamesD.push_back(sD);
      ss >> t;   ss >> sIr1;
      vstrIr1.push_back(sIr1); 
      ss >> t;   ss >> sIr2; 
      vstrIr2.push_back(sIr2); 
    }
  }
  return true; 
}








