# A browser build is a touch-only wasm32 module with asynchronous local reads.
option(OPTION_WASM_64 "Build wasm64 module" OFF)
option(OPTION_WASM_JSPI "Use JSPI instead of Asyncify" OFF)
if(OPTION_WASM_64 OR OPTION_WASM_JSPI)
    message(FATAL_ERROR "The mobile web filesystem currently requires wasm32 and Asyncify")
endif()

set(FALLOUT_WEB_DAT_TOOL "" CACHE FILEPATH "Native ce-dat-tool for packaging the web app")
set(FALLOUT_WEB_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Web app output directory")

target_sources(${EXECUTABLE_NAME} PRIVATE
    "src/platform/web/storage.cc"
    "src/platform/web/storage.h"
    "src/platform/web/window.cc"
    "src/platform/web/window.h")
target_compile_definitions(${EXECUTABLE_NAME} PRIVATE FALLOUT_FORCE_TOUCH_ONLY=1)

# Release must run the LLVM and Binaryen optimizers, including after Asyncify.
# -O1 prioritizes iteration time and skips these link-time optimizations.
set(_web_ports --use-port=sdl2 --use-port=sdl2_mixer --use-port=zlib)
target_compile_options(${EXECUTABLE_NAME} PRIVATE ${_web_ports} "$<$<CONFIG:Release>:-O2>")
target_link_options(${EXECUTABLE_NAME} PRIVATE
    ${_web_ports}
    "$<$<CONFIG:Release>:-O2>"
    -sASYNCIFY
    -sASYNCIFY_STACK_SIZE=20480
    -sENVIRONMENT=web
    -sALLOW_MEMORY_GROWTH
    -sMEMORY_GROWTH_LINEAR_STEP=33554432
    -sNO_DISABLE_EXCEPTION_CATCHING
    -sCASE_INSENSITIVE_FS
    -sMODULARIZE
    -sEXPORTED_RUNTIME_METHODS=callMain,FS
    -sEXPORT_NAME=createFallout2
    -sEXIT_RUNTIME
    --js-library=${CMAKE_SOURCE_DIR}/os/web/gamefs.js
    --js-library=${CMAKE_SOURCE_DIR}/os/web/userfs.js)
set_property(TARGET ${EXECUTABLE_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/os/web/gamefs.js" "${CMAKE_SOURCE_DIR}/os/web/userfs.js")
set_target_properties(${EXECUTABLE_NAME} PROPERTIES OUTPUT_NAME "fallout2-ce" SUFFIX ".js")

# No version or commit in the engine: the page gives them (app_version.h),
# so the engine's files stay the same from build to build while its code
# does, and an update downloads only the files that changed (sw.js).
if(NOT FALLOUT_APP_VERSION)
    set(FALLOUT_APP_VERSION "development")
endif()
target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
    _BUILD_AUTHOR="" _BUILD_BRANCH="" _BUILD_HASH="" _BUILD_VER="" _BUILD_DATE="")

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
add_subdirectory("third_party/libarchive")
add_executable(fallout2-archive "os/web/archive-reader.cc" "src/save_composition.cc")
target_include_directories(fallout2-archive PRIVATE "src")
target_link_libraries(fallout2-archive PRIVATE archive_read)
target_link_options(fallout2-archive PRIVATE -O2 -sASYNCIFY -sALLOW_MEMORY_GROWTH
    -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node
    -sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString,HEAPU8
    -sEXPORTED_FUNCTIONS=_malloc,_free --no-entry)
set_target_properties(fallout2-archive PROPERTIES SUFFIX ".js")
add_custom_target(web-dist
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/os/web/package.py"
        --engine "$<TARGET_FILE:${EXECUTABLE_NAME}>"
        --dat-tool "${FALLOUT_WEB_DAT_TOOL}"
        --output "${FALLOUT_WEB_DIST_DIR}"
        --app-version "${FALLOUT_APP_VERSION}"
        --configuration "$<CONFIG>"
        --archive "$<TARGET_FILE:fallout2-archive>"
    DEPENDS ${EXECUTABLE_NAME} fallout2-archive
    COMMENT "Packaging the mobile web app and current ce.dat"
    VERBATIM)
