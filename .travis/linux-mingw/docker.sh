#!/bin/bash -ex

cd /yuzu
MINGW_PACKAGES="sdl2-mingw-w64 qt5base-mingw-w64 qt5tools-mingw-w64 libsamplerate-mingw-w64 qt5multimedia-mingw-w64"
apt-get update
apt-get install -y gpg wget git python3-pip python ccache g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 mingw-w64-tools cmake
echo 'deb http://ppa.launchpad.net/tobydox/mingw-w64/ubuntu bionic main ' > /etc/apt/sources.list.d/extras.list
apt-key adv --keyserver keyserver.ubuntu.com --recv '72931B477E22FEFD47F8DECE02FE5F12ADDE29B2'
apt-get update
apt-get install -y ${MINGW_PACKAGES}

# fix a problem in current MinGW headers
wget -q https://raw.githubusercontent.com/Alexpux/mingw-w64/d0d7f784833bbb0b2d279310ddc6afb52fe47a46/mingw-w64-headers/crt/errno.h -O /usr/x86_64-w64-mingw32/include/errno.h
# override Travis CI unreasonable ccache size
echo 'max_size = 3.0G' > "$HOME/.ccache/ccache.conf"

# Dirty hack to trick unicorn makefile into believing we are in a MINGW system
mv /bin/uname /bin/uname1 && echo -e '#!/bin/sh\necho MINGW64' >> /bin/uname
chmod +x /bin/uname

# Dirty hack to trick unicorn makefile into believing we have cmd
echo '' >> /bin/cmd
chmod +x /bin/cmd

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE="$(pwd)/../CMakeModules/MinGWCross.cmake" -DUSE_CCACHE=ON -DYUZU_USE_BUNDLED_UNICORN=ON -DENABLE_COMPATIBILITY_LIST_DOWNLOAD=ON -DCMAKE_BUILD_TYPE=Release
ninja

# Clean up the dirty hacks
rm /bin/uname && mv /bin/uname1 /bin/uname
rm /bin/cmd

ccache -s

echo "Tests skipped"
#ctest -VV -C Release

echo 'Prepare binaries...'
cd ..
mkdir package

QT_PLATFORM_DLL_PATH='/usr/x86_64-w64-mingw32/lib/qt5/plugins/platforms/'
find build/ -name "yuzu*.exe" -exec cp {} 'package' \;

# copy Qt plugins
mkdir package/platforms
cp "${QT_PLATFORM_DLL_PATH}/qwindows.dll" package/platforms/
cp -rv "${QT_PLATFORM_DLL_PATH}/../mediaservice/" package/
cp -rv "${QT_PLATFORM_DLL_PATH}/../imageformats/" package/
rm -f package/mediaservice/*d.dll

for i in package/*.exe; do
  # we need to process pdb here, however, cv2pdb
  # does not work here, so we just simply strip all the debug symbols
  x86_64-w64-mingw32-strip "${i}"
done

pip3 install pefile
python3 .travis/linux-mingw/scan_dll.py package/*.exe "package/"
python3 .travis/linux-mingw/scan_dll.py package/imageformats/*.dll "package/"
