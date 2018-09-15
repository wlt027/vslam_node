#!/bin/bash

# David Z, Sep 11, 2018 (hzhang8@vcu.edu)
# 
# run rovio with given bagfile and times 
# ./run_rovio.sh [bag_dir] [bagfile] [times] 
#

cur_dir=`pwd`

# rosbag_file="/home/davidz/work/data/drone/dataset_3/rgbd_imu.bag"
# rosbag_file="/home/hzhang8/work/data/tum_vio/dataset-room4_512_16.bag"
rosbag_dir="/home/hzhang8/work/data/tum_vio"
rosbag_name="room4_512_16"
roslaunch_file="$cur_dir/../rovio/rovio_rosbag_node_tum.launch"
result_dir="$cur_dir/../result"

times=1

if [ $# -ge 1 ]; then
    # rosbag_file=$1
    rosbag_dir=$1
    echo "reset rosbag_dir: $rosbag_dir"
fi
if [ $# -ge 2 ]; then
    rosbag_name=$2
    echo "reset rosbag_name: $rosbag_name"
fi
if [ $# -ge 3 ]; then
    times_=$3
    echo "reset run times: $times_"
fi

rosbag_file="$rosbag_dir/dataset-$rosbag_name.bag"

do_it(){
    i=$1
    echo "roslaunch $roslaunch_file"
    roslaunch $roslaunch_file bag_file:=$rosbag_file >/dev/null 2>&1

    # wait for roslaunch start 
    sleep 1

    ### process the result 
    cd $result_dir
    echo "handle $result_dir"
    if [ ! -d "tum_vio_result/$rosbag_name/rovio" ]; then
	mkdir -p "tum_vio_result/$rosbag_name/rovio"
    fi
    
    ### convert from bag to log
    ../../../devel/lib/vslam_rovio/traj_bag_to_log "rovio_result.bag" "rovio_result.log"
    rm "rovio_result.info"
    mv "rovio_result.log" "tum_vio_result/$rosbag_name/rovio/result_$i.log"

    echo -ne '\n'
}

i=1
while [ $i -le $times ]; do
    echo "rovio $i"
    do_it $i
    i=$((i+1))
    sleep 2
done

echo "finish the job, return to $cur_dir"
cd $cur_dir

exit 0


