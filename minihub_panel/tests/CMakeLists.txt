cmake_minimum_required(VERSION 3.16)
project(minihub_panel_phase1 C)

enable_testing()
add_executable(test_phase1 test_phase1.c
    ../components/app_panel/panel_prefs.c
    ../components/app_hub/hub_matter_status.c)
target_include_directories(test_phase1 PRIVATE
    ../components/app_panel/include
    ../components/app_hub/include)
target_compile_features(test_phase1 PRIVATE c_std_11)
target_compile_options(test_phase1 PRIVATE -Wall -Wextra -Werror)
add_test(NAME phase1 COMMAND test_phase1)
