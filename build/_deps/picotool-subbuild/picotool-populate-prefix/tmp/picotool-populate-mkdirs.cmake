# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-src"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-build"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/tmp"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/src"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
