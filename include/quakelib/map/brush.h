#pragma once

#include "face.h"
#include "types.h"
#include <memory>
#include <vector>

#include <quakelib/vertex.h>

namespace quakelib::map {
  /**
   * @brief Helper type for returning a boolean status along with a value.
   */
  template <class T> using boolRet = std::pair<bool, T>;

  /**
   * @brief Represents a convex 3D volume (brush) defined by a set of planes (faces).
   *
   * Brushes are the fundamental building blocks of Quake map geometry.
   */
  class Brush {
  public:
    /**
     * @brief Default constructor.
     */
    Brush() = default;

    /**
     * @brief Checks if this brush intersects with another brush.
     * @param other The other brush to test against.
     * @return True if the brushes intersect, false otherwise.
     */
    bool DoesIntersect(const Brush &other);

    /**
     * @brief Constructs the actual polygonal geometry from the plane definitions.
     *
     * This process involves intersection calculation, vertex generation, and winding.
     * @param faceTypes Map of texture IDs to face types.
     * @param texBounds Map of texture IDs to texture dimensions.
     */
    void buildGeometry(const std::map<int, MapSurface::eFaceType> &faceTypes,
                       const std::map<int, textureBounds> &texBounds);

    /**
     * @brief Expands a bounding box to include this brush.
     * @param[in,out] min The current minimum bound, updated if brush extends beyond it.
     * @param[in,out] max The current maximum bound, updated if brush extends beyond it.
     */
    void GetBiggerBBox(Vec3 &min, Vec3 &max);

    /**
     * @brief Checks if the brush is a blocking volume (e.g., world geometry).
     * @return True if it blocks, false if it is a trigger or detailed geometry.
     */
    bool IsBlockVolume() const { return m_isBlockVolume; }

    /**
     * @brief Checks if this brush contains only non-solid faces (CLIP/SKIP/NODRAW).
     * @return True if all faces are non-solid, false otherwise.
     */
    bool IsNonSolidBrush() const { return m_isNonSolid; }

    /**
     * @brief Gets the faces that make up this brush.
     * @return A vector of pointers to the faces.
     */
    [[nodiscard]] inline const std::vector<FacePtr> &Faces() const { return m_faces; }

    /**
     * @brief Adds a face to the brush.
     * @param face The face to add.
     */
    void AddFace(FacePtr face) { m_faces.push_back(face); }

    Vec3 min{}; ///< Minimum coordinate of the brush's axial bounding box.
    Vec3 max{}; ///< Maximum coordinate of the brush's axial bounding box.

  private:
    std::vector<FacePtr> m_faces;

    void generatePolygons(const std::map<int, MapSurface::eFaceType> &faceTypes,
                          const std::map<int, textureBounds> &texBounds);
    std::vector<FacePtr> clipToBrush(const Brush &other, bool keepOnPlane);
    void indexFaceVertices();
    void calculateAABB();
    void clipFace(FacePtr face, FaceIter plane, const FaceIter &planeEnd, std::vector<FacePtr> &outFaces,
                  bool keepOnPlane, bool isCoplanar = false);
    static void splitFace(const FacePtr &in, const FacePtr &plane, FacePtr &front, FacePtr &back);

    bool m_isBlockVolume = false;
    bool m_isNonSolid = false;

    friend class QMapFile;
    friend class SolidMapEntity;
  };
} // namespace quakelib::map
