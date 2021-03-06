add_executable(eosio-wast2wasm Assemble.cpp CLI.h)
target_link_libraries(eosio-wast2wasm Logging IR WAST WASM)
set_target_properties(eosio-wast2wasm PROPERTIES FOLDER Programs)
INSTALL(TARGETS eosio-wast2wasm DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(Disassemble Disassemble.cpp CLI.h)
target_link_libraries(Disassemble Logging IR WAST WASM)
set_target_properties(Disassemble PROPERTIES FOLDER Programs)

add_executable(Test Test.cpp CLI.h)
target_link_libraries(Test Logging IR WAST Runtime)
set_target_properties(Test PROPERTIES FOLDER Programs)

add_executable(wavm wavm.cpp CLI.h)
target_link_libraries(wavm Logging IR WAST WASM Runtime Emscripten)
set_target_properties(wavm PROPERTIES FOLDER Programs)


set(BUILD_STATIC_LIB ON)


IF(BUILD_STATIC_LIB)
  ADD_LIBRARY(wavm-shared STATIC wavm.cpp CLI.h)
ELSE()
  ADD_LIBRARY(wavm-shared SHARED wavm.cpp CLI.h)
ENDIF()

TARGET_LINK_LIBRARIES(wavm-shared Logging IR WAST WASM Runtime)