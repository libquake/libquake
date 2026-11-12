#pragma once

#include "vect.h"
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using std::vector, std::string, std::map;

namespace quakelib::bsp {
#ifndef uint32_t
  typedef unsigned int uint32_t;
#endif

#ifndef int32_t
  typedef int int32_t;
#endif

#ifndef uint16_t
  typedef unsigned short uint16_t;
#endif

  const uint32_t MAGIC_V29 = 29;
  const uint32_t MAGIC_V30 = 30;

  const int NUM_HEADER_LUMPS = 15;
  const int MAX_TEXNAME = 16;
  const int MAX_MIPLEVEL = 4;

  enum ELumpType {
    LUMP_ENTITIES = 0,
    LUMP_PLANES = 1,
    LUMP_TEXTURES = 2,
    LUMP_VERTICES = 3,
    LUMP_VISIBILITY = 4,
    LUMP_NODES = 5,
    LUMP_TEXINFO = 6,
    LUMP_FACES = 7,
    LUMP_LIGHTING = 8,
    LUMP_CLIPNODES = 9,
    LUMP_LEAFS = 10,
    LUMP_MARKSURFACES = 11,
    LUMP_EDGES = 12,
    LUMP_SURFEDGES = 13,
    LUMP_MODELS = 14,
  };

  struct lump_t {
    uint32_t offset; // offset (in bytes) of the data from the beginning of the file
    uint32_t length; // length (in bytes) of the data
  };

  struct header_t {
    uint32_t version;              // version of the BSP format (29 or 30)
    lump_t lump[NUM_HEADER_LUMPS]; // directory of the lumps
  };

  struct fPlane_t {
    vec3f_t normal; // Vector orthogonal to plane (Nx,Ny,Nz)
                    // with Nx2+Ny2+Nz2 = 1
    float dist;     // Offset to plane, along the normal vector.
                    // Distance from (0,0,0) to the plane
    int32_t type;   // Type of plane, depending on normal vector.
  };

  struct fEdge_t {
    uint16_t vertex0; // index of the start vertex
                      //  must be in [0,numvertices[
    uint16_t vertex1; // index of the end vertex
                      //  must be in [0,numvertices[
  };

  struct fFace_t {
    uint16_t plane_id;      // The plane in which the face lies
                            //           must be in [0,numplanes[
    uint16_t side;          // 0 if in front of the plane, 1 if behind the plane
    uint32_t ledge_id;      // first edge in the List of edges
                            //           must be in [0,numledges[
    uint16_t ledge_num;     // number of edges in the List of edges
    uint16_t texinfo_id;    // index of the Texture info the face is part of
                            //           must be in [0,numtexinfos[
    unsigned char light[4]; // two additional light models
    int32_t lightmap;       // Pointer inside the general light map, or -1
                            // this define the start of the face light map
  };

  struct fSurfaceInfo_t {
    vec3f_t u_axis;      // U vector, horizontal in texture space)
    float u_offset;      // horizontal offset in texture space
    vec3f_t v_axis;      // V vector, vertical in texture space
    float v_offset;      // vertical offset in texture space
    uint32_t texture_id; // Index of Mip Texture
                         //           must be in [0,numtex[
    uint32_t animated;   // 0 for ordinary textures, 1 for water
  };

  struct fModel_t {
    bbox3f_t bound;   // The bounding box of the Model
    vec3f_t origin;   // origin of model, usually (0,0,0)
    int32_t node_id0; // index of first BSP node
    int32_t node_id1; // index of the first Clip node
    int32_t node_id2; // index of the second Clip node
    int32_t node_id3; // usually zero
    int32_t numleafs; // number of BSP leaves
    int32_t face_id;  // index of Faces
    int32_t face_num; // number of Faces
  };

  struct mipheader_t // Mip texture list header
  {
    int32_t numtex;  // Number of textures in Mip Texture list
    int32_t *offset; // Offset to each of the individual texture
  };

  struct miptex_t // Mip Texture
  {
    char name[MAX_TEXNAME];        // Name of the texture.
    uint32_t width;                // width of picture, must be a multiple of 8
    uint32_t height;               // height of picture, must be a multiple of 8
    uint32_t offset[MAX_MIPLEVEL]; // Offset to each of the individual texture
  };

  struct fNode_t {
    int plane_id;      // The plane that splits the node
                       //           must be in [0,numplanes[
    int16_t front;     // If bit15==0, index of Front child node
                       // If bit15==1, ~front = index of child leaf
    int16_t back;      // If bit15==0, id of Back child node
                       // If bit15==1, ~back =  id of child leaf
    bbox3s_t box;      // Bounding box of node and all childs
    uint16_t face_id;  // Index of first Polygons in the node
    uint16_t face_num; // Number of faces in the node
  };

  struct fLeaf_t {
    int32_t type;           // Special type of leaf
    int32_t vislist;        // Beginning of visibility lists
                            //     must be -1 or in [0,numvislist[
    bbox3s_t bound;         // Bounding box of the leaf
    uint16_t lface_id;      // First item of the list of faces
                            //     must be in [0,numlfaces[
    uint16_t lface_num;     // Number of faces in the leaf
    unsigned char sndwater; // level of the four ambient sounds:
    unsigned char sndsky;   //   0    is no sound
    unsigned char sndslime; //   0xFF is maximum volume
    unsigned char sndlava;  //
  };

  struct bspFileContent {
    header_t header{};
    vector<fPlane_t> planes;
    vector<fLeaf_t> leafs;
    vector<fNode_t> nodes;
    vector<vec3f_t> vertices;
    vector<fFace_t> faces;
    vector<fEdge_t> edges;
    vector<fSurfaceInfo_t> surfaces;
    vector<fModel_t> models;
    vector<miptex_t> miptextures;
    vector<int32_t> surfEdges;
  };
} // namespace quakelib::bsp