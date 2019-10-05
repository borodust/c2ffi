cmake_minimum_required(VERSION 3.8)

project(c2ffi)

set(SOURCE_ROOT ${CMAKE_SOURCE_DIR})

find_package(LLVM 8.0 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM installed in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_CXX_STANDARD 11)

message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")

file(GLOB SOURCE_GLOB
  ${SOURCE_ROOT}/src/*.cpp
  ${SOURCE_ROOT}/src/drivers/*.cpp
  )

file(GLOB HEADER_GLOB
  ${SOURCE_ROOT}/include/*.h
  ${SOURCE_ROOT}/include/c2ffi/*.h
  )

set(SOURCE_FILES
  ${SOURCE_GLOB}
  )

set(HEADER_FILES
  ${HEADER_GLOB}
  )

add_executable(c2ffi ${SOURCE_FILES} ${HEADER_FILES})
target_compile_options(c2ffi PUBLIC -fno-exceptions -fno-rtti)
set_target_properties(c2ffi PROPERTIES LINK_SEARCH_START_STATIC 1)
set_target_properties(c2ffi PROPERTIES LINK_SEARCH_END_STATIC 1)


target_include_directories(c2ffi PUBLIC
  ${LLVM_INCLUDE_DIRS}
  ${SOURCE_ROOT}/src/include
  )
target_link_libraries(c2ffi PUBLIC
  clangFrontendTool
  clangFrontend
  clangDriver
  clangSerialization
  clangCodeGen
  clangParse
  clangSema
  clangStaticAnalyzerFrontend
  clangStaticAnalyzerCheckers
  clangStaticAnalyzerCore
  clangAnalysis
  clangARCMigrate
  clangRewriteFrontend
  clangRewrite
  clangEdit
  clangDynamicASTMatchers
  clangASTMatchers
  clangAST
  clangLex
  clangBasic
  clangFormat
  clangIndex
  clangToolingCore
  clangTooling
  version)

llvm_config(c2ffi core support mcparser bitreader profiledata option)

set(APP_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
set_target_properties(c2ffi PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${APP_BIN_DIR}"
  )

install(TARGETS c2ffi DESTINATION bin)
