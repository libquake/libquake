add_library(${PROJECT_NAME} SHARED
        common/surface.cpp
        common/vertex.cpp
        common/entity.cpp
        common/entity_parser.cpp
        common/level.cpp
        common/level_util.cpp
        common/lightmap_layout.cpp
        common/lightmap_baker.cpp
        common/texture_name.cpp

        bsp/qbsp.cpp
        bsp/qbsp_provider.cpp
        bsp/entity_solid.cpp
        bsp/surface.cpp
        bsp/lightmap.cpp
        bsp/bsp_level.cpp

        map/map_file.cpp
        map/map_document.cpp
        map/map.cpp
        map/brush.cpp
        map/face.cpp
        map/entity_solid.cpp
        map/csg.cpp
        map/lightmap_generator.cpp
        map/qmap_provider.cpp
        map/map_mesh.cpp
        map/map_level.cpp

        wad/palette.cpp
        wad/wad.cpp
        wad/texture.cpp
        wad/wad_manager.cpp

        utils/xatlas/xatlas.cpp

        wrapper.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC "../include/")

# the C++ API has no export macros, MSVC exports nothing from a dll without this
set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(Threads REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
