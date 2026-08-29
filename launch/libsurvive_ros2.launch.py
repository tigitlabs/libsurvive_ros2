# Copyright 2022 Andrew Symington
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, OpaqueFunction
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def _launch_setup(context):
    force_recalibrate = LaunchConfiguration(
        'force_recalibrate').perform(context).strip()
    driver_args = '--disable-calibrate'
    if force_recalibrate == 'true':
        driver_args = '--force-calibrate --disable-calibrate'

    parameters = [
        {'driver_args': driver_args},
        {'imu_topic': 'imu'},
        {'joy_topic': 'joy'},
        {'cfg_topic': 'cfg'},
        {'velocity_topic': 'velocity'},
        {'battery_topic': 'battery'},
        {'occlusion_topic': 'occlusion'},
        {'lighthouse_rate': 4.0}
    ]

    # Non-composable launch (regular node)
    libsurvive_node = Node(
        package='libsurvive_ros2',
        executable='libsurvive_ros2_node',
        name='libsurvive_ros2_node',
        namespace=LaunchConfiguration('namespace'),
        output='screen',
        parameters=parameters)

    return [
        libsurvive_node,
    ]


def generate_launch_description():
    arguments = [
        DeclareLaunchArgument('namespace', default_value='libsurvive',
                              description='Namespace for the non-TF topics'),
        DeclareLaunchArgument('force_recalibrate', default_value='false',
                              choices=['true', 'false'],
                              description='Recompute Lighthouse poses at startup'),
    ]

    return LaunchDescription(arguments + [OpaqueFunction(function=_launch_setup)])
