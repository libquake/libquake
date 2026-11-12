#pragma once

#include "bsp_file.h"
#include "primitives.h"
#include <quakelib/entities.h>

namespace quakelib::bsp {
  class SolidEntity : public quakelib::SolidEntity {
  public:
    SolidEntity(const bspFileContent &ctx, ParsedEntity *pe);
    const std::vector<SurfacePtr> &Faces();
    bool IsWorldSpawn();

    int ModelId() const { return m_modelId; }

  protected:
    void convertToOpenGLCoords();

  private:
    void buildBSPTree(const fNode_t &);
    void getSurfaceIDsFromLeaf(int leafID);
    int getVertIndexFromEdge(int surfEdge);
    std::vector<SurfacePtr> m_faces;
    int m_modelId = 0;

    friend class QBsp;
  };

  typedef std::shared_ptr<SolidEntity> SolidEntityPtr;

} // namespace quakelib::bsp