#ifndef GEO_HEIGHTMAP_H

#include <vector>

#include "Geo_Mesh.hpp"

// Image Support
//
//      Routines taking Rasteron types exist only while RASTERON_H or TOPL_ENABLE_TEXTURES is defined, both in this header and
//      in Geo_Heightmap.cpp, so the source has to be compiled with the same definitions as the code that calls them.
//      Only fields of the Rasteron types are read, no Rasteron functions are called and nothing links against it

#if defined(TOPL_ENABLE_TEXTURES) && !defined(RASTERON_H)
#if defined(__has_include)
#if __has_include("Rasteron.h")
#include "Rasteron.h"
#endif
#else
#include "Rasteron.h"
#endif
#endif

#if defined(RASTERON_H) && !defined(GEO_RECON_IMAGES)
#define GEO_RECON_IMAGES
#endif

// OpenMVG Support
//
//      GEO_ENABLE_OPENMVG is defined by scripts/internalTargets.cmake once SUPPORT_PHOTOGRAM locates OpenMVG, images loaded
//      through OpenMVG can then be used for heights

#ifdef GEO_ENABLE_OPENMVG
#include <openMVG/image/image_container.hpp>
#endif

// Definitions

enum HEIGHTMAP_Channel { HEIGHTMAP_Gray = 0, HEIGHTMAP_Red = 1, HEIGHTMAP_Green = 2, HEIGHTMAP_Blue = 3, HEIGHTMAP_Alpha = 4 };

struct Heightmap_Params {
    float size = 2.0F; // length of the longer side, mesh is centered on the origin across x and y
    float depth = 1.0F; // distance along z between heights of 0 and 1
    float z = DEFAULT_Z; // where a height of 0 sits
    float thickness = 0.0F; // above zero closes the mesh with sides and a flat base this far below the lowest point
    unsigned short stride = 1; // rows and columns between samples, higher values lower detail
    unsigned short smoothing = 0; // times each sample is averaged with adjacent samples
    HEIGHTMAP_Channel channel = HEIGHTMAP_Gray; // part of image colors used for height
    bool isInverted = false; // dark pixels become high
};

// Generation Functions

// Heights are rows from top to bottom with values from 0 to 1, the first row ends up along the top of the mesh facing z
DllExport void genHeightmap_mesh(const float* heights, unsigned rows, unsigned cols, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport void genHeightmap_heights(const unsigned* colors, unsigned count, HEIGHTMAP_Channel channel, std::vector<float>& heights); // colors are alpha, red, green, blue from the highest byte

#ifdef GEO_RECON_IMAGES
DllExport void genHeightmap_image(const Rasteron_Image* image, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
#ifdef RASTERON_SPACE_H
// Rows and columns come from the heightmap bounds and heights are rescaled to 0 and 1 from its depths.
// Note loadHeightmap() passes width as height when creating bounds, images that are not square need genHeightmap_mesh() with their real size
DllExport void genHeightmap_map(const Rasteron_Heightmap* heightmap, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
#endif
#endif

#ifdef GEO_ENABLE_OPENMVG
inline std::vector<float> getHeightmap_heights(const openMVG::image::Image<unsigned char>& image){ // grayscale image
    std::vector<float> heights;
    for(int r = 0; r < image.Height(); r++)
        for(int c = 0; c < image.Width(); c++) heights.push_back(image(r, c) / 255.0F);
    return heights;
}

inline std::vector<float> getHeightmap_heights(const openMVG::image::Image<float>& image){ // values are used as they are
    std::vector<float> heights;
    for(int r = 0; r < image.Height(); r++)
        for(int c = 0; c < image.Width(); c++) heights.push_back(image(r, c));
    return heights;
}
#endif

// Heightmap
//
//      Grid of vertices raised along z by heights. Geo_Mesh has no virtual destructor so members need no cleanup

class Geo_Heightmap : public Geo_Mesh {
public:
    DllExport Geo_Heightmap(const float* heights, unsigned rows, unsigned cols, Heightmap_Params params = Heightmap_Params());
#ifdef GEO_RECON_IMAGES
    DllExport Geo_Heightmap(const Rasteron_Image* image, Heightmap_Params params = Heightmap_Params());
#ifdef RASTERON_SPACE_H
    DllExport Geo_Heightmap(const Rasteron_Heightmap* heightmap, Heightmap_Params params = Heightmap_Params());
#endif
#endif
#ifdef GEO_ENABLE_OPENMVG
    Geo_Heightmap(const openMVG::image::Image<unsigned char>& image, Heightmap_Params params = Heightmap_Params())
    : Geo_Heightmap(getHeightmap_heights(image).data(), (unsigned)image.Height(), (unsigned)image.Width(), params){}
    Geo_Heightmap(const openMVG::image::Image<float>& image, Heightmap_Params params = Heightmap_Params())
    : Geo_Heightmap(getHeightmap_heights(image).data(), (unsigned)image.Height(), (unsigned)image.Width(), params){}
#endif

    const Heightmap_Params* getParams() const { return &_params; }
    bool isSolid() const { return _params.thickness > 0.0F; }
private:
    Heightmap_Params _params;
};

#define GEO_HEIGHTMAP_H
#endif
