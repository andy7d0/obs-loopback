cmake_minimum_required(VERSION 3.5)
project(obs-loopback)


include(external/FindLibObs.cmake)
find_package(LibObs REQUIRED)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_PREFIX_PATH "${QTDIR}")
set(CMAKE_INCLUDE_CURRENT_DIR ON)
#set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTOUIC ON)

#find_package(Qt5Core REQUIRED)
#find_package(Qt5Widgets REQUIRED)
find_package(PulseAudio)

if(NOT PULSEAUDIO_FOUND)
	message(FATAL_ERROR "PulseAudio not found")
endif()



set(virtualoutput_SOURCES
	src/loopback_service.cpp
	)

set(virtualoutput_HEADERS
	)

include_directories(
	"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
	${PULSEAUDIO_INCLUDE_DIR}
	)
	
add_library(obs-loopback MODULE
	${virtualoutput_SOURCES}
	${virtualoutput_HEADERS}
	)

target_link_libraries(obs-loopback
	libobs
	pulse-simple
	${PULSEAUDIO_LIBRARY}
	)
#	Qt5::Core
#	Qt5::Widgets

if(ARCH EQUAL 64)
	set(ARCH_NAME "x86_64")
else()
	set(ARCH_NAME "i686")
endif()

set_target_properties(obs-loopback PROPERTIES PREFIX "")

install(TARGETS obs-loopback
	LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/obs-plugins)

install(DIRECTORY locale/
	DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/loopback/locale")

