
# https://download.qt.io/official_releases/online_installers/
wget https://download.qt.io/official_releases/online_installers/qt-online-installer-linux-x64-online.run

# or
sudo dnf update -y
sudo dnf install @development-tools git cmake
sudo dnf install qt6-qtbase-devel qt6-qttools-devel qt6-qtdeclarative-devel qt6-qtsvg-devel unzip wget curl libxkbcommon-devel patchelf

sudo apt update && sudo apt upgrade -y
sudo apt install -y build-essential git cmake
sudo apt install -y qt6-base-dev qt6-tools-dev qt6-declarative-dev qt6-svg-dev unzip wget curl libxkbcommon-dev patchelf

