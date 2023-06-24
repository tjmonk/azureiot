cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

project(iothub
	VERSION 0.1
    DESCRIPTION "Connector service for Azure IOTHub"
)

set(CMAKE_C_STANDARD 99)

find_library ( LIB_IOTHUB_CLIENT iothub_client REQUIRED )
find_library ( LIB_IOTHUB_CLIENT_AMQP_TRANSPORT iothub_client_amqp_transport REQUIRED )
find_library ( LIB_IOTHUB_CLIENT_AMQP_WS_TRANSPORT iothub_client_amqp_ws_transport REQUIRED )
find_library ( LIB_IOTHUB_CLIENT_HTTP_TRANSPORT iothub_client_http_transport REQUIRED )
find_library ( LIB_IOTHUB_CLIENT_MQTT_TRANSPORT iothub_client_mqtt_transport REQUIRED )
find_library ( LIB_IOTHUB_CLIENT_MQTT_WS_TRANSPORT iothub_client_mqtt_ws_transport REQUIRED )
find_library ( LIB_IOTHUB_SERVICE_CLIENT iothub_service_client REQUIRED )
find_library ( LIB_SERIALIZER serializer REQUIRED )
find_library ( LIB_UAMQP uamqp REQUIRED )
find_library ( LIB_UHTTP uhttp REQUIRED )
find_library ( LIB_UMQTT umqtt REQUIRED )
find_library ( LIB_UMOCK_C umock_c REQUIRED )
find_library ( LIB_SSL ssl REQUIRED )
find_library ( LIB_CRYPTO crypto REQUIRED )
find_library ( LIB_CURL curl REQUIRED )
find_library ( LIB_PTHREAD pthread REQUIRED )
find_library ( LIB_M m REQUIRED )
find_library ( LIB_RT rt REQUIRED )
find_library ( LIB_PARSON parson REQUIRED )
find_library ( LIB_UUID uuid REQUIRED )
find_package ( azure_c_shared_utility REQUIRED CONFIG )

add_executable( ${PROJECT_NAME}
	src/iothub.c
)

target_include_directories( ${PROJECT_NAME}
	PRIVATE inc
)

target_link_libraries( ${PROJECT_NAME}
	${CMAKE_THREAD_LIBS_INIT}
	${LIB_SERIALIZER}
	${LIB_IOTHUB_CLIENT_HTTP_TRANSPORT}
	${LIB_IOTHUB_CLIENT_AMQP_TRANSPORT}
	${LIB_IOTHUB_CLIENT_AMQP_WS_TRANSPORT}
	${LIB_UAMQP}
	${LIB_IOTHUB_CLIENT_MQTT_TRANSPORT}
	${LIB_IOTHUB_CLIENT_MQTT_WS_TRANSPORT}
	${LIB_UMQTT}
	${LIB_IOTHUB_CLIENT}
	${LIB_UHTTP}
	${LIB_UMOCK_C}
	${LIB_RT}
	${LIB_PTHREAD}
	varserver
	${LIB_SSL}
	${LIB_CRYPTO}
	${LIB_CURL}
	${LIB_M}
	${LIB_PARSON}
	${LIB_UUID}
	aziotsharedutil
)

install(TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
