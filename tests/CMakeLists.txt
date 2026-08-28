add_executable(vram_budget_test vram_budget_test.cpp)
target_include_directories(vram_budget_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_features(vram_budget_test PRIVATE cxx_std_11)

add_test(NAME vram_budget_test COMMAND vram_budget_test)
