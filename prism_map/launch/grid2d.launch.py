import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.conditions import IfCondition
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    pkg = get_package_share_directory('prism_map')
    params = os.path.join(pkg, 'params', 'grid2d.yaml')
    use_rviz = LaunchConfiguration('rviz')
    return LaunchDescription([
        DeclareLaunchArgument('rviz', default_value='true'),
        Node(package='prism_map', executable='prism_map_node_main', name='prism_map',
             output='screen', parameters=[params, {'use_sim_time': True}]),
        Node(package='rviz2', executable='rviz2', name='rviz2',
             condition=IfCondition(use_rviz),
             arguments=['-d', os.path.join(pkg, 'rviz', 'prism_map.rviz')]),
    ])
