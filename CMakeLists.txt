cmake_minimum_required(VERSION 3.5)

project(rematrix-filter)

########################################
# find libobs                                                          #
########################################
#include(external/ObsPluginHelpers.cmake)
#include(external/FindLibObs.cmake)
#find_package(LibObs REQUIRED)

include_directories(
	/
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
#	"${LIBOBS_INCLUDE_DIR}/../UI/obs-frontend-api"
)

#set(OBS_FRONTEND_LIB "OBS_FRONTEND_LIB-NOTFOUND" CACHE FILEPATH "OBS frontend library")
#if(OBS_FRONTEND_LIB EQUAL "OBS_FRONTEND_LIB-NOTFOUND")
#	message(FATAL_ERROR "Could not find OBS Frontend API\'s library !")
#endif()

set(rematrix-filter_SOURCES
	rematrix-filter.c
)

add_library(rematrix-filter MODULE
	${rematrix-filter_SOURCES}
)

target_link_libraries(rematrix-filter
	libobs
#	"${OBS_FRONTEND_LIB}"
)

install_obs_plugin_with_data(rematrix-filter data)
#install_external_plugin_with_data(rematrix-filter data)