cmake_minimum_required(VERSION 3.5)

project(rematrix-filter)

########################################
# find libobs                                                          #
########################################
include(external/FindLibObs.cmake)
find_package(LibObs REQUIRED)

include_directories(
	/
)

set(rematrix-filter_SOURCES
	rematrix-filter.c
)

add_library(rematrix-filter MODULE
	${rematrix-filter_SOURCES}
)

target_link_libraries(rematrix-filter
	libobs
)

install_external_plugin_with_data(rematrix-filter data)