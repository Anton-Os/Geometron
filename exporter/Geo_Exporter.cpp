// Geo_Exporter: command line tool that encodes a preset Geo_Mesh or Geo_Construct into a 3D model file

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "meshes/Geo_Surface.hpp"
#include "meshes/Geo_Volume.hpp"
#include "meshes/Geo_Cone.hpp"
#include "meshes/Geo_Orboid.hpp"

#include "constructs/Geo_Grid.hpp"
#include "constructs/Geo_Chain.hpp"
#include "constructs/Geo_Board.hpp"

#include "model/Geo_Model.hpp"

// Preset Shapes

enum SHAPE_Family { SHAPE_Surface, SHAPE_Ext2D, SHAPE_Volume, SHAPE_Ext3D, SHAPE_Cone, SHAPE_ExtCone, SHAPE_Orb, SHAPE_Torus };

struct Shape_Preset {
    SHAPE_Family family;
    unsigned short segments; // default segment count
    const char* description;
};

static const std::vector<std::pair<std::string, Shape_Preset>>& getPresets(){
    static const std::vector<std::pair<std::string, Shape_Preset>> presets = {
        { "surface", { SHAPE_Surface, 4, "Geo_Surface, flat polygon (--segments)" } },
        { "trig2d", { SHAPE_Surface, 3, "Geo_Trig2D, flat triangle" } },
        { "quad2d", { SHAPE_Surface, 4, "Geo_Quad2D, flat square" } },
        { "hex2d", { SHAPE_Surface, 6, "Geo_Hex2D, flat hexagon" } },
        { "circle2d", { SHAPE_Surface, CIRCLE_SEGMENTS, "Geo_Circle2D, flat circle" } },
        { "ext2d", { SHAPE_Ext2D, 4, "Geo_Ext2D, recursively subdivided surface (--iters)" } },
        { "volume", { SHAPE_Volume, 4, "Geo_Volume, extruded polygon (--segments, --depth)" } },
        { "trig3d", { SHAPE_Volume, 3, "Geo_Trig3D, triangular prism" } },
        { "quad3d", { SHAPE_Volume, 4, "Geo_Quad3D, box" } },
        { "hex3d", { SHAPE_Volume, 6, "Geo_Hex3D, hexagonal prism" } },
        { "circle3d", { SHAPE_Volume, CIRCLE_SEGMENTS, "Geo_Circle3D, cylinder" } },
        { "ext3d", { SHAPE_Ext3D, 4, "Geo_Ext3D, stacked volume (--iters)" } },
        { "cone", { SHAPE_Cone, 4, "Geo_Cone, polygon based cone (--segments, --apex)" } },
        { "trigcone", { SHAPE_Cone, 3, "Geo_TrigCone, tetrahedron-like pyramid" } },
        { "quadcone", { SHAPE_Cone, 4, "Geo_QuadCone, square pyramid" } },
        { "hexcone", { SHAPE_Cone, 6, "Geo_HexCone, hexagonal pyramid" } },
        { "circlecone", { SHAPE_Cone, CIRCLE_SEGMENTS, "Geo_CircleCone, round cone" } },
        { "extcone", { SHAPE_ExtCone, 4, "Geo_ExtCone, cone with repeating base (--iters)" } },
        { "orb", { SHAPE_Orb, CIRCLE_SEGMENTS, "Geo_Orb, sphere (--segments x,y)" } },
        { "trigorb", { SHAPE_Orb, 3, "Geo_TrigOrb" } },
        { "quadorb", { SHAPE_Orb, 4, "Geo_QuadOrb" } },
        { "hexorb", { SHAPE_Orb, 6, "Geo_HexOrb" } },
        { "octorb", { SHAPE_Orb, 8, "Geo_OctOrb" } },
        { "decorb", { SHAPE_Orb, 10, "Geo_DecOrb" } },
        { "torus", { SHAPE_Torus, CIRCLE_SEGMENTS, "Geo_Torus, ring (--segments x,y, --diameter)" } }
    };
    return presets;
}

static const char* constructList[][2] = {
    { "grid", "Geo_Grid, mesh repeated across --count x,y,z cells with --spacing" },
    { "chain", "Geo_Chain, mesh repeated --count times along --direction" },
    { "board", "Geo_Board, --count rows,cols of panes with a backing pane (mesh options ignored)" }
};

// Options

struct Exporter_Opts {
    std::string mesh = "hexcone";
    std::string construct = ""; // empty exports mesh only
    std::string output = "";
    std::string format = "";

    float radius = 1.0F;
    std::vector<unsigned> segments; // empty uses preset default
    bool hasDepth = false;
    float depth = 1.0F;
    bool hasApex = false;
    Vec3f apex = VEC_3F_ZERO;
    float diameter = 0.5F;
    unsigned iters = 1;
    unsigned tess = 0;

    Vec3f shift = VEC_3F_ZERO, rotate = VEC_3F_ZERO, scale = VEC_3F_ONES;

    std::vector<unsigned> count;
    std::vector<float> spacing;
    bool hasDirection = false;
    Vec3f direction = VEC_3F_ZERO;

    Geo_Model_ExportOpts exportOpts;
};

static void printUsage(const char* program){
    std::cout <<
        "Usage: " << program << " [options]\n"
        "Encodes a preset Geo_Mesh or Geo_Construct into a 3D model file.\n\n"
        "Mesh options:\n"
        "  -m, --mesh <type>          Preset mesh to generate (default: hexcone), see --list\n"
        "  -r, --radius <f>           Shape radius (default: 1.0)\n"
        "  -n, --segments <n>[,<n>]   Segment count, orb and torus accept x,y (default: preset)\n"
        "  -d, --depth <f>            Depth of volume shapes, z height of surface shapes\n"
        "      --apex <x,y,z>         Apex of cone shapes (default: 0,0,radius)\n"
        "      --diameter <f>         Tube diameter factor of torus (default: 0.5)\n"
        "      --iters <n>            Iterations for ext2d, ext3d, and extcone (default: 1)\n"
        "      --tess <n>             Tesselation passes applied to the mesh (default: 0)\n"
        "      --shift <x,y,z>        Translates the mesh\n"
        "      --rotate <x,y,z>       Rotates the mesh, radians (same convention as Geo_Mesh::rotate)\n"
        "      --scale <x,y,z|f>      Scales the mesh\n\n"
        "Construct options:\n"
        "  -c, --construct <type>     Construct built from the mesh: grid, chain, board (default: none)\n"
        "      --count <n>[,<n>[,<n>]] grid: cells per axis (default: 3,3,1)\n"
        "                             chain: link count (default: 5), board: rows,cols (default: 3,3)\n"
        "      --spacing <f>[,<f>[,<f>]] grid cell spacing per axis (default: mesh size * 1.25)\n"
        "      --direction <x,y,z>    chain offset between links (default: mesh width * 1.25 along x)\n\n"
        "Output options:\n"
        "  -o, --output <path>        Output file (default: <mesh>[_<construct>].<ext>)\n"
        "  -f, --format <id|ext>      Format id or extension such as obj, stl, stlb, ply, fbx, glb, gltf, dae,\n"
        "                             3mf (default: from --output extension, otherwise obj)\n"
        "      --normals <mode>       smooth, flat, mesh, or none (default: smooth)\n"
        "      --no-texcoords         Omits texture coordinates\n"
        "      --colors               Writes vertex colors (for formats that support them, e.g. ply)\n"
        "      --merge                Combines all construct elements into a single mesh\n\n"
        "Information:\n"
        "      --list                 Lists preset meshes and constructs\n"
        "      --formats              Lists every export format available\n"
        "  -h, --help                 Shows this message\n\n"
        "Examples:\n"
        "  " << program << " --mesh hexcone -o cone.obj\n"
        "  " << program << " --mesh orb --segments 24,24 --radius 2 -f glb\n"
        "  " << program << " --mesh quad3d --construct grid --count 4,4,2 --spacing 1.5 -o grid.fbx\n"
        "  " << program << " --mesh circlecone --construct chain --count 6 --direction 0,0,1.2 -f stl\n";
}

static void printList(){
    std::cout << "Meshes:\n";
    for(const auto& preset : getPresets()){
        std::string name = "  " + preset.first;
        name.resize(14, ' ');
        std::cout << name << preset.second.description << "\n";
    }
    std::cout << "\nConstructs:\n";
    for(unsigned c = 0; c < sizeof(constructList) / sizeof(constructList[0]); c++){
        std::string name = std::string("  ") + constructList[c][0];
        name.resize(14, ' ');
        std::cout << name << constructList[c][1] << "\n";
    }
}

static bool parseFloats(const std::string& str, std::vector<float>& values){
    values.clear();
    std::stringstream stream(str);
    std::string token;
    while(std::getline(stream, token, ',')){
        char* end = nullptr;
        float value = std::strtof(token.c_str(), &end);
        if(token.empty() || end == token.c_str() || *end != '\0') return false;
        values.push_back(value);
    }
    return !values.empty();
}

static bool parseUnsigneds(const std::string& str, std::vector<unsigned>& values){
    values.clear();
    std::stringstream stream(str);
    std::string token;
    while(std::getline(stream, token, ',')){
        char* end = nullptr;
        long value = std::strtol(token.c_str(), &end, 10);
        if(token.empty() || end == token.c_str() || *end != '\0' || value < 0) return false;
        values.push_back((unsigned)value);
    }
    return !values.empty();
}

static bool parseVec3(const std::string& str, Vec3f& vec, bool allowScalar){
    std::vector<float> values;
    if(!parseFloats(str, values)) return false;
    if(values.size() == 1 && allowScalar) vec = Vec3f({ values[0], values[0], values[0] });
    else if(values.size() == 3) vec = Vec3f({ values[0], values[1], values[2] });
    else return false;
    return true;
}

static std::string getExtension(const std::string& path){
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if(dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return path.substr(dot + 1);
}

// returns 0 to continue, 1 for error, 2 when program should exit successfully
static int parseArgs(int argc, char** argv, Exporter_Opts& opts){
    for(int a = 1; a < argc; a++){
        std::string arg = argv[a];
        auto next = [&](std::string& value) -> bool {
            if(a + 1 >= argc){ std::cerr << "Missing value for " << arg << "\n"; return false; }
            value = argv[++a];
            return true;
        };
        auto bad = [&](const std::string& value) -> int {
            std::cerr << "Invalid value for " << arg << ": " << value << "\n";
            return 1;
        };
        std::string value;
        std::vector<float> floats;

        if(arg == "-h" || arg == "--help"){ printUsage(argv[0]); return 2; }
        else if(arg == "--list"){ printList(); return 2; }
        else if(arg == "--formats"){
            for(const std::string& line : getExportFormats()) std::cout << line << "\n";
            return 2;
        }
        else if(arg == "-m" || arg == "--mesh"){ if(!next(opts.mesh)) return 1; }
        else if(arg == "-c" || arg == "--construct"){ if(!next(opts.construct)) return 1; }
        else if(arg == "-o" || arg == "--output"){ if(!next(opts.output)) return 1; }
        else if(arg == "-f" || arg == "--format"){ if(!next(opts.format)) return 1; }
        else if(arg == "-r" || arg == "--radius"){
            if(!next(value)) return 1;
            if(!parseFloats(value, floats) || floats.size() != 1 || floats[0] <= 0.0F) return bad(value);
            opts.radius = floats[0];
        }
        else if(arg == "-n" || arg == "--segments"){
            if(!next(value)) return 1;
            if(!parseUnsigneds(value, opts.segments) || opts.segments.size() > 2) return bad(value);
        }
        else if(arg == "-d" || arg == "--depth"){
            if(!next(value)) return 1;
            if(!parseFloats(value, floats) || floats.size() != 1) return bad(value);
            opts.depth = floats[0]; opts.hasDepth = true;
        }
        else if(arg == "--apex"){
            if(!next(value)) return 1;
            if(!parseVec3(value, opts.apex, false)) return bad(value);
            opts.hasApex = true;
        }
        else if(arg == "--diameter"){
            if(!next(value)) return 1;
            if(!parseFloats(value, floats) || floats.size() != 1 || floats[0] <= 0.0F) return bad(value);
            opts.diameter = floats[0];
        }
        else if(arg == "--iters" || arg == "--tess"){
            if(!next(value)) return 1;
            std::vector<unsigned> values;
            if(!parseUnsigneds(value, values) || values.size() != 1) return bad(value);
            ((arg == "--iters")? opts.iters : opts.tess) = values[0];
        }
        else if(arg == "--shift"){ if(!next(value)) return 1; if(!parseVec3(value, opts.shift, false)) return bad(value); }
        else if(arg == "--rotate"){ if(!next(value)) return 1; if(!parseVec3(value, opts.rotate, false)) return bad(value); }
        else if(arg == "--scale"){ if(!next(value)) return 1; if(!parseVec3(value, opts.scale, true)) return bad(value); }
        else if(arg == "--count"){
            if(!next(value)) return 1;
            if(!parseUnsigneds(value, opts.count) || opts.count.size() > 3) return bad(value);
            for(unsigned c : opts.count) if(c == 0) return bad(value);
        }
        else if(arg == "--spacing"){
            if(!next(value)) return 1;
            if(!parseFloats(value, opts.spacing) || opts.spacing.size() > 3) return bad(value);
        }
        else if(arg == "--direction"){
            if(!next(value)) return 1;
            if(!parseVec3(value, opts.direction, false)) return bad(value);
            opts.hasDirection = true;
        }
        else if(arg == "--normals"){
            if(!next(value)) return 1;
            if(value == "smooth") opts.exportOpts.normals = EXPORT_Normals_Smooth;
            else if(value == "flat") opts.exportOpts.normals = EXPORT_Normals_Flat;
            else if(value == "mesh") opts.exportOpts.normals = EXPORT_Normals_Mesh;
            else if(value == "none") opts.exportOpts.normals = EXPORT_Normals_None;
            else return bad(value);
        }
        else if(arg == "--no-texcoords") opts.exportOpts.texcoords = false;
        else if(arg == "--colors") opts.exportOpts.colors = true;
        else if(arg == "--merge") opts.exportOpts.merge = true;
        else {
            std::cerr << "Unknown argument: " << arg << " (see --help)\n";
            return 1;
        }
    }
    return 0;
}

// Mesh Generation

static Geo_Mesh* genMesh(const Exporter_Opts& opts, std::string& error){
    const Shape_Preset* preset = nullptr;
    for(const auto& p : getPresets()) if(p.first == opts.mesh) preset = &p.second;
    if(preset == nullptr){
        error = "Unknown mesh type: " + opts.mesh + " (see --list)";
        return nullptr;
    }

    unsigned xSegs = (opts.segments.size() > 0)? opts.segments[0] : preset->segments;
    unsigned ySegs = (opts.segments.size() > 1)? opts.segments[1] : xSegs;
    if(xSegs < 3 || ySegs < 3 || xSegs > 1024 || ySegs > 1024){
        error = "Segments must be between 3 and 1024";
        return nullptr;
    }

    Shape2D shape2D = { opts.radius, (unsigned short)xSegs };
    Shape3D shape3D = { opts.radius, (unsigned short)xSegs, (unsigned short)ySegs };
    Vec3f apex = (opts.hasApex)? opts.apex : Vec3f({ 0.0F, 0.0F, opts.radius + DEFAULT_Z });
    float z = (opts.hasDepth)? opts.depth : DEFAULT_Z;
    float depth = (opts.hasDepth)? opts.depth : 1.0F;
    unsigned short iters = (unsigned short)opts.iters;

    Geo_Mesh* mesh = new Geo_Mesh(); // preset is copied in so all types are owned the same way
    switch(preset->family){
        case SHAPE_Surface: mesh->extend(Geo_Surface(shape2D, z)); break;
        case SHAPE_Ext2D: mesh->extend(Geo_Ext2D(shape2D, z, iters)); break;
        case SHAPE_Volume: mesh->extend(Geo_Volume(shape2D, depth)); break;
        case SHAPE_Ext3D: mesh->extend(Geo_Ext3D(shape2D, depth, iters)); break;
        case SHAPE_Cone: mesh->extend(Geo_Cone(shape2D, apex)); break;
        case SHAPE_ExtCone: mesh->extend(Geo_ExtCone(shape2D, apex, iters)); break;
        case SHAPE_Orb: mesh->extend(Geo_Orb(shape3D)); break;
        case SHAPE_Torus: mesh->extend(Geo_Torus(opts.diameter, shape3D)); break;
    }

    if(opts.tess > 0) mesh->tesselate((unsigned short)opts.tess);
    if(opts.scale != VEC_3F_ONES) mesh->scale(opts.scale);
    if(!opts.rotate.isZero()) mesh->rotate(opts.rotate);
    if(!opts.shift.isZero()) mesh->shift(opts.shift);
    return mesh;
}

static Vec3f getMeshSize(const Geo_Mesh* mesh){
    if(mesh->getVertexCount() == 0) return VEC_3F_ONES;
    Vec3f minVec = mesh->getVertices()->position, maxVec = minVec;
    for(unsigned v = 0; v < mesh->getVertexCount(); v++){
        Vec3f pos = (mesh->getVertices() + v)->position;
        for(unsigned a = 0; a < 3; a++){
            if(pos.data[a] < minVec.data[a]) minVec.data[a] = pos.data[a];
            if(pos.data[a] > maxVec.data[a]) maxVec.data[a] = pos.data[a];
        }
    }
    return maxVec - minVec;
}

static float spacingOr(const Exporter_Opts& opts, unsigned axis, float fallback){
    if(opts.spacing.empty()) return fallback;
    return (axis < opts.spacing.size())? opts.spacing[axis] : opts.spacing.back();
}

static bool reportResult(const Geo_Model_ExportResult& result){
    if(!result.success){
        std::cerr << result.message << "\n";
        return false;
    }
    std::cout << result.message << " (" << result.meshCount << " mesh" << ((result.meshCount != 1)? "es" : "")
              << ", " << result.vertexCount << " vertices, " << result.faceCount << " triangles)\n";
    if(result.droppedFaces > 0)
        std::cout << "Note: skipped " << result.droppedFaces << " degenerate or out of range triangles from the source mesh\n";
    return true;
}

int main(int argc, char** argv){
    Exporter_Opts opts;
    int status = parseArgs(argc, argv, opts);
    if(status != 0) return (status == 2)? 0 : 1;

    // Resolve format and output path

    std::string outputExt = getExtension(opts.output);
    std::string formatId = getExportFormatId((!opts.format.empty())? opts.format : (!outputExt.empty())? outputExt : "obj");
    if(formatId.empty()){
        std::cerr << "Unsupported format: " << ((!opts.format.empty())? opts.format : outputExt) << " (see --formats)\n";
        return 1;
    }
    std::string formatExt = getExportFormatExt(formatId);
    if(opts.output.empty()) opts.output = opts.mesh + ((!opts.construct.empty())? "_" + opts.construct : "") + "." + formatExt;
    else if(outputExt.empty()) opts.output += "." + formatExt;

    // Generate geometry and export

    std::string error;
    Geo_Mesh* mesh = nullptr;
    if(opts.construct != "board"){
        mesh = genMesh(opts, error);
        if(mesh == nullptr){
            std::cerr << error << "\n";
            return 1;
        }
    }

    bool success = false;
    if(opts.construct.empty()) success = reportResult(Geo_Model3D::exportModel(opts.output, mesh, formatId, opts.exportOpts));
    else if(opts.construct == "grid"){
        Vec3f size = getMeshSize(mesh);
        float defaultSpacing = std::max(std::max(size.data[0], size.data[1]), std::max(size.data[2], 0.1F)) * 1.25F;
        unsigned short x = (unsigned short)((opts.count.size() > 0)? opts.count[0] : 3);
        unsigned short y = (unsigned short)((opts.count.size() > 1)? opts.count[1] : ((opts.count.size() == 1)? x : 3));
        unsigned short z = (unsigned short)((opts.count.size() > 2)? opts.count[2] : 1);

        Geo_Grid grid("grid", mesh, Geo_Grid_Params(
            std::make_pair(x, spacingOr(opts, 0, defaultSpacing)),
            std::make_pair(y, spacingOr(opts, 1, defaultSpacing)),
            std::make_pair(z, spacingOr(opts, 2, defaultSpacing))
        ));
        success = reportResult(Geo_Model3D::exportModel(opts.output, &grid, formatId, opts.exportOpts));
    }
    else if(opts.construct == "chain"){
        Vec3f size = getMeshSize(mesh);
        Vec3f direction = (opts.hasDirection)? opts.direction : Vec3f({ std::max(size.data[0], 0.1F) * 1.25F, 0.0F, 0.0F });
        unsigned count = (opts.count.size() > 0)? opts.count[0] : 5;

        Geo_Chain chain("chain", mesh, direction, count);
        success = reportResult(Geo_Model3D::exportModel(opts.output, &chain, formatId, opts.exportOpts));
    }
    else if(opts.construct == "board"){
        unsigned short rows = (unsigned short)((opts.count.size() > 0)? opts.count[0] : 3);
        unsigned short cols = (unsigned short)((opts.count.size() > 1)? opts.count[1] : rows);

        Geo_Board board("board", rows, cols);
        success = reportResult(Geo_Model3D::exportModel(opts.output, &board, formatId, opts.exportOpts));
    }
    else std::cerr << "Unknown construct type: " << opts.construct << " (see --list)\n";

    delete mesh;
    return (success)? 0 : 1;
}
