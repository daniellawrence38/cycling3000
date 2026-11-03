add_executable(simunit main.c dirfs.c player.c)
target_include_directories(simunit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simunit cc700core)
