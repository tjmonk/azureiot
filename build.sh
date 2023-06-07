#!/bin/sh

basedir=`pwd`

# Get and Build the Azure IOT SDK
git clone https://github.com/Azure/azure-iot-sdk-c
cd azure-iot-sdk-c
git submodule init
git submodule update
sudo apt-get update
sudo apt-get install -y cmake build-essential curl libcurl4-openssl-dev libssl-dev uuid-dev ca-certificates
mkdir -p cmake && cd cmake
cmake ..
cmake --build .
sudo make install
cd $basedir

# build the iothub service
mkdir -p build && cd build
cmake ..
make
sudo make install
cd ..
