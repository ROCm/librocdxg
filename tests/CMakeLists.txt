add_executable(adapter_policy_test
  adapter_policy_test.cpp
  ${PROJECT_SOURCE_DIR}/src/wddm/adapter_policy.cpp)

target_include_directories(adapter_policy_test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src)

target_include_directories(adapter_policy_test SYSTEM PRIVATE ${WIN_SDK})

target_compile_definitions(adapter_policy_test PRIVATE
  LINUX
  __AMD64__
  LITTLEENDIAN_CPU
  HSA_LARGE_MODEL)

add_test(NAME adapter_policy_test COMMAND adapter_policy_test)

add_executable(events_test
  events_test.cpp
  ${PROJECT_SOURCE_DIR}/src/events.cpp)

target_include_directories(events_test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src)

target_include_directories(events_test SYSTEM PRIVATE ${WIN_SDK})

target_compile_definitions(events_test PRIVATE
  LINUX
  __AMD64__
  LITTLEENDIAN_CPU
  HSA_LARGE_MODEL)

target_compile_options(events_test PRIVATE
  -fPIC
  -include ${PROJECT_SOURCE_DIR}/src/librocdxg.h)

target_link_libraries(events_test PRIVATE pthread rt c ${CMAKE_DL_LIBS})

add_test(NAME events_test COMMAND events_test)
