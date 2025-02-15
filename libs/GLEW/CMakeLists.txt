# Copyright (c) 2010 Daniel Pfeifer <daniel@pfeifer-mail.de>
#               2011 Stefan Eilemann <eile@eyescale.ch>

if(GLEW_MX_FOUND)
  include_directories(BEFORE SYSTEM ${GLEW_MX_INCLUDE_DIRS})
  set(GLEW_LIBRARY ${GLEW_MX_LIBRARIES})
else()
  set(GLEW_VERSION 1.7.0)
  set(GLEW_DIR ${CMAKE_BINARY_DIR}/glew-${GLEW_VERSION})
  set(GLEW_INCLUDE_DIRS "${OUTPUT_INCLUDE_DIR}/eq")
  set(GLEW_LIBRARY "GLEW_MX-Equalizer")
  set(GLEW_TGZ ${CMAKE_CURRENT_SOURCE_DIR}/GLEW/glew-${GLEW_VERSION}.tgz)

  if(NOT EXISTS ${GLEW_DIR})
    message(STATUS "  Extracting GLEW to ${GLEW_DIR}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf
      ${GLEW_TGZ} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    # 'Fix' glxew.h
    set(_glxew_h "${GLEW_DIR}/include/GL/glxew.h")
    file(READ ${_glxew_h} _glxew_h_contents)

    string(REGEX REPLACE "GL/glew.h" "eq/GL/glew.h"
           _glxew_h_new "${_glxew_h_contents}")
    file(WRITE ${_glxew_h} "${_glxew_h_new}")
  endif(NOT EXISTS ${GLEW_DIR})

  set(GLEW_INCLUDES GL/glew.h GL/glxew.h GL/wglew.h)
  foreach(file ${GLEW_INCLUDES})
    configure_file(${GLEW_DIR}/include/${file} ${OUTPUT_INCLUDE_DIR}/eq/${file} COPYONLY)
    install(FILES ${OUTPUT_INCLUDE_DIR}/eq/${file} DESTINATION include/eq/GL COMPONENT eqdev)
  endforeach()

  include_directories(BEFORE SYSTEM ${GLEW_INCLUDE_DIRS})
  add_definitions(-DGLEW_STATIC=1 -DGLEW_MX=1 -DGLEW_NO_GLU)
  add_library(${GLEW_LIBRARY} STATIC ${GLEW_DIR}/src/glew.c)
  set_target_properties(${GLEW_LIBRARY} PROPERTIES
                        FOLDER "Libraries" VERSION ${GLEW_VERSION})

  if(EQ_GLX_USED)
    add_definitions(-DGLEW_APPLE_GLX=1)
  endif(EQ_GLX_USED)
  
  install(TARGETS ${GLEW_LIBRARY} ARCHIVE DESTINATION lib COMPONENT eqdev)
  message(STATUS "${GLEW_LIBRARY}-${GLEW_VERSION} will be built and linked statically to Equalizer.")
endif()

