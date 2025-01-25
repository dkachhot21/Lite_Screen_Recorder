cmake_minimum_required(VERSION 3.10)
project(ScreenRecorder)

set(CMAKE_CXX_STANDARD 17)

# Include project headers
include_directories(${PROJECT_SOURCE_DIR}/include)

# Include FFmpeg headers
include_directories(${PROJECT_SOURCE_DIR}/thirdparty/ffmpeg/include)

# Link FFmpeg libraries
link_directories(${PROJECT_SOURCE_DIR}/thirdparty/ffmpeg/lib)

# Add executable
add_executable(ScreenRecorder src/main.cpp src/ScreenRecorder.cpp)

# Link libraries
target_link_libraries(ScreenRecorder
    d3d9
    avcodec
    avformat
    avutil
    swscale
)

# Copy FFmpeg DLLs to the output directory
add_custom_command(TARGET ScreenRecorder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${PROJECT_SOURCE_DIR}/thirdparty/ffmpeg/bin
    $<TARGET_FILE_DIR:ScreenRecorder>
)