find_program(CLANG NAMES clang clang++)

if (CLANG)
  message(STATUS "clang found at: ${CLANG}")
else ()
  message(WARNING "Could NOT find clang executable.")
endif ()

get_filename_component(CLANG_PATH ${CLANG} DIRECTORY)
message(STATUS "Clang Path = ${CLANG_PATH}")

# guessing include dir and lib dir
set(CLANG_INCLUDE_DIR_GUESS ${CLANG_PATH}/../include)
set(CLANG_LIB_DIR ${CLANG_PATH}/../lib)

find_library(CLANG_LIB NAMES clang libclang
  PATHS
  ${CLANG_LIB_DIR})

find_path(CLANG_INCLUDE_DIR clang-c
  PATHS ${CLANG_INCLUDE_DIR_GUESS})

if(CLANG_LIB)
  message(STATUS "LibClang = ${CLANG_LIB}")
else()
  message(WARNING "Could NOT find libclang.")
endif()

if(CLANG_INCLUDE_DIR)
  message(STATUS "LibClang CLANG_INCLUDE_DIR = ${CLANG_INCLUDE_DIR}")
else()
  message(WARNING "Could NOT find libclang include dir.")
endif()

set(LibClang_FOUND (CLANG_INCLUDE_DIR AND CLANG_LIB))