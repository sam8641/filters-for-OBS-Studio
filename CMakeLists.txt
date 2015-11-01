project(sam8641-filters)

set(sam8641-filters_SOURCES
	main.c
	filter1.c
)

add_library(sam8641-filters MODULE
	${sam8641-filters_SOURCES})
target_link_libraries(sam8641-filters
	libobs)

install_obs_plugin_with_data(sam8641-filters data)
