# Install script for directory: /Users/ivan/daw/build/_deps/fluidsynth-src/src

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fluidsynth_program" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/ivan/daw/build/_deps/fluidsynth-build/src/fluidsynth")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/fluidsynth" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/fluidsynth")
    execute_process(COMMAND "/usr/bin/install_name_tool"
      -change "@rpath/FluidSynth.framework/Versions/3/FluidSynth" "FluidSynth.framework/Versions/3/FluidSynth"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/fluidsynth")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/Users/ivan/daw/build/_deps/fluidsynth-build/src"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/fluidsynth")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/fluidsynth")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fluidsynth_runtime" OR NOT CMAKE_INSTALL_COMPONENT)
  list(APPEND CMAKE_ABSOLUTE_DESTINATION_FILES
   "/usr/local/Library/Frameworks/FluidSynth.framework")
  if(CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(WARNING "ABSOLUTE path INSTALL DESTINATION : ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  if(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION)
    message(FATAL_ERROR "ABSOLUTE path INSTALL DESTINATION forbidden (by caller): ${CMAKE_ABSOLUTE_DESTINATION_FILES}")
  endif()
  file(INSTALL DESTINATION "/usr/local/Library/Frameworks" TYPE DIRECTORY FILES "/Users/ivan/daw/build/_deps/fluidsynth-build/src/FluidSynth.framework" USE_SOURCE_PERMISSIONS)
  if(EXISTS "$ENV{DESTDIR}/usr/local/Library/Frameworks/FluidSynth.framework/Versions/3/FluidSynth" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}/usr/local/Library/Frameworks/FluidSynth.framework/Versions/3/FluidSynth")
    execute_process(COMMAND "/usr/bin/install_name_tool"
      -id "FluidSynth.framework/Versions/3/FluidSynth"
      "$ENV{DESTDIR}/usr/local/Library/Frameworks/FluidSynth.framework/Versions/3/FluidSynth")
    execute_process(COMMAND /usr/bin/install_name_tool
      -delete_rpath "/usr/local/lib"
      "$ENV{DESTDIR}/usr/local/Library/Frameworks/FluidSynth.framework/Versions/3/FluidSynth")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -x "$ENV{DESTDIR}/usr/local/Library/Frameworks/FluidSynth.framework/Versions/3/FluidSynth")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "fluidsynth_development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth/FluidSynth-shared-targets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth/FluidSynth-shared-targets.cmake"
         "/Users/ivan/daw/build/_deps/fluidsynth-build/src/CMakeFiles/Export/6a6ea4714c6c3916014c1d187313a071/FluidSynth-shared-targets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth/FluidSynth-shared-targets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth/FluidSynth-shared-targets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth" TYPE FILE FILES "/Users/ivan/daw/build/_deps/fluidsynth-build/src/CMakeFiles/Export/6a6ea4714c6c3916014c1d187313a071/FluidSynth-shared-targets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/fluidsynth" TYPE FILE FILES "/Users/ivan/daw/build/_deps/fluidsynth-build/src/CMakeFiles/Export/6a6ea4714c6c3916014c1d187313a071/FluidSynth-shared-targets-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/ivan/daw/build/_deps/fluidsynth-build/src/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
