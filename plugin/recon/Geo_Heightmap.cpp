#include <cmath>

#include "Geo_Heightmap.hpp"

// Helper Functions

static Vec3f unitVec(const Vec3f& vec) { // result has a length of one, unlike Vec3f::normalize()
    float length = vec.len();
    return (length > 0.0F && std::isfinite(length))? vec * (1.0F / length) : VEC_3F_ZERO;
}

void genHeightmap_heights(const unsigned* colors, unsigned count, HEIGHTMAP_Channel channel, std::vector<float>& heights) {
    heights.resize((colors != nullptr)? count : 0);
    for (size_t p = 0; p < heights.size(); p++) {
        unsigned color = *(colors + p);
        unsigned alpha = (color >> 24) & 0xFF, red = (color >> 16) & 0xFF, green = (color >> 8) & 0xFF, blue = color & 0xFF;
        switch (channel) {
            case HEIGHTMAP_Red: heights[p] = red / 255.0F; break;
            case HEIGHTMAP_Green: heights[p] = green / 255.0F; break;
            case HEIGHTMAP_Blue: heights[p] = blue / 255.0F; break;
            case HEIGHTMAP_Alpha: heights[p] = alpha / 255.0F; break;
            default: heights[p] = ((red + green + blue) / 3) / 255.0F; break; // same average as channel_gray() in Rasteron
        }
    }
}

// Generation Functions

void genHeightmap_mesh(const float* heights, unsigned rows, unsigned cols, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (heights == nullptr || rows < 2 || cols < 2) return;

    const unsigned stride = (params.stride > 0)? params.stride : 1;
    const unsigned sRows = ((rows - 1) / stride) + 1, sCols = ((cols - 1) / stride) + 1; // samples taken
    if (sRows < 2 || sCols < 2) return;

    std::vector<float> samples(sRows * sCols);
    for (unsigned r = 0; r < sRows; r++)
        for (unsigned c = 0; c < sCols; c++) {
            float height = *(heights + ((size_t)(r * stride) * cols) + (c * stride));
            if (!std::isfinite(height)) height = 0.0F;
            samples[(r * sCols) + c] = (params.isInverted)? 1.0F - height : height;
        }

    for (unsigned short s = 0; s < params.smoothing; s++) { // average of each sample and the samples around it
        std::vector<float> smoothed(samples.size());
        for (unsigned r = 0; r < sRows; r++)
            for (unsigned c = 0; c < sCols; c++) {
                float total = 0.0F;
                unsigned count = 0;
                for (int nr = (int)r - 1; nr <= (int)r + 1; nr++)
                    for (int nc = (int)c - 1; nc <= (int)c + 1; nc++)
                        if (nr >= 0 && nc >= 0 && nr < (int)sRows && nc < (int)sCols) {
                            total += samples[(nr * sCols) + nc];
                            count++;
                        }
                smoothed[(r * sCols) + c] = total / count;
            }
        samples.swap(smoothed);
    }

    const unsigned base = (unsigned)vertices.size();
    const float cell = params.size / (((sRows > sCols)? sRows : sCols) - 1); // distance between samples
    const float left = (sCols - 1) * cell * -0.5F, top = (sRows - 1) * cell * 0.5F;

    float lowest = samples[0];
    for (size_t s = 0; s < samples.size(); s++) if (samples[s] < lowest) lowest = samples[s];

    for (unsigned r = 0; r < sRows; r++)
        for (unsigned c = 0; c < sCols; c++) {
            float height = samples[(r * sCols) + c];
            unsigned prevC = (c > 0)? c - 1 : c, nextC = (c + 1 < sCols)? c + 1 : c;
            unsigned prevR = (r > 0)? r - 1 : r, nextR = (r + 1 < sRows)? r + 1 : r;
            float slopeX = ((samples[(r * sCols) + nextC] - samples[(r * sCols) + prevC]) * params.depth) / ((nextC - prevC) * cell);
            float slopeY = ((samples[(prevR * sCols) + c] - samples[(nextR * sCols) + c]) * params.depth) / ((nextR - prevR) * cell); // rows run down as y runs up

            vertices.push_back(Geo_Vertex(
                Vec3f({ left + (c * cell), top - (r * cell), params.z + (height * params.depth) }),
                Vec3f({ (float)c / (sCols - 1), (float)r / (sRows - 1), height }),
                unitVec(Vec3f({ -slopeX, -slopeY, 1.0F })),
                VEC_3F_ONES
            ));
        }

    for (unsigned r = 0; r + 1 < sRows; r++)
        for (unsigned c = 0; c + 1 < sCols; c++) { // two triangles per square of samples, counter clockwise when seen from z
            unsigned topLeft = base + (r * sCols) + c, topRight = topLeft + 1;
            unsigned botLeft = topLeft + sCols, botRight = botLeft + 1;
            unsigned square[6] = { topLeft, botLeft, botRight, topLeft, botRight, topRight };
            indices.insert(indices.end(), square, square + 6);
        }

    if (!(params.thickness > 0.0F)) return;

    // Sides and base, the outline is walked clockwise as seen from z starting at the top left

    std::vector<unsigned> outline; // samples along the outline
    for (unsigned c = 0; c + 1 < sCols; c++) outline.push_back(c);
    for (unsigned r = 0; r + 1 < sRows; r++) outline.push_back((r * sCols) + (sCols - 1));
    for (unsigned c = sCols - 1; c > 0; c--) outline.push_back(((sRows - 1) * sCols) + c);
    for (unsigned r = sRows - 1; r > 0; r--) outline.push_back(r * sCols);

    const float baseZ = params.z + (lowest * params.depth) - params.thickness;
    const Vec3f sideNormals[4] = { Vec3f({ 0.0F, 1.0F, 0.0F }), Vec3f({ 1.0F, 0.0F, 0.0F }), Vec3f({ 0.0F, -1.0F, 0.0F }), Vec3f({ -1.0F, 0.0F, 0.0F }) };
    const unsigned sideEnds[4] = { sCols - 1, (sCols - 1) + (sRows - 1), ((sCols - 1) * 2) + (sRows - 1), (unsigned)outline.size() };

    unsigned side = 0;
    for (unsigned o = 0; o < outline.size(); o++) {
        while (side < 3 && o >= sideEnds[side]) side++;
        Vec3f upper1 = vertices[base + outline[o]].position, upper2 = vertices[base + outline[(o + 1) % outline.size()]].position;
        Vec3f lower1 = upper1, lower2 = upper2;
        lower1.data[2] = baseZ;
        lower2.data[2] = baseZ;

        const unsigned wall = (unsigned)vertices.size();
        const Vec3f corners[4] = { upper1, upper2, lower2, lower1 };
        for (unsigned v = 0; v < 4; v++)
            vertices.push_back(Geo_Vertex(corners[v], Vec3f({ (float)(o + (v == 1 || v == 2)) / outline.size(), (v < 2)? 0.0F : 1.0F, 0.0F }), sideNormals[side], VEC_3F_ONES));
        unsigned square[6] = { wall, wall + 1, wall + 2, wall, wall + 2, wall + 3 };
        indices.insert(indices.end(), square, square + 6);
    }

    const unsigned middle = (unsigned)vertices.size(); // base fans out from the middle so that it shares every edge with the sides
    const Vec3f down = Vec3f({ 0.0F, 0.0F, -1.0F });
    vertices.push_back(Geo_Vertex(Vec3f({ 0.0F, 0.0F, baseZ }), Vec3f({ 0.5F, 0.5F, 0.0F }), down, VEC_3F_ONES));
    for (unsigned o = 0; o < outline.size(); o++) {
        Vec3f position = vertices[base + outline[o]].position;
        position.data[2] = baseZ;
        vertices.push_back(Geo_Vertex(position, vertices[base + outline[o]].texcoord, down, VEC_3F_ONES));
    }
    for (unsigned o = 0; o < outline.size(); o++) {
        indices.push_back(middle);
        indices.push_back(middle + 1 + o);
        indices.push_back(middle + 1 + ((o + 1) % (unsigned)outline.size()));
    }
}

#ifdef GEO_RECON_IMAGES
void genHeightmap_image(const Rasteron_Image* image, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (image == nullptr || image->data == nullptr) return;

    std::vector<float> heights;
    genHeightmap_heights((const unsigned*)image->data, image->width * image->height, params.channel, heights);
    genHeightmap_mesh(heights.data(), image->height, image->width, params, vertices, indices);
}

#ifdef RASTERON_SPACE_H
void genHeightmap_map(const Rasteron_Heightmap* heightmap, const Heightmap_Params& params, std::vector<Geo_Vertex>& vertices, std::vector<unsigned>& indices) {
    if (heightmap == nullptr || heightmap->data == nullptr) return;

    // Bounds hold half of the size divided by the sprite scale, see createBounds() in Rasteron
    const long cols = lround((heightmap->bounds.topRight[0] - heightmap->bounds.topLeft[0]) * SPRITE_SCALE);
    const long rows = lround((heightmap->bounds.topLeft[1] - heightmap->bounds.botLeft[1]) * SPRITE_SCALE);
    if (rows < 2 || cols < 2) return;

    std::vector<float> heights((size_t)rows * cols);
    const float range = (heightmap->maxDepth != 0.0F)? heightmap->maxDepth : 1.0F; // stored heights go from minDepth to minDepth + maxDepth
    for (size_t h = 0; h < heights.size(); h++) heights[h] = (*(heightmap->data + h) - heightmap->minDepth) / range;
    genHeightmap_mesh(heights.data(), (unsigned)rows, (unsigned)cols, params, vertices, indices);
}
#endif
#endif

// Constructors

Geo_Heightmap::Geo_Heightmap(const float* heights, unsigned rows, unsigned cols, Heightmap_Params params) : Geo_Mesh() {
    _params = params;
    genHeightmap_mesh(heights, rows, cols, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}

#ifdef GEO_RECON_IMAGES
Geo_Heightmap::Geo_Heightmap(const Rasteron_Image* image, Heightmap_Params params) : Geo_Mesh() {
    _params = params;
    genHeightmap_image(image, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}

#ifdef RASTERON_SPACE_H
Geo_Heightmap::Geo_Heightmap(const Rasteron_Heightmap* heightmap, Heightmap_Params params) : Geo_Mesh() {
    _params = params;
    genHeightmap_map(heightmap, params, _vertices, _indices);
    drawMode = DRAW_Triangles;
}
#endif
#endif
