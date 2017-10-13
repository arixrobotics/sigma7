//==============================================================================
/*
    Software License Agreement (BSD License)
    Copyright (c) 2016, Nearlab
    Refer to Readme.txt for full license text

    \author    <http://nearlab.polimi.it/>
    \author    Nima Enayati
    \version   -
*/
//==============================================================================

#include "SigmaDevice.hpp"
int SigmaDevice::id = -1;

//-----------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------
SigmaDevice::SigmaDevice(ros::NodeHandle n,
                                     const std::string ns)
        : new_wrench_msg(false)
{
    id++;

    // setup the publishers and the subscriber
    pub_pose = n.advertise<geometry_msgs::PoseStamped>(ns+"/pose",1, 0);
    pub_twist = n.advertise<geometry_msgs::TwistStamped>(ns+"/twist", 1, 0);
    pub_gripper = n.advertise<std_msgs::Float64>(ns+"/gripper_angle", 1, 0);
    pub_button  = n.advertise<std_msgs::Int8>(ns+"/button", 1, 0);
    pub_pedal = n.advertise <std_msgs::Int8> (ns+"/pedal", 1, 0);

    sub_wrench	= n.subscribe("/sigma/"+ns+"/forceFeedback", 1,
                                    &SigmaDevice::WrenchCallback, this);

    // params
    n.param<bool>("enable_gripper_button", enable_gripper_button, 0);
    n.param<bool>("lock_orientation", lock_orient, 0);
}


//------------------------------------------------------------------------------
// WrenchCallback
void SigmaDevice::WrenchCallback(
        const geometry_msgs::WrenchStampedConstPtr &msg) {
    //newDataDirect = true;
    wrench.wrench = msg->wrench;
    new_wrench_msg = true;
}

//------------------------------------------------------------------------------
// CalibrateDevice
int SigmaDevice::CalibrateDevice() {

    // open device
    if (drdOpenID ((char)id) < 0) {
        ROS_ERROR("No device %i found. dhd says: %s", id, dhdErrorGetLastStr());
        dhdSleep (2.0);
        drdClose ((char)id);
        return -1;
    }

    //Calibrate the device if it is not already calibrated;
    if(drdIsInitialized((char)id)){
        ROS_INFO("Device %i is already calibrated.", id);
    }
    else if(drdAutoInit((char)id)<0) {
        ROS_ERROR("Initialization of device %i failed. dhd says: (%s)", id,
                  dhdErrorGetLastStr ());
        dhdSleep(2.0);
    }

    // // center of workspace
    //	 double nullPose[DHD_MAX_DOF] = { 0.0, 0.0, 0.0, //base  (translations)
    //	                                  0.0, 0.0, 0.0, //wrist (rotations)
    //	                                  0.0 };         //gripper
    // //move to center
    // drdMoveTo (nullPose);

    // stop regulation (and leave force enabled)
    drdStop(true, (char)id);

    // enable force
    dhdEnableForce (DHD_ON, (char)id);

    //Enable the gripper button
    if(enable_gripper_button)
        dhdEmulateButton(DHD_ON, (char)id);

    ROS_INFO("Device %i ready.", id);
    return 0;
}

int SigmaDevice::ReadMeasurementsFromDevice() {

    // -------------------------------
    // Pose
    double p[3];
    double orient_m[3][3];
    //Reading the data from the device
    dhdGetPositionAndOrientationFrame(&p[0], &p[1], &p[2], orient_m, (char)id);

    // convert to pose message
    KDL::Rotation rot;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) { rot(r, c) = orient_m[r][c]; }
    }
    tf::poseKDLToMsg(KDL::Frame(rot, KDL::Vector(p[0],p[1],p[2])),
                     pose_msg.pose);
    // stamp the msg
    pose_msg.header.stamp = ros::Time::now();

    // -----------------------------
    // Twist
    double v[6];

    dhdGetLinearVelocity(&v[0], &v[1], &v[2], (char)id);
    dhdGetAngularVelocityRad(&v[3], &v[4], &v[5], (char)id);
    // convert to twist message
    twist_msg.twist.linear.x = v[0];
    twist_msg.twist.linear.y = v[1];
    twist_msg.twist.linear.z = v[2];
    twist_msg.twist.angular.x = v[3];
    twist_msg.twist.angular.y = v[4];
    twist_msg.twist.angular.z = v[5];
    // stamp the msg
    twist_msg.header.stamp = ros::Time::now();

    // ------------------------------
    // gripper
    dhdGetGripperAngleRad(&gripper_angle.data);

    // ------------------------------
    // buttons
    // saving the previous states of button and pedal
    sigma_button_previous_state = sigma_button_state;
    sigma_button_state.data = dhdGetButton(0, (char)id);

    pedal_previous_state = pedal_state;
    pedal_state.data = dhdGetButton(1, (char)id);
    return 0;
}

void SigmaDevice::PublishPoseTwistButtonPedal() {

    pub_pose.publish(pose_msg);
    pub_twist.publish(twist_msg);

    // Publish gripper angle
    pub_gripper.publish(gripper_angle);

    // publish button when there is a change
    if(sigma_button_state.data != sigma_button_previous_state.data){
        pub_button.publish(sigma_button_state);
    }

    //Publish pedal only when it changes
    if(pedal_state.data != pedal_previous_state.data){
        pub_pedal.publish(pedal_state);
    }

}

void SigmaDevice::HandleWrench() {

    // should we use new_wrench_msg?

    if(pedal_state.data == 1) {
        if (dhdSetForceAndTorqueAndGripperForce(wrench.wrench.force.x,
                                                wrench.wrench.force.y,
                                                wrench.wrench.force.z,
                                                wrench.wrench.torque.x,
                                                wrench.wrench.torque.y,
                                                wrench.wrench.torque.z,
                                                0.0, (char)id) < DHD_NO_ERROR){
            printf("error: cannot set force (%s)\n", dhdErrorGetLastStr());
        }
        dhdGetOrientationRad(&locked_orient[0], &locked_orient[1],&locked_orient[2]);
    }
    else if (lock_orient){
        drdRegulatePos  (false);
        drdRegulateRot  (true);
        drdRegulateGrip (false);
        drdStart();
        drdMoveToRot (locked_orient[0], locked_orient[1],locked_orient[2]);
        drdStop(true);
    }

}
