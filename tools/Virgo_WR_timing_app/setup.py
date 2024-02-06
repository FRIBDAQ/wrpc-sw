import subprocess
import os

linux_packages = ["snmp-mibs-downloader", "libxcb-cursor0"]
python_packages = ["pysnmp-pysmi", "pyQt6", "pysnmp"]

def install_linux_packages(packages):
    for package in packages:
        subprocess.run(["sudo", "apt-get", "install", "-y", package])

def install_python_packages(packages):
    for package in packages:
        subprocess.run(["pip", "install", package])

def remove_last_two_directories(path):
    components = path.split('/')

    # Check if the path has more than 3 components
    if len(components) > 3:
        components = components[:-2]
        updated_path = '/'.join(components)

        return updated_path
    else:
        return path


def convert_mib(path):
    # Expand the tilde in the path
    mibdump_path = os.path.expanduser("~/.local/bin/mibdump")
    # passed a path variable since relative paths were buggy
    mib_file_path = os.path.abspath(path + "/boards/spec7/WR-WRPC-MIB-SPEC7.txt")

    command = [mibdump_path, mib_file_path]
    subprocess.run(command)

if __name__ == '__main__':
    # Install Linux packages
    install_linux_packages(linux_packages)

    # Install Python packages
    install_python_packages(python_packages)
    
    # Get the path where wrpc-sw is located.
    current_path = os.getcwd()
    wrpc_sw_path = remove_last_two_directories(current_path)

    # Convert MIB from .txt to .py
    convert_mib(wrpc_sw_path)
    