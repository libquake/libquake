#include "bake_bvh.h"

#include <embree4/rtcore.h>

#include <stdexcept>

namespace quakelib::detail {

  struct Bvh::Impl {
    RTCDevice device = nullptr;
    RTCScene scene = nullptr;

    ~Impl() {
      if (scene)
        rtcReleaseScene(scene);
      if (device)
        rtcReleaseDevice(device);
    }
  };

  Bvh::Bvh(const std::vector<Vec3> &positions) : m_impl(std::make_unique<Impl>()) {
    const size_t n = positions.size() / 3;
    if (n == 0)
      return;

    // pinning threads to cores fails without notice under Rosetta and gains the baker nothing
    m_impl->device = rtcNewDevice("set_affinity=0");
    if (!m_impl->device)
      throw std::runtime_error("Embree: no device");

    m_impl->scene = rtcNewScene(m_impl->device);
    rtcSetSceneBuildQuality(m_impl->scene, RTC_BUILD_QUALITY_HIGH);

    RTCGeometry geometry = rtcNewGeometry(m_impl->device, RTC_GEOMETRY_TYPE_TRIANGLE);
    auto *vertices = (float *)rtcSetNewGeometryBuffer(geometry, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3,
                                                      3 * sizeof(float), n * 3);
    auto *indices = (unsigned *)rtcSetNewGeometryBuffer(geometry, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3,
                                                        3 * sizeof(unsigned), n);
    for (size_t i = 0; i < n * 3; i++) {
      for (int a = 0; a < 3; a++)
        vertices[i * 3 + a] = positions[i][a];
      indices[i] = (unsigned)i;
    }
    rtcCommitGeometry(geometry);
    rtcAttachGeometry(m_impl->scene, geometry);
    rtcReleaseGeometry(geometry);
    rtcCommitScene(m_impl->scene);
  }

  Bvh::~Bvh() = default;

  namespace {
    RTCRay toEmbree(const Ray &r, float tmin, float tmax) {
      RTCRay ray{};
      ray.org_x = r.origin[0];
      ray.org_y = r.origin[1];
      ray.org_z = r.origin[2];
      ray.dir_x = r.direction[0];
      ray.dir_y = r.direction[1];
      ray.dir_z = r.direction[2];
      ray.tnear = tmin;
      ray.tfar = tmax;
      ray.mask = ~0u;
      return ray;
    }
  } // namespace

  int Bvh::Closest(const Ray &r, float tmin, float tmax, float &tHit) const {
    if (!m_impl->scene)
      return -1;

    RTCRayHit hit{};
    hit.ray = toEmbree(r, tmin, tmax);
    hit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(m_impl->scene, &hit);
    if (hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
      return -1;

    tHit = hit.ray.tfar;
    return (int)hit.hit.primID;
  }

  bool Bvh::Any(const Ray &r, float tmin, float tmax) const {
    if (!m_impl->scene)
      return false;

    RTCRay ray = toEmbree(r, tmin, tmax);
    rtcOccluded1(m_impl->scene, &ray);
    // a hit sets tfar to -inf
    return ray.tfar < 0;
  }

} // namespace quakelib::detail
