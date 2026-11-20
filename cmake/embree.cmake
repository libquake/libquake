# Embree for the light baker, prebuilt in third_party/embree. Sets QLIB_BVH_SOURCE to the source of the ray
# backend and, when Embree is used, creates the imported target quakelib_embree. Without a prebuilt for the
# platform the baker uses its own bvh.

option(QLIB_EMBREE "Trace the baker's rays with Embree when a prebuilt exists for the platform" ON)
option(QLIB_EMBREE_STATIC_RUNTIME "Windows: the prebuilt for builds that link the C runtime statically (/MT)" OFF)

set(QLIB_EMBREE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../third_party/embree")
set(_qlib_own_bvh "${CMAKE_CURRENT_LIST_DIR}/../src/common/bake_bvh.cpp")
set(QLIB_BVH_SOURCE "${_qlib_own_bvh}")

if(QLIB_EMBREE)
  if(APPLE)
    set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/macos/libembree4.a")
  elseif(WIN32)
    if(QLIB_EMBREE_STATIC_RUNTIME)
      set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/windows-x64-static-runtime/embree4.lib")
    else()
      set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/windows-x64/embree4.lib")
    endif()
  else()
    set(_qlib_embree_lib "${QLIB_EMBREE_ROOT}/linux-${CMAKE_SYSTEM_PROCESSOR}/libembree4.a")
  endif()

  if(EXISTS "${_qlib_embree_lib}")
    set(_qlib_embree_bvh "${CMAKE_CURRENT_LIST_DIR}/../src/common/bake_bvh_embree.cpp")
    if(NOT TARGET quakelib_embree)
      find_package(Threads REQUIRED)
      add_library(quakelib_embree INTERFACE IMPORTED)
      # the kernels per ISA reference the core and the other way round, linkers other than Apple's need the
      # core again after them
      string(REGEX REPLACE "embree4(\\.[a-z]+)$" "embree4_isa\\1" _qlib_embree_isa "${_qlib_embree_lib}")
      set(_qlib_embree_links "${_qlib_embree_lib}")
      if(EXISTS "${_qlib_embree_isa}")
        list(APPEND _qlib_embree_links "${_qlib_embree_isa}")
        if(NOT APPLE)
          list(APPEND _qlib_embree_links "${_qlib_embree_lib}")
        endif()
      endif()
      list(APPEND _qlib_embree_links Threads::Threads ${CMAKE_DL_LIBS})
      if(MSVC)
        # Embree is built for Release, MSVC's debug builds use other runtime checks and keep the own bvh
        list(TRANSFORM _qlib_embree_links PREPEND "$<$<NOT:$<CONFIG:Debug>>:")
        list(TRANSFORM _qlib_embree_links APPEND ">")
      endif()
      set_target_properties(quakelib_embree PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${QLIB_EMBREE_ROOT}/include"
        INTERFACE_LINK_LIBRARIES "${_qlib_embree_links}")
    endif()
    if(MSVC)
      set(QLIB_BVH_SOURCE "$<IF:$<CONFIG:Debug>,${_qlib_own_bvh},${_qlib_embree_bvh}>")
    else()
      set(QLIB_BVH_SOURCE "${_qlib_embree_bvh}")
    endif()
    message(STATUS "libquake: baker traces with Embree")
  else()
    message(STATUS "libquake: no Embree prebuilt at ${_qlib_embree_lib}, the baker uses its own bvh")
  endif()
endif()
