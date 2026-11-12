#pragma once

#include <cstdint>
#include <quakelib/vertex.h>
#include <vector>

namespace quakelib {
  /**
   * @brief Enumeration of supported surface types.
   */
  enum class SurfaceType {
    SOLID,  ///< A standard solid surface that is drawn.
    CLIP,   ///< A clipping surface used for collision but not drawn.
    SKIP,   ///< A surface that should be ignored/removed.
    NODRAW, ///< A surface that is neither drawn nor used for standard collision (e.g., triggers).
  };

  /**
   * @brief Represents a surface, typically part of a brush.
   *
   * A surface contains geometry data (vertices and indices) and material information.
   */
  class Surface {
  public:
    /**
     * @brief Gets the vertices of the surface.
     * @return A const reference to the vector of vertices.
     */
    const std::vector<Vertex> &Vertices() const;

    /**
     * @brief Gets the vertices of the surface for modification.
     * @return A reference to the vector of vertices.
     */
    std::vector<Vertex> &VerticesRW();

    /**
     * @brief Gets the indices defining the surface geometry (i.e. for triangulation).
     * @return A const reference to the vector of indices.
     */
    const std::vector<uint32_t> &Indices() const;

    /**
     * @brief Gets the ID of the texture applied to this surface.
     * @return The texture ID.
     */
    int TextureID() const;

    /**
     * @brief Gets the type of the surface.
     * @return The SurfaceType enum value.
     */
    SurfaceType Type();

  protected:
    std::vector<Vertex> m_vertices;  ///< Vertices defining the surface geometry.
    std::vector<uint32_t> m_indices; ///< Indices into the vertex array.
    int m_textureID;                 ///< Index or ID of the texture.
    SurfaceType m_surfType;          ///< Classification of the surface.
  };
} // namespace quakelib