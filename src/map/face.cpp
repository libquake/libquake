#include <cmath>
#include <iostream>
#include <quakelib/map/face.h>

namespace quakelib::map {
  static constexpr double CMP_EPSILON_DISTANCE = 0.001;

  void MapSurface::initPlane() {
    setPlanePoints({toDVec3(m_planePoints[0]), toDVec3(m_planePoints[1]), toDVec3(m_planePoints[2])});
  }

  void MapSurface::setPlanePoints(const std::array<DVec3, 3> &points) {
    m_planePointsD = points;
    for (int i = 0; i < 3; i++)
      m_planePoints[i] = toVec3(points[i]);

    DVec3 v0v1 = points[1] - points[0];
    DVec3 v1v2 = points[2] - points[1];
    m_planeNormalD = normalize(cross(v1v2, v0v1));
    m_planeDistD = dot(m_planeNormalD, points[0]);
    m_planeNormal = toVec3(m_planeNormalD);
    m_planeDist = (float)m_planeDistD;
  }

  // Projection plane for standard (paraxial) texturing, same order as the Quake tools so ties on 45 degree
  // planes go to the floor/ceiling first, then the x axis walls. 0: z, 1: x, 2: y
  int MapSurface::standardAxis() const {
    double dz = std::abs(m_planeNormalD[2]);
    double dx = std::abs(m_planeNormalD[0]);
    double dy = std::abs(m_planeNormalD[1]);
    if (dz >= dx && dz >= dy)
      return 0;
    if (dx >= dy)
      return 1;
    return 2;
  }

  FacePtr MapSurface::Copy() const {
    auto newp = std::make_shared<MapSurface>();
    newp->m_vertices = m_vertices;
    newp->m_indices = m_indices;
    newp->m_planeNormal = m_planeNormal;
    newp->m_planeDist = m_planeDist;
    newp->m_planeNormalD = m_planeNormalD;
    newp->m_planeDistD = m_planeDistD;
    newp->m_planePointsD = m_planePointsD;
    newp->m_valveUV = m_valveUV;
    newp->m_standardUV = m_standardUV;
    newp->m_textureID = m_textureID;
    newp->m_rotation = m_rotation;
    newp->m_scaleY = m_scaleY;
    newp->m_scaleX = m_scaleX;
    newp->m_hasValveUV = m_hasValveUV;
    newp->m_planePoints = m_planePoints;
    newp->m_type = m_type;
    newp->min = min;
    newp->max = max;
    return newp;
  }

  void MapSurface::UpdateNormals() {
    // brush faces are flat and the plane normal points out of the brush, the winding is clockwise
    // seen from the front so a normal taken from the triangles would point inwards
    for (auto &v : m_vertices) {
      v.normal = m_planeNormal;
    }
  }

  MapSurface::eFaceClassification MapSurface::Classify(const MapSurface *other) {
    bool bFront = false, bBack = false;
    for (int i = 0; i < (int)other->m_vertices.size(); i++) {
      double dist = dot(m_planeNormalD, toDVec3(other->m_vertices[i].point)) - m_planeDistD;
      if (dist > CMP_EPSILON_DISTANCE) {
        if (bBack) {
          return eFaceClassification::SPANNING;
        }

        bFront = true;
      } else if (dist < -CMP_EPSILON_DISTANCE) {
        if (bFront) {
          return eFaceClassification::SPANNING;
        }

        bBack = true;
      }
    }

    if (bFront) {
      return eFaceClassification::FRONT;
    } else if (bBack) {
      return eFaceClassification::BACK;
    }

    return eFaceClassification::ON_PLANE;
  }

  MapSurface::eFaceClassification MapSurface::ClassifyPoint(const Vec3 &v) {
    double dist = dot(m_planeNormalD, toDVec3(v)) - m_planeDistD;
    if (dist > epsilon) {
      return MapSurface::eFaceClassification::FRONT;
    }
    if (dist < -epsilon) {
      return MapSurface::eFaceClassification::BACK;
    }
    return MapSurface::eFaceClassification::ON_PLANE;
  }

  void MapSurface::UpdateAB() {
    if (m_vertices.empty())
      return;

    min = m_vertices[0].point;
    max = m_vertices[0].point;

    for (const auto &vert : m_vertices) {
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
  }

  bool MapSurface::operator==(const MapSurface &other) const {
    if (m_vertices.size() != other.m_vertices.size() || m_planeDist != other.m_planeDist ||
        m_planeNormal != other.m_planeNormal)
      return false;

    for (int i = 0; i < m_vertices.size(); i++) {
      if (m_vertices[i].point != other.m_vertices[i].point)
        return false;

      if (m_vertices[i].uv[0] != other.m_vertices[i].uv[0])
        return false;

      if (m_vertices[i].uv[1] != other.m_vertices[i].uv[1])
        return false;
    }

    if (m_textureID == other.m_textureID)
      return true;

    return true;
  }

  Vec4 MapSurface::CalcTangent() {
    // tangent points along increasing u on the face, w tells if increasing v runs along cross(n, t) or
    // against it
    DVec3 gradU, gradV;
    if (m_hasValveUV) {
      gradU = toDVec3(m_valveUV.u.XYZ) * (1.0 / m_scaleX);
      gradV = toDVec3(m_valveUV.v.XYZ) * (1.0 / m_scaleY);
    } else {
      static const DVec3 axes[3][2] = {
          {{1, 0, 0}, {0, -1, 0}},
          {{0, 1, 0}, {0, 0, -1}},
          {{1, 0, 0}, {0, 0, -1}},
      };
      const auto &a = axes[standardAxis()][0];
      const auto &b = axes[standardAxis()][1];
      double angle = m_rotation * (HMM_PI / 180.0);
      double c = std::cos(angle), s = std::sin(angle);
      gradU = (a * c - b * s) * (1.0 / m_scaleX);
      gradV = (a * s + b * c) * (1.0 / m_scaleY);
    }

    // the in-plane directions dP/du and dP/dv, these differ from the projected gradients whenever the
    // projection is skewed, which is the case for standard texturing on any sloped face
    const DVec3 &n = m_planeNormalD;
    DVec3 dPdu = cross(gradV, n);
    DVec3 dPdv = cross(n, gradU);
    double du = dot(gradU, dPdu);
    double dv = dot(gradV, dPdv);
    if (std::abs(du) < 1e-12 || std::abs(dv) < 1e-12)
      return Vec4{0, 0, 0, 1};

    DVec3 t = normalize(dPdu * (du > 0 ? 1.0 : -1.0));
    DVec3 b = dPdv * (dv > 0 ? 1.0 : -1.0);
    double w = dot(cross(n, t), b) >= 0 ? 1.0 : -1.0;
    return Vec4{(float)t[0], (float)t[1], (float)t[2], (float)w};
  }

  Vec2 MapSurface::calcStandardUV(Vec3 vertex, float texW, float texH) {
    Vec2 uvOut{0};

    switch (standardAxis()) {
    case 0:
      uvOut = Vec2{vertex[0], -vertex[1]};
      break;
    case 1:
      uvOut = Vec2{vertex[1], -vertex[2]};
      break;
    default:
      uvOut = Vec2{vertex[0], -vertex[2]};
      break;
    }

    float angle = (float)(m_rotation * (HMM_PI / 180));
    uvOut =
        Vec2{uvOut[0] * cos(angle) - uvOut[1] * sin(angle), uvOut[0] * sin(angle) + uvOut[1] * cos(angle)};

    uvOut[0] /= texW;
    uvOut[1] /= texH;

    uvOut[0] /= m_scaleX;
    uvOut[1] /= m_scaleY;

    uvOut[0] += m_standardUV.u / texW;
    uvOut[1] += m_standardUV.v / texH;
    return uvOut;
  }

  Vec2 MapSurface::calcValveUV(Vec3 vertex, float texW, float texH) {
    Vec2 uvOut{0};
    Vec3 uAxis = m_valveUV.u.XYZ;
    Vec3 vAxis = m_valveUV.v.XYZ;
    float uShift = m_valveUV.u[3];
    float vShift = m_valveUV.v[3];

    uvOut[0] = math::Dot(uAxis, vertex);
    uvOut[1] = math::Dot(vAxis, vertex);

    uvOut[0] /= texW;
    uvOut[1] /= texH;

    uvOut[0] /= m_scaleX;
    uvOut[1] /= m_scaleY;

    uvOut[0] += uShift / texW;
    uvOut[1] += vShift / texH;

    return uvOut;
  }

  Vec2 MapSurface::calcStandardLightmapUV(Vec3 vertex) {
    Vec2 uvOut{0};

    switch (standardAxis()) {
    case 0:
      uvOut = Vec2{vertex[0], -vertex[1]};
      break;
    case 1:
      uvOut = Vec2{vertex[1], -vertex[2]};
      break;
    default:
      uvOut = Vec2{vertex[0], -vertex[2]};
      break;
    }
    return uvOut;
  }

  Vec2 MapSurface::calcValveLightmapUV(Vec3 vertex) {
    Vec2 uvOut{0};
    Vec3 uAxis = m_valveUV.u.XYZ;
    Vec3 vAxis = m_valveUV.v.XYZ;

    uvOut[0] = math::Dot(uAxis, vertex);
    uvOut[1] = math::Dot(vAxis, vertex);

    return uvOut;
  }

  static Vec3 SolvePlanes(const Vec3 &n, float d, const Vec3 &u, float u_val, const Vec3 &v, float v_val) {

    Vec3 nu_cross = math::Cross(n, u);
    float det = math::Dot(nu_cross, v);

    if (std::abs(det) < 1e-5f)
      return {0, 0, 0};

    return (math::Cross(u, v) * d + math::Cross(v, n) * u_val + math::Cross(n, u) * v_val) / det;
  }

  Vec3 MapSurface::calcWorldFromStandardLightmapUV(Vec2 uv) {

    Vec3 uAxis, vAxis;
    switch (standardAxis()) {
    case 0:
      uAxis = {1, 0, 0};
      vAxis = {0, -1, 0};
      break;
    case 1:
      uAxis = {0, 1, 0};
      vAxis = {0, 0, -1};
      break;
    default:
      uAxis = {1, 0, 0};
      vAxis = {0, 0, -1};
      break;
    }

    return SolvePlanes(m_planeNormal, m_planeDist, uAxis, uv[0], vAxis, uv[1]);
  }

  Vec3 MapSurface::calcWorldFromValveLightmapUV(Vec2 uv) {
    Vec3 uAxis = m_valveUV.u.XYZ;
    Vec3 vAxis = m_valveUV.v.XYZ;

    return SolvePlanes(m_planeNormal, m_planeDist, uAxis, uv[0], vAxis, uv[1]);
  }

} // namespace quakelib::map