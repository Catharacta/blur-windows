# GUI Demo Application
add_executable(BlurWindowGUIDemo WIN32 main.cpp)

target_link_libraries(BlurWindowGUIDemo PRIVATE 
    blurwindow
    comctl32.lib
)

# Copy DLL to output directory
add_custom_command(TARGET BlurWindowGUIDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:blurwindow>
    $<TARGET_FILE_DIR:BlurWindowGUIDemo>
)

set_target_properties(BlurWindowGUIDemo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
