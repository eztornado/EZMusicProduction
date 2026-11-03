cmake_minimum_required(VERSION 3.22)

project(Cadence VERSION 0.1.0 LANGUAGES C CXX)

# ObjC/ObjC++ only exist for the macOS open-panel bridge (native_dialogs.mm);
# Linux and Windows builds are pure C/C++.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(engine)
