# Copyright (c) 2011 Stefan Eilemann <eile@eyescale.ch>

include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
if(GLEW_MX_FOUND)
  include_directories(SYSTEM ${GLEW_MX_INCLUDE_DIRS})
endif()
if(CUDA_FOUND)
  include_directories(SYSTEM ${CUDA_INCLUDE_DIRS})
endif()

add_subdirectory(fabric)
add_subdirectory(client)
add_subdirectory(admin)
add_subdirectory(server)

install(FILES eq.h DESTINATION include/eq COMPONENT eqdev)

# TODO: this will be incorporated into PURPLE_ADD_LIBRARY
configure_file(Equalizer.pc.in Equalizer.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/Equalizer.pc
        DESTINATION lib/pkgconfig COMPONENT eqlib)

