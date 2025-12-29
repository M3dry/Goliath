SET(GOIVIEW_SOURCES
    main.cpp
)

add_executable(goiview ${GOIVIEW_SOURCES})
target_link_libraries(goiview PRIVATE goliath)
