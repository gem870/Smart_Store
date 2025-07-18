find_package(PkgConfig REQUIRED)
pkg_check_modules(JSON REQUIRED jsoncpp)
include_directories(${JSON_INCLUDE_DIRS})
