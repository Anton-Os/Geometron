#ifndef GEO_FORMS_H

#include <cmath>
#include <cstdlib>
#include <utility>
#include <vector>
#include <unordered_map>
#include <initializer_list>

#include "Geo_Mesh.hpp"

// OpenVDB Support
//
//      GEO_ENABLE_OPENVDB is defined by scripts/internalTargets.cmake once SUPPORT_ALGOFORMS locates OpenVDB.
//      Every form works without it, the definition adds conversions between meshes and OpenVDB grids

#ifdef GEO_ENABLE_OPENVDB
#include <openvdb/openvdb.h>
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#endif

// Definitions

#define FORM_NEBRS 27 // target and adjacent points within one step along each axis
#define FORM_NEBR(x, y, z) (((x) + 1) + (((y) + 1) * 3) + (((z) + 1) * 9)) // x, y, and z are offsets of -1, 0, or 1
#define FORM_TARGET 13 // same as FORM_NEBR(0, 0, 0)
#define FORM_CORNERS 8 // corners of a cell
#define FORM_CORNER(x, y, z) ((x) + ((y) * 2) + ((z) * 4)) // x, y, and z are 0 or 1
#define FORM_NO_INDEX 0xFFFFFFFF // point is culled or outside of form
#define FORM_MESHLET_MAX 36 // enough for 12 triangles
#define FORM_ISOLEVEL 0.5F // corner values above are inside of a voxel form
#define FORM_WELD 0.00001F // fraction of form size within which vertices are merged

#define CSG_VOLUME_DIVS 128 // voxels across the meshes of a volume composite

#define RECURSIVE_MAX_CELLS 81 // cells along each axis of a recursive grid, 4 levels of 3 divisions
#define RECURSIVE_MAX_LEVELS 6 // levels of a koch form
#define RECURSIVE_MAX_VERTICES 1000000 // vertices of a form built from copies or koch growth, levels stop before passing it

enum VOXEL_Cell {
    VOXEL_Cube = 0, // box filling the cell, faces between filled cells are removed
    VOXEL_Octa = 1, // octahedron touching the middle of each cell face
    VOXEL_Point = 2 // single vertex at the cell center
};

enum CSG_Op {
    CSG_Union = 0, // space inside of any mesh
    CSG_Difference = 1, // space inside of first mesh with following meshes removed
    CSG_Intersect = 2, // space inside of every mesh
    CSG_Xor = 3 // space inside of an odd number of meshes
};

enum CSG_Method {
    CSG_Exact = 0, // splits polygons along a tree of planes, keeps sharp edges
    CSG_Volume = 1 // combines level sets, requires GEO_ENABLE_OPENVDB and is otherwise the same as exact
};

enum RECURSIVE_Type {
    RECURSIVE_Menger = 0, // menger sponge, cube with the center of each face and of the cube removed
    RECURSIVE_Cross = 1, // inverse of the sponge, only cube center and face centers are kept
    RECURSIVE_Dust = 2, // cantor dust, only corners are kept
    RECURSIVE_Mosely = 3, // mosely snowflake, cube with the corners and the center removed
    RECURSIVE_Koch = 4, // koch surface, each triangle of a tetrahedron grows a smaller tetrahedron
    RECURSIVE_KochPrism = 5, // koch snowflake curve extruded along z
    RECURSIVE_Tetrix = 6 // sierpinski tetrahedron
};

#ifdef __cplusplus
extern "C" {
#endif

// Form Description

struct Form { // cube centered on the origin that is divided along each axis
    unsigned getCount() const { return xDivs * yDivs * zDivs; }

    float radius; // forms span from -radius to radius
    unsigned xDivs;
    unsigned yDivs;
    unsigned zDivs;
};

struct Form_Lattice { // placement of points or cells within space
    unsigned getCount() const { return dims[0] * dims[1] * dims[2]; }
    unsigned getIndex(unsigned x, unsigned y, unsigned z) const { return x + (y * dims[0]) + (z * dims[0] * dims[1]); }
    Vec3f getPoint(unsigned x, unsigned y, unsigned z) const {
        return Vec3f({ origin.data[0] + (x * spacing.data[0]), origin.data[1] + (y * spacing.data[1]), origin.data[2] + (z * spacing.data[2]) });
    }

    Vec3f origin; // first point
    Vec3f spacing; // distance between points along each axis
    unsigned dims[3]; // points along each axis
};

struct Form_Nebrs { // point of a freeform with adjacent points, offset by -1, 0, or 1 along each axis
    bool has(int x, int y, int z) const { return indices[FORM_NEBR(x, y, z)] != FORM_NO_INDEX; }
    Vec3f getPoint(int x, int y, int z) const { return points[FORM_NEBR(x, y, z)]; }
    unsigned getIndex(int x, int y, int z) const { return indices[FORM_NEBR(x, y, z)]; }

    Vec3f points[FORM_NEBRS]; // target is at FORM_TARGET, VEC_3F_BAD when culled or outside of form
    unsigned indices[FORM_NEBRS]; // vertex index for each point, FORM_NO_INDEX when culled or outside of form
    unsigned coords[3]; // position of target inside of lattice
    unsigned count; // points kept after culling
};

struct Form_Voxel { // cell of a voxel form with surroundings
    bool has(int x, int y, int z) const { return ((nebrs >> FORM_NEBR(x, y, z)) & 1) != 0; }

    Vec3f center;
    Vec3f size;
    Vec3f corners[FORM_CORNERS]; // corner positions ordered by FORM_CORNER
    float values[FORM_CORNERS]; // fraction of the cells around each corner that are filled, see FORM_ISOLEVEL
    unsigned coords[3]; // position of cell inside of lattice
    unsigned nebrs; // bit FORM_NEBR(x, y, z) is set when the adjacent cell is filled
    bool isFilled; // empty cells beside filled cells are also visited
};

struct Form_Meshlet { // geometry spawned for a point or cell, either as new vertices, as indices to existing points, or both
    bool addVertex(Vec3f position){
        if(vertexCount >= FORM_MESHLET_MAX) return false;
        positions[vertexCount++] = position;
        return true;
    }
    bool addIndex(unsigned index){
        if(indexCount >= FORM_MESHLET_MAX || index == FORM_NO_INDEX) return false;
        indices[indexCount++] = index;
        return true;
    }

    unsigned short vertexCount;
    Vec3f positions[FORM_MESHLET_MAX];
    unsigned short indexCount;
    unsigned indices[FORM_MESHLET_MAX];
    DRAW_Mode mode; // what the vertices and indices describe, triangles unless a callback changes it to lines or points
};

struct Form_Tform { // copy of a shape relative to its parent
    Vec3f shift;
    Vec3f rotation; // same convention as rotateTForm()
    float scale;
};

// Callbacks

typedef bool (*fCullCallback)(Vec3f, double); // position and form radius, points and cells are kept when true
typedef Geo_Meshlet (*fSpawnCallback)(Vec3f, unsigned); // spawns positions from a point and the point count, positions are freed by the form
typedef void (*fAlgoCallback)(const Form_Nebrs*, Form_Meshlet*); // spawns vertices or indices from a point and adjacent points
typedef void (*fCellCallback)(const Form_Voxel*, Form_Meshlet*); // spawns triangle vertices for a cell, indices are not used
typedef bool (*fRuleCallback)(unsigned, unsigned, unsigned, unsigned short, unsigned short); // x, y, and z of a cell within its parent, divisions, and level, cell is kept when true

// Freeform Functions

DllExport bool freeformCull_none(Vec3f input, double level); // keeps every point
DllExport bool freeformCull_ball(Vec3f input, double level); // keeps points inside of radius
DllExport bool freeformCull_shell(Vec3f input, double level); // keeps points between half and full radius

DllExport Geo_Meshlet freeformSpawn_lines(Vec3f input, unsigned count); // line from point towards origin

DllExport void freeformAlgo_links(const Form_Nebrs* nebrs, Form_Meshlet* meshlet); // vertices, lines to adjacent points
DllExport void freeformAlgo_trigs(const Form_Nebrs* nebrs, Form_Meshlet* meshlet); // vertices, triangles between pairs of adjacent points
DllExport void freeformAlgo_hull(const Form_Nebrs* nebrs, Form_Meshlet* meshlet); // indices, closed surface around the points kept

// Voxelform Functions

DllExport bool voxelCull_none(Vec3f input, double level); // keeps every cell
DllExport bool voxelCull_ball(Vec3f input, double level); // keeps cells inside of radius
DllExport bool voxelCull_tube(Vec3f input, double level); // keeps cells inside of a ring around z

DllExport void voxelCell_surface(const Form_Voxel* voxel, Form_Meshlet* meshlet); // surface along the corner values, rounds off steps and drops parts thinner than two cells

// Recursive Functions

DllExport bool recursiveRule_menger(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level);
DllExport bool recursiveRule_cross(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level);
DllExport bool recursiveRule_dust(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level);
DllExport bool recursiveRule_mosely(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level);

#ifdef __cplusplus
}

// Generation Functions

DllExport DRAW_Mode genFreeform_mesh(const Form_Lattice& lattice, fCullCallback cullCallback, fAlgoCallback algoCallback, double level, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);

DllExport void genVoxelform_cells(const Form_Lattice& lattice, fCullCallback cullCallback, double level, std::vector<unsigned char>& cells); // one entry per cell, nonzero when filled
DllExport void genVoxelform_mesh(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, VOXEL_Cell cell, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport void genVoxelform_meshExt(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, const Geo_Mesh* cellMesh, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport void genVoxelform_meshDyn(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, fCellCallback cellCallback, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);

DllExport bool genComposite_mesh(const Geo_Mesh* const* meshes, unsigned meshCount, CSG_Op op, CSG_Method method, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport Geo_Mesh* genComposite_union(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2); // result is owned by the caller
DllExport Geo_Mesh* genComposite_difference(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2);
DllExport Geo_Mesh* genComposite_intersect(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2);
DllExport Geo_Mesh* genComposite_xor(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2);

DllExport void genRecursive_cells(float radius, unsigned short divs, unsigned short levels, fRuleCallback ruleCallback, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport void genRecursive_koch(const Geo_Mesh* mesh, float radius, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices); // mesh is optional, defaults to a tetrahedron
DllExport void genRecursive_kochPrism(float radius, float depth, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);
DllExport void genRecursive_copies(const Geo_Mesh* mesh, const Form_Tform* tforms, unsigned tformCount, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices);

// Shared Form
//
//      Base for every form inside of plugin/form, static members are helpers that the form source files share.
//      Geo_Mesh has no virtual destructor so forms should only add members that need no cleanup

struct Form_Key { // quantized position and normal used to locate matching vertices
    long long data[6];

    bool operator == (const Form_Key& key) const {
        for(unsigned d = 0; d < 6; d++) if(data[d] != key.data[d]) return false;
        return true;
    }
};

struct Form_KeyHash {
    size_t operator()(const Form_Key& key) const {
        size_t hash = 2166136261U;
        for(unsigned d = 0; d < 6; d++){
            hash = (hash ^ (size_t)(key.data[d] & 0xFFFFFFFF)) * 16777619U;
            hash = (hash ^ (size_t)((key.data[d] >> 32) & 0xFFFFFFFF)) * 16777619U;
        }
        return hash;
    }
};

class Geo_Form : public Geo_Mesh {
public:
    Geo_Form() : Geo_Mesh() {}
    Geo_Form(Form shape) : Geo_Mesh() { _shape = shape; }

    Form getShape() const { return _shape; }
    float getRadius() const { return _shape.radius; }
    void getBounds(Vec3f* minBound, Vec3f* maxBound) const { calcBounds(_vertices, minBound, maxBound); }
    double getVolume() const { return calcVolume(_vertices, _indices); } // space enclosed by a closed triangle mesh
    bool isClosed() const { return isClosed(_vertices, _indices, getWeld(_vertices)); } // every edge is shared by two triangles facing the same way

    // Vector Helpers

    static float dot(const Vec3f& a, const Vec3f& b){ return (a.data[0] * b.data[0]) + (a.data[1] * b.data[1]) + (a.data[2] * b.data[2]); }
    static Vec3f cross(const Vec3f& a, const Vec3f& b){
        return Vec3f({ (a.data[1] * b.data[2]) - (a.data[2] * b.data[1]), (a.data[2] * b.data[0]) - (a.data[0] * b.data[2]), (a.data[0] * b.data[1]) - (a.data[1] * b.data[0]) });
    }
    static Vec3f unit(const Vec3f& vec){ // result has a length of one, unlike Vec3f::normalize()
        float length = vec.len();
        return (length > 0.0F && std::isfinite(length))? vec * (1.0F / length) : VEC_3F_ZERO;
    }
    static Geo_Vertex getVertex(const Vec3f& position, const Vec3f& normal){ return Geo_Vertex(position, VEC_3F_ZERO, normal, VEC_3F_ONES); }

    // Lattice Helpers

    static Form_Lattice getPoints(Form shape){ // points with the first and last of each axis on the form boundary
        Form_Lattice lattice;
        unsigned divs[3] = { shape.xDivs, shape.yDivs, shape.zDivs };
        for(unsigned a = 0; a < 3; a++){
            lattice.dims[a] = (divs[a] > 0)? divs[a] : 1;
            lattice.spacing.data[a] = (lattice.dims[a] > 1)? (shape.radius * 2.0F) / (lattice.dims[a] - 1) : 0.0F;
            lattice.origin.data[a] = (lattice.dims[a] > 1)? -shape.radius : 0.0F;
        }
        return lattice;
    }
    static Form_Lattice getCells(Form shape){ // centers of the cells that fill the form
        Form_Lattice lattice;
        unsigned divs[3] = { shape.xDivs, shape.yDivs, shape.zDivs };
        for(unsigned a = 0; a < 3; a++){
            lattice.dims[a] = (divs[a] > 0)? divs[a] : 1;
            lattice.spacing.data[a] = (shape.radius * 2.0F) / lattice.dims[a];
            lattice.origin.data[a] = -shape.radius + (lattice.spacing.data[a] * 0.5F);
        }
        return lattice;
    }

    // Geometry Helpers

    static void calcBounds(const std::vector<Geo_Vertex>& vertices, Vec3f* minBound, Vec3f* maxBound){
        Vec3f lo = VEC_3F_ZERO, hi = VEC_3F_ZERO;
        for(size_t v = 0; v < vertices.size(); v++)
            for(unsigned a = 0; a < 3; a++){
                float value = vertices[v].position.data[a];
                if(v == 0 || value < lo.data[a]) lo.data[a] = value;
                if(v == 0 || value > hi.data[a]) hi.data[a] = value;
            }
        if(minBound != nullptr) *minBound = lo;
        if(maxBound != nullptr) *maxBound = hi;
    }

    static float getWeld(const std::vector<Geo_Vertex>& vertices){ // distance within which vertices of a mesh count as the same
        Vec3f lo, hi;
        calcBounds(vertices, &lo, &hi);
        float size = (hi - lo).len();
        return (size > 0.0F)? size * FORM_WELD : FORM_WELD;
    }

    static double calcVolume(const std::vector<Geo_Vertex>& vertices, const std::vector<unsigned>& indices){
        double volume = 0.0;
        for(size_t i = 0; i + 2 < indices.size(); i += 3){
            if(indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) continue;
            const Vec3f& a = vertices[indices[i]].position;
            const Vec3f& b = vertices[indices[i + 1]].position;
            const Vec3f& c = vertices[indices[i + 2]].position;
            volume += ((double)a.data[0] * (((double)b.data[1] * c.data[2]) - ((double)b.data[2] * c.data[1])))
                + ((double)a.data[1] * (((double)b.data[2] * c.data[0]) - ((double)b.data[0] * c.data[2])))
                + ((double)a.data[2] * (((double)b.data[0] * c.data[1]) - ((double)b.data[1] * c.data[0])));
        }
        return volume / 6.0;
    }

    static void calcNormals(std::vector<Geo_Vertex>& vertices, const std::vector<unsigned>& indices){ // area weighted from triangles
        std::vector<Vec3f> normals(vertices.size(), VEC_3F_ZERO);
        for(size_t i = 0; i + 2 < indices.size(); i += 3){
            unsigned t[3] = { indices[i], indices[i + 1], indices[i + 2] };
            if(t[0] >= vertices.size() || t[1] >= vertices.size() || t[2] >= vertices.size()) continue;
            Vec3f normal = cross(vertices[t[1]].position - vertices[t[0]].position, vertices[t[2]].position - vertices[t[0]].position);
            for(unsigned n = 0; n < 3; n++) normals[t[n]] = normals[t[n]] + normal;
        }
        for(size_t v = 0; v < vertices.size(); v++){
            Vec3f normal = unit(normals[v]);
            vertices[v].normal = (!normal.isZero())? normal : unit(vertices[v].position);
        }
    }

    static void calcTexcoords(std::vector<Geo_Vertex>& vertices, bool isBoxed){ // from bounds, boxed maps along the axis each normal faces most
        Vec3f lo, hi;
        calcBounds(vertices, &lo, &hi);
        Vec3f size = hi - lo;
        for(unsigned a = 0; a < 3; a++) if(size.data[a] <= 0.0F) size.data[a] = 1.0F;

        for(size_t v = 0; v < vertices.size(); v++){
            Vec3f coord = vertices[v].position - lo;
            for(unsigned a = 0; a < 3; a++) coord.data[a] /= size.data[a];

            const Vec3f& normal = vertices[v].normal;
            float nx = (float)fabs(normal.data[0]), ny = (float)fabs(normal.data[1]), nz = (float)fabs(normal.data[2]);
            if(isBoxed && nx > ny && nx > nz) vertices[v].texcoord = Vec3f({ coord.data[2], 1.0F - coord.data[1], coord.data[0] });
            else if(isBoxed && ny > nz) vertices[v].texcoord = Vec3f({ coord.data[0], coord.data[2], coord.data[1] });
            else vertices[v].texcoord = Vec3f({ coord.data[0], 1.0F - coord.data[1], coord.data[2] });
        }
    }

    static Form_Key getKey(const Geo_Vertex& vertex, float epsilon, bool isHard){
        Form_Key key;
        double scale = 1.0 / ((epsilon > 0.0F)? epsilon : FORM_WELD);
        for(unsigned a = 0; a < 3; a++){
            key.data[a] = (long long)std::floor((vertex.position.data[a] * scale) + 0.5);
            key.data[a + 3] = (isHard)? (long long)std::floor((vertex.normal.data[a] * 512.0) + 0.5) : 0;
        }
        return key;
    }

    // Merges vertices of a triangle mesh that share a position, and a normal when hard, then drops triangles left without area
    static unsigned weld(std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices, float epsilon, bool isHard){
        if(indices.empty()) for(unsigned v = 0; v < vertices.size(); v++) indices.push_back(v);

        std::unordered_map<Form_Key, unsigned, Form_KeyHash> lookup;
        std::vector<Geo_Vertex> welded;
        std::vector<unsigned> remap(vertices.size());
        for(size_t v = 0; v < vertices.size(); v++){
            Form_Key key = getKey(vertices[v], epsilon, isHard);
            std::unordered_map<Form_Key, unsigned, Form_KeyHash>::iterator match = lookup.find(key);
            if(match == lookup.end()){
                remap[v] = (unsigned)welded.size();
                lookup.insert(std::make_pair(key, remap[v]));
                welded.push_back(vertices[v]);
            } else remap[v] = match->second;
        }

        std::vector<unsigned> triangles;
        for(size_t i = 0; i + 2 < indices.size(); i += 3){
            if(indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) continue;
            unsigned t[3] = { remap[indices[i]], remap[indices[i + 1]], remap[indices[i + 2]] };
            if(t[0] != t[1] && t[1] != t[2] && t[0] != t[2]) triangles.insert(triangles.end(), t, t + 3);
        }

        unsigned removed = (unsigned)(vertices.size() - welded.size());
        vertices.swap(welded);
        indices.swap(triangles);
        return removed;
    }

    static bool isClosed(const std::vector<Geo_Vertex>& vertices, const std::vector<unsigned>& indices, float epsilon){
        if(indices.size() < 3) return false;

        std::unordered_map<Form_Key, unsigned, Form_KeyHash> lookup; // matches vertices by position
        std::vector<unsigned> remap(vertices.size());
        for(size_t v = 0; v < vertices.size(); v++){
            Form_Key key = getKey(vertices[v], epsilon, false);
            std::unordered_map<Form_Key, unsigned, Form_KeyHash>::iterator match = lookup.find(key);
            if(match == lookup.end()){
                remap[v] = (unsigned)lookup.size();
                lookup.insert(std::make_pair(key, remap[v]));
            } else remap[v] = match->second;
        }

        std::unordered_map<unsigned long long, std::pair<int, int>> edges; // uses of each edge and their balance of direction
        for(size_t i = 0; i + 2 < indices.size(); i += 3){
            if(indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) return false;
            unsigned t[3] = { remap[indices[i]], remap[indices[i + 1]], remap[indices[i + 2]] };
            if(t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
            for(unsigned n = 0; n < 3; n++){
                unsigned a = t[n], b = t[(n + 1) % 3];
                unsigned long long edge = (a < b)? (((unsigned long long)a << 32) | b) : (((unsigned long long)b << 32) | a);
                std::pair<int, int>& uses = edges[edge];
                uses.first++;
                uses.second += (a < b)? 1 : -1;
            }
        }

        if(edges.empty()) return false;
        for(std::unordered_map<unsigned long long, std::pair<int, int>>::const_iterator e = edges.begin(); e != edges.end(); e++)
            if(e->second.first != 2 || e->second.second != 0) return false;
        return true;
    }

    static bool isRayCrossing(const double* origin, const double* direction, const Vec3f& p1, const Vec3f& p2, const Vec3f& p3){
        double edge1[3], edge2[3], offset[3];
        for(unsigned a = 0; a < 3; a++){
            edge1[a] = (double)p2.data[a] - p1.data[a];
            edge2[a] = (double)p3.data[a] - p1.data[a];
            offset[a] = origin[a] - p1.data[a];
        }
        double h[3] = { (direction[1] * edge2[2]) - (direction[2] * edge2[1]), (direction[2] * edge2[0]) - (direction[0] * edge2[2]), (direction[0] * edge2[1]) - (direction[1] * edge2[0]) };
        double det = (edge1[0] * h[0]) + (edge1[1] * h[1]) + (edge1[2] * h[2]);
        if(fabs(det) < 1e-18) return false; // ray runs along triangle

        double u = ((offset[0] * h[0]) + (offset[1] * h[1]) + (offset[2] * h[2])) / det;
        if(u < 0.0 || u > 1.0) return false;
        double q[3] = { (offset[1] * edge1[2]) - (offset[2] * edge1[1]), (offset[2] * edge1[0]) - (offset[0] * edge1[2]), (offset[0] * edge1[1]) - (offset[1] * edge1[0]) };
        double v = ((direction[0] * q[0]) + (direction[1] * q[1]) + (direction[2] * q[2])) / det;
        if(v < 0.0 || u + v > 1.0) return false;
        return (((edge2[0] * q[0]) + (edge2[1] * q[1]) + (edge2[2] * q[2])) / det) > 0.0;
    }

    // Turns triangles so that every connected piece faces outwards, pieces sitting inside of other pieces face inwards as hollows.
    // Triangles with bad indices or without area are removed from the indices
    static void orient(const std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices){
        struct Edge { unsigned count; unsigned uses[2]; }; // triangle and corner of each use

        const float epsilon = getWeld(vertices);
        std::unordered_map<Form_Key, unsigned, Form_KeyHash> lookup; // matches vertices by position
        std::vector<unsigned> points(vertices.size());
        for(size_t v = 0; v < vertices.size(); v++){
            Form_Key key = getKey(vertices[v], epsilon, false);
            std::unordered_map<Form_Key, unsigned, Form_KeyHash>::iterator match = lookup.find(key);
            if(match == lookup.end()){
                points[v] = (unsigned)lookup.size();
                lookup.insert(std::make_pair(key, points[v]));
            } else points[v] = match->second;
        }

        std::vector<unsigned> trigs;
        for(size_t i = 0; i + 2 < indices.size(); i += 3){
            unsigned t[3] = { indices[i], indices[i + 1], indices[i + 2] };
            if(t[0] >= vertices.size() || t[1] >= vertices.size() || t[2] >= vertices.size()) continue;
            if(points[t[0]] == points[t[1]] || points[t[1]] == points[t[2]] || points[t[0]] == points[t[2]]) continue;

            bool isFinite = true;
            for(unsigned n = 0; n < 3; n++)
                for(unsigned a = 0; a < 3; a++) if(!std::isfinite(vertices[t[n]].position.data[a])) isFinite = false;
            if(isFinite) trigs.insert(trigs.end(), t, t + 3);
        }
        const unsigned trigCount = (unsigned)(trigs.size() / 3);

        std::unordered_map<unsigned long long, Edge> edges;
        for(unsigned t = 0; t < trigCount; t++)
            for(unsigned n = 0; n < 3; n++){
                unsigned a = points[trigs[(t * 3) + n]], b = points[trigs[(t * 3) + ((n + 1) % 3)]];
                unsigned long long key = (a < b)? (((unsigned long long)a << 32) | b) : (((unsigned long long)b << 32) | a);
                std::unordered_map<unsigned long long, Edge>::iterator match = edges.find(key);
                if(match == edges.end()){
                    Edge edge = { 1, { (t * 3) + n, 0 } };
                    edges.insert(std::make_pair(key, edge));
                } else {
                    if(match->second.count < 2) match->second.uses[match->second.count] = (t * 3) + n;
                    match->second.count++;
                }
            }

        unsigned partCount = 0; // connected pieces
        std::vector<unsigned> parts(trigCount, FORM_NO_INDEX);
        for(unsigned start = 0; start < trigCount; start++){ // spreads a matching direction across shared edges
            if(parts[start] != FORM_NO_INDEX) continue;
            std::vector<unsigned> pending(1, start);
            parts[start] = partCount;

            while(!pending.empty()){
                unsigned t = pending.back();
                pending.pop_back();
                for(unsigned n = 0; n < 3; n++){
                    unsigned a = points[trigs[(t * 3) + n]], b = points[trigs[(t * 3) + ((n + 1) % 3)]];
                    unsigned long long key = (a < b)? (((unsigned long long)a << 32) | b) : (((unsigned long long)b << 32) | a);
                    const Edge& edge = edges[key];
                    if(edge.count != 2) continue; // open or shared by more than two triangles

                    unsigned use = (edge.uses[0] / 3 == t)? edge.uses[1] : edge.uses[0];
                    unsigned other = use / 3;
                    if(other == t || parts[other] != FORM_NO_INDEX) continue;

                    if(points[trigs[use]] == a) std::swap(trigs[(other * 3) + 1], trigs[(other * 3) + 2]); // runs along the edge the same way
                    parts[other] = partCount;
                    pending.push_back(other);
                }
            }
            partCount++;
        }

        std::vector<double> volumes(partCount, 0.0);
        std::vector<unsigned> firsts(partCount, FORM_NO_INDEX);
        for(unsigned t = 0; t < trigCount; t++){
            if(firsts[parts[t]] == FORM_NO_INDEX) firsts[parts[t]] = t;
            const Vec3f& origin = vertices[trigs[firsts[parts[t]] * 3]].position; // measured from the piece itself
            Vec3f a = vertices[trigs[t * 3]].position - origin;
            Vec3f b = vertices[trigs[(t * 3) + 1]].position - origin;
            Vec3f c = vertices[trigs[(t * 3) + 2]].position - origin;
            volumes[parts[t]] += dot(a, cross(b, c));
        }

        std::vector<bool> isTurned(partCount, false);
        for(unsigned p = 0; p < partCount; p++) isTurned[p] = volumes[p] < 0.0;

        if(partCount > 1 && (double)partCount * (double)trigCount < 400000000.0){ // counts the pieces around each piece
            const double direction[3] = { 0.37139, 0.55708, 0.74278 }; // unlikely to line up with edges
            for(unsigned p = 0; p < partCount; p++){
                double origin[3] = { 0.0, 0.0, 0.0 };
                for(unsigned n = 0; n < 3; n++)
                    for(unsigned a = 0; a < 3; a++) origin[a] += vertices[trigs[(firsts[p] * 3) + n]].position.data[a] / 3.0;

                std::vector<unsigned> crossings(partCount, 0);
                for(unsigned t = 0; t < trigCount; t++)
                    if(parts[t] != p && isRayCrossing(origin, direction, vertices[trigs[t * 3]].position, vertices[trigs[(t * 3) + 1]].position, vertices[trigs[(t * 3) + 2]].position))
                        crossings[parts[t]]++;

                unsigned depth = 0;
                for(unsigned other = 0; other < partCount; other++) if(crossings[other] % 2 == 1) depth++;
                if(depth % 2 == 1) isTurned[p] = !isTurned[p];
            }
        }

        for(unsigned t = 0; t < trigCount; t++)
            if(isTurned[parts[t]]) std::swap(trigs[(t * 3) + 1], trigs[(t * 3) + 2]);
        indices.swap(trigs);
    }

    // Box around every filled cell, faces between two filled cells are left out and vertices are shared across each flat side
    static void genCells(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices){
        static const int sides[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
        static const unsigned char corners[6][4] = { // counter clockwise from outside, see FORM_CORNER
            { 0, 4, 6, 2 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 2, 3, 1 }, { 4, 5, 7, 6 }
        };
        if(cells.size() < lattice.getCount()) return;

        const unsigned long long rowSize = lattice.dims[0] + 1, sliceSize = rowSize * (lattice.dims[1] + 1);
        std::unordered_map<unsigned long long, unsigned> lookup; // corner and side to vertex
        for(unsigned z = 0; z < lattice.dims[2]; z++)
            for(unsigned y = 0; y < lattice.dims[1]; y++)
                for(unsigned x = 0; x < lattice.dims[0]; x++){
                    if(!cells[lattice.getIndex(x, y, z)]) continue;
                    unsigned coords[3] = { x, y, z };

                    for(unsigned s = 0; s < 6; s++){
                        bool isCovered = true; // by an adjacent filled cell
                        unsigned nebr[3];
                        for(unsigned a = 0; a < 3; a++){
                            long long target = (long long)coords[a] + sides[s][a];
                            if(target < 0 || target >= (long long)lattice.dims[a]) isCovered = false;
                            nebr[a] = (unsigned)target;
                        }
                        if(isCovered && cells[lattice.getIndex(nebr[0], nebr[1], nebr[2])]) continue;

                        unsigned quad[4];
                        for(unsigned c = 0; c < 4; c++){
                            unsigned cx = x + (corners[s][c] & 1), cy = y + ((corners[s][c] >> 1) & 1), cz = z + ((corners[s][c] >> 2) & 1);
                            unsigned long long key = ((cx + (cy * rowSize) + (cz * sliceSize)) * 6) + s;
                            std::unordered_map<unsigned long long, unsigned>::iterator match = lookup.find(key);
                            if(match == lookup.end()){
                                Vec3f position = Vec3f({
                                    lattice.origin.data[0] + ((cx - 0.5F) * lattice.spacing.data[0]),
                                    lattice.origin.data[1] + ((cy - 0.5F) * lattice.spacing.data[1]),
                                    lattice.origin.data[2] + ((cz - 0.5F) * lattice.spacing.data[2])
                                });
                                quad[c] = (unsigned)vertices.size();
                                lookup.insert(std::make_pair(key, quad[c]));
                                vertices.push_back(getVertex(position, Vec3f({ (float)sides[s][0], (float)sides[s][1], (float)sides[s][2] })));
                            } else quad[c] = match->second;
                        }

                        unsigned triangles[6] = { quad[0], quad[1], quad[2], quad[0], quad[2], quad[3] };
                        indices.insert(indices.end(), triangles, triangles + 6);
                    }
                }
    }

    // Copies for the Geo_Mesh constructors that take ownership of vertex and index data

    static Geo_Vertex* copyVertices(const std::vector<Geo_Vertex>& vertices){
        if(vertices.empty()) return nullptr;
        Geo_Vertex* vertexData = (Geo_Vertex*)malloc(vertices.size() * sizeof(Geo_Vertex));
        for(size_t v = 0; v < vertices.size(); v++) *(vertexData + v) = vertices[v];
        return vertexData;
    }
    static unsigned* copyIndices(const std::vector<unsigned>& indices){
        if(indices.empty()) return nullptr;
        unsigned* indexData = (unsigned*)malloc(indices.size() * sizeof(unsigned));
        for(size_t i = 0; i < indices.size(); i++) *(indexData + i) = indices[i];
        return indexData;
    }

#ifdef GEO_ENABLE_OPENVDB
    // Volume Helpers

    openvdb::FloatGrid::Ptr toVolume(float voxelSize) const { return toVolume(*this, voxelSize, 3.0F); }

    static openvdb::FloatGrid::Ptr toVolume(const Geo_Mesh& mesh, float voxelSize, float halfWidth){ // level set of a closed triangle mesh
        openvdb::initialize(); // has no effect after the first call

        std::vector<openvdb::Vec3s> points;
        std::vector<openvdb::Vec3I> triangles;
        const unsigned vCount = (unsigned)mesh.getVertexCount();
        const unsigned iCount = (unsigned)((mesh.getIndexCount() > 0)? mesh.getIndexCount() : vCount);
        for(unsigned v = 0; v < vCount; v++){
            const Vec3f& position = (mesh.getVertices() + v)->position;
            points.push_back(openvdb::Vec3s(position.data[0], position.data[1], position.data[2]));
        }
        for(unsigned i = 0; i + 2 < iCount; i += 3){
            unsigned t[3];
            for(unsigned n = 0; n < 3; n++) t[n] = (mesh.getIndexCount() > 0)? *(mesh.getIndices() + i + n) : i + n;
            if(t[0] < vCount && t[1] < vCount && t[2] < vCount && t[0] != t[1] && t[1] != t[2] && t[0] != t[2])
                triangles.push_back(openvdb::Vec3I(t[0], t[1], t[2]));
        }

        openvdb::math::Transform::Ptr transform = openvdb::math::Transform::createLinearTransform((voxelSize > 0.0F)? voxelSize : 0.05);
        return openvdb::tools::meshToLevelSet<openvdb::FloatGrid>(*transform, points, triangles, halfWidth);
    }

    static void fromVolume(const openvdb::FloatGrid& grid, double isovalue, double adaptivity, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices){
        std::vector<openvdb::Vec3s> points;
        std::vector<openvdb::Vec3I> triangles;
        std::vector<openvdb::Vec4I> quads;
        openvdb::tools::volumeToMesh(grid, points, triangles, quads, isovalue, adaptivity);

        const unsigned base = (unsigned)vertices.size();
        const size_t start = indices.size();
        for(size_t p = 0; p < points.size(); p++)
            vertices.push_back(getVertex(Vec3f({ points[p][0], points[p][1], points[p][2] }), VEC_3F_ZERO));
        for(size_t t = 0; t < triangles.size(); t++)
            for(unsigned n = 0; n < 3; n++) indices.push_back(base + triangles[t][2 - n]);
        for(size_t q = 0; q < quads.size(); q++){
            unsigned quad[6] = { base + quads[q][2], base + quads[q][1], base + quads[q][0], base + quads[q][3], base + quads[q][2], base + quads[q][0] };
            indices.insert(indices.end(), quad, quad + 6);
        }

        std::vector<unsigned> added(indices.begin() + start, indices.end());
        if(calcVolume(vertices, added) < 0.0) // keeps triangles counter clockwise from outside whichever way the grid was meshed
            for(size_t i = start; i + 2 < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
    }

    static bool isInside(const openvdb::FloatGrid& grid, const openvdb::FloatGrid::ConstAccessor& accessor, const openvdb::Coord& coord){
        return (grid.getGridClass() == openvdb::GRID_LEVEL_SET)? accessor.getValue(coord) < 0.0F : accessor.isValueOn(coord);
    }

    // Lattice with a point for each voxel inside the active bounds of a grid, entries are nonzero when inside of the volume
    static Form_Lattice getVoxels(const openvdb::FloatGrid& grid, std::vector<unsigned char>& voxels){
        Form_Lattice lattice;
        lattice.origin = VEC_3F_ZERO;
        lattice.spacing = VEC_3F_ZERO;
        for(unsigned a = 0; a < 3; a++) lattice.dims[a] = 0;
        voxels.clear();

        openvdb::CoordBBox bounds = grid.evalActiveVoxelBoundingBox();
        if(bounds.empty()) return lattice;

        openvdb::Vec3d origin = grid.indexToWorld((bounds.min)());
        openvdb::Vec3d spacing = grid.voxelSize();
        openvdb::Coord dims = bounds.dim();
        for(unsigned a = 0; a < 3; a++){
            lattice.origin.data[a] = (float)origin[a];
            lattice.spacing.data[a] = (float)spacing[a];
            lattice.dims[a] = (unsigned)dims[a];
        }

        openvdb::FloatGrid::ConstAccessor accessor = grid.getConstAccessor();
        voxels.resize(lattice.getCount(), 0);
        for(unsigned z = 0; z < lattice.dims[2]; z++)
            for(unsigned y = 0; y < lattice.dims[1]; y++)
                for(unsigned x = 0; x < lattice.dims[0]; x++)
                    if(isInside(grid, accessor, (bounds.min)().offsetBy((int)x, (int)y, (int)z))) voxels[lattice.getIndex(x, y, z)] = 1;
        return lattice;
    }
#endif
protected:
    // Takes over generated geometry, vertices without indices are numbered in order as Geo_Mesh::populate() does
    void assign(std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices, DRAW_Mode mode){
        _vertices.swap(vertices);
        _indices.swap(indices);
        if(_indices.empty()) for(unsigned v = 0; v < _vertices.size(); v++) _indices.push_back(v);
        drawMode = mode;
    }

    Form _shape = { 1.0F, 1, 1, 1 };
};

// Freeform
//
//      Lattice of points that are culled, each point kept spawns a meshlet from itself or from adjacent points

class Geo_Freeform : public Geo_Form {
public:
    DllExport Geo_Freeform(Form shape); // lines between adjacent points
    DllExport Geo_Freeform(Form shape, fSpawnCallback spawnCallback);
    DllExport Geo_Freeform(Form shape, fSpawnCallback spawnCallback, fCullCallback cullCallback);
    DllExport Geo_Freeform(Form shape, fAlgoCallback algoCallback, fCullCallback cullCallback); // draw mode comes from the meshlets
#ifdef GEO_ENABLE_OPENVDB
    DllExport Geo_Freeform(const openvdb::FloatGrid& grid, fAlgoCallback algoCallback); // points are the voxels inside of grid
#endif
};

// Voxelform
//
//      Grid of cells that are culled, each cell kept gets a primitive or geometry generated from its surroundings

class Geo_Voxelform : public Geo_Form {
public:
    DllExport Geo_Voxelform(Form shape); // every cell as a cube
    DllExport Geo_Voxelform(Form shape, fCullCallback cullCallback);
    DllExport Geo_Voxelform(Form shape, fCullCallback cullCallback, VOXEL_Cell cell); // primitive per cell
    DllExport Geo_Voxelform(Form shape, fCullCallback cullCallback, const Geo_Mesh* cellMesh); // mesh fitted into each cell
    DllExport Geo_Voxelform(Form shape, fCullCallback cullCallback, fCellCallback cellCallback); // dynamic geometry per cell
#ifdef GEO_ENABLE_OPENVDB
    DllExport Geo_Voxelform(const openvdb::FloatGrid& grid, VOXEL_Cell cell); // cells are the voxels inside of grid
    DllExport Geo_Voxelform(const openvdb::FloatGrid& grid, fCellCallback cellCallback);
#endif
};

// Composite
//
//      Constructive solid geometry, combines closed meshes from first to last

class Geo_Composite : public Geo_Form {
public:
    DllExport Geo_Composite(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2, CSG_Op op);
    DllExport Geo_Composite(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2, CSG_Op op, CSG_Method method);
    DllExport Geo_Composite(std::initializer_list<const Geo_Mesh*> meshes, CSG_Op op);
    DllExport Geo_Composite(std::initializer_list<const Geo_Mesh*> meshes, CSG_Op op, CSG_Method method);

    DllExport bool combine(const Geo_Mesh* mesh, CSG_Op op); // applies another mesh to the result so far

    CSG_Op getOp() const { return _op; }
    bool isValid() const { return _isValid; } // false when a mesh was missing or had no triangles
private:
    void init(const Geo_Mesh* const* meshes, unsigned meshCount);

    CSG_Op _op = CSG_Union;
    CSG_Method _method = CSG_Exact;
    bool _isValid = false;
};

// Recursive
//
//      Self similar shapes, built from cell rules, from growth on triangles, or from scaled copies

class Geo_Recursive : public Geo_Form {
public:
    DllExport Geo_Recursive(RECURSIVE_Type type, float radius, unsigned short levels);
    DllExport Geo_Recursive(float radius, unsigned short divs, unsigned short levels, fRuleCallback ruleCallback); // custom cell rule
    DllExport Geo_Recursive(const Geo_Mesh* mesh, unsigned short levels); // koch growth on each triangle of mesh
    DllExport Geo_Recursive(const Geo_Mesh* mesh, const Form_Tform* tforms, unsigned tformCount, unsigned short levels); // custom copies

    unsigned short getLevels() const { return _levels; }
private:
    unsigned short _levels = 0;
};

#endif

#define GEO_FORMS_H
#endif
