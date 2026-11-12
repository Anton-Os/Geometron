#include "Geo_Forms.hpp"

// Rule Functions

static unsigned countMiddle(unsigned x, unsigned y, unsigned z, unsigned short divs) { // axes along which a cell sits between the first and last division
    unsigned count = 0;
    if (x > 0 && x + 1 < divs) count++;
    if (y > 0 && y + 1 < divs) count++;
    if (z > 0 && z + 1 < divs) count++;
    return count;
}

bool recursiveRule_menger(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level) { return countMiddle(x, y, z, divs) < 2; }
bool recursiveRule_cross(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level) { return countMiddle(x, y, z, divs) >= 2; }
bool recursiveRule_dust(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level) { return countMiddle(x, y, z, divs) == 0; }
bool recursiveRule_mosely(unsigned x, unsigned y, unsigned z, unsigned short divs, unsigned short level) {
    unsigned count = countMiddle(x, y, z, divs);
    return count == 1 || count == 2;
}

// Cell Generation
//
//      A cube is split into divisions along each axis and the rule picks the cells to keep, every level repeats this inside
//      of each cell kept. Menger sponges and similar shapes only differ by rule

void genRecursive_cells(float radius, unsigned short divs, unsigned short levels, fRuleCallback ruleCallback, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (divs < 2) divs = 2;

    unsigned size = 1; // cells along each axis
    unsigned short depth = 0;
    while (depth < levels && size * divs <= RECURSIVE_MAX_CELLS) {
        size *= divs;
        depth++;
    }

    Form_Lattice lattice;
    for (unsigned a = 0; a < 3; a++) {
        lattice.dims[a] = size;
        lattice.spacing.data[a] = (radius * 2.0F) / size;
        lattice.origin.data[a] = -radius + (lattice.spacing.data[a] * 0.5F);
    }

    std::vector<unsigned char> cells(lattice.getCount(), 1);
    if (ruleCallback != nullptr)
        for (unsigned z = 0; z < size; z++)
            for (unsigned y = 0; y < size; y++)
                for (unsigned x = 0; x < size; x++) {
                    unsigned scale = 1; // size in cells of the parent divisions, from smallest to largest
                    for (unsigned short l = 0; l < depth; l++) {
                        if (!ruleCallback((x / scale) % divs, (y / scale) % divs, (z / scale) % divs, divs, (unsigned short)(depth - 1 - l))) {
                            cells[lattice.getIndex(x, y, z)] = 0;
                            break;
                        }
                        scale *= divs;
                    }
                }

    const size_t base = vertices.size();
    Geo_Form::genCells(lattice, cells, vertices, indices);
    if (base == 0) Geo_Form::calcTexcoords(vertices, true);
}

// Koch Generation

struct Koch_Trig { Vec3f points[3]; };

static void addFlatTrig(const Vec3f& p1, const Vec3f& p2, const Vec3f& p3, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    Vec3f normal = Geo_Form::unit(Geo_Form::cross(p2 - p1, p3 - p1));
    if (normal.isZero()) return; // no area

    const Vec3f points[3] = { p1, p2, p3 };
    for (unsigned p = 0; p < 3; p++) {
        indices.push_back((unsigned)vertices.size());
        vertices.push_back(Geo_Form::getVertex(points[p], normal));
    }
}

static void genTetra(float radius, std::vector<Koch_Trig>& trigs) { // corners at radius from the origin
    const float size = radius / sqrtf(3.0F);
    const Vec3f corners[4] = { Vec3f({ size, size, size }), Vec3f({ size, -size, -size }), Vec3f({ -size, size, -size }), Vec3f({ -size, -size, size }) };
    const unsigned char faces[4][3] = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } };

    for (unsigned f = 0; f < 4; f++) {
        Koch_Trig trig = { { corners[faces[f][0]], corners[faces[f][1]], corners[faces[f][2]] } };
        Vec3f normal = Geo_Form::cross(trig.points[1] - trig.points[0], trig.points[2] - trig.points[0]);
        if (Geo_Form::dot(normal, trig.points[0]) < 0.0F) std::swap(trig.points[1], trig.points[2]); // faces away from the origin
        trigs.push_back(trig);
    }
}

// Every level splits each triangle into four and raises a tetrahedron from the middle one, the three dimensional take on the koch curve
void genRecursive_koch(const Geo_Mesh* mesh, float radius, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    std::vector<Koch_Trig> trigs;
    if (mesh == nullptr || mesh->getVertexCount() < 3) genTetra(radius, trigs);
    else {
        std::vector<Geo_Vertex> meshVertices(mesh->getVertices(), mesh->getVertices() + mesh->getVertexCount());
        std::vector<unsigned> meshIndices;
        if (mesh->getIndexCount() > 0) meshIndices.assign(mesh->getIndices(), mesh->getIndices() + mesh->getIndexCount());
        else for (unsigned v = 0; v < meshVertices.size(); v++) meshIndices.push_back(v);

        Geo_Form::orient(meshVertices, meshIndices); // growth follows the way triangles face
        for (size_t i = 0; i + 2 < meshIndices.size(); i += 3) {
            Koch_Trig trig = { { meshVertices[meshIndices[i]].position, meshVertices[meshIndices[i + 1]].position, meshVertices[meshIndices[i + 2]].position } };
            trigs.push_back(trig);
        }
    }
    if (trigs.empty()) return;

    if (levels > RECURSIVE_MAX_LEVELS) levels = RECURSIVE_MAX_LEVELS;
    for (unsigned short l = 0; l < levels; l++) {
        if (trigs.size() * 18 > RECURSIVE_MAX_VERTICES) break; // six triangles replace each one

        std::vector<Koch_Trig> grown;
        grown.reserve(trigs.size() * 6);
        for (size_t t = 0; t < trigs.size(); t++) {
            const Vec3f& a = trigs[t].points[0];
            const Vec3f& b = trigs[t].points[1];
            const Vec3f& c = trigs[t].points[2];
            Vec3f ab = (a + b) * 0.5F, bc = (b + c) * 0.5F, ca = (c + a) * 0.5F;

            float edge = ((ab - bc).len() + (bc - ca).len() + (ca - ab).len()) / 3.0F;
            Vec3f normal = Geo_Form::unit(Geo_Form::cross(b - a, c - a));
            Vec3f apex = ((ab + bc + ca) * (1.0F / 3.0F)) + (normal * (edge * sqrtf(2.0F / 3.0F))); // height of a tetrahedron with this edge

            Koch_Trig pieces[6] = { { { a, ab, ca } }, { { ab, b, bc } }, { { ca, bc, c } }, { { ab, bc, apex } }, { { bc, ca, apex } }, { { ca, ab, apex } } };
            grown.insert(grown.end(), pieces, pieces + 6);
        }
        trigs.swap(grown);
    }

    const size_t base = vertices.size();
    std::vector<Geo_Vertex> kochVertices;
    std::vector<unsigned> kochIndices;
    for (size_t t = 0; t < trigs.size(); t++) addFlatTrig(trigs[t].points[0], trigs[t].points[1], trigs[t].points[2], kochVertices, kochIndices);
    Geo_Form::weld(kochVertices, kochIndices, Geo_Form::getWeld(kochVertices), true);
    Geo_Form::calcTexcoords(kochVertices, true);

    vertices.insert(vertices.end(), kochVertices.begin(), kochVertices.end());
    for (size_t i = 0; i < kochIndices.size(); i++) indices.push_back((unsigned)base + kochIndices[i]);
}

// Koch snowflake outline across x and y, given depth along z
void genRecursive_kochPrism(float radius, float depth, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    std::vector<Vec2f> outline; // counter clockwise
    std::vector<Vec2f> caps; // triangles that fill the outline, three points each
    for (unsigned p = 0; p < 3; p++) {
        float angle = (MATH_PI * 0.5F) + (p * MATH_PI * 2.0F / 3.0F);
        outline.push_back(Vec2f({ cosf(angle) * radius, sinf(angle) * radius }));
    }
    caps = outline;

    if (levels > RECURSIVE_MAX_LEVELS) levels = RECURSIVE_MAX_LEVELS;
    for (unsigned short l = 0; l < levels; l++) {
        std::vector<Vec2f> grown;
        grown.reserve(outline.size() * 4);
        for (size_t p = 0; p < outline.size(); p++) { // middle third of each edge becomes a peak
            const Vec2f& start = outline[p];
            Vec2f step = (outline[(p + 1) % outline.size()] - start) * (1.0F / 3.0F);
            Vec2f third1 = start + step, third2 = start + (step * 2.0F);
            Vec2f peak = ((third1 + third2) * 0.5F) + (Vec2f({ step.data[1], -step.data[0] }) * (sqrtf(3.0F) * 0.5F)); // outwards is to the right of the edge

            const Vec2f points[4] = { start, third1, peak, third2 };
            grown.insert(grown.end(), points, points + 4);
            const Vec2f cap[3] = { third1, peak, third2 };
            caps.insert(caps.end(), cap, cap + 3);
        }
        outline.swap(grown);
    }

    const size_t base = vertices.size();
    std::vector<Geo_Vertex> prismVertices;
    std::vector<unsigned> prismIndices;
    const float front = depth * 0.5F, back = depth * -0.5F;
    for (size_t c = 0; c + 2 < caps.size(); c += 3) {
        addFlatTrig(Vec3f({ caps[c].data[0], caps[c].data[1], front }), Vec3f({ caps[c + 1].data[0], caps[c + 1].data[1], front }), Vec3f({ caps[c + 2].data[0], caps[c + 2].data[1], front }), prismVertices, prismIndices);
        addFlatTrig(Vec3f({ caps[c].data[0], caps[c].data[1], back }), Vec3f({ caps[c + 2].data[0], caps[c + 2].data[1], back }), Vec3f({ caps[c + 1].data[0], caps[c + 1].data[1], back }), prismVertices, prismIndices);
    }
    for (size_t p = 0; p < outline.size(); p++) { // side for each edge of the outline
        const Vec2f& start = outline[p];
        const Vec2f& end = outline[(p + 1) % outline.size()];
        addFlatTrig(Vec3f({ start.data[0], start.data[1], back }), Vec3f({ end.data[0], end.data[1], back }), Vec3f({ end.data[0], end.data[1], front }), prismVertices, prismIndices);
        addFlatTrig(Vec3f({ start.data[0], start.data[1], back }), Vec3f({ end.data[0], end.data[1], front }), Vec3f({ start.data[0], start.data[1], front }), prismVertices, prismIndices);
    }
    Geo_Form::weld(prismVertices, prismIndices, Geo_Form::getWeld(prismVertices), true);
    Geo_Form::calcTexcoords(prismVertices, true);

    vertices.insert(vertices.end(), prismVertices.begin(), prismVertices.end());
    for (size_t i = 0; i < prismIndices.size(); i++) indices.push_back((unsigned)base + prismIndices[i]);
}

// Copy Generation
//
//      Every level replaces a shape with one copy per transform, each placed relative to the shape it replaces.
//      The mesh is only drawn at the last level

struct Copy_Tform { // rotation with scale as columns, then shift
    double axes[3][3];
    double shift[3];
};

static Vec3f applyTform(const Copy_Tform& tform, const Vec3f& input, bool isShifted) {
    Vec3f result;
    for (unsigned a = 0; a < 3; a++)
        result.data[a] = (float)((tform.axes[0][a] * input.data[0]) + (tform.axes[1][a] * input.data[1]) + (tform.axes[2][a] * input.data[2]) + ((isShifted)? tform.shift[a] : 0.0));
    return result;
}

static Copy_Tform getTform(const Form_Tform& input) {
    Copy_Tform tform;
    for (unsigned c = 0; c < 3; c++) {
        Vec3f axis = VEC_3F_ZERO;
        axis.data[c] = input.scale;
        if (!input.rotation.isZero()) axis = rotateTForm(axis, input.rotation);
        for (unsigned a = 0; a < 3; a++) tform.axes[c][a] = axis.data[a];
    }
    for (unsigned a = 0; a < 3; a++) tform.shift[a] = input.shift.data[a];
    return tform;
}

static Copy_Tform joinTforms(const Copy_Tform& parent, const Copy_Tform& child) { // child is placed inside of parent
    Copy_Tform tform;
    for (unsigned c = 0; c < 3; c++)
        for (unsigned a = 0; a < 3; a++)
            tform.axes[c][a] = (parent.axes[0][a] * child.axes[c][0]) + (parent.axes[1][a] * child.axes[c][1]) + (parent.axes[2][a] * child.axes[c][2]);
    for (unsigned a = 0; a < 3; a++)
        tform.shift[a] = (parent.axes[0][a] * child.shift[0]) + (parent.axes[1][a] * child.shift[1]) + (parent.axes[2][a] * child.shift[2]) + parent.shift[a];
    return tform;
}

void genRecursive_copies(const Geo_Mesh* mesh, const Form_Tform* tforms, unsigned tformCount, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (mesh == nullptr || mesh->getVertexCount() == 0) return;
    const unsigned vCount = (unsigned)mesh->getVertexCount();
    const unsigned iCount = (unsigned)((mesh->getIndexCount() > 0)? mesh->getIndexCount() : vCount);

    Copy_Tform identity = { { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, { 0.0, 0.0, 0.0 } };
    std::vector<Copy_Tform> copies(1, identity);
    std::vector<Copy_Tform> steps;
    for (unsigned t = 0; tforms != nullptr && t < tformCount; t++) steps.push_back(getTform(*(tforms + t)));

    for (unsigned short l = 0; l < levels && !steps.empty(); l++) {
        if ((double)copies.size() * steps.size() * vCount > RECURSIVE_MAX_VERTICES) break;

        std::vector<Copy_Tform> placed;
        placed.reserve(copies.size() * steps.size());
        for (size_t c = 0; c < copies.size(); c++)
            for (size_t s = 0; s < steps.size(); s++) placed.push_back(joinTforms(copies[c], steps[s]));
        copies.swap(placed);
    }

    for (size_t c = 0; c < copies.size(); c++) {
        const unsigned base = (unsigned)vertices.size();
        for (unsigned v = 0; v < vCount; v++) {
            Geo_Vertex vertex = *(mesh->getVertices() + v);
            vertex.position = applyTform(copies[c], vertex.position, true);
            vertex.normal = Geo_Form::unit(applyTform(copies[c], vertex.normal, false));
            vertices.push_back(vertex);
        }
        for (unsigned i = 0; i + 2 < iCount; i += 3) {
            unsigned t[3];
            for (unsigned n = 0; n < 3; n++) t[n] = (mesh->getIndexCount() > 0)? *(mesh->getIndices() + i + n) : i + n;
            if (t[0] < vCount && t[1] < vCount && t[2] < vCount)
                for (unsigned n = 0; n < 3; n++) indices.push_back(base + t[n]);
        }
    }
}

static void genRecursive_tetrix(float radius, unsigned short levels, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    std::vector<Koch_Trig> trigs;
    genTetra(radius, trigs);

    std::vector<Geo_Vertex> tetraVertices;
    std::vector<unsigned> tetraIndices;
    for (size_t t = 0; t < trigs.size(); t++) addFlatTrig(trigs[t].points[0], trigs[t].points[1], trigs[t].points[2], tetraVertices, tetraIndices);
    Geo_Form::calcTexcoords(tetraVertices, true);
    Geo_Mesh tetra((unsigned)tetraVertices.size(), Geo_Form::copyVertices(tetraVertices), (unsigned)tetraIndices.size(), Geo_Form::copyIndices(tetraIndices));

    const float size = radius / sqrtf(3.0F);
    const Form_Tform tforms[4] = { // half sized copy towards each corner
        { Vec3f({ size, size, size }) * 0.5F, VEC_3F_ZERO, 0.5F }, { Vec3f({ size, -size, -size }) * 0.5F, VEC_3F_ZERO, 0.5F },
        { Vec3f({ -size, size, -size }) * 0.5F, VEC_3F_ZERO, 0.5F }, { Vec3f({ -size, -size, size }) * 0.5F, VEC_3F_ZERO, 0.5F }
    };
    genRecursive_copies(&tetra, tforms, 4, levels, vertices, indices);
}

// Constructors

Geo_Recursive::Geo_Recursive(RECURSIVE_Type type, float radius, unsigned short levels) : Geo_Form() {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    switch (type) {
        case RECURSIVE_Cross: genRecursive_cells(radius, 3, levels, recursiveRule_cross, vertices, indices); break;
        case RECURSIVE_Dust: genRecursive_cells(radius, 3, levels, recursiveRule_dust, vertices, indices); break;
        case RECURSIVE_Mosely: genRecursive_cells(radius, 3, levels, recursiveRule_mosely, vertices, indices); break;
        case RECURSIVE_Koch: genRecursive_koch(nullptr, radius, levels, vertices, indices); break;
        case RECURSIVE_KochPrism: genRecursive_kochPrism(radius, radius, levels, vertices, indices); break;
        case RECURSIVE_Tetrix: genRecursive_tetrix(radius, levels, vertices, indices); break;
        default: genRecursive_cells(radius, 3, levels, recursiveRule_menger, vertices, indices); break;
    }
    assign(vertices, indices, DRAW_Triangles);

    _shape = { radius, 1, 1, 1 };
    _levels = levels;
}

Geo_Recursive::Geo_Recursive(float radius, unsigned short divs, unsigned short levels, fRuleCallback ruleCallback) : Geo_Form() {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genRecursive_cells(radius, divs, levels, ruleCallback, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);

    _shape = { radius, divs, divs, divs };
    _levels = levels;
}

Geo_Recursive::Geo_Recursive(const Geo_Mesh* mesh, unsigned short levels) : Geo_Form() {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genRecursive_koch(mesh, 1.0F, levels, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, 1, 1, 1 };
    _levels = levels;
}

Geo_Recursive::Geo_Recursive(const Geo_Mesh* mesh, const Form_Tform* tforms, unsigned tformCount, unsigned short levels) : Geo_Form() {
    std::vector<Geo_Vertex> vertices;
    std::vector<unsigned> indices;
    genRecursive_copies(mesh, tforms, tformCount, levels, vertices, indices);
    assign(vertices, indices, DRAW_Triangles);

    Vec3f minBound, maxBound;
    getBounds(&minBound, &maxBound);
    _shape = { (maxBound - minBound).len() * 0.5F, 1, 1, 1 };
    _levels = levels;
}
