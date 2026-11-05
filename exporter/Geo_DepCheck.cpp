// Geo_DepCheck: verifies the external geometry libraries compile, link and run on this platform
// Exit code 0 means every enabled library passed

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifdef GEO_ENABLE_ALGOFORMS
#include <openvdb/openvdb.h>
#include <openvdb/tools/LevelSetSphere.h>
#include <openvdb/tools/VolumeToMesh.h>

static bool checkOpenVDB() { // builds a sphere level set and meshes it (exercises TBB threading)
    openvdb::initialize();
    openvdb::FloatGrid::Ptr sphere = openvdb::tools::createLevelSetSphere<openvdb::FloatGrid>(1.0F, openvdb::Vec3f(0.0F), 0.05F);

    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;
    openvdb::tools::volumeToMesh(*sphere, points, triangles, quads, 0.0);

    std::printf("OpenVDB %s: sphere with %zu active voxels meshed into %zu vertices, %zu quads\n",
        openvdb::getLibraryVersionString(), (size_t)sphere->activeVoxelCount(), points.size(), quads.size());
    openvdb::uninitialize();
    return !points.empty() && !quads.empty();
}
#endif

#ifdef GEO_ENABLE_PHOTOGRAM
#include <openMVG/cameras/Camera_Pinhole.hpp>
#include <openMVG/image/image_container.hpp>
#include <openMVG/image/image_io.hpp>
#include <openMVG/multiview/projection.hpp>
#include <openMVG/multiview/triangulation.hpp>
#include <openMVG/numeric/eigen_alias_definition.hpp>

static bool checkOpenMVG() { // triangulates a point seen by two cameras and round-trips an image through the codecs
    using namespace openMVG;

    const cameras::Pinhole_Intrinsic camera(640, 480, 500.0, 320.0, 240.0);
    const Vec3 point(0.25, -0.1, 4.0);

    Mat34 P0, P1; // second camera shifted along x
    P_From_KRt(camera.K(), Mat3::Identity(), Vec3(0.0, 0.0, 0.0), &P0);
    P_From_KRt(camera.K(), Mat3::Identity(), Vec3(-0.5, 0.0, 0.0), &P1);

    const Vec2 x0 = Project(P0, point), x1 = Project(P1, point);
    Vec4 Xh;
    TriangulateDLT(P0, x0.homogeneous(), P1, x1.homogeneous(), &Xh);
    const double error = (Xh.hnormalized() - point).norm();

    image::Image<image::RGBColor> img(32, 16, true, image::RGBColor(200, 40, 10));
    bool codecs = true;
    for (const char* ext : { ".png", ".jpg", ".tif" }) {
        const std::string path = (std::filesystem::temp_directory_path() / (std::string("geo_depcheck") + ext)).string();
        image::Image<image::RGBColor> back;
        const bool ok = image::WriteImage(path.c_str(), img) && image::ReadImage(path.c_str(), &back) && back.Width() == 32 && back.Height() == 16;
        std::filesystem::remove(path);
        if (!ok) std::printf("OpenMVG: %s write/read failed\n", ext);
        codecs = codecs && ok;
    }

    std::printf("OpenMVG: triangulation error %.3g, image codecs %s\n", error, codecs ? "ok" : "FAILED");
    return error < 1e-6 && codecs;
}
#endif

int main() {
    bool passed = true;
#ifdef GEO_ENABLE_ALGOFORMS
    passed = checkOpenVDB() && passed;
#endif
#ifdef GEO_ENABLE_PHOTOGRAM
    passed = checkOpenMVG() && passed;
#endif
    std::printf(passed ? "Geo_DepCheck passed\n" : "Geo_DepCheck FAILED\n");
    return passed ? 0 : 1;
}
