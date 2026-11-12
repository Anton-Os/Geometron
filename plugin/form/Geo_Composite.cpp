#include <algorithm>

#include "Geo_Forms.hpp"

// Polygon Types

struct Csg_Vertex {
    double position[3];
    float texcoord[3];
    float color[3];
};

struct Csg_Plane {
    double normal[3];
    double w; // distance from origin along normal
};

struct Csg_Polygon { // flat and convex, counter clockwise when seen from the side its plane faces
    std::vector<Csg_Vertex> vertices;
    Csg_Plane plane;
};

enum CSG_Side { CSG_Coplanar = 0, CSG_Front = 1, CSG_Back = 2, CSG_Spanning = 3 };

static double dot3(const double* a, const double* b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }

static void cross3(const double* a, const double* b, double* result) {
    result[0] = (a[1] * b[2]) - (a[2] * b[1]);
    result[1] = (a[2] * b[0]) - (a[0] * b[2]);
    result[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

static Csg_Vertex lerpVertex(const Csg_Vertex& v1, const Csg_Vertex& v2, double t) {
    Csg_Vertex vertex;
    for (unsigned a = 0; a < 3; a++) {
        vertex.position[a] = v1.position[a] + ((v2.position[a] - v1.position[a]) * t);
        vertex.texcoord[a] = v1.texcoord[a] + ((v2.texcoord[a] - v1.texcoord[a]) * (float)t);
        vertex.color[a] = v1.color[a] + ((v2.color[a] - v1.color[a]) * (float)t);
    }
    return vertex;
}

static void flipPolygon(Csg_Polygon& polygon) {
    std::reverse(polygon.vertices.begin(), polygon.vertices.end());
    for (unsigned a = 0; a < 3; a++) polygon.plane.normal[a] = -polygon.plane.normal[a];
    polygon.plane.w = -polygon.plane.w;
}

// Sorts a polygon by the side of the plane it is on, a polygon crossing the plane is cut into a piece for each side
static void splitPolygon(const Csg_Plane& plane, const Csg_Polygon& polygon, std::vector<Csg_Polygon>& coplanarFront, std::vector<Csg_Polygon>& coplanarBack, std::vector<Csg_Polygon>& front, std::vector<Csg_Polygon>& back, double epsilon) {
    unsigned polygonSide = CSG_Coplanar;
    std::vector<unsigned char> sides(polygon.vertices.size());
    for (size_t v = 0; v < polygon.vertices.size(); v++) {
        double distance = dot3(plane.normal, polygon.vertices[v].position) - plane.w;
        sides[v] = (distance < -epsilon)? CSG_Back : (distance > epsilon)? CSG_Front : CSG_Coplanar;
        polygonSide |= sides[v];
    }

    switch (polygonSide) {
        case CSG_Coplanar: ((dot3(plane.normal, polygon.plane.normal) > 0.0)? coplanarFront : coplanarBack).push_back(polygon); break;
        case CSG_Front: front.push_back(polygon); break;
        case CSG_Back: back.push_back(polygon); break;
        default: {
            Csg_Polygon frontPiece, backPiece;
            frontPiece.plane = polygon.plane;
            backPiece.plane = polygon.plane;
            for (size_t v = 0; v < polygon.vertices.size(); v++) {
                size_t next = (v + 1) % polygon.vertices.size();
                const Csg_Vertex& v1 = polygon.vertices[v];
                const Csg_Vertex& v2 = polygon.vertices[next];

                if (sides[v] != CSG_Back) frontPiece.vertices.push_back(v1);
                if (sides[v] != CSG_Front) backPiece.vertices.push_back(v1);
                if ((sides[v] | sides[next]) == CSG_Spanning) { // edge crosses plane
                    double edge[3] = { v2.position[0] - v1.position[0], v2.position[1] - v1.position[1], v2.position[2] - v1.position[2] };
                    double t = (plane.w - dot3(plane.normal, v1.position)) / dot3(plane.normal, edge);
                    Csg_Vertex vertex = lerpVertex(v1, v2, t);
                    frontPiece.vertices.push_back(vertex);
                    backPiece.vertices.push_back(vertex);
                }
            }
            if (frontPiece.vertices.size() >= 3) front.push_back(frontPiece);
            if (backPiece.vertices.size() >= 3) back.push_back(backPiece);
        }
    }
}

// Tree of Planes
//
//      Each node holds the polygons on its plane, with the space in front of and behind the plane under child nodes.
//      Loops stand in for recursion since trees of round meshes get as deep as their polygon count

struct Csg_Node {
    Csg_Plane plane;
    bool hasPlane = false;
    int front = -1, back = -1; // child nodes
    std::vector<Csg_Polygon> polygons;
};

class Csg_Tree {
public:
    Csg_Tree(double epsilon) { _epsilon = epsilon; }

    void build(std::vector<Csg_Polygon>& polygons) { // adds polygons, list is emptied
        if (polygons.empty()) return;
        if (_nodes.empty()) _nodes.push_back(Csg_Node());

        std::vector<std::pair<int, std::vector<Csg_Polygon>>> pending;
        pending.push_back(std::make_pair(0, std::vector<Csg_Polygon>()));
        pending.back().second.swap(polygons);
        while (!pending.empty()) {
            int n = pending.back().first;
            std::vector<Csg_Polygon> list;
            list.swap(pending.back().second);
            pending.pop_back();
            if (list.empty()) continue;

            if (!_nodes[n].hasPlane) {
                _nodes[n].plane = list[0].plane;
                _nodes[n].hasPlane = true;
            }
            const Csg_Plane plane = _nodes[n].plane;

            std::vector<Csg_Polygon> front, back;
            for (size_t p = 0; p < list.size(); p++) splitPolygon(plane, list[p], _nodes[n].polygons, _nodes[n].polygons, front, back, _epsilon);

            if (!front.empty()) {
                if (_nodes[n].front < 0) {
                    int child = (int)_nodes.size();
                    _nodes.push_back(Csg_Node());
                    _nodes[n].front = child;
                }
                pending.push_back(std::make_pair(_nodes[n].front, std::vector<Csg_Polygon>()));
                pending.back().second.swap(front);
            }
            if (!back.empty()) {
                if (_nodes[n].back < 0) {
                    int child = (int)_nodes.size();
                    _nodes.push_back(Csg_Node());
                    _nodes[n].back = child;
                }
                pending.push_back(std::make_pair(_nodes[n].back, std::vector<Csg_Polygon>()));
                pending.back().second.swap(back);
            }
        }
    }

    void invert() { // swaps solid and empty space
        for (size_t n = 0; n < _nodes.size(); n++) {
            for (size_t p = 0; p < _nodes[n].polygons.size(); p++) flipPolygon(_nodes[n].polygons[p]);
            for (unsigned a = 0; a < 3; a++) _nodes[n].plane.normal[a] = -_nodes[n].plane.normal[a];
            _nodes[n].plane.w = -_nodes[n].plane.w;
            std::swap(_nodes[n].front, _nodes[n].back);
        }
    }

    void clipPolygons(const std::vector<Csg_Polygon>& polygons, std::vector<Csg_Polygon>& result) const { // removes parts inside of this tree
        if (_nodes.empty() || !_nodes[0].hasPlane) {
            result.insert(result.end(), polygons.begin(), polygons.end());
            return;
        }

        std::vector<std::pair<int, std::vector<Csg_Polygon>>> pending;
        pending.push_back(std::make_pair(0, polygons));
        while (!pending.empty()) {
            int n = pending.back().first;
            std::vector<Csg_Polygon> list;
            list.swap(pending.back().second);
            pending.pop_back();

            std::vector<Csg_Polygon> front, back;
            for (size_t p = 0; p < list.size(); p++) splitPolygon(_nodes[n].plane, list[p], front, back, front, back, _epsilon);

            if (_nodes[n].front < 0) result.insert(result.end(), front.begin(), front.end());
            else if (!front.empty()) {
                pending.push_back(std::make_pair(_nodes[n].front, std::vector<Csg_Polygon>()));
                pending.back().second.swap(front);
            }
            if (_nodes[n].back >= 0 && !back.empty()) { // polygons behind a plane without a back node are inside
                pending.push_back(std::make_pair(_nodes[n].back, std::vector<Csg_Polygon>()));
                pending.back().second.swap(back);
            }
        }
    }

    void clipTo(const Csg_Tree& tree) { // removes parts of this tree that are inside of another
        for (size_t n = 0; n < _nodes.size(); n++) {
            std::vector<Csg_Polygon> clipped;
            tree.clipPolygons(_nodes[n].polygons, clipped);
            _nodes[n].polygons.swap(clipped);
        }
    }

    void getPolygons(std::vector<Csg_Polygon>& polygons) const {
        for (size_t n = 0; n < _nodes.size(); n++) polygons.insert(polygons.end(), _nodes[n].polygons.begin(), _nodes[n].polygons.end());
    }
private:
    std::vector<Csg_Node> _nodes;
    double _epsilon;
};

// Combines two sets of polygons that each enclose a solid, both lists are emptied
static void combinePolygons(std::vector<Csg_Polygon>& polygons1, std::vector<Csg_Polygon>& polygons2, CSG_Op op, double epsilon, std::vector<Csg_Polygon>& result) {
    if (polygons1.empty() || polygons2.empty()) { // nothing to cut against
        if (op == CSG_Union || op == CSG_Xor) result.swap((polygons1.empty())? polygons2 : polygons1);
        else if (op == CSG_Difference) result.swap(polygons1);
        return;
    }

    if (op == CSG_Xor) { // space of each solid outside of the other
        std::vector<Csg_Polygon> copy1 = polygons1, copy2 = polygons2, other;
        combinePolygons(polygons1, polygons2, CSG_Difference, epsilon, result);
        combinePolygons(copy2, copy1, CSG_Difference, epsilon, other);
        result.insert(result.end(), other.begin(), other.end());
        return;
    }

    Csg_Tree tree1(epsilon), tree2(epsilon);
    tree1.build(polygons1);
    tree2.build(polygons2);
    std::vector<Csg_Polygon> remaining;

    switch (op) {
        case CSG_Difference:
            tree1.invert();
            tree1.clipTo(tree2);
            tree2.clipTo(tree1);
            tree2.invert();
            tree2.clipTo(tree1);
            tree2.invert();
            tree2.getPolygons(remaining);
            tree1.build(remaining);
            tree1.invert();
            break;
        case CSG_Intersect:
            tree1.invert();
            tree2.clipTo(tree1);
            tree2.invert();
            tree1.clipTo(tree2);
            tree2.clipTo(tree1);
            tree2.getPolygons(remaining);
            tree1.build(remaining);
            tree1.invert();
            break;
        default: // union
            tree1.clipTo(tree2);
            tree2.clipTo(tree1);
            tree2.invert();
            tree2.clipTo(tree1);
            tree2.invert();
            tree2.getPolygons(remaining);
            tree1.build(remaining);
            break;
    }
    tree1.getPolygons(result);
}

// Mesh Conversion

static void meshToPolygons(const Geo_Mesh* mesh, std::vector<Csg_Polygon>& polygons) {
    if (mesh == nullptr || mesh->getVertexCount() == 0) return;

    const unsigned vCount = (unsigned)mesh->getVertexCount();
    std::vector<Geo_Vertex> vertices(mesh->getVertices(), mesh->getVertices() + vCount);
    std::vector<unsigned> indices;
    if (mesh->getIndexCount() > 0) indices.assign(mesh->getIndices(), mesh->getIndices() + mesh->getIndexCount());
    else for (unsigned v = 0; v < vCount; v++) indices.push_back(v);

    Geo_Form::orient(vertices, indices); // core meshes mix both windings between their faces, which the plane tree cannot work with

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        Csg_Polygon polygon;
        for (unsigned n = 0; n < 3; n++) {
            const Geo_Vertex& source = vertices[indices[i + n]];
            Csg_Vertex vertex;
            for (unsigned a = 0; a < 3; a++) {
                vertex.position[a] = source.position.data[a];
                vertex.texcoord[a] = source.texcoord.data[a];
                vertex.color[a] = source.color.data[a];
            }
            polygon.vertices.push_back(vertex);
        }

        double edge1[3], edge2[3], normal[3];
        for (unsigned a = 0; a < 3; a++) {
            edge1[a] = polygon.vertices[1].position[a] - polygon.vertices[0].position[a];
            edge2[a] = polygon.vertices[2].position[a] - polygon.vertices[0].position[a];
        }
        cross3(edge1, edge2, normal);
        double length = sqrt(dot3(normal, normal));
        if (!(length > 0.0)) continue; // no area

        for (unsigned a = 0; a < 3; a++) polygon.plane.normal[a] = normal[a] / length;
        polygon.plane.w = dot3(polygon.plane.normal, polygon.vertices[0].position);
        polygons.push_back(polygon);
    }
}

static void polygonsToMesh(const std::vector<Csg_Polygon>& polygons, float epsilon, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    for (size_t p = 0; p < polygons.size(); p++) {
        const Csg_Polygon& polygon = polygons[p];
        Vec3f normal = Vec3f({ (float)polygon.plane.normal[0], (float)polygon.plane.normal[1], (float)polygon.plane.normal[2] });

        const unsigned base = (unsigned)vertices.size();
        for (size_t v = 0; v < polygon.vertices.size(); v++) {
            const Csg_Vertex& source = polygon.vertices[v];
            vertices.push_back(Geo_Vertex(
                Vec3f({ (float)source.position[0], (float)source.position[1], (float)source.position[2] }),
                Vec3f({ source.texcoord[0], source.texcoord[1], source.texcoord[2] }),
                normal,
                Vec3f({ source.color[0], source.color[1], source.color[2] })
            ));
        }
        for (unsigned v = 1; v + 1 < polygon.vertices.size(); v++) { // fan across the polygon
            indices.push_back(base);
            indices.push_back(base + v);
            indices.push_back(base + v + 1);
        }
    }
    Geo_Form::weld(vertices, indices, epsilon, true); // keeps a normal per face so edges stay sharp
}

// Generation Functions

#ifdef GEO_ENABLE_OPENVDB
static bool genComposite_volume(const std::vector<const Geo_Mesh*>& meshes, CSG_Op op, float size, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    const float voxelSize = size / CSG_VOLUME_DIVS;

    openvdb::FloatGrid::Ptr result = Geo_Form::toVolume(*meshes[0], voxelSize, 3.0F);
    for (size_t m = 1; m < meshes.size(); m++) {
        openvdb::FloatGrid::Ptr grid = Geo_Form::toVolume(*meshes[m], voxelSize, 3.0F); // combining leaves this grid empty
        switch (op) {
            case CSG_Difference: openvdb::tools::csgDifference(*result, *grid); break;
            case CSG_Intersect: openvdb::tools::csgIntersection(*result, *grid); break;
            case CSG_Xor: {
                openvdb::FloatGrid::Ptr resultCopy = result->deepCopy();
                openvdb::FloatGrid::Ptr gridCopy = grid->deepCopy();
                openvdb::tools::csgDifference(*result, *grid);
                openvdb::tools::csgDifference(*gridCopy, *resultCopy);
                openvdb::tools::csgUnion(*result, *gridCopy);
                break;
            }
            default: openvdb::tools::csgUnion(*result, *grid); break;
        }
    }

    const size_t base = vertices.size();
    Geo_Form::fromVolume(*result, 0.0, 0.0, vertices, indices);
    if (base == 0) {
        Geo_Form::calcNormals(vertices, indices);
        Geo_Form::calcTexcoords(vertices, false);
    }
    return true;
}
#endif

// Combines meshes in order, each result is combined with the mesh after it. Returns false when fewer than two meshes have triangles
bool genComposite_mesh(const Geo_Mesh* const* meshes, unsigned meshCount, CSG_Op op, CSG_Method method, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    std::vector<const Geo_Mesh*> targets;
    std::vector<Geo_Vertex> bounds; // collects every vertex to measure the meshes together
    for (unsigned m = 0; meshes != nullptr && m < meshCount; m++) {
        const Geo_Mesh* mesh = *(meshes + m);
        if (mesh == nullptr || mesh->getVertexCount() < 3) continue;
        targets.push_back(mesh);
        bounds.insert(bounds.end(), mesh->getVertices(), mesh->getVertices() + mesh->getVertexCount());
    }
    if (targets.empty()) return false;

    Vec3f minBound, maxBound;
    Geo_Form::calcBounds(bounds, &minBound, &maxBound);
    float size = (maxBound - minBound).len();
    if (!(size > 0.0F)) return false;

#ifdef GEO_ENABLE_OPENVDB
    if (method == CSG_Volume && targets.size() > 1) return genComposite_volume(targets, op, size, vertices, indices);
#endif

    const double epsilon = size * 0.00001;
    std::vector<Csg_Polygon> result;
    meshToPolygons(targets[0], result);
    for (size_t m = 1; m < targets.size(); m++) {
        std::vector<Csg_Polygon> polygons, combined;
        meshToPolygons(targets[m], polygons);
        combinePolygons(result, polygons, op, epsilon, combined);
        result.swap(combined);
    }

    polygonsToMesh(result, size * FORM_WELD, vertices, indices);
    return targets.size() > 1;
}

static Geo_Mesh* genComposite_pair(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2, CSG_Op op) {
    const Geo_Mesh* meshes[2] = { mesh1, mesh2 };
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genComposite_mesh(meshes, 2, op, CSG_Exact, vertices, indices);

    if (vertices.empty() || indices.empty()) return new Geo_Mesh();
    return new Geo_Mesh((unsigned)vertices.size(), Geo_Form::copyVertices(vertices), (unsigned)indices.size(), Geo_Form::copyIndices(indices));
}

Geo_Mesh* genComposite_union(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2) { return genComposite_pair(mesh1, mesh2, CSG_Union); }
Geo_Mesh* genComposite_difference(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2) { return genComposite_pair(mesh1, mesh2, CSG_Difference); }
Geo_Mesh* genComposite_intersect(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2) { return genComposite_pair(mesh1, mesh2, CSG_Intersect); }
Geo_Mesh* genComposite_xor(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2) { return genComposite_pair(mesh1, mesh2, CSG_Xor); }

// Constructors

void Geo_Composite::init(const Geo_Mesh* const* meshes, unsigned meshCount) {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    _isValid = genComposite_mesh(meshes, meshCount, _op, _method, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, 1, 1, 1 };
}

Geo_Composite::Geo_Composite(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2, CSG_Op op) : Geo_Form() {
    const Geo_Mesh* meshes[2] = { mesh1, mesh2 };
    _op = op;
    init(meshes, 2);
}

Geo_Composite::Geo_Composite(const Geo_Mesh* mesh1, const Geo_Mesh* mesh2, CSG_Op op, CSG_Method method) : Geo_Form() {
    const Geo_Mesh* meshes[2] = { mesh1, mesh2 };
    _op = op;
    _method = method;
    init(meshes, 2);
}

Geo_Composite::Geo_Composite(std::initializer_list<const Geo_Mesh*> meshes, CSG_Op op) : Geo_Form() {
    std::vector<const Geo_Mesh*> list(meshes.begin(), meshes.end());
    _op = op;
    init(list.data(), (unsigned)list.size());
}

Geo_Composite::Geo_Composite(std::initializer_list<const Geo_Mesh*> meshes, CSG_Op op, CSG_Method method) : Geo_Form() {
    std::vector<const Geo_Mesh*> list(meshes.begin(), meshes.end());
    _op = op;
    _method = method;
    init(list.data(), (unsigned)list.size());
}

bool Geo_Composite::combine(const Geo_Mesh* mesh, CSG_Op op) {
    const Geo_Mesh* meshes[2] = { this, mesh };
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    bool isCombined = genComposite_mesh(meshes, 2, op, _method, vertices, indices);
    if (!isCombined) return false; // keeps the result so far

    _op = op;
    assign(vertices, indices, DRAW_Triangles);
    return true;
}
