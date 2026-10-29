cmake_minimum_required(VERSION 3.22.1)
project(luma_dsp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(REPOSITORY_ROOT "\${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)

add_library(luma_dsp SHARED
    dsp_jni.cpp
    "\${REPOSITORY_ROOT}/consumer_enhancer-3.cpp"
    "\${REPOSITORY_ROOT}/engine_selector.cpp"
    "\${REPOSITORY_ROOT}/halo_engine.cpp"
    "\${REPOSITORY_ROOT}/peq_engine.cpp"
)

target_include_directories(luma_dsp PRIVATE "\${REPOSITORY_ROOT}")
target_compile_options(luma_dsp PRIVATE -Wall -Wextra -Wno-unused-parameter -O2)

find_library(log-lib log)
target_link_libraries(luma_dsp PRIVATE \${log-lib})
