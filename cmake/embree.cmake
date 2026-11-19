# Embree for the light baker, prebuilt in third_party/embree. Sets QLIB_BVH_SOURCE to the source of the ray
# backend and, when Embree is used, creates the imported target quakelib_embree. Without a prebuilt for the
# platform the baker uses its own bvh.

option(QLIB_EMBREE "Trace the baker's rays with Embree when a prebuilt exists for the platform" ON)

set(QLIB_EMBREE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/embree")
set(QLIB_BVH_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../src/common/bake_bvh.cpp")

if(QLIB_EMBREE)
  if(APPLE)
    set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/macos/libembree4.a")
  elseif(WIN32)
    set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/windows-x64/embree4.lib")
  else()
    set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/linux-${CMAKE_SYSTEM_PROCESSOR}/libembree4.a")
  endif()

  if(EXISTS "${_qlib_embree_lib}")
    if(NOT TARGET quakelib_embree)
      find_package(Threads REQUIRED)
      add_library(quakelib_embree STATIC IMPORTED)
      # the kernels per ISA reference the core and the other way round, linkers other than Apple's need the
      # core again after them
      string(REGEX REPLACE "embree4(\\.[a-z]+)$" "embree4_isa\\1" _qlib_embree_isa "${_qlib_embree_lib}")
      set(_qlib_embree_links "")
      if(EXISTS "${_qlib_embree_isa}")
        set(_qlib_embree_links "${_qlib_embree_isa}")
        if(NOT APPLE)
          list(APPEND _qlib_embree_links "${_qlib_embree_lib}")
        endif()
      endif()
      set_target_properties(quakelib_embree PROPERTIES
        IMPORTED_LOCATION "${_qlib_embree_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${QLIB_EMBREE_ROOT}/include"
        INTERFACE_LINK_LIBRARIES "${_qlib_embree_links};Threads::Threads;${CMAKE_DL_LIBS}")
    endif()
    set(QLIB_BVH_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../src/common/bake_bvh_embree.cpp")
    message(STATUS "libquake: baker traces with Embree")
  else()
    message(STATUS "libquake: no Embree prebuilt at ${_qlib_embree_lib}, the baker uses its own bvh")
  endif()
endif()
