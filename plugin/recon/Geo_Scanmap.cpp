#include <cmath>
#include <unordered_map>

#include "Geo_Scanmap.hpp"

// Helper Functions

static float dotVec(const Vec3f& a, const Vec3f& b) { return (a.data[0] * b.data[0]) + (a.data[1] * b.data[1]) + (a.data[2] * b.data[2]); }

static Vec3f crossVec(const Vec3f& a, const Vec3f& b) {
    return Vec3f({ (a.data[1] * b.data[2]) - (a.data[2] * b.data[1]), (a.data[2] * b.data[0]) - (a.data[0] * b.data[2]), (a.data[0] * b.data[1]) - (a.data[1] * b.data[0]) });
}

static Vec3f unitVec(const Vec3f& vec) { // result has a length of one, unlike Vec3f::normalize()
    float length = vec.len();
    return (length > 0.0F && std::isfinite(length))? vec * (1.0F / length) : VEC_3F_ZERO;
}

static bool isBackground(unsigned color, const Scanmap_Params& params) {
    if (((color >> 24) & 0xFF) <= params.tolerance) return true; // transparent
    if (((params.background >> 24) & 0xFF) == 0) return false; // no background color to compare with

    for (unsigned shift = 0; shift < 24; shift += 8) {
        int difference = (int)((color >> shift) & 0xFF) - (int)((params.background >> shift) & 0xFF);
        if (difference < 0) difference = -difference;
        if (difference > params.tolerance) return false;
    }
    return true;
}

static bool isUsable(const Scanmap_Frame& frame, const Scanmap_View& view) {
    return frame.data != nullptr && frame.width > 0 && frame.height > 0 && view.focal >= 0.0F && (view.focal > 0.0F || view.scale > 0.0F);
}

// Pixel that a position lands on, depth is the distance from the camera along its forward direction
static bool projectView(const Scanmap_View& view, const Vec3f& position, float* x, float* y, float* depth) {
    Vec3f offset = position - view.position;
    *depth = dotVec(offset, view.forward);
    if (view.focal > 0.0F) {
        if (*depth <= 0.000001F) return false; // behind camera
        *x = view.centerX + ((view.focal * dotVec(offset, view.right)) / *depth);
        *y = view.centerY - ((view.focal * dotVec(offset, view.up)) / *depth);
    } else {
        *x = view.centerX + (view.scale * dotVec(offset, view.right));
        *y = view.centerY - (view.scale * dotVec(offset, view.up));
    }
    return true;
}

static Scanmap_View getView(unsigned frame, unsigned frameCount, unsigned width, unsigned height, const Scanmap_Params& params) {
    const float degrees = MATH_PI / 180.0F;
    float turn = 0.0F; // around y
    if (frameCount > 1) turn = (fabs(params.sweep) >= 360.0F)? (params.sweep * frame) / frameCount : (params.sweep * frame) / (frameCount - 1); // full circles do not repeat the first camera
    turn *= degrees;
    float rise = params.elevation * degrees;

    Scanmap_View view;
    Vec3f back = Vec3f({ sinf(turn) * cosf(rise), sinf(rise), cosf(turn) * cosf(rise) }); // from center towards camera
    view.position = params.center + (back * params.distance);
    view.forward = back * -1.0F;
    view.right = unitVec(crossVec(Vec3f({ 0.0F, 1.0F, 0.0F }), back));
    if (view.right.isZero()) view.right = Vec3f({ 1.0F, 0.0F, 0.0F }); // looking straight down or up
    view.up = crossVec(back, view.right);

    view.centerX = width * 0.5F;
    view.centerY = height * 0.5F;
    view.focal = (params.fov > 0.0F && params.fov < 180.0F)? (height * 0.5F) / tanf(params.fov * degrees * 0.5F) : 0.0F;
    view.scale = (params.radius > 0.0F)? height / (params.radius * 2.0F) : 1.0F;
    return view;
}

void genScanmap_views(unsigned frameCount, unsigned width, unsigned height, const Scanmap_Params& params, Scanmap_View* views) {
    for (unsigned f = 0; views != nullptr && f < frameCount; f++) *(views + f) = getView(f, frameCount, width, height, params);
}

// Generation Functions

struct Scan_Layer { // per pixel data worked out from a frame
    std::vector<float> masks; // 1 on subject and 0 on background, softened across the outline
    std::vector<unsigned char> solids; // nonzero on subject
    std::vector<float> depths; // distance from camera, depth mode only
};

// Averages each pixel with the pixels within reach along rows then columns, pixels past the frame count as background.
// An outline softened across the width of a cell lets the surface settle between cell corners rather than snap to them
static void softenMasks(std::vector<float>& masks, unsigned width, unsigned height, unsigned reach) {
    if (reach == 0 || masks.size() < (size_t)width * height) return;

    std::vector<float> sums((((width > height)? width : height) + 1), 0.0F);
    std::vector<float> line((width > height)? width : height);
    for (unsigned pass = 0; pass < 2; pass++) {
        const unsigned count = (pass == 0)? width : height, lines = (pass == 0)? height : width;
        const size_t step = (pass == 0)? 1 : width;
        for (unsigned l = 0; l < lines; l++) {
            const size_t start = (pass == 0)? (size_t)l * width : l;
            for (unsigned i = 0; i < count; i++) sums[i + 1] = sums[i] + masks[start + (i * step)]; // running total along the line
            for (unsigned i = 0; i < count; i++) {
                unsigned first = (i > reach)? i - reach : 0, last = (i + reach + 1 < count)? i + reach + 1 : count;
                line[i] = (sums[last] - sums[first]) / ((reach * 2) + 1);
            }
            for (unsigned i = 0; i < count; i++) masks[start + (i * step)] = line[i];
        }
    }
}

// How far a position is inside of what a frame shows, from -1 for outside to 1 for inside
static float sampleView(const Scanmap_Frame& frame, const Scanmap_View& view, const Scan_Layer& layer, const Vec3f& position, const Scanmap_Params& params, float band) {
    float x, y, depth;
    if (!projectView(view, position, &x, &y, &depth)) return -1.0F;

    float fx = x - 0.5F, fy = y - 0.5F; // pixels are centered on half steps
    long x0 = (long)floorf(fx), y0 = (long)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    float mask = 0.0F, weight = 0.0F, surface = 0.0F; // blended between the four closest pixels, anything past the frame is background
    for (unsigned c = 0; c < 4; c++) {
        long px = x0 + (c & 1), py = y0 + (c >> 1);
        if (px < 0 || py < 0 || px >= (long)frame.width || py >= (long)frame.height) continue;

        size_t pixel = ((size_t)py * frame.width) + px;
        float amount = ((c & 1)? tx : 1.0F - tx) * ((c >> 1)? ty : 1.0F - ty);
        mask += layer.masks[pixel] * amount;
        if (params.mode == SCAN_Depth && layer.solids[pixel]) { // depth is only known on the subject
            weight += amount;
            surface += layer.depths[pixel] * amount;
        }
    }
    float inside = (mask * 2.0F) - 1.0F;

    if (params.mode == SCAN_Depth) {
        if (weight <= 0.0F) return -1.0F; // no surface was seen here
        float behind = (depth - (surface / weight)) / band; // space between camera and surface is empty
        if (behind < -1.0F) behind = -1.0F;
        if (behind < inside) inside = behind;
    }
    return inside;
}

// Space around the subject is split into cells. Every corner of a cell is tested against all frames and is inside only when
// no frame rules it out, the surface is then drawn where neighboring corners disagree
void genScanmap_mesh(const Scanmap_Frame* frames, const Scanmap_View* views, unsigned frameCount, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (frames == nullptr || views == nullptr || !(params.radius > 0.0F)) return;

    unsigned divs = (params.divs > 2)? params.divs : 2;
    if (divs > SCANMAP_MAX_DIVS) divs = SCANMAP_MAX_DIVS;
    const unsigned steps = divs + 1; // corners along each axis
    const float cell = (params.radius * 2.0F) / divs;
    const float band = cell * 2.0F; // distance over which depth fades from outside to inside

    std::vector<unsigned> targets; // frames that can be used
    std::vector<Scan_Layer> layers(frameCount);
    for (unsigned f = 0; f < frameCount; f++) {
        const Scanmap_Frame& frame = *(frames + f);
        if (!isUsable(frame, *(views + f))) continue;
        targets.push_back(f);

        const size_t pixels = (size_t)frame.width * frame.height;
        layers[f].masks.resize(pixels);
        layers[f].solids.resize(pixels);
        if (params.mode == SCAN_Depth) layers[f].depths.resize(pixels);
        for (size_t p = 0; p < pixels; p++) {
            unsigned color = *(frame.data + p);
            layers[f].solids[p] = (isBackground(color, params))? 0 : 1;
            layers[f].masks[p] = layers[f].solids[p];
            if (params.mode == SCAN_Depth) {
                float level = ((((color >> 16) & 0xFF) + ((color >> 8) & 0xFF) + (color & 0xFF)) / 3) / 255.0F;
                layers[f].depths[p] = params.farDepth + ((params.nearDepth - params.farDepth) * level);
            }
        }

        const Scanmap_View& view = *(views + f);
        float reach = (view.focal > 0.0F)? (view.focal * cell) / (params.center - view.position).len() : view.scale * cell; // pixels that a cell covers
        if (reach > SCANMAP_MAX_SOFTEN) reach = SCANMAP_MAX_SOFTEN;
        if (reach >= 1.0F) softenMasks(layers[f].masks, frame.width, frame.height, (unsigned)reach);
    }
    if (targets.empty()) return;

    std::vector<float> field((size_t)steps * steps * steps, -1.0F); // edge of the space stays outside so that the mesh is closed
    std::vector<Vec3f> points(field.size());
    for (unsigned z = 0; z < steps; z++)
        for (unsigned y = 0; y < steps; y++)
            for (unsigned x = 0; x < steps; x++) {
                size_t corner = x + ((size_t)y * steps) + ((size_t)z * steps * steps);
                points[corner] = Vec3f({ params.center.data[0] - params.radius + (x * cell), params.center.data[1] - params.radius + (y * cell), params.center.data[2] - params.radius + (z * cell) });
                if (x == 0 || y == 0 || z == 0 || x == divs || y == divs || z == divs) continue;

                float inside = 1.0F;
                for (size_t t = 0; t < targets.size() && inside > -1.0F; t++) {
                    float sample = sampleView(*(frames + targets[t]), *(views + targets[t]), layers[targets[t]], points[corner], params, band);
                    if (sample < inside) inside = sample;
                }
                field[corner] = inside;
            }

    // Surface, each cell is split into six tetrahedra around its diagonal and vertices sit on the edges where inside meets outside

    static const unsigned char tetras[6][4] = { { 0, 1, 3, 7 }, { 0, 3, 2, 7 }, { 0, 2, 6, 7 }, { 0, 6, 4, 7 }, { 0, 4, 5, 7 }, { 0, 5, 1, 7 } };

    std::vector<Geo_Vertex> scanVertices;
    std::vector<unsigned> scanIndices;
    std::unordered_map<unsigned long long, unsigned> lookup; // edge between two corners to vertex
    const unsigned long long cornerCount = field.size();

    for (unsigned z = 0; z < divs; z++)
        for (unsigned y = 0; y < divs; y++)
            for (unsigned x = 0; x < divs; x++) {
                size_t corners[8];
                bool hasInside = false, hasOutside = false;
                for (unsigned c = 0; c < 8; c++) {
                    corners[c] = (x + (c & 1)) + ((size_t)(y + ((c >> 1) & 1)) * steps) + ((size_t)(z + ((c >> 2) & 1)) * steps * steps);
                    if (field[corners[c]] > 0.0F) hasInside = true;
                    else hasOutside = true;
                }
                if (!hasInside || !hasOutside) continue;

                for (unsigned t = 0; t < 6; t++) {
                    size_t inside[4], outside[4];
                    unsigned insideCount = 0, outsideCount = 0;
                    Vec3f insideMid = VEC_3F_ZERO, outsideMid = VEC_3F_ZERO;
                    for (unsigned c = 0; c < 4; c++) {
                        size_t corner = corners[tetras[t][c]];
                        if (field[corner] > 0.0F) {
                            inside[insideCount++] = corner;
                            insideMid = insideMid + points[corner];
                        } else {
                            outside[outsideCount++] = corner;
                            outsideMid = outsideMid + points[corner];
                        }
                    }
                    if (insideCount == 0 || outsideCount == 0) continue;

                    size_t edges[4][2]; // corners that each vertex of the surface sits between
                    unsigned edgeCount = 0;
                    if (insideCount == 1) for (unsigned c = 0; c < 3; c++) { edges[edgeCount][0] = inside[0]; edges[edgeCount++][1] = outside[c]; }
                    else if (outsideCount == 1) for (unsigned c = 0; c < 3; c++) { edges[edgeCount][0] = inside[c]; edges[edgeCount++][1] = outside[0]; }
                    else {
                        const unsigned char order[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } }; // around the square between both pairs
                        for (unsigned c = 0; c < 4; c++) { edges[edgeCount][0] = inside[order[c][0]]; edges[edgeCount++][1] = outside[order[c][1]]; }
                    }

                    unsigned shape[4];
                    for (unsigned e = 0; e < edgeCount; e++) {
                        size_t first = (edges[e][0] < edges[e][1])? edges[e][0] : edges[e][1];
                        size_t second = (edges[e][0] < edges[e][1])? edges[e][1] : edges[e][0];
                        unsigned long long key = ((unsigned long long)first * cornerCount) + second;

                        std::unordered_map<unsigned long long, unsigned>::iterator match = lookup.find(key);
                        if (match == lookup.end()) {
                            float range = field[second] - field[first];
                            float amount = (range != 0.0F)? -field[first] / range : 0.5F;
                            if (amount < 0.0F) amount = 0.0F;
                            if (amount > 1.0F) amount = 1.0F;

                            shape[e] = (unsigned)scanVertices.size();
                            lookup.insert(std::make_pair(key, shape[e]));
                            scanVertices.push_back(Geo_Vertex(points[first] + ((points[second] - points[first]) * amount), VEC_3F_ZERO, VEC_3F_ZERO, VEC_3F_ONES));
                        } else shape[e] = match->second;
                    }

                    Vec3f direction = (outsideMid * (1.0F / outsideCount)) - (insideMid * (1.0F / insideCount)); // away from the subject
                    for (unsigned s = 0; s + 2 < edgeCount; s++) {
                        unsigned trig[3] = { shape[0], shape[s + 1], shape[s + 2] };
                        if (trig[0] == trig[1] || trig[1] == trig[2] || trig[0] == trig[2]) continue;

                        Vec3f normal = crossVec(scanVertices[trig[1]].position - scanVertices[trig[0]].position, scanVertices[trig[2]].position - scanVertices[trig[0]].position);
                        if (dotVec(normal, direction) < 0.0F) std::swap(trig[1], trig[2]);
                        scanIndices.insert(scanIndices.end(), trig, trig + 3);
                    }
                }
            }

    // Normals, texture coordinates, and colors

    std::vector<Vec3f> normals(scanVertices.size(), VEC_3F_ZERO);
    for (size_t i = 0; i + 2 < scanIndices.size(); i += 3) {
        Vec3f normal = crossVec(scanVertices[scanIndices[i + 1]].position - scanVertices[scanIndices[i]].position, scanVertices[scanIndices[i + 2]].position - scanVertices[scanIndices[i]].position);
        for (unsigned n = 0; n < 3; n++) normals[scanIndices[i + n]] = normals[scanIndices[i + n]] + normal;
    }

    for (size_t v = 0; v < scanVertices.size(); v++) {
        Geo_Vertex& vertex = scanVertices[v];
        vertex.normal = unitVec(normals[v]);

        Vec3f coord = (vertex.position - params.center + Vec3f({ params.radius, params.radius, params.radius })) * (1.0F / (params.radius * 2.0F));
        vertex.texcoord = Vec3f({ coord.data[0], 1.0F - coord.data[1], coord.data[2] });
        if (!params.hasColors) continue;

        float best = 0.0F; // how directly the closest matching camera faces the vertex
        for (size_t t = 0; t < targets.size(); t++) {
            const Scanmap_Frame& frame = *(frames + targets[t]);
            const Scanmap_View& view = *(views + targets[t]);

            Vec3f toCamera = (view.focal > 0.0F)? unitVec(view.position - vertex.position) : view.forward * -1.0F;
            float facing = dotVec(vertex.normal, toCamera);
            if (facing <= best) continue;

            float x, y, depth;
            if (!projectView(view, vertex.position, &x, &y, &depth)) continue;
            long px = (long)floorf(x), py = (long)floorf(y);
            if (px < 0 || py < 0 || px >= (long)frame.width || py >= (long)frame.height) continue;

            unsigned color = *(frame.data + ((size_t)py * frame.width) + px);
            if (isBackground(color, params)) continue;
            vertex.color = Vec3f({ ((color >> 16) & 0xFF) / 255.0F, ((color >> 8) & 0xFF) / 255.0F, (color & 0xFF) / 255.0F });
            best = facing;
        }
    }

    const unsigned base = (unsigned)vertices.size();
    vertices.insert(vertices.end(), scanVertices.begin(), scanVertices.end());
    for (size_t i = 0; i < scanIndices.size(); i++) indices.push_back(base + scanIndices[i]);
}

#ifdef GEO_RECON_IMAGES
void genScanmap_image(const Rasteron_Image* image, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (image == nullptr || image->data == nullptr) return;

    Scanmap_Frame frame = { (const unsigned*)image->data, image->width, image->height };
    Scanmap_View view = getView(0, 1, image->width, image->height, params);
    genScanmap_mesh(&frame, &view, 1, params, vertices, indices);
}

#ifdef RASTERON_QUEUE_H
void genScanmap_queue(const Rasteron_Queue* queue, const Scanmap_View* views, unsigned viewCount, const Scanmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (queue == nullptr || queue->frameData == nullptr || queue->frameCount == 0) return;

    std::vector<Scanmap_Frame> frames(queue->frameCount);
    std::vector<Scanmap_View> frameViews(queue->frameCount);
    for (unsigned f = 0; f < queue->frameCount; f++) {
        const Rasteron_Image* image = *(queue->frameData + f);
        bool hasImage = image != nullptr && image->data != nullptr;
        frames[f].data = (hasImage)? (const unsigned*)image->data : nullptr;
        frames[f].width = (hasImage)? image->width : 0;
        frames[f].height = (hasImage)? image->height : 0;

        if (views == nullptr) frameViews[f] = getView(f, queue->frameCount, frames[f].width, frames[f].height, params);
        else if (f < viewCount) frameViews[f] = *(views + f);
        else { // no camera known for this frame
            frameViews[f] = getView(f, queue->frameCount, frames[f].width, frames[f].height, params);
            frameViews[f].focal = -1.0F;
        }
    }
    genScanmap_mesh(frames.data(), frameViews.data(), queue->frameCount, params, vertices, indices);
}
#endif
#endif

// Constructors

Geo_Scanmap::Geo_Scanmap(const Scanmap_Frame* frames, const Scanmap_View* views, unsigned frameCount, Scanmap_Params params) : Geo_Mesh() {
    _params = params;
    genScanmap_mesh(frames, views, frameCount, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}

#ifdef GEO_RECON_IMAGES
Geo_Scanmap::Geo_Scanmap(const Rasteron_Image* image, Scanmap_Params params) : Geo_Mesh() {
    _params = params;
    genScanmap_image(image, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}

#ifdef RASTERON_QUEUE_H
Geo_Scanmap::Geo_Scanmap(const Rasteron_Queue* queue, Scanmap_Params params) : Geo_Mesh() {
    _params = params;
    genScanmap_queue(queue, nullptr, 0, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}

Geo_Scanmap::Geo_Scanmap(const Rasteron_Queue* queue, const Scanmap_View* views, unsigned viewCount, Scanmap_Params params) : Geo_Mesh() {
    _params = params;
    genScanmap_queue(queue, views, viewCount, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}
#endif
#endif
