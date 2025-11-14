cmake_minimum_required(VERSION 3.20)

project(FovControl VERSION 2.31.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

add_subdirectory(deps/red4ext.sdk)

add_library(FovControl SHARED src/red4ext/main.cpp)

if(WIN32)
  target_sources(FovControl PRIVATE ${CMAKE_SOURCE_DIR}/src/red4ext/version.rc)
endif()

target_link_libraries(FovControl PRIVATE RED4ext.SDK)

if(EXISTS ${CMAKE_SOURCE_DIR}/vendor/RedLib/CMakeLists.txt)
  add_subdirectory(vendor/RedLib)
  target_link_libraries(FovControl PRIVATE RedLib)
endif()

set_target_properties(FovControl PROPERTIES OUTPUT_NAME FovControl)

set_target_properties(FovControl PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/red4ext/plugins/FovControl)

add_custom_command(
  TARGET FovControl
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory
    "${CMAKE_SOURCE_DIR}/bin/r6"
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_SOURCE_DIR}/bin/r6/scripts/FovControl"
  COMMAND ${CMAKE_COMMAND} -E copy
    "${CMAKE_SOURCE_DIR}/src/redscript/FovControl.reds"
    "${CMAKE_SOURCE_DIR}/bin/r6/scripts/FovControl/FovControl.reds"
)
