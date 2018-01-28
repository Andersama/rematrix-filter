project(rematrix-filter)

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

install_obs_plugin_with_data(rematrix-filter data)