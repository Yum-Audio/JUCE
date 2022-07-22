#!/bin/sh
apt-get update
apt-get install -y g++ libgtk-3-dev libfreetype6-dev libx11-dev libxinerama-dev libxrandr-dev libxcursor-dev mesa-common-dev libasound2-dev freeglut3-dev libxcomposite-dev libcurl4-openssl-dev
add-apt-repository -y ppa:webkit-team && sudo apt-get update
apt-get install libwebkit2gtk-4.0-37 libwebkit2gtk-4.0-dev
