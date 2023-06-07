# iothub
Azure IOTHub Client

## Overview

The Azure IOTHub client is a wrapper for the Azure IOT SDK client library.  It runs as a service and provides a single interface for
other IOT clients to send and receive IOT messages to an Azure IOT Hub.

It provides a message queue for interfacing with the IOT client applications.  The libiotclient library provides APIs for communicating
with the iothub service to send and receive messages.  This keeps the
IOT clients simple and encapsulates all of the complexity of communicating with the Azure IOT SDK inside the iothub service.


## Prerequisites

The iothub service requires the following components:

- varserver : variable server ( https://github.com/tjmonk/varserver )
- tjson : JSON parser library ( https://github.com/tjmonk/libtjson )
- paho_mqtt3a : Paho MQTT3 Async library ( https://github.com/eclipse/paho.mqtt.c )

## Build

```
./build.sh
```
