# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-src"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps/picotool-build"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/_deps"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/tmp"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/src/picotoolBuild-stamp"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/src"
  "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/src/picotoolBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/src/picotoolBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/tme520/Downloads/LPS-Companion-PicoCalc/build/picotool/src/picotoolBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
