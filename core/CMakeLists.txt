file(GLOB CC_CORE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(cc700core STATIC ${CC_CORE_SRC})
target_include_directories(cc700core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/hal)
target_link_libraries(cc700core PUBLIC m)

add_library(unity STATIC tests/unity/unity.c)
target_include_directories(unity PUBLIC tests/unity)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)
target_compile_options(unity PRIVATE -Wno-error)

# One test executable per tests/test_*.c file
file(GLOB CC_CORE_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.c)
foreach(t ${CC_CORE_TESTS})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t} tests/support.c)
  target_include_directories(${name} PRIVATE tests)
  target_link_libraries(${name} cc700core unity)
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endforeach()
