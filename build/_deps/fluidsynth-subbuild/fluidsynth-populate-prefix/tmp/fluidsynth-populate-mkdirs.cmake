# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/ivan/daw/build/_deps/fluidsynth-src")
  file(MAKE_DIRECTORY "/Users/ivan/daw/build/_deps/fluidsynth-src")
endif()
file(MAKE_DIRECTORY
  "/Users/ivan/daw/build/_deps/fluidsynth-build"
  "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix"
  "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/tmp"
  "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/src/fluidsynth-populate-stamp"
  "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/src"
  "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/src/fluidsynth-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/src/fluidsynth-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/ivan/daw/build/_deps/fluidsynth-subbuild/fluidsynth-populate-prefix/src/fluidsynth-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
