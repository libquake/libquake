enable_language(C)

foreach(EXAMPLE load_bsp build_map bake light_styles wad_textures)
  add_executable(example_${EXAMPLE} ${EXAMPLE}.cpp)
  target_link_libraries(example_${EXAMPLE} PRIVATE quakelib)
endforeach()

# the C interface is plain C, this example makes sure it stays that way
add_executable(example_c_api c_api.c)
target_include_directories(example_c_api PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(example_c_api PRIVATE quakelib)
