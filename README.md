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
- libiotclient : IOT Client library ( https://github.com/tjmonk/libiotclient )
- azure-iot-skd-c : Azure IOT SDK C library ( https://github.com/Azure/azure-iot-sdk-c )

## Build

The build script will automatically download the azure-iot-sdk-c library and build and install it.  It will then build and install the iothub service.

```
./build.sh
```

## Set up an Azure IOT Hub

To use the iothub service you will need an Azure IOTHub created in the Azure cloud.  You can visit portal.azure.com to set up a free account.
Once your free account is created, create an IoT Hub.  You can create
a free IoT Hub which supports up to 8000 messages/day.  With your new IoT Hub created, create a new Device.  When creating a new device, select "Symmetric Key" Authentication type, and Auto-generate keys.
Once the device is created, copy its primary connection string.  You will need this when running the iothub service.

## Run the iothub service

The IOTHUB service uses a connection string to connect to the Azure IOT Hub.  The connection string can be specified using the -c command line argument when running the iothub service.  Get the connection string
from the IOTHub devices view in the Azure portal.  Select the device you created in the previous step, and copy its primary connection string.  Specify the connection string in double quotes.

Run the iothub service in verbose mode so you can observe its operation.

```
iothub -v -c "iothub -v -c "HostName=my-iot-hub.azure-devices.net;DeviceId=device-001;SharedAccessKey=ROpU5sG+XRIFWJeJGWCm7xLv8VIYyGx6uKfyNjPduAs="
```

You should see something similar to the following:

```
Connected
-> Header (AMQP 0.1.0.0)
<- Header (AMQP 0.1.0.0)
-> [OPEN]* {4de142ca-8b13-4498-a93c-a1d99140330c,tgmy-iot-hub.azure-devices.net,4294967295,65535,240000}
<- [OPEN]* {DeviceGateway_fc6fc7cafdf54f52bcfd14e4cbc4e5dd,localhost,65536,8191,120000,NULL,NULL,NULL,NULL,NULL}
-> [BEGIN]* {NULL,0,4294967295,100,4294967295}
<- [BEGIN]* {0,1,5000,4294967295,262143,NULL,NULL,NULL}
-> [ATTACH]* {$cbs-sender,0,false,0,0,* {$cbs},* {$cbs},NULL,NULL,0,0}
-> [ATTACH]* {$cbs-receiver,1,true,0,0,* {$cbs},* {$cbs},NULL,NULL,NULL,0}
<- [ATTACH]* {$cbs-sender,0,true,0,0,* {$cbs,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {$cbs,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,NULL,1048576,NULL,NULL,NULL}
<- [FLOW]* {0,5000,1,4294967295,0,0,100,0,NULL,false,NULL}
-> [TRANSFER]* {0,0,<01 00 00 00>,0,false,false}
<- [ATTACH]* {$cbs-receiver,1,false,0,0,* {$cbs,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {$cbs,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,0,1048576,NULL,NULL,NULL}
-> [FLOW]* {1,4294967295,1,99,1,0,10000}
<- [DISPOSITION]* {true,0,NULL,true,* {},NULL}
<- [TRANSFER]* {1,0,<01 00 00 00>,0,NULL,false,NULL,NULL,NULL,NULL,false}
-> [DISPOSITION]* {true,0,0,true,* {}}
-> [ATTACH]* {link-snd-device-001-774b88dd-4d39-45dd-9e5f-480c42abd19a,2,false,0,0,* {link-snd-device-001-774b88dd-4d39-45dd-9e5f-480c42abd19a-source},* {amqps://my-iot-hub/devices/device-001/messages/events},NULL,NULL,0,18446744073709551615,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)]}}
-> [ATTACH]* {link-snd-device-001-25c18955-76ed-4d28-9681-ca46953f9966,3,false,0,0,* {link-snd-device-001-25c18955-76ed-4d28-9681-ca46953f9966-source},* {amqps://my-iot-hub/devices/device-001/twin},NULL,NULL,0,18446744073709551615,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)],[com.microsoft:channel-correlation-id:twin:ab764da7-69ea-4e11-b1aa-04b23c5b7e74],[com.microsoft:api-version:2020-09-30]}}
<- [ATTACH]* {link-snd-device-001-774b88dd-4d39-45dd-9e5f-480c42abd19a,2,true,0,NULL,* {link-snd-device-001-774b88dd-4d39-45dd-9e5f-480c42abd19a-source,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {amqps://my-iot-hub/devices/device-001/messages/events,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,NULL,1048576,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)]}}
<- [FLOW]* {1,5000,2,4294967295,2,0,1000,0,NULL,false,NULL}
-> [ATTACH]* {link-rcv-device-001-ca90b658-025c-4ab4-90d4-047cf4b7e335,4,true,0,0,* {amqps://my-iot-hub/devices/device-001/messages/devicebound},* {link-rcv-device-001-ca90b658-025c-4ab4-90d4-047cf4b7e335-target},NULL,NULL,NULL,65536,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)]}}
<- [ATTACH]* {link-snd-device-001-25c18955-76ed-4d28-9681-ca46953f9966,3,true,1,0,* {link-snd-device-001-25c18955-76ed-4d28-9681-ca46953f9966-source,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {amqps://my-iot-hub/devices/device-001/twin,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,NULL,1048576,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)],[com.microsoft:channel-correlation-id:twin:ab764da7-69ea-4e11-b1aa-04b23c5b7e74],[com.microsoft:api-version:2020-09-30]}}
<- [FLOW]* {1,5000,2,4294967295,3,0,1000,0,NULL,false,NULL}
-> [ATTACH]* {link-rcv-device-001-7c58c6fb-9146-4cec-8e7d-d2aecc35f3b4,5,true,0,0,* {amqps://my-iot-hub/devices/device-001/twin},* {link-rcv-device-001-7c58c6fb-9146-4cec-8e7d-d2aecc35f3b4-target},NULL,NULL,NULL,18446744073709551615,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)],[com.microsoft:channel-correlation-id:twin:ab764da7-69ea-4e11-b1aa-04b23c5b7e74],[com.microsoft:api-version:2020-09-30]}}
<- [ATTACH]* {link-rcv-device-001-ca90b658-025c-4ab4-90d4-047cf4b7e335,4,false,NULL,1,* {amqps://my-iot-hub/devices/device-001/messages/devicebound,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {link-rcv-device-001-ca90b658-025c-4ab4-90d4-047cf4b7e335-target,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,0,65536,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)]}}
-> [FLOW]* {2,4294967294,1,99,4,0,10000}
<- [ATTACH]* {link-rcv-device-001-7c58c6fb-9146-4cec-8e7d-d2aecc35f3b4,5,false,1,0,* {amqps://my-iot-hub/devices/device-001/twin,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL},* {link-rcv-device-001-7c58c6fb-9146-4cec-8e7d-d2aecc35f3b4-target,NULL,NULL,NULL,NULL,NULL,NULL},NULL,NULL,0,1048576,NULL,NULL,{[com.microsoft:client-version:iothubclient/1.11.0 (native; Linux; aarch64)],[com.microsoft:channel-correlation-id:twin:ab764da7-69ea-4e11-b1aa-04b23c5b7e74],[com.microsoft:api-version:2020-09-30]}}
-> [FLOW]* {2,4294967294,1,99,5,0,10000}
```

Congratulations!  Your iothub service is connected to the Azure IoT Hub and awaiting communication with clients.

You can exercise the iothub service with the iotsend and iotexec clients:

- https://github.com/tjmonk/iotsend
- https://github.com/tjmonk/iotexec

