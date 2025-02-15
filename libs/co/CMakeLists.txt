##
# Path : libs/collage/CMakeLists.txt
# Copyright (c) 2010 Daniel Pfeifer <daniel@pfeifer-mail.de>
#               2010-2011 Stefan Eilemann <eile@eyescale.ch>
#               2010 Cedric Stalder <cedric.stalder@gmail.ch>
##

include(files.cmake)
include(configure.cmake)

option(COLLAGE_AGGRESSIVE_CACHING "Disable to reduce memory consumption" ON)
option(COLLAGE_BUILD_STATIC_LIBS "Build static Collage libraries" OFF)
option(COLLAGE_BUILD_MODULARIZED "Build intermediate Collage libraries" OFF)

if(APPLE)
  if(_CMAKE_OSX_MACHINE MATCHES "ppc")
    set(COLLAGE_OSX_ARCHITECTURES "ppc;ppc64" CACHE STRING
        "Collage build architectures")
  else()
    set(COLLAGE_OSX_ARCHITECTURES "i386;x86_64" CACHE STRING
        "Collage build architectures")
  endif()
  mark_as_advanced(COLLAGE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "${COLLAGE_OSX_ARCHITECTURES}")
  message(STATUS "Building Collage for ${CMAKE_OSX_ARCHITECTURES}")
endif()

mark_as_advanced(COLLAGE_AGGRESSIVE_CACHING)
mark_as_advanced(COLLAGE_BUILD_STATIC_LIBS)
mark_as_advanced(COLLAGE_BUILD_MODULARIZED)

if(NOT COLLAGE_BUILD_STATIC_LIBS)
  set(CO_LIB_VARIANT SHARED)
endif()
if(NOT COLLAGE_BUILD_MODULARIZED)
  list(APPEND CO_LIB_VARIANT EXCLUDE_FROM_ALL)
endif(NOT COLLAGE_BUILD_MODULARIZED)

add_definitions(-DEQ_PLUGIN_BUILD)
add_subdirectory( base )

set(CO_ADD_LINKLIB)

if(WIN32)
  list(APPEND CO_ADD_LINKLIB ws2_32 mswsock)
endif(WIN32)
if(MSVC)
  list(APPEND CO_SOURCES pgmConnection.cpp)
endif(MSVC)

if(Boost_FOUND)
  list(APPEND CO_SOURCES rspConnection.cpp)
  list(APPEND CO_ADD_LINKLIB ${Boost_SYSTEM_LIBRARY})
  if(NOT Boost_USE_STATIC_LIBS)
    list(APPEND CO_ADD_LINKLIB ${Boost_DATE_TIME_LIBRARY})
  endif()
endif(Boost_FOUND)

if(OFED_FOUND)
  list(APPEND CO_HEADERS rdmaConnection.h)
  list(APPEND CO_SOURCES rdmaConnection.cpp)
  list(APPEND CO_ADD_LINKLIB ${OFED_LIBRARIES})
endif()


source_group(base REGULAR_EXPRESSION .*)
source_group(\\ FILES CMakeLists.txt)
source_group(plugin FILES ${PLUGIN_HEADERS} )
source_group(collage FILES ${CO_PUBLIC_HEADERS} ${CO_HEADERS} ${CO_SOURCES} )
source_group(compressor FILES ${CO_COMPRESSOR_HEADERS} ${CO_COMPRESSOR_SOURCES})

purple_add_library(co ${CO_LIB_VARIANT}
  HEADERS_PREFIX "co/"
  HEADERS ${CO_PUBLIC_HEADERS} ${PLUGIN_HEADERS}
  PRECOMPILE_HEADERS precompile.h
  SOURCES ${CO_HEADERS} ${CO_SOURCES}
          ${CO_COMPRESSOR_HEADERS} ${CO_COMPRESSOR_SOURCES}
  LINK_LIBRARIES shared co_base ${CO_ADD_LINKLIB}
  )
set_target_properties(lib_co_shared PROPERTIES FOLDER "Transitional")

purple_add_amalgamation(Collage co ${CO_VERSION} ${VERSION_ABI}
    shared co_base shared co
  )

# TODO: this will be incorporated into PURPLE_ADD_LIBRARY
configure_file(Collage.pc.in Collage.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Collage.pc DESTINATION lib/pkgconfig COMPONENT colib)
