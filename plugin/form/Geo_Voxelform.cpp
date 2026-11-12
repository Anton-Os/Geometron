#include "Geo_Forms.hpp"

// Culling Functions

bool voxelCull_none(Vec3f input, double level) { return true; } // no culling
bool voxelCull_ball(Vec3f input, double level) { return input.len() <= level; }
bool voxelCull_tube(Vec3f input, double level) {
    double ring = sqrt((input.data[0] * input.data[0]) + (input.data[1] * input.data[1])) - (level * 0.65); // distance from center line of ring
    return sqrt((ring * ring) + (input.data[2] * input.data[2])) <= level * 0.3;
}

// Cell Functions

static Vec3f lerpCorners(const Form_Voxel* voxel, unsigned c1, unsigned c2) { // where the surface crosses between two corners
    if (c1 > c2) std::swap(c1, c2); // always from the lower corner so that adjacent cells agree
    float range = voxel->values[c2] - voxel->values[c1];
    float t = (range != 0.0F)? (FORM_ISOLEVEL - voxel->values[c1]) / range : 0.5F;
    if (t < 0.0F) t = 0.0F;
    if (t > 1.0F) t = 1.0F;
    return voxel->corners[c1] + ((voxel->corners[c2] - voxel->corners[c1]) * t);
}

static void addSurfaceTrig(Form_Meshlet* meshlet, Vec3f p1, Vec3f p2, Vec3f p3, const Vec3f& outside) { // outside is the direction away from the form
    if (Geo_Form::dot(Geo_Form::cross(p2 - p1, p3 - p1), outside) < 0.0F) std::swap(p2, p3);
    meshlet->addVertex(p1);
    meshlet->addVertex(p2);
    meshlet->addVertex(p3);
}

// Splits the cell into six tetrahedra around the diagonal from first to last corner, each one adds up to two triangles
void voxelCell_surface(const Form_Voxel* voxel, Form_Meshlet* meshlet) {
    static const unsigned char tetras[6][4] = { { 0, 1, 3, 7 }, { 0, 3, 2, 7 }, { 0, 2, 6, 7 }, { 0, 6, 4, 7 }, { 0, 4, 5, 7 }, { 0, 5, 1, 7 } };

    for (unsigned t = 0; t < 6; t++) {
        unsigned inside[4], outside[4];
        unsigned insideCount = 0, outsideCount = 0;
        Vec3f insideMid = VEC_3F_ZERO, outsideMid = VEC_3F_ZERO;
        for (unsigned c = 0; c < 4; c++) {
            unsigned corner = tetras[t][c];
            if (voxel->values[corner] > FORM_ISOLEVEL) {
                inside[insideCount++] = corner;
                insideMid = insideMid + voxel->corners[corner];
            } else {
                outside[outsideCount++] = corner;
                outsideMid = outsideMid + voxel->corners[corner];
            }
        }
        if (insideCount == 0 || outsideCount == 0) continue; // surface does not pass through

        Vec3f direction = (outsideMid * (1.0F / outsideCount)) - (insideMid * (1.0F / insideCount));
        if (insideCount == 1)
            addSurfaceTrig(meshlet, lerpCorners(voxel, inside[0], outside[0]), lerpCorners(voxel, inside[0], outside[1]), lerpCorners(voxel, inside[0], outside[2]), direction);
        else if (outsideCount == 1)
            addSurfaceTrig(meshlet, lerpCorners(voxel, outside[0], inside[0]), lerpCorners(voxel, outside[0], inside[1]), lerpCorners(voxel, outside[0], inside[2]), direction);
        else { // two corners on each side
            Vec3f quad[4] = {
                lerpCorners(voxel, inside[0], outside[0]), lerpCorners(voxel, inside[0], outside[1]),
                lerpCorners(voxel, inside[1], outside[1]), lerpCorners(voxel, inside[1], outside[0])
            };
            addSurfaceTrig(meshlet, quad[0], quad[1], quad[2], direction);
            addSurfaceTrig(meshlet, quad[0], quad[2], quad[3], direction);
        }
    }
}

// Generation Functions

void genVoxelform_cells(const Form_Lattice& lattice, fCullCallback cullCallback, double level, std::vector<unsigned char>& cells) {
    cells.assign(lattice.getCount(), 0);
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++)
                if (cullCallback == nullptr || cullCallback(lattice.getPoint(x, y, z), level)) cells[lattice.getIndex(x, y, z)] = 1;
}

static void genVoxelform_octas(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++) {
                if (!cells[lattice.getIndex(x, y, z)]) continue;
                Vec3f center = lattice.getPoint(x, y, z);

                for (unsigned f = 0; f < 8; f++) { // one face per combination of directions
                    float signs[3] = { (f & 1)? -1.0F : 1.0F, (f & 2)? -1.0F : 1.0F, (f & 4)? -1.0F : 1.0F };
                    Vec3f points[3];
                    for (unsigned a = 0; a < 3; a++) {
                        points[a] = center;
                        points[a].data[a] += signs[a] * lattice.spacing.data[a] * 0.5F;
                    }
                    if (signs[0] * signs[1] * signs[2] < 0.0F) std::swap(points[1], points[2]); // keeps face counter clockwise from outside

                    Vec3f normal = Geo_Form::unit(Geo_Form::cross(points[1] - points[0], points[2] - points[0]));
                    for (unsigned p = 0; p < 3; p++) {
                        indices.push_back((unsigned)vertices.size());
                        vertices.push_back(Geo_Form::getVertex(points[p], normal));
                    }
                }
            }
}

void genVoxelform_mesh(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, VOXEL_Cell cell, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (cells.size() < lattice.getCount()) return;
    const size_t base = vertices.size();

    switch (cell) {
        case VOXEL_Octa: genVoxelform_octas(lattice, cells, vertices, indices); break;
        case VOXEL_Point:
            for (unsigned z = 0; z < lattice.dims[2]; z++)
                for (unsigned y = 0; y < lattice.dims[1]; y++)
                    for (unsigned x = 0; x < lattice.dims[0]; x++)
                        if (cells[lattice.getIndex(x, y, z)]) {
                            indices.push_back((unsigned)vertices.size());
                            vertices.push_back(Geo_Form::getVertex(lattice.getPoint(x, y, z), Geo_Form::unit(lattice.getPoint(x, y, z))));
                        }
            break;
        default: Geo_Form::genCells(lattice, cells, vertices, indices); break;
    }

    if (base == 0) Geo_Form::calcTexcoords(vertices, cell != VOXEL_Point);
}

// Copies of a mesh, scaled evenly so that it fits inside of a cell
void genVoxelform_meshExt(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, const Geo_Mesh* cellMesh, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (cells.size() < lattice.getCount() || cellMesh == nullptr || cellMesh->getVertexCount() == 0) return;

    const unsigned vCount = (unsigned)cellMesh->getVertexCount();
    const unsigned iCount = (unsigned)((cellMesh->getIndexCount() > 0)? cellMesh->getIndexCount() : vCount);
    std::vector<Geo_Vertex> cellVertices(cellMesh->getVertices(), cellMesh->getVertices() + vCount);

    Vec3f minBound, maxBound;
    Geo_Form::calcBounds(cellVertices, &minBound, &maxBound);
    Vec3f middle = (minBound + maxBound) * 0.5F;
    float scale = 0.0F;
    for (unsigned a = 0; a < 3; a++) {
        float extent = maxBound.data[a] - minBound.data[a];
        if (extent > 0.0F && (scale == 0.0F || lattice.spacing.data[a] / extent < scale)) scale = lattice.spacing.data[a] / extent;
    }
    if (scale == 0.0F) scale = 1.0F;

    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++) {
                if (!cells[lattice.getIndex(x, y, z)]) continue;
                Vec3f center = lattice.getPoint(x, y, z);

                const unsigned base = (unsigned)vertices.size();
                for (unsigned v = 0; v < vCount; v++) {
                    Geo_Vertex vertex = cellVertices[v];
                    vertex.position = ((vertex.position - middle) * scale) + center;
                    vertices.push_back(vertex);
                }
                for (unsigned i = 0; i + 2 < iCount; i += 3) {
                    unsigned t[3];
                    for (unsigned n = 0; n < 3; n++) t[n] = (cellMesh->getIndexCount() > 0)? *(cellMesh->getIndices() + i + n) : i + n;
                    if (t[0] < vCount && t[1] < vCount && t[2] < vCount)
                        for (unsigned n = 0; n < 3; n++) indices.push_back(base + t[n]);
                }
            }
}

// Visits filled cells and the empty cells that touch them, triangles returned by the callback are joined into a smooth surface
void genVoxelform_meshDyn(const Form_Lattice& lattice, const std::vector<unsigned char>& cells, fCellCallback cellCallback, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (cells.size() < lattice.getCount() || cellCallback == nullptr) return;

    std::vector<Geo_Vertex> cellVertices;
    std::vector<unsigned> cellIndices;

    Form_Voxel voxel;
    voxel.size = lattice.spacing;
    for (unsigned z = 0; z < lattice.dims[2]; z++)
        for (unsigned y = 0; y < lattice.dims[1]; y++)
            for (unsigned x = 0; x < lattice.dims[0]; x++) {
                voxel.nebrs = 0;
                for (int oz = -1; oz <= 1; oz++)
                    for (int oy = -1; oy <= 1; oy++)
                        for (int ox = -1; ox <= 1; ox++) {
                            long long nx = (long long)x + ox, ny = (long long)y + oy, nz = (long long)z + oz;
                            if (nx < 0 || ny < 0 || nz < 0 || nx >= lattice.dims[0] || ny >= lattice.dims[1] || nz >= lattice.dims[2]) continue;
                            if (cells[lattice.getIndex((unsigned)nx, (unsigned)ny, (unsigned)nz)]) voxel.nebrs |= 1U << FORM_NEBR(ox, oy, oz);
                        }
                if (voxel.nebrs == 0) continue; // nothing nearby

                voxel.coords[0] = x; voxel.coords[1] = y; voxel.coords[2] = z;
                voxel.center = lattice.getPoint(x, y, z);
                voxel.isFilled = voxel.has(0, 0, 0);
                for (unsigned c = 0; c < FORM_CORNERS; c++) {
                    int cx = c & 1, cy = (c >> 1) & 1, cz = (c >> 2) & 1;
                    voxel.corners[c] = Vec3f({ // from whole steps so that adjacent cells get identical corners
                        lattice.origin.data[0] + (((float)(x + cx) - 0.5F) * lattice.spacing.data[0]),
                        lattice.origin.data[1] + (((float)(y + cy) - 0.5F) * lattice.spacing.data[1]),
                        lattice.origin.data[2] + (((float)(z + cz) - 0.5F) * lattice.spacing.data[2])
                    });

                    unsigned filled = 0; // out of the 8 cells that meet at the corner
                    for (int oz = cz - 1; oz <= cz; oz++)
                        for (int oy = cy - 1; oy <= cy; oy++)
                            for (int ox = cx - 1; ox <= cx; ox++)
                                if (voxel.has(ox, oy, oz)) filled++;
                    voxel.values[c] = filled / 8.0F;
                }

                Form_Meshlet meshlet;
                meshlet.vertexCount = 0;
                meshlet.indexCount = 0;
                meshlet.mode = DRAW_Triangles;
                cellCallback(&voxel, &meshlet);
                if (meshlet.vertexCount > FORM_MESHLET_MAX) meshlet.vertexCount = FORM_MESHLET_MAX;

                for (unsigned v = 0; v + 2 < meshlet.vertexCount; v += 3)
                    for (unsigned n = 0; n < 3; n++) {
                        cellIndices.push_back((unsigned)cellVertices.size());
                        cellVertices.push_back(Geo_Form::getVertex(meshlet.positions[v + n], VEC_3F_ZERO));
                    }
            }

    float size = (lattice.spacing.len() > 0.0F)? lattice.spacing.len() : 1.0F;
    Geo_Form::weld(cellVertices, cellIndices, size * FORM_WELD, false);
    Geo_Form::calcNormals(cellVertices, cellIndices);
    Geo_Form::calcTexcoords(cellVertices, false);

    const unsigned base = (unsigned)vertices.size();
    vertices.insert(vertices.end(), cellVertices.begin(), cellVertices.end());
    for (size_t i = 0; i < cellIndices.size(); i++) indices.push_back(base + cellIndices[i]);
}

// Constructors

Geo_Voxelform::Geo_Voxelform(Form shape) : Geo_Form(shape) {
    std::vector<unsigned char> cells;
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_cells(getCells(shape), voxelCull_none, shape.radius, cells);
    genVoxelform_mesh(getCells(shape), cells, VOXEL_Cube, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);
}

Geo_Voxelform::Geo_Voxelform(Form shape, fCullCallback cullCallback) : Geo_Form(shape) {
    std::vector<unsigned char> cells;
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_cells(getCells(shape), cullCallback, shape.radius, cells);
    genVoxelform_mesh(getCells(shape), cells, VOXEL_Cube, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);
}

Geo_Voxelform::Geo_Voxelform(Form shape, fCullCallback cullCallback, VOXEL_Cell cell) : Geo_Form(shape) {
    std::vector<unsigned char> cells;
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_cells(getCells(shape), cullCallback, shape.radius, cells);
    genVoxelform_mesh(getCells(shape), cells, cell, vertices, indices);
    assign(vertices, indices, (cell == VOXEL_Point)? DRAW_Points : DRAW_Triangles);
}

Geo_Voxelform::Geo_Voxelform(Form shape, fCullCallback cullCallback, const Geo_Mesh* cellMesh) : Geo_Form(shape) {
    std::vector<unsigned char> cells;
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_cells(getCells(shape), cullCallback, shape.radius, cells);
    genVoxelform_meshExt(getCells(shape), cells, cellMesh, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);
}

Geo_Voxelform::Geo_Voxelform(Form shape, fCullCallback cullCallback, fCellCallback cellCallback) : Geo_Form(shape) {
    std::vector<unsigned char> cells;
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_cells(getCells(shape), cullCallback, shape.radius, cells);
    genVoxelform_meshDyn(getCells(shape), cells, cellCallback, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);
}

#ifdef GEO_ENABLE_OPENVDB
Geo_Voxelform::Geo_Voxelform(const openvdb::FloatGrid& grid, VOXEL_Cell cell) : Geo_Form() {
    std::vector<unsigned char> cells;
    Form_Lattice lattice = getVoxels(grid, cells);

    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_mesh(lattice, cells, cell, vertices, indices);
    assign(vertices, indices, (cell == VOXEL_Point)? DRAW_Points : DRAW_Triangles);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, lattice.dims[0], lattice.dims[1], lattice.dims[2] };
}

Geo_Voxelform::Geo_Voxelform(const openvdb::FloatGrid& grid, fCellCallback cellCallback) : Geo_Form() {
    std::vector<unsigned char> cells;
    Form_Lattice lattice = getVoxels(grid, cells);

    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genVoxelform_meshDyn(lattice, cells, cellCallback, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, lattice.dims[0], lattice.dims[1], lattice.dims[2] };
}
#endif
