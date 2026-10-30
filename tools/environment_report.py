"""Read-only Linux build dependency inventory; never installs packages or starts ROS."""
import json,platform,shutil,subprocess,sys
from pathlib import Path

def report():
    if sys.platform!='linux':raise ValueError('Run inside Ubuntu')
    packages={}
    names=('g++','cmake','make','python3-colcon-core','libcurl4-openssl-dev','libgtest-dev','nlohmann-json3-dev',
        'ros-humble-ament-cmake','ros-humble-rclcpp','ros-humble-std-msgs','ros-humble-rmw-fastrtps-cpp')
    for name in names:
        p=subprocess.run(['dpkg-query','-W','-f=${Version}\t${Status}',name],capture_output=True,text=True,timeout=5)
        value=p.stdout.split('\t',1)
        packages[name]={'installed':p.returncode==0 and len(value)==2 and value[1]=='install ok installed',
            'version':value[0] if p.returncode==0 else None}
    tools={name:shutil.which(name) for name in ('c++','cmake','colcon','make','python3')}
    root=Path(__file__).resolve().parent.parent
    return {'read_only':True,'architecture':platform.machine(),'os_release':Path('/etc/os-release').read_text().splitlines(),
        'tools':tools,'packages':packages,'ros_humble_setup_present':Path('/opt/ros/humble/setup.bash').is_file(),
        'bundled_json_present':(root/'contestant_ws/src/rmtc_bot/vendor/include/nlohmann/json.hpp').is_file(),
        'notes':['GoogleTest is needed only for test builds','nlohmann/json uses system 3.10+ or bundled v3.11.3',
            'Inventory is not a clean OS, container or restored VM verification'],
        'network_requests':0,'system_changes':0}

if __name__=='__main__':
    try:print(json.dumps(report(),ensure_ascii=False,indent=2))
    except (OSError,ValueError,subprocess.TimeoutExpired):raise SystemExit('Could not read build dependency inventory')
