#include <algorithm>
#include <quakelib/map/entities.h>

namespace quakelib::map {

  static Vertex interpolate(const Vertex &v1, const Vertex &v2, float t) {
    Vertex res;
    res.point = v1.point + (v2.point - v1.point) * t;
    res.uv = v1.uv + (v2.uv - v1.uv) * t;
    res.normal = v1.normal;
    res.tangent = v1.tangent;
    res.lightmap_uv = v1.lightmap_uv + (v2.lightmap_uv - v1.lightmap_uv) * t;
    return res;
  }

  void SolidMapEntity::csgUnion() {
    if (!m_brushes.empty()) {
      m_min = m_brushes[0].min;
      m_max = m_brushes[0].max;
    }
    for (auto &b1 : m_brushes) {
      // Skip CSG for non-solid brushes (CLIP/SKIP/NODRAW) - export them as-is
      // the clipped brushes get their own faces, the post processing below must not touch the original
      // brushes, they stay closed convex hulls (e.g. for collision)
      auto cpBrush = b1;
      for (auto &f : cpBrush.m_faces)
        f = f->Copy();

      if (b1.IsNonSolidBrush()) {
        m_clippedBrushes.push_back(cpBrush);
        b1.GetBiggerBBox(m_min, m_max);
        continue;
      }

      for (auto &b2 : m_brushes) {
        if (&b1 == &b2 || b2.m_faces.empty()) {
          continue;
        }

        // Don't clip against non-solid brushes
        if (b2.IsNonSolidBrush()) {
          continue;
        }

        if (!b1.DoesIntersect(b2) || (b1.IsBlockVolume() || b2.IsBlockVolume())) {
          continue;
        }

        bool keepOnPlane = &b1 < &b2;
        auto clippedFaces = cpBrush.clipToBrush(b2, keepOnPlane);
        cpBrush.m_faces = clippedFaces;
      }
      if (!cpBrush.m_faces.empty()) {
        cpBrush.indexFaceVertices();
        m_clippedBrushes.push_back(cpBrush);
        cpBrush.GetBiggerBBox(m_min, m_max);
        m_stats_clippedFaces +=
            static_cast<long long>(b1.m_faces.size()) - static_cast<long long>(cpBrush.m_faces.size());
      }
    }
    m_center = math::CalculateCenterFromBBox(m_min, m_max);
    if (m_brushes.size() > 0 && m_clippedBrushes.size() > 0) {
      m_wasClipped = true;
      // collinear cleanup has to run before the t-junction pass, the split points it adds are collinear
      weldVertices();
      removeCollinearVertices();
      fixTJunctions();
      triangulateFaces();
    }
  }

  void SolidMapEntity::weldVertices() {
    std::vector<Vertex *> allVerts;
    double weld_epsilon = 0.005;

    auto &targetBrushes = m_clippedBrushes.empty() ? m_brushes : m_clippedBrushes;

    for (auto &b : targetBrushes) {
      for (auto &f : b.Faces()) {
        for (auto &v : f->m_vertices) {
          allVerts.push_back(&v);
        }
      }
    }

    std::sort(allVerts.begin(), allVerts.end(),
              [](const Vertex *a, const Vertex *b) { return a->point[0] < b->point[0]; });

    for (size_t i = 0; i < allVerts.size(); ++i) {
      for (size_t j = i + 1; j < allVerts.size(); ++j) {
        float dx = allVerts[j]->point[0] - allVerts[i]->point[0];
        if (dx > weld_epsilon)
          break;

        if (dist3(allVerts[i]->point, allVerts[j]->point) < weld_epsilon) {
          allVerts[j]->point = allVerts[i]->point;
        }
      }
    }
  }

  void SolidMapEntity::fixTJunctions() {
    auto &targetBrushes = m_clippedBrushes.empty() ? m_brushes : m_clippedBrushes;
    double edge_epsilon = 0.05;

    std::vector<Vec3> uniqueVerts;
    uniqueVerts.reserve(targetBrushes.size() * 32);

    for (const auto &b : targetBrushes) {
      for (const auto &f : b.Faces()) {
        if (f->Type() != MapSurface::SOLID)
          continue;
        for (const auto &v : f->m_vertices) {
          uniqueVerts.push_back(v.point);
        }
      }
    }

    // exact ordering, the positions are already snapped by weldVertices
    std::sort(uniqueVerts.begin(), uniqueVerts.end(), [](const Vec3 &a, const Vec3 &b) {
      if (a[0] != b[0])
        return a[0] < b[0];
      if (a[1] != b[1])
        return a[1] < b[1];
      return a[2] < b[2];
    });
    uniqueVerts.erase(std::unique(uniqueVerts.begin(), uniqueVerts.end()), uniqueVerts.end());

    for (auto &b : targetBrushes) {
      bool modified = false;
      for (auto &f : b.Faces()) {
        std::vector<Vertex> newVerts;
        const auto &oldVerts = f->m_vertices;
        size_t count = oldVerts.size();

        if (count < 3 || f->Type() != MapSurface::SOLID)
          continue;

        for (size_t i = 0; i < count; ++i) {
          const auto &v1 = oldVerts[i];
          const auto &v2 = oldVerts[(i + 1) % count];

          newVerts.push_back(v1);

          Vec3 dir = v2.point - v1.point;
          float len = math::Len(dir);
          if (len < edge_epsilon)
            continue;

          Vec3 dirNorm = math::Norm(dir);
          std::vector<Vec3> splits;

          float min_x = std::min(v1.point[0], v2.point[0]) - edge_epsilon;
          float max_x = std::max(v1.point[0], v2.point[0]) + edge_epsilon;

          auto it = std::lower_bound(uniqueVerts.begin(), uniqueVerts.end(), min_x,
                                     [](const Vec3 &p, float val) { return p[0] < val; });

          for (; it != uniqueVerts.end() && (*it)[0] <= max_x; ++it) {
            const auto &testP = *it;

            if (dist3(testP, v1.point) < edge_epsilon || dist3(testP, v2.point) < edge_epsilon)
              continue;

            Vec3 v1_to_p = testP - v1.point;
            float t = math::Dot(v1_to_p, dirNorm);

            if (t > edge_epsilon && t < len - edge_epsilon) {
              Vec3 closest = v1.point + (dirNorm * t);
              if (dist3(closest, testP) < edge_epsilon) {
                splits.push_back(testP);
              }
            }
          }

          if (splits.empty())
            continue;

          std::sort(splits.begin(), splits.end(),
                    [&](const Vec3 &a, const Vec3 &b) { return dist3(a, v1.point) < dist3(b, v1.point); });

          for (const auto &splitP : splits) {
            if (!newVerts.empty() && dist3(newVerts.back().point, splitP) < 0.001)
              continue;

            float t = math::Len(splitP - v1.point) / len;
            Vertex splitV = interpolate(v1, v2, t);
            splitV.point = splitP;
            newVerts.push_back(splitV);
            modified = true;
          }
        }
        f->m_vertices = newVerts;
      }
      if (modified) {
        b.indexFaceVertices();
      }
    }
  }

  void SolidMapEntity::removeCollinearVertices() {
    auto &targetBrushes = m_wasClipped ? m_clippedBrushes : m_brushes;

    for (auto &brush : targetBrushes) {
      for (const auto &face : brush.m_faces) {
        auto &verts = face->m_vertices;
        if (verts.size() < 3)
          continue;

        bool changed = true;
        while (changed && verts.size() >= 3) {
          changed = false;
          for (size_t i = 0; i < verts.size(); ++i) {
            size_t prev = (i + verts.size() - 1) % verts.size();
            size_t next = (i + 1) % verts.size();

            Vec3 p = verts[prev].point;
            Vec3 c = verts[i].point;
            Vec3 n = verts[next].point;

            Vec3 e1 = c - p;
            Vec3 e2 = n - c;

            if (math::Len(e1) < math::CMP_EPSILON || math::Len(e2) < math::CMP_EPSILON) {
              verts.erase(verts.begin() + i);
              changed = true;
              break;
            }

            if (math::Len(math::Cross(math::Norm(e1), math::Norm(e2))) < math::CMP_EPSILON) {
              verts.erase(verts.begin() + i);
              changed = true;
              break;
            }
          }
        }
      }
      brush.indexFaceVertices();
    }
  }

  void SolidMapEntity::triangulateFaces() {
    auto &targetBrushes = m_wasClipped ? m_clippedBrushes : m_brushes;

    auto emitTriangle = [](const FacePtr &face, const Vertex &a, const Vertex &b, const Vertex &c,
                           std::vector<FacePtr> &out) {
      FacePtr tri = face->Copy();
      tri->m_vertices = {a, b, c};
      tri->m_indices = {0, 1, 2};
      tri->UpdateNormals();
      out.push_back(tri);
    };

    for (auto &brush : targetBrushes) {
      std::vector<FacePtr> newFaces;
      newFaces.reserve(brush.m_faces.size() * 2);

      for (auto &face : brush.m_faces) {
        const auto &verts = face->m_vertices;
        size_t n = verts.size();
        if (n <= 3) {
          newFaces.push_back(face);
          continue;
        }

        // faces are convex, the only trouble are collinear points added by the t-junction pass.
        // a fan from vertex c is clean if neither edge touching c carries such a point.
        std::vector<bool> collinear(n, false);
        bool anyCollinear = false;
        for (size_t i = 0; i < n; ++i) {
          Vec3 e1 = verts[i].point - verts[(i + n - 1) % n].point;
          Vec3 e2 = verts[(i + 1) % n].point - verts[i].point;
          float l = math::Len(e1) * math::Len(e2);
          collinear[i] = l <= 0 || math::Len(math::Cross(e1, e2)) <= 1e-4f * l;
          anyCollinear |= collinear[i];
        }

        int fanFrom = anyCollinear ? -1 : 0;
        for (size_t c = 0; anyCollinear && c < n; ++c) {
          size_t prev = (c + n - 1) % n, next = (c + 1) % n;
          if (!collinear[c] && !collinear[prev] && !collinear[next]) {
            fanFrom = (int)c;
            break;
          }
        }

        if (fanFrom >= 0) {
          for (size_t i = 1; i + 1 < n; ++i) {
            emitTriangle(face, verts[fanFrom], verts[(fanFrom + i) % n], verts[(fanFrom + i + 1) % n],
                         newFaces);
          }
          continue;
        }

        // every corner has a split point next to it, fan around the centroid instead.
        // uv and lightmap uv are affine in the position, so averaging them matches the averaged point.
        Vertex center = verts[0];
        center.point = Vec3{0, 0, 0};
        center.uv = Vec2{0, 0};
        center.lightmap_uv = Vec2{0, 0};
        for (const auto &v : verts) {
          center.point += v.point;
          center.uv += v.uv;
          center.lightmap_uv += v.lightmap_uv;
        }
        center.point /= (float)n;
        center.uv /= (float)n;
        center.lightmap_uv /= (float)n;

        for (size_t i = 0; i < n; ++i) {
          emitTriangle(face, center, verts[i], verts[(i + 1) % n], newFaces);
        }
      }
      brush.m_faces = newFaces;
    }
  }

} // namespace quakelib::map
