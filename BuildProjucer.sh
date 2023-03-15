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
"$FRUT_PATH/bin/Jucer2CMake" reprojucer "$JUCERPROJ" "$FRUT_PATH/cmake/Reprojucer.cmake"

if [ ! -d  "$BUILD_DIR" ]; then
mkdir "$BUILD_DIR"
fi

cd "$BUILD_DIR"

if [ $currentos = MinGw ]; then 
cmake .. -G "Visual Studio 17 2022"
elif [ $currentos = Linux ]; then
cmake .. -G "Unix Makefiles"
else
cmake .. -G "Xcode" -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
fi

cmake --build . --config "Release" --parallel 8



if [ $currentos = MinGw ]; then 
BUILD="$BUILD_DIR/Release/App/Projucer.exe"
elif [ $currentos = Mac ]; then 
BUILD="$BUILD_DIR/Release/Projucer.app"
elif [ $currentos = Linux ]; then 
BUILD="$BUILD_DIR/Projucer"
fi

"$BUILD"