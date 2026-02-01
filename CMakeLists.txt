cmake_minimum_required(VERSION 3.10)

project(CGSME)

# 1. Gather all your C source files into one list.
file(GLOB CGSME_SOURCES
    "generator.c"
    "cgsme_debug.c"
    "cgsme_utils.c"
    "cgsme_noise.c"
    "cgsme_topology.c"
    "cgsme_solver.c"
    "tinycthread/tinycthread.c" 
)


# 2. CREATE the library target from the source files.
# This is the most important step. The target "CGSME" now exists.
add_library(${PROJECT_NAME} SHARED ${CGSME_SOURCES})

# 3. MODIFY the target to tell the compiler where to find header files.
# This will now work because CMake knows what "CGSME" is.
target_include_directories(${PROJECT_NAME} PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinycthread"
)

# 4. MODIFY the target to link necessary system libraries (for Linux).
# On Android the sysroot provides pthread functionality via Bionic and
# linking explicitly to -lpthread can fail (NDK's linker can't find -lpthread).
# Avoid linking pthread when building for Android.
if(UNIX AND NOT ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# NOTE: Instrumentation files exist under this folder but we intentionally
# avoid adding compile options or extra definitions here — keep Unity's
# existing build flow unchanged as requested.