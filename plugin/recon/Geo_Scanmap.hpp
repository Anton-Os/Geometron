#ifndef GEO_SCANMAP_H

#include <vector>

#include "Geo_Mesh.hpp"

// Image Support
//
//      Routines taking Rasteron types exist only while RASTERON_H or TOPL_ENABLE_TEXTURES is defined, both in this header and
//      in Geo_Scanmap.cpp, so the source has to be compiled with the same definitions as the code that calls them.
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
//      GEO_ENABLE_OPENMVG is defined by scripts/internalTargets.cmake once SUPPORT_PHOTOGRAM locates OpenMVG. Cameras that
//      OpenMVG solved for a set of photos can then stand in for the evenly spaced cameras assumed otherwise

#ifdef GEO_ENABLE_OPENMVG
#include <openMVG/sfm/sfm_data.hpp>
#include <openMVG/cameras/Camera_Pinhole.hpp>
#endif

// Definitions

#define SCANMAP_MAX_DIVS 256 // cells along each axis
#define SCANMAP_MAX_SOFTEN 64 // pixels that outlines are softened by

enum SCAN_Mode {
    SCAN_Silhouette = 0, // frames show the outline of the subject, space outside of any outline is carved away
    SCAN_Depth = 1 // frames also hold distance as brightness, space in front of each surface is carved away as well
};

struct Scanmap_Frame { // image of the subject
    const unsigned* data; // colors are alpha, red, green, blue from the highest byte, rows from top to bottom
    unsigned width;
    unsigned height;
};

struct Scanmap_View { // camera that a frame was taken from
    Vec3f position;
    Vec3f right, up, forward; // directions of length one, forward runs from the camera into the scene
    float focal; // in pixels, zero for a camera without perspective, below zero when unknown and the frame is skipped
    float centerX, centerY; // pixel that the forward direction passes through
    float scale; // pixels per unit of space, used without perspective
};

struct Scanmap_Params {
    SCAN_Mode mode = SCAN_Silhouette;
    Vec3f center = VEC_3F_ZERO; // middle of the space searched for the subject
    float radius = 1.0F; // space reaches this far from center along each axis
    unsigned short divs = 48; // cells along each axis, detail of the mesh

    unsigned background = 0x00000000; // color behind the subject, without alpha only transparent pixels are background
    unsigned char tolerance = 16; // difference per channel that still counts as background or as transparent

    float fov = 0.0F; // degrees from top to bottom of a frame, zero for cameras without perspective that fit the space exactly
    float distance = 3.0F; // from cameras to center
    float elevation = 0.0F; // degrees that cameras are raised
    float sweep = 360.0F; // degrees that the frames of a queue cover while circling around y, first frame faces down z

    float nearDepth = 2.0F, farDepth = 4.0F; // distances from the camera that white and black stand for, depth mode only
    bool hasColors = true; // vertices take their color from the frame that faces them most
};

// Generation Functions

DllExport void genScanmap_views(unsigned frameCount, unsigned width, unsigned height, const Scanmap_Params& params, Scanmap_View* views); // cameras spaced evenly across sweep
DllExport void genScanmap_mesh(const Scanmap_Frame* frames, const Scanmap_View* views, unsigned frameCount, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);

#ifdef GEO_RECON_IMAGES
DllExport void genScanmap_image(const Rasteron_Image* image, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices); // single camera facing down z
#ifdef RASTERON_QUEUE_H
// Each frame of the queue is a view of the subject, views are optional and default to genScanmap_views()
DllExport void genScanmap_queue(const Rasteron_Queue* queue, const Scanmap_View* views, unsigned viewCount, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
#endif
#endif

#ifdef GEO_ENABLE_OPENMVG
// Cameras from a scene solved by OpenMVG, one view per image ordered by view id to line up with frames.
// Scenes can be read with openMVG::sfm::Load() from the sfm_data file its pipeline writes, lens distortion is not applied
inline std::vector<Scanmap_View> getScanmap_views(const openMVG::sfm::SfM_Data& scene){
    std::vector<Scanmap_View> views;
    for(const auto& entry : scene.GetViews()){
        Scanmap_View view;
        view.position = VEC_3F_ZERO;
        view.right = Vec3f({ 1.0F, 0.0F, 0.0F });
        view.up = Vec3f({ 0.0F, 1.0F, 0.0F });
        view.forward = Vec3f({ 0.0F, 0.0F, -1.0F });
        view.focal = -1.0F; // skipped unless solved
        view.centerX = 0.0F; view.centerY = 0.0F;
        view.scale = 1.0F;

        const openMVG::sfm::View* target = entry.second.get();
        if(target != nullptr && scene.IsPoseAndIntrinsicDefined(target)){
            const openMVG::cameras::Pinhole_Intrinsic* camera = dynamic_cast<const openMVG::cameras::Pinhole_Intrinsic*>(scene.GetIntrinsics().at(target->id_intrinsic).get());
            if(camera != nullptr){
                const openMVG::geometry::Pose3 pose = scene.GetPoseOrDie(target);
                const openMVG::Mat3 rotation = pose.rotation(); // rows are the camera axes with y pointing down the image
                const openMVG::Vec3 center = pose.center();
                for(unsigned a = 0; a < 3; a++){
                    view.position.data[a] = (float)center(a);
                    view.right.data[a] = (float)rotation(0, a);
                    view.up.data[a] = (float)-rotation(1, a);
                    view.forward.data[a] = (float)rotation(2, a);
                }
                view.focal = (float)camera->focal();
                view.centerX = (float)camera->principal_point()(0);
                view.centerY = (float)camera->principal_point()(1);
            }
        }
        views.push_back(view);
    }
    return views;
}

// Space around the points OpenMVG located, for the center and radius of Scanmap_Params. Returns false without any points
inline bool getScanmap_space(const openMVG::sfm::SfM_Data& scene, Vec3f* center, float* radius){
    if(scene.GetLandmarks().empty()) return false;

    double middle[3] = { 0.0, 0.0, 0.0 };
    for(const auto& entry : scene.GetLandmarks())
        for(unsigned a = 0; a < 3; a++) middle[a] += entry.second.X(a) / scene.GetLandmarks().size();

    double reach = 0.0;
    for(const auto& entry : scene.GetLandmarks())
        for(unsigned a = 0; a < 3; a++){
            double offset = entry.second.X(a) - middle[a];
            if(offset < 0.0) offset = -offset;
            if(offset > reach) reach = offset;
        }

    if(center != nullptr) *center = Vec3f({ (float)middle[0], (float)middle[1], (float)middle[2] });
    if(radius != nullptr) *radius = (float)reach;
    return reach > 0.0;
}
#endif

// Scanmap
//
//      Mesh rebuilt from views of a subject. Geo_Mesh has no virtual destructor so members need no cleanup

class Geo_Scanmap : public Geo_Mesh {
public:
    DllExport Geo_Scanmap(const Scanmap_Frame* frames, const Scanmap_View* views, unsigned frameCount, Scanmap_Params params = Scanmap_Params());
#ifdef GEO_RECON_IMAGES
    DllExport Geo_Scanmap(const Rasteron_Image* image, Scanmap_Params params = Scanmap_Params());
#ifdef RASTERON_QUEUE_H
    DllExport Geo_Scanmap(const Rasteron_Queue* queue, Scanmap_Params params = Scanmap_Params());
    DllExport Geo_Scanmap(const Rasteron_Queue* queue, const Scanmap_View* views, unsigned viewCount, Scanmap_Params params = Scanmap_Params());
#ifdef GEO_ENABLE_OPENMVG
    Geo_Scanmap(const Rasteron_Queue* queue, const openMVG::sfm::SfM_Data& scene, Scanmap_Params params = Scanmap_Params())
    : Geo_Scanmap(queue, getScanmap_views(scene).data(), (unsigned)scene.GetViews().size(), params){}
#endif
#endif
#endif

    const Scanmap_Params* getParams() const { return &_params; }
private:
    Scanmap_Params _params;
};

#define GEO_SCANMAP_H
#endif
