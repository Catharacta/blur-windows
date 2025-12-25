cmake_minimum_required(VERSION 3.20)
project(BlurWindowDemo)

add_executable(BlurWindowDemo
    main.cpp
)

target_link_libraries(BlurWindowDemo PRIVATE
    blurwindow
)

target_include_directories(BlurWindowDemo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Copy DLL to output directory
add_custom_command(TARGET BlurWindowDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:blurwindow>
        $<TARGET_FILE_DIR:BlurWindowDemo>
)
