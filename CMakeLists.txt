cmake_minimum_required(VERSION 3.13)

project(iothpy)

find_package(PythonExtensions REQUIRED)

include(CheckIncludeFile)

# Check for picoxnet library and header files
set(CMAKE_BUILD_TYPE Debug)
set(LIBS_REQUIRED ioth)
set(HEADERS_REQUIRED ioth.h iothconf.h iothdns.h)

foreach(THISLIB IN LISTS LIBS_REQUIRED)
  find_library(LIB${THISLIB}_OK ${THISLIB})
  if(NOT LIB${THISLIB}_OK)
    message(FATAL_ERROR "library ${THISLIB} not found")
  endif()
endforeach(THISLIB)

foreach(HEADER IN LISTS HEADERS_REQUIRED)
  check_include_file(${HEADER} ${HEADER}_OK)
  if(NOT ${HEADER}_OK)
    message(FATAL_ERROR "header file ${HEADER} not found")
  endif()
endforeach(HEADER)

# Target for python extension module
add_library(_iothpy MODULE iothpy/iothpy.c iothpy/iothpy_socket.c iothpy/iothpy_stack.c iothpy/utils.c)
target_link_libraries(_iothpy -lioth -liothconf -liothdns)
python_extension_module(_iothpy)

add_library(_const_linkadd MODULE iothpy/const_linkadd.c)
python_extension_module(_const_linkadd)

install(TARGETS _iothpy _const_linkadd LIBRARY DESTINATION iothpy)