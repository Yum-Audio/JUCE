#!/bin/sh

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)     machine=Linux;;
    Darwin*)    machine=Mac;;
    CYGWIN*)    machine=Cygwin;;
    MINGW*)     machine=MinGw;;
    *)          machine="UNKNOWN:${unameOut}"
esac
currentos=${machine}

BASEDIR=$(dirname "$0")
APPNAME="Projucer"

PROJUCER_DIR="$BASEDIR/extras/$APPNAME"
FRUT_PATH="$BASEDIR/../FRUT/prefix/FRUT"
JUCERPROJ="$PROJUCER_DIR/$APPNAME.jucer"
BUILD_DIR="$PROJUCER_DIR/cmakebuild"

"$FRUT_PATH/../../Build.sh"

cd "$PROJUCER_DIR"
"$FRUT_PATH/bin/Jucer2Cmake" reprojucer "$JUCERPROJ" "$FRUT_PATH/cmake/Reprojucer.cmake"

if [ ! -d  "$BUILD_DIR" ]; then
mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

if [ $currentos == MinGw ]; then 
cmake .. -G "Visual Studio 16 2019"
else
cmake .. -G "Xcode" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
fi

cmake --build . --config "Release" --parallel 8

BUILD="$BUILD_DIR/Release/Projucer.app"

"$BUILD"