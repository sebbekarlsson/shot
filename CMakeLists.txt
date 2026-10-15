cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(PROJ_NAME shot)

project(${PROJ_NAME})

# `make install` goes to ~/.local unless a prefix is given with -DCMAKE_INSTALL_PREFIX.
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
endif()

######### DEBUG
set(CMAKE_BUILD_TYPE RelWithDebInfo)


######## keep assert() calls
string(REPLACE "-DNDEBUG" "" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
#############################

SET(${PROJ_NAME}_COMPILE_FLAGS "-fno-strict-aliasing -Wall ${CMAKE_CXX_FLAGS}")

SET(CMAKE_CXX_FLAGS ${${PROJ_NAME}_COMPILE_FLAGS}) 
SET(CMAKE_C_FLAGS ${${PROJ_NAME}_COMPILE_FLAGS})


####################
include(FetchContent)
####################


file(
  GLOB_RECURSE
  ${PROJ_NAME}_SOURCE_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)

add_library(${PROJ_NAME} STATIC ${${PROJ_NAME}_SOURCE_FILES})
target_include_directories(${PROJ_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(${PROJ_NAME} PUBLIC m jpeg png X11 Xrandr pthread atomic)
target_compile_options(${PROJ_NAME} PUBLIC -Wall -mavx2 -mfma)
target_include_directories(${PROJ_NAME} PUBLIC ${cglm_SOURCE_DIR}/include ${glfw_SOURCE_DIR}/include)

function (add_sub_project SUBPROJ_NAME)
  if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/exe/${SUBPROJ_NAME})
    make_directory(${CMAKE_CURRENT_SOURCE_DIR}/exe/${SUBPROJ_NAME}/src)
    write_file(${CMAKE_CURRENT_SOURCE_DIR}/exe/${SUBPROJ_NAME}/src/main.c "#include <stdio.h>\n\nint main(int argc, char* argv[]) {\n  printf(\"hello\\n\");\n  return 0;\n}")
  endif()
  file(GLOB_RECURSE subproj_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/exe/${SUBPROJ_NAME}/src/*.c)
  add_executable(${SUBPROJ_NAME} ${subproj_SOURCE_FILES})
  target_compile_options(${SUBPROJ_NAME} PUBLIC -Wall -mavx2 -mfma)
  target_compile_options(${SUBPROJ_NAME} PRIVATE $<$<CONFIG:Release>:-fno-strict-aliasing>)
  target_link_libraries(${SUBPROJ_NAME} PRIVATE ${PROJ_NAME})
  target_include_directories(${SUBPROJ_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/exe/${SUBPROJ_NAME}/include)
endfunction()


add_sub_project(shot_cli)

install(TARGETS shot_cli RUNTIME DESTINATION bin)
