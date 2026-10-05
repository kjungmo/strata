import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg = get_package_share_directory('strata')
    params = LaunchConfiguration('params_file')
    use_rviz = LaunchConfiguration('rviz')
    return LaunchDescription([
        DeclareLaunchArgument('rviz', default_value='true'),
        DeclareLaunchArgument('params_file',
                              default_value=os.path.join(pkg, 'params', 'grid2d.yaml')),
        Node(package='strata', executable='strata_node_main', name='strata',
             output='screen', parameters=[params, {'use_sim_time': True}]),
        Node(package='rviz2', executable='rviz2', name='rviz2',
             condition=IfCondition(use_rviz),
             arguments=['-d', os.path.join(pkg, 'rviz', 'strata.rviz')]),
    ])
