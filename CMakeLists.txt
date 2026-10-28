cmake_minimum_required(VERSION 3.25)

project(PointCloudReverseModeling VERSION 0.1.0 LANGUAGES C CXX)

include(cmake/common.cmake)
include(cmake/Dependencies.cmake)

enable_testing()
add_subdirectory(ReverseModeling)
add_subdirectory(tests)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ReverseModeler)
