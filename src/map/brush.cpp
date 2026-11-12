#include <algorithm>
#include <iostream>
#include <quakelib/map/brush.h>
#include <quakelib/qmath.h>

namespace quakelib::map {
  void Brush::buildGeometry(const std::map<int, MapSurface::eFaceType> &faceTypes,
                            const std::map<int, textureBounds> &texBounds) {
    generatePolygons(faceTypes, texBounds);
    indexFaceVertices();
    calculateAABB();
  }

  void Brush::GetBiggerBBox(Vec3 &outMin, Vec3 &outMax) {
    outMax[0] = max[0] > outMax[0] ? max[0] : outMax[0];
    outMax[1] = max[1] > outMax[1] ? max[1] : outMax[1];
    outMax[2] = max[2] > outMax[2] ? max[2] : outMax[2];

    outMin[0] = min[0] < outMin[0] ? min[0] : outMin[0];
    outMin[1] = min[1] < outMin[1] ? min[1] : outMin[1];
    outMin[2] = min[2] < outMin[2] ? min[2] : outMin[2];
  }

  void Brush::indexFaceVertices() {

    for (auto &f : m_faces) {
      if (f->m_vertices.size() < 3)
        continue;

      f->m_indices.clear();
      f->m_indices.reserve((f->m_vertices.size() - 2) * 3);
      for (int i = 0; i < f->m_vertices.size() - 2; i++) {
        f->m_indices.push_back(0);
        f->m_indices.push_back(i + 1);
        f->m_indices.push_back(i + 2);
      }
      f->UpdateNormals();
    }
  }

  static Vertex interpolate(const Vertex &v1, const Vertex &v2, float t) {
    Vertex res;
    res.point = v1.point + (v2.point - v1.point) * t;
    res.uv = v1.uv + (v2.uv - v1.uv) * t;
    res.normal = v1.normal;
    res.tangent = v1.tangent;
    res.lightmap_uv = v1.lightmap_uv + (v2.lightmap_uv - v1.lightmap_uv) * t;
    return res;
  }

  void Brush::splitFace(const FacePtr &in, const FacePtr &plane, FacePtr &front, FacePtr &back) {
    if (!in || in->m_vertices.empty())
      return;

    std::vector<Vertex> fVerts, bVerts;
    const auto &verts = in->m_vertices;
    const auto &normal = plane->m_planeNormalD;
    const double dist = plane->m_planeDistD;

    std::vector<double> dists;
    dists.reserve(verts.size());

    double split_epsilon = math::CMP_EPSILON;

    for (const auto &v : verts) {
      dists.push_back(dot(normal, toDVec3(v.point)) - dist);
    }

    for (size_t i = 0; i < verts.size(); ++i) {
      const auto &v1 = verts[i];
      double d1 = dists[i];

      size_t next = (i + 1) % verts.size();
      const auto &v2 = verts[next];
      double d2 = dists[next];

      if (d1 >= -split_epsilon)
        fVerts.push_back(v1);
      if (d1 <= split_epsilon)
        bVerts.push_back(v1);

      if ((d1 > split_epsilon && d2 < -split_epsilon) || (d1 < -split_epsilon && d2 > split_epsilon)) {
        float t = (float)(d1 / (d1 - d2));
        Vertex mid = interpolate(v1, v2, t);
        fVerts.push_back(mid);
        bVerts.push_back(mid);
      }
    }

    if (fVerts.size() >= 3) {
      front = in->Copy();
      front->m_vertices = fVerts;
      front->m_indices.clear();
    } else
      front = nullptr;

    if (bVerts.size() >= 3) {
      back = in->Copy();
      back->m_vertices = bVerts;
      back->m_indices.clear();
    } else
      back = nullptr;
  }

  void Brush::clipFace(FacePtr face, FaceIter plane, const FaceIter &planeEnd, std::vector<FacePtr> &outFaces,
                       bool keepOnPlane, bool isCoplanar) {
    if (face->Type() == MapSurface::CLIP || face->Type() == MapSurface::SKIP)
      return;

    if (plane == planeEnd) {
      if (isCoplanar && keepOnPlane) {
        outFaces.push_back(face);
      }
      return;
    }

    if ((*plane)->Type() != MapSurface::SOLID) {
      clipFace(face, plane + 1, planeEnd, outFaces, keepOnPlane, isCoplanar);
      return;
    }

    auto ecp = (*plane)->Classify(face.get());
    switch (ecp) {
    case MapSurface::FRONT:
      outFaces.push_back(face);
      return;
    case MapSurface::BACK:
      clipFace(face, plane + 1, planeEnd, outFaces, keepOnPlane, isCoplanar);
      return;
    case MapSurface::ON_PLANE: {
      double angle = math::Dot(face->m_planeNormal, (*plane)->m_planeNormal) - 1;
      if ((angle < math::CMP_EPSILON) && (angle > -math::CMP_EPSILON)) {
        clipFace(face, plane + 1, planeEnd, outFaces, keepOnPlane, true);
        return;
      }
      clipFace(face, plane + 1, planeEnd, outFaces, keepOnPlane, isCoplanar);
      return;
    }
    case MapSurface::SPANNING: {
      FacePtr front = nullptr, back = nullptr;
      splitFace(face, *plane, front, back);
      if (front)
        outFaces.push_back(front);
      if (back)
        clipFace(back, plane + 1, planeEnd, outFaces, keepOnPlane, isCoplanar);
      return;
    }
    }
  }

  static double polygonArea(const std::vector<Vertex> &verts) {
    Vec3 area{0, 0, 0};
    for (size_t i = 0; i < verts.size(); ++i)
      area += math::Cross(verts[i].point, verts[(i + 1) % verts.size()].point);
    return 0.5 * math::Len(area);
  }

  std::vector<FacePtr> Brush::clipToBrush(const Brush &other, bool keepOnPlane) {
    std::vector<FacePtr> clippedFaces;
    std::vector<FacePtr> pieces;
    auto planes_begin = other.m_faces.begin();
    auto planes_end = other.m_faces.cend();

    for (auto &face : m_faces) {
      pieces.clear();
      clipFace(face, planes_begin, planes_end, pieces, keepOnPlane);

      // nothing of the face was inside the other brush, keep it whole instead of the split pieces
      if (pieces.size() > 1) {
        double kept = 0;
        for (const auto &p : pieces)
          kept += polygonArea(p->m_vertices);
        double full = polygonArea(face->m_vertices);
        if (std::abs(full - kept) <= full * 1e-5) {
          clippedFaces.push_back(face);
          continue;
        }
      }
      clippedFaces.insert(clippedFaces.end(), pieces.begin(), pieces.end());
    }
    return clippedFaces;
  }

  namespace {
    // larger than any Quake map, the base polygon of every face starts at this size and gets clipped down
    constexpr double BASE_WINDING_SIZE = 1 << 20;
    constexpr double CLIP_EPSILON = 1e-4;

    std::vector<DVec3> baseWinding(const DVec3 &normal, double dist) {
      DVec3 up = std::abs(normal[2]) > 0.9 ? DVec3{1, 0, 0} : DVec3{0, 0, 1};
      DVec3 right = normalize(cross(up, normal));
      up = cross(normal, right);
      DVec3 org = normal * dist;
      right = right * BASE_WINDING_SIZE;
      up = up * BASE_WINDING_SIZE;
      return {org - right + up, org + right + up, org + right - up, org - right - up};
    }

    // keeps the part behind the plane, i.e. inside the brush
    void clipWinding(std::vector<DVec3> &w, const DVec3 &normal, double dist) {
      std::vector<DVec3> out;
      out.reserve(w.size() + 1);
      for (size_t i = 0; i < w.size(); ++i) {
        const DVec3 &a = w[i];
        const DVec3 &b = w[(i + 1) % w.size()];
        double da = dot(normal, a) - dist;
        double db = dot(normal, b) - dist;
        if (da <= CLIP_EPSILON)
          out.push_back(a);
        if ((da > CLIP_EPSILON && db < -CLIP_EPSILON) || (da < -CLIP_EPSILON && db > CLIP_EPSILON)) {
          out.push_back(a + (b - a) * (da / (da - db)));
        }
      }
      w.swap(out);
    }

    // map coordinates are mostly integers, snapping the rounding noise keeps shared corners identical
    double snap(double v) {
      double r = std::round(v);
      return std::abs(v - r) < 1e-4 ? r : v;
    }
  } // namespace

  void Brush::generatePolygons(const std::map<int, MapSurface::eFaceType> &faceTypes,
                               const std::map<int, textureBounds> &texBounds) {
    for (auto &face : m_faces) {
      auto kv = faceTypes.find(face->m_textureID);
      if (kv != faceTypes.end()) {
        face->m_type = kv->second;
        if (face->m_type == MapSurface::CLIP)
          m_isBlockVolume = true;
      }
    }

    for (size_t i = 0; i < m_faces.size(); i++) {
      auto &face = m_faces[i];
      face->m_vertices.clear();
      auto winding = baseWinding(face->m_planeNormalD, face->m_planeDistD);

      for (size_t j = 0; j < m_faces.size() && winding.size() >= 3; j++) {
        if (i == j)
          continue;
        const auto &other = m_faces[j];
        // the same plane twice, only the first one produces a face
        if (dot(face->m_planeNormalD, other->m_planeNormalD) > 1.0 - 1e-9 &&
            std::abs(face->m_planeDistD - other->m_planeDistD) < CLIP_EPSILON) {
          if (j < i)
            winding.clear();
          continue;
        }
        clipWinding(winding, other->m_planeNormalD, other->m_planeDistD);
      }

      std::vector<DVec3> points;
      for (auto p : winding) {
        p = {snap(p[0]), snap(p[1]), snap(p[2])};
        if (points.empty() || std::sqrt(dot(p - points.back(), p - points.back())) > CLIP_EPSILON)
          points.push_back(p);
      }
      while (points.size() > 1 &&
             std::sqrt(dot(points.front() - points.back(), points.front() - points.back())) <= CLIP_EPSILON)
        points.pop_back();
      if (points.size() < 3)
        continue;

      // the rest of the library expects clockwise winding seen from the front, like Quake
      DVec3 area{0, 0, 0};
      for (size_t k = 0; k < points.size(); k++)
        area = area + cross(points[k], points[(k + 1) % points.size()]);
      if (dot(area, face->m_planeNormalD) > 0)
        std::reverse(points.begin(), points.end());

      Vec4 tangent = face->CalcTangent();
      auto tb = texBounds.find(face->m_textureID);
      bool hasBounds = tb != texBounds.end() && tb->second.width > 0 && tb->second.height > 0;

      face->m_vertices.reserve(points.size());
      for (const auto &p : points) {
        Vertex v{};
        v.point = toVec3(p);
        v.normal = face->m_planeNormal;
        v.tangent = tangent;
        if (hasBounds)
          v.uv = face->CalcUV(v.point, tb->second.width, tb->second.height);
        v.lightmap_uv = face->CalcLightmapUV(v.point);
        face->m_vertices.push_back(v);
      }
      face->UpdateAB();
    }
  }

  bool Brush::DoesIntersect(const Brush &other) {
    if ((min[0] > other.max[0]) || (other.min[0] > max[0]))
      return false;

    if ((min[1] > other.max[1]) || (other.min[1] > max[1]))
      return false;

    if ((min[2] > other.max[2]) || (other.min[2] > max[2]))
      return false;

    return true;
  }

  void Brush::calculateAABB() {
    bool first = true;
    for (const auto &face : m_faces) {
      if (!face->m_vertices.empty() && first) {
        min = max = face->m_vertices[0].point;
        first = false;
      }
    }
    if (first)
      return;

    for (const auto &face : m_faces)
      for (const auto &vert : face->m_vertices) {
        if (vert.point[0] < min[0])
          min[0] = vert.point[0];

        if (vert.point[1] < min[1])
          min[1] = vert.point[1];

        if (vert.point[2] < min[2])
          min[2] = vert.point[2];

        if (vert.point[0] > max[0])
          max[0] = vert.point[0];

        if (vert.point[1] > max[1])
          max[1] = vert.point[1];

        if (vert.point[2] > max[2])
          max[2] = vert.point[2];
      }

    if (!m_faces.empty()) {
      m_isNonSolid = (m_faces[0]->Type() != MapSurface::SOLID);
    }
  }
} // namespace quakelib::map
