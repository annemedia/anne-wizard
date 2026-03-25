#!/bin/bash
cd ~/Documents/ANNEWizard
svn up
rm -rf build
rm annewizard.AppImage
cmake -B build && cmake --build build

rm -rf annewizard-portable
mkdir -p annewizard-portable

cp build/annewizard annewizard-portable/
cp assets/annewizard.png annewizard-portable/

cat > annewizard-portable/annewizard.desktop << 'EOF'
[Desktop Entry]
Name=ANNE Wizard
Comment=ANNE Setup Wizard
Exec=annewizard
Icon=annewizard
Type=Application
Terminal=false
Categories=Utility;
StartupWMClass=ANNEWizard
StartupNotify=true
EOF

# Find the qmake in your Qt installation
QT6_QMAKE=$(find ~/Qt -name "qmake" -type f | grep 6. | head -1)
if [ -z "$QT6_QMAKE" ]; then
    QT6_QMAKE=$(find ~/Qt -name "qmake6" -type f | head -1)
fi

echo "Using qmake: $QT6_QMAKE"

# Run linuxdeployqt to create AppImage
./linuxdeployqt-continuous-x86_64.AppImage annewizard-portable/annewizard.desktop \
    -qmake="$QT6_QMAKE" \
    -verbose=2 \
    -appimage

# or use appimagetool
 ./appimagetool-x86_64.AppImage annewizard-portable annewizard.AppImage