#ifndef Geo_Model_Node_H

#include <assimp/scene.h>

#include "Geo_Actor.hpp"

// Node Mesh

#define MESH_SCALE 0.5F

enum MESH_Attribute {
	MESH_Vertex,
	MESH_Index
	// MESH_Bone
};

#ifdef __cplusplus
extern "C" {
#endif

DllExport unsigned getMeshAttribCount(const aiMesh* mesh, MESH_Attribute attrib); // for singular mesh
DllExport unsigned getMeshesAttribCount(const std::vector<const aiMesh*>& meshes, MESH_Attribute attrib); // for multiple meshes

// DllExport Geo_Vertex* genModel_vertices(std::string modelPath, unsigned short meshIndex); // TODO: Include functions that are bindable for vertex generation
// DllExport unsigned* genModel_indices(std::string modelPath, unsigned short meshIndex); // TODO: Include functions that are bindable for index generation

#ifdef __cplusplus

class Geo_Model_Mesh : public Geo_Mesh {
public:
	DllExport Geo_Model_Mesh(const aiMesh* mesh) : Geo_Mesh(
		getMeshAttribCount(mesh, MESH_Vertex),
		getMeshAttribCount(mesh, MESH_Index)
	){
		_assimpMeshes = { mesh };
		init();
	}

	DllExport Geo_Model_Mesh(const std::vector<const aiMesh*>& meshes) : Geo_Mesh(
		getMeshesAttribCount(meshes, MESH_Vertex),
		getMeshesAttribCount(meshes, MESH_Index)
	) {
		_assimpMeshes = std::vector<const aiMesh*>(meshes.begin(), meshes.end());
		init();
	}

	DllExport unsigned short getMeshCount() const { return _assimpMeshes.size(); }
private:
	DllExport void init();

	std::vector<const aiMesh*> _assimpMeshes;
};

// Node Actor

#define NO_NODE_COUNT 0
#define SINGLE_NODE_COUNT 1

class Geo_Model_Node : public Geo_Actor {
public:
	DllExport Geo_Model_Node() : Geo_Actor(){} // Empty Constructor
    DllExport Geo_Model_Node(const aiScene* scene, const aiNode* node) : Geo_Actor(){ 
		if (scene != nullptr && node != nullptr) init(scene, node); 
	}
	~Geo_Model_Node() { if(_mesh != nullptr) delete(_mesh);}

	DllExport const Geo_Model_Mesh* getMesh() const { return _mesh; }
private:
	DllExport void init(const aiScene* scene, const aiNode* node);
    const aiScene* _sceneRef = nullptr;
    const aiNode* _nodeRef = nullptr;

	Geo_Model_Mesh* _mesh = nullptr;
};

#endif

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// Model Exporting

#include <string>
#include <vector>

enum EXPORT_Normals {
	EXPORT_Normals_Smooth = 0, // averaged per-vertex normals computed from triangles
	EXPORT_Normals_Flat = 1, // faceted normals, vertices are split per triangle
	EXPORT_Normals_Mesh = 2, // normals stored inside the Geo_Vertex data
	EXPORT_Normals_None = 3 // no normals are written
};

struct Geo_Model_ExportOpts {
	EXPORT_Normals normals = EXPORT_Normals_Smooth;
	bool texcoords = true; // writes Geo_Vertex texcoords as first uv channel
	bool colors = false; // writes Geo_Vertex colors as first color channel
	bool merge = false; // combines all items into a single mesh
};

struct Geo_Model_ExportItem { // mesh with placement, transforms are baked into exported vertices
	Geo_Model_ExportItem(){}
	Geo_Model_ExportItem(const std::string& n, const Geo_Mesh* m) : name(n), mesh(m){}

	std::string name = "mesh";
	const Geo_Mesh* mesh = nullptr;
	Vec3f position = VEC_3F_ZERO;
	Vec3f rotation = VEC_3F_ZERO; // same convention as rotateTForm()
	Vec3f scale = VEC_3F_ONES;
};

struct Geo_Model_ExportResult {
	bool success = false;
	std::string message; // error or summary message
	unsigned meshCount = 0, vertexCount = 0, faceCount = 0;
	unsigned droppedFaces = 0; // triangles skipped for invalid or degenerate indices
};

DllExport std::string getExportFormatId(const std::string& format); // resolves format id or file extension, empty if unsupported
DllExport std::string getExportFormatExt(const std::string& formatId); // file extension for a format id
DllExport std::vector<std::string> getExportFormats(); // lines of "id  .ext  description"

DllExport Geo_Model_ExportResult exportModelItems(
	const std::string& filePath, const std::vector<Geo_Model_ExportItem>& items,
	const std::string& format, const Geo_Model_ExportOpts& opts
);
#endif

#define Geo_Model_Node_H
#endif