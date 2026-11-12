#include "Geo_Forms.hpp"

// Culling Functions

bool freeformCull_none(Vec3f input, double level) { return true; } // no culling
bool freeformCull_ball(Vec3f input, double level) { return input.len() <= level; }
bool freeformCull_shell(Vec3f input, double level) { return input.len() <= level && input.len() >= level * 0.5; }

// Spawning Functions

Geo_Meshlet freeformSpawn_lines(Vec3f input, unsigned count) {
    Geo_Meshlet lineMeshlet;
    lineMeshlet.count = 2;
    lineMeshlet.positions = (Vec3f*)malloc(sizeof(Vec3f) * 2); // freed by the form
    for (unsigned p = 0; p < 2; p++) *(lineMeshlet.positions + p) = input * ((p == 0)? 1.0F : (1.0F / (p + 1)));
    return lineMeshlet;
}

// Algorithm Functions

static const int formAxes[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

void freeformAlgo_links(const Form_Nebrs* nebrs, Form_Meshlet* meshlet) {
    meshlet->mode = DRAW_Lines;
    for (unsigned a = 0; a < 3; a++) // forward only so each pair of points is linked once
        if (nebrs->has(formAxes[a][0], formAxes[a][1], formAxes[a][2])) {
            meshlet->addVertex(nebrs->points[FORM_TARGET]);
            meshlet->addVertex(nebrs->getPoint(formAxes[a][0], formAxes[a][1], formAxes[a][2]));
        }
}

void freeformAlgo_trigs(const Form_Nebrs* nebrs, Form_Meshlet* meshlet) {
    for (unsigned a = 0; a < 3; a++) { // one triangle per pair of axes
        const int* axis1 = formAxes[a];
        const int* axis2 = formAxes[(a + 1) % 3];
        if (!nebrs->has(axis1[0], axis1[1], axis1[2]) || !nebrs->has(axis2[0], axis2[1], axis2[2])) continue;

        meshlet->addVertex(nebrs->points[FORM_TARGET]);
        meshlet->addVertex(nebrs->getPoint(axis1[0], axis1[1], axis1[2]));
        meshlet->addVertex(nebrs->getPoint(axis2[0], axis2[1], axis2[2]));
    }
}

// Adjacent points form cubes, a square of points is part of the surface when a full cube of points sits on only one side of it
void freeformAlgo_hull(const Form_Nebrs* nebrs, Form_Meshlet* meshlet) {
    static const int corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }; // steps along the two axes of a square

    for (unsigned a = 0; a < 3; a++) {
        const int* axis1 = formAxes[(a + 1) % 3];
        const int* axis2 = formAxes[(a + 2) % 3];
        const int* axis3 = formAxes[a]; // square faces along this axis

        unsigned square[4];
        bool isSquare = true, isAbove = true, isBelow = true; // whether points exist on the square and on the cubes to each side
        for (unsigned c = 0; c < 4; c++) {
            int offset[3];
            for (unsigned d = 0; d < 3; d++) offset[d] = (axis1[d] * corners[c][0]) + (axis2[d] * corners[c][1]);

            square[c] = nebrs->getIndex(offset[0], offset[1], offset[2]);
            if (square[c] == FORM_NO_INDEX) isSquare = false;
            if (!nebrs->has(offset[0] + axis3[0], offset[1] + axis3[1], offset[2] + axis3[2])) isAbove = false;
            if (!nebrs->has(offset[0] - axis3[0], offset[1] - axis3[1], offset[2] - axis3[2])) isBelow = false;
        }
        if (!isSquare || isAbove == isBelow) continue; // inside of the form or not attached to it

        unsigned short winding[6] = { 0, 1, 2, 0, 2, 3 }; // faces along axis, away from the cube below
        if (isAbove) { winding[1] = 2; winding[2] = 1; winding[4] = 3; winding[5] = 2; }
        for (unsigned i = 0; i < 6; i++) meshlet->addIndex(square[winding[i]]);
    }
}

// Generation Functions

static bool isTriangles(DRAW_Mode mode) { return mode == DRAW_Triangles || mode == DRAW_Default; }

static void finishFreeform(DRAW_Mode mode, std::vector<Geo_Vertex>& vertices, const std::vector<unsigned>& indices) {
    if (isTriangles(mode) && !indices.empty()) Geo_Form::calcNormals(vertices, indices);
    else if (isTriangles(mode)) { // unindexed triangles
        std::vector<unsigned> order;
        for (unsigned v = 0; v < vertices.size(); v++) order.push_back(v);
        Geo_Form::calcNormals(vertices, order);
    }
    else for (size_t v = 0; v < vertices.size(); v++) vertices[v].normal = Geo_Form::unit(vertices[v].position);
    Geo_Form::calcTexcoords(vertices, false);
}

// Spawns a meshlet for each point kept, indices refer to the points and vertices get added after them
static DRAW_Mode spawnFreeform(const Form_Lattice& lattice, const std::vector<unsigned char>& points, fAlgoCallback algoCallback, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    DRAW_Mode mode = DRAW_Triangles;
    if (algoCallback == nullptr || points.size() < lattice.getCount()) return mode;

    std::vector<unsigned> lookup(lattice.getCount(), FORM_NO_INDEX); // lattice point to vertex
    std::vector<Vec3f> positions;
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++)
                if (points[lattice.getIndex(x, y, z)]) {
                    lookup[lattice.getIndex(x, y, z)] = (unsigned)positions.size();
                    positions.push_back(lattice.getPoint(x, y, z));
                }

    std::vector<unsigned> pointIndices; // spawned as indices
    std::vector<Vec3f> spawnPositions; // spawned as vertices
    bool hasMode = false;

    Form_Nebrs nebrs;
    nebrs.count = (unsigned)positions.size();
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++) {
                if (!points[lattice.getIndex(x, y, z)]) continue;

                nebrs.coords[0] = x; nebrs.coords[1] = y; nebrs.coords[2] = z;
                for (int oz = -1; oz <= 1; oz++)
                    for (int oy = -1; oy <= 1; oy++)
                        for (int ox = -1; ox <= 1; ox++) {
                            long long nx = (long long)x + ox, ny = (long long)y + oy, nz = (long long)z + oz;
                            bool isInside = nx >= 0 && ny >= 0 && nz >= 0 && nx < lattice.dims[0] && ny < lattice.dims[1] && nz < lattice.dims[2];
                            unsigned index = (isInside)? lookup[lattice.getIndex((unsigned)nx, (unsigned)ny, (unsigned)nz)] : FORM_NO_INDEX;

                            nebrs.indices[FORM_NEBR(ox, oy, oz)] = index;
                            nebrs.points[FORM_NEBR(ox, oy, oz)] = (index != FORM_NO_INDEX)? positions[index] : VEC_3F_BAD;
                        }

                Form_Meshlet meshlet;
                meshlet.vertexCount = 0;
                meshlet.indexCount = 0;
                meshlet.mode = DRAW_Triangles;
                algoCallback(&nebrs, &meshlet);

                if (meshlet.vertexCount > FORM_MESHLET_MAX) meshlet.vertexCount = FORM_MESHLET_MAX;
                if (meshlet.indexCount > FORM_MESHLET_MAX) meshlet.indexCount = FORM_MESHLET_MAX;
                if (meshlet.vertexCount == 0 && meshlet.indexCount == 0) continue;

                if (!hasMode) { // first meshlet decides how the form is drawn
                    mode = meshlet.mode;
                    hasMode = true;
                }
                else if (meshlet.mode != mode) continue;

                bool isValid = true;
                for (unsigned i = 0; i < meshlet.indexCount; i++) if (meshlet.indices[i] >= positions.size()) isValid = false;
                if (isValid) pointIndices.insert(pointIndices.end(), meshlet.indices, meshlet.indices + meshlet.indexCount);
                spawnPositions.insert(spawnPositions.end(), meshlet.positions, meshlet.positions + meshlet.vertexCount);
            }

    const unsigned base = (unsigned)vertices.size();
    const size_t start = indices.size();
    if (!pointIndices.empty()) { // only points that are referenced become vertices
        std::vector<unsigned> remap(positions.size(), FORM_NO_INDEX);
        for (size_t i = 0; i < pointIndices.size(); i++) {
            if (remap[pointIndices[i]] == FORM_NO_INDEX) {
                remap[pointIndices[i]] = (unsigned)vertices.size();
                vertices.push_back(Geo_Form::getVertex(positions[pointIndices[i]], VEC_3F_ZERO));
            }
            indices.push_back(remap[pointIndices[i]]);
        }
    }
    for (size_t p = 0; p < spawnPositions.size(); p++) {
        if (!pointIndices.empty() || base > 0) indices.push_back((unsigned)vertices.size());
        vertices.push_back(Geo_Form::getVertex(spawnPositions[p], VEC_3F_ZERO));
    }

    if (base == 0) finishFreeform(mode, vertices, indices);
    else { // appended to existing geometry, which keeps its own normals
        std::vector<Geo_Vertex> added(vertices.begin() + base, vertices.end());
        std::vector<unsigned> addedIndices;
        for (size_t i = start; i < indices.size(); i++) addedIndices.push_back(indices[i] - base);
        finishFreeform(mode, added, addedIndices);
        std::copy(added.begin(), added.end(), vertices.begin() + base);
    }
    return mode;
}

DRAW_Mode genFreeform_mesh(const Form_Lattice& lattice, fCullCallback cullCallback, fAlgoCallback algoCallback, double level, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    std::vector<unsigned char> points(lattice.getCount(), 0);
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++)
                if (cullCallback == nullptr || cullCallback(lattice.getPoint(x, y, z), level)) points[lattice.getIndex(x, y, z)] = 1;

    return spawnFreeform(lattice, points, algoCallback, vertices, indices);
}

// Spawns positions from each point alone, kept for callbacks written before adjacent points were available
static void genFreeform_spawn(const Form_Lattice& lattice, fCullCallback cullCallback, fSpawnCallback spawnCallback, double level, std::vector<Geo_Vertex>& vertices) {
    std::vector<Vec3f> positions;
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++)
                if (cullCallback == nullptr || cullCallback(lattice.getPoint(x, y, z), level)) positions.push_back(lattice.getPoint(x, y, z));
    if (spawnCallback == nullptr) return;

    for (size_t p = 0; p < positions.size(); p++) {
        Geo_Meshlet meshlet = spawnCallback(positions[p], (unsigned)positions.size());
        if (meshlet.positions == nullptr) continue;
        for (unsigned c = 0; c < meshlet.count; c++) vertices.push_back(Geo_Form::getVertex(*(meshlet.positions + c), VEC_3F_ZERO));
        free(meshlet.positions); // make sure to deallocate memory
    }

    for (size_t v = 0; v < vertices.size(); v++) vertices[v].normal = Geo_Form::unit(vertices[v].position);
    Geo_Form::calcTexcoords(vertices, false);
}

// Constructors

Geo_Freeform::Geo_Freeform(Form shape) : Geo_Form(shape) {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    DRAW_Mode mode = genFreeform_mesh(getPoints(shape), freeformCull_none, freeformAlgo_links, shape.radius, vertices, indices);
    assign(vertices, indices, mode);
}

Geo_Freeform::Geo_Freeform(Form shape, fSpawnCallback spawnCallback) : Geo_Form(shape) {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genFreeform_spawn(getPoints(shape), freeformCull_none, spawnCallback, shape.radius, vertices);
    assign(vertices, indices, DRAW_Default);
}

Geo_Freeform::Geo_Freeform(Form shape, fSpawnCallback spawnCallback, fCullCallback cullCallback) : Geo_Form(shape) {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genFreeform_spawn(getPoints(shape), cullCallback, spawnCallback, shape.radius, vertices);
    assign(vertices, indices, DRAW_Default);
}

Geo_Freeform::Geo_Freeform(Form shape, fAlgoCallback algoCallback, fCullCallback cullCallback) : Geo_Form(shape) {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    DRAW_Mode mode = genFreeform_mesh(getPoints(shape), cullCallback, algoCallback, shape.radius, vertices, indices);
    assign(vertices, indices, mode);
}

#ifdef GEO_ENABLE_OPENVDB
Geo_Freeform::Geo_Freeform(const openvdb::FloatGrid& grid, fAlgoCallback algoCallback) : Geo_Form() {
    std::vector<unsigned char> voxels;
    Form_Lattice lattice = getVoxels(grid, voxels);

    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    DRAW_Mode mode = spawnFreeform(lattice, voxels, algoCallback, vertices, indices);
    assign(vertices, indices, mode);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, lattice.dims[0], lattice.dims[1], lattice.dims[2] };
}
#endif
