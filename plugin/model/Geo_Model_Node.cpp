// #ifdef TOPL_ENABLE_MODELS

#include <algorithm>
#include <cctype>
#include <cmath>

#include <assimp/Exporter.hpp>

#include "Geo_Model_Node.hpp"

// Node Mesh

unsigned getMeshAttribCount(const aiMesh* mesh, MESH_Attribute attrib){
	unsigned count = 0;

	switch(attrib){
		case MESH_Vertex: return mesh->mNumVertices; // gets vertex count for single mesh
		case MESH_Index: // gets index count for single mesh
			for(unsigned f = 0; f < mesh->mNumFaces; f++){
				const aiFace* face = mesh->mFaces + f;
				count += face->mNumIndices;
			}
			return count;
		default: return 0; // Attribute not available
	}
}

unsigned getMeshesAttribCount(const std::vector<const aiMesh*>& meshes, MESH_Attribute attrib){
	unsigned count = 0;
	
	switch(attrib){
		case MESH_Vertex: // gets vertex count for multiple meshes
			for(std::vector<const aiMesh*>::const_iterator mesh = meshes.cbegin(); mesh < meshes.cend(); mesh++)
				count += getMeshAttribCount(*mesh, MESH_Vertex);
			return count;
		case MESH_Index: // gets index count for multiple meshes
			for(std::vector<const aiMesh*>::const_iterator mesh = meshes.cbegin(); mesh < meshes.cend(); mesh++)
				count += getMeshAttribCount(*mesh, MESH_Index);
			return count;
		default: return 0; // Attribute not available
	}
}

void Geo_Model_Mesh::init(){
	for (std::vector<const aiMesh*>::const_iterator mesh = _assimpMeshes.cbegin(); mesh < _assimpMeshes.cend(); mesh++)
		for (unsigned v = 0; v < _vertices.size(); v++) {
			Vec3f pos, normal;
			Vec3f texcoord;
			if ((*mesh)->HasPositions()) {
				aiVector3D* attrib = (*mesh)->mVertices + v;
				// pos = Vec3f({ attrib->x, attrib->y, attrib->z });
				pos = Vec3f({ attrib->x, attrib->y, attrib->z }) * MESH_SCALE; // scale down for testing
			}
			if ((*mesh)->HasNormals()) {
				aiVector3D* attrib = (*mesh)->mNormals + v;
				normal = Vec3f({ attrib->x, attrib->y, attrib->z });
			}
			if ((*mesh)->HasTextureCoords(0)) {
				aiVector3D* attrib = *((*mesh)->mTextureCoords) + v;
				texcoord = Vec3f({ attrib->x, attrib->y, attrib->z });
			}

			_vertices[v] = Geo_Vertex(pos, texcoord, normal, Vec3f({ 1.0F, 1.0F, 1.0F }));
		}

	unsigned i = 0;

	for (std::vector<const aiMesh*>::const_iterator mesh = _assimpMeshes.cbegin(); mesh < _assimpMeshes.cend(); mesh++)
		if ((*mesh)->HasFaces())
			for (unsigned f = 0; f < (*mesh)->mNumFaces; f++) {
				const aiFace* face = (*mesh)->mFaces + f;
				for (unsigned n = 0; n < face->mNumIndices; n++) {
					_indices[i] = *(face->mIndices + n); // index in current face
					i++;
				}
			}
}

// Node Actor

void Geo_Model_Node::init(const aiScene* scene, const aiNode* node) {
	_sceneRef = scene;
	_nodeRef = node;

	setName(std::string(_nodeRef->mName.C_Str()));
	setPos({ _nodeRef->mTransformation.a4 * MESH_SCALE, _nodeRef->mTransformation.b4 * MESH_SCALE - 1.0F, _nodeRef->mTransformation.c4 * MESH_SCALE });
	setRot({ 0.0f, 0.0f, 0.0f}); // TODO: Determine values from transformation matrix
	setSize({ _nodeRef->mTransformation.a1, _nodeRef->mTransformation.b2, _nodeRef->mTransformation.c3 });
	
	if (_nodeRef->mNumMeshes == SINGLE_NODE_COUNT) {
		const aiMesh* refMesh = *(_sceneRef->mMeshes + *(_nodeRef->mMeshes));
		_mesh = new Geo_Model_Mesh(refMesh);
	} else if(_nodeRef->mNumMeshes > SINGLE_NODE_COUNT){
		std::vector<const aiMesh*> refMeshes = std::vector<const aiMesh*>(_nodeRef->mNumMeshes);
		for(unsigned m = 0; m < _nodeRef->mNumMeshes; m++) // iterates through all meshes within current node
			refMeshes[m] = *(_sceneRef->mMeshes + *(_nodeRef->mMeshes + m)); 
		_mesh = new Geo_Model_Mesh(refMeshes);
	}

	if(_mesh != nullptr) setMesh((Geo_Mesh*)_mesh);
}

// Model Exporting

struct Export_Buffer { // intermediate geometry before conversion into an aiMesh
	std::vector<aiVector3D> positions, normals, texcoords;
	std::vector<aiColor4D> colors;
	std::vector<unsigned> indices; // triangle list
};

static aiVector3D toAiVec(Vec3f vec){ return aiVector3D(vec.data[0], vec.data[1], vec.data[2]); }

static aiVector3D safeNormalize(aiVector3D vec){
	float length = vec.Length();
	return (length > 0.0F && std::isfinite(length))? vec / length : aiVector3D(0.0F, 0.0F, 0.0F);
}

static Vec3f transformPos(Vec3f pos, const Geo_Model_ExportItem& item){
	pos = scaleTForm(pos, item.scale);
	if(!item.rotation.isZero()) pos = rotateTForm(pos, item.rotation);
	return shiftTForm(pos, item.position);
}

static Vec3f transformNormal(Vec3f normal, const Geo_Model_ExportItem& item){
	normal = Vec3f({ normal.data[0] / ((item.scale.data[0] != 0.0F)? item.scale.data[0] : 1.0F), normal.data[1] / ((item.scale.data[1] != 0.0F)? item.scale.data[1] : 1.0F), normal.data[2] / ((item.scale.data[2] != 0.0F)? item.scale.data[2] : 1.0F) });
	return (!item.rotation.isZero())? rotateTForm(normal, item.rotation) : normal;
}

static bool isFiniteVec(Vec3f vec){ return std::isfinite(vec.data[0]) && std::isfinite(vec.data[1]) && std::isfinite(vec.data[2]); }

// Appends item geometry into buffer, returns number of triangles added
static unsigned appendItem(Export_Buffer& buffer, const Geo_Model_ExportItem& item, const Geo_Model_ExportOpts& opts, unsigned* droppedFaces){
	const Geo_Mesh* mesh = item.mesh;
	if(mesh == nullptr || mesh->getVertexCount() == 0) return 0;

	const unsigned vCount = mesh->getVertexCount();
	const Geo_Vertex* vertices = mesh->getVertices();
	const unsigned iCount = (mesh->getIndexCount() > 0)? mesh->getIndexCount() : vCount;
	const unsigned* meshIndices = (mesh->getIndexCount() > 0)? mesh->getIndices() : nullptr;

	std::vector<unsigned> triangles; // validated triangle list in mesh vertex space
	for(unsigned i = 0; i + 2 < iCount; i += 3){
		unsigned t[3];
		for(unsigned n = 0; n < 3; n++) t[n] = (meshIndices != nullptr)? *(meshIndices + i + n) : i + n;

		bool isValid = t[0] < vCount && t[1] < vCount && t[2] < vCount && t[0] != t[1] && t[1] != t[2] && t[0] != t[2];
		if(isValid) for(unsigned n = 0; n < 3; n++) isValid = isValid && isFiniteVec((vertices + t[n])->position);
		if(isValid){
			aiVector3D a = toAiVec((vertices + t[0])->position), b = toAiVec((vertices + t[1])->position), c = toAiVec((vertices + t[2])->position);
			isValid = ((b - a) ^ (c - a)).Length() > 0.0F; // excludes zero area triangles
		}

		if(isValid) triangles.insert(triangles.end(), t, t + 3);
		else if(droppedFaces != nullptr) (*droppedFaces)++;
	}
	if(triangles.empty()) return 0;

	const bool isFlat = opts.normals == EXPORT_Normals_Flat;
	const unsigned base = buffer.positions.size();
	std::vector<int> remap(vCount, -1); // compacts to referenced vertices only
	std::vector<unsigned> local; // triangle list in buffer space

	for(unsigned i = 0; i < triangles.size(); i++){
		unsigned v = triangles[i];
		if(isFlat || remap[v] < 0){
			const Geo_Vertex* vertex = vertices + v;
			if(!isFlat) remap[v] = buffer.positions.size();
			local.push_back(buffer.positions.size());

			buffer.positions.push_back(toAiVec(transformPos(vertex->position, item)));
			buffer.texcoords.push_back(aiVector3D(vertex->texcoord.data[0], vertex->texcoord.data[1], 0.0F));
			buffer.colors.push_back(aiColor4D(vertex->color.data[0], vertex->color.data[1], vertex->color.data[2], 1.0F));
			buffer.normals.push_back((opts.normals == EXPORT_Normals_Mesh)? safeNormalize(toAiVec(transformNormal(vertex->normal, item))) : aiVector3D(0.0F, 0.0F, 0.0F));
		} else local.push_back((unsigned)remap[v]);
	}

	if(opts.normals == EXPORT_Normals_Smooth || opts.normals == EXPORT_Normals_Flat){ // area weighted face normals
		for(unsigned i = 0; i < local.size(); i += 3){
			const aiVector3D& a = buffer.positions[local[i]];
			aiVector3D faceNormal = (buffer.positions[local[i + 1]] - a) ^ (buffer.positions[local[i + 2]] - a);
			for(unsigned n = 0; n < 3; n++) buffer.normals[local[i + n]] += faceNormal;
		}
		for(unsigned v = base; v < buffer.normals.size(); v++) buffer.normals[v] = safeNormalize(buffer.normals[v]);
	}

	buffer.indices.insert(buffer.indices.end(), local.begin(), local.end());
	return local.size() / 3;
}

static aiMesh* genAssimp_mesh(const Export_Buffer& buffer, const std::string& name, const Geo_Model_ExportOpts& opts){
	aiMesh* mesh = new aiMesh();
	mesh->mName = aiString(name);
	mesh->mMaterialIndex = 0;
	mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;

	mesh->mNumVertices = buffer.positions.size();
	mesh->mVertices = new aiVector3D[mesh->mNumVertices];
	std::copy(buffer.positions.begin(), buffer.positions.end(), mesh->mVertices);
	if(opts.normals != EXPORT_Normals_None){
		mesh->mNormals = new aiVector3D[mesh->mNumVertices];
		std::copy(buffer.normals.begin(), buffer.normals.end(), mesh->mNormals);
	}
	if(opts.texcoords){
		mesh->mNumUVComponents[0] = 2;
		mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
		std::copy(buffer.texcoords.begin(), buffer.texcoords.end(), mesh->mTextureCoords[0]);
	}
	if(opts.colors){
		mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];
		std::copy(buffer.colors.begin(), buffer.colors.end(), mesh->mColors[0]);
	}

	mesh->mNumFaces = buffer.indices.size() / 3;
	mesh->mFaces = new aiFace[mesh->mNumFaces];
	for(unsigned f = 0; f < mesh->mNumFaces; f++){
		aiFace* face = mesh->mFaces + f;
		face->mNumIndices = 3;
		face->mIndices = new unsigned[3];
		for(unsigned n = 0; n < 3; n++) face->mIndices[n] = buffer.indices[(f * 3) + n];
	}

	return mesh;
}

static std::string toLower(std::string str){
	std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c){ return (char)std::tolower(c); });
	return str;
}

std::string getExportFormatId(const std::string& format){
	std::string target = toLower(format);
	while(!target.empty() && target[0] == '.') target.erase(0, 1);
	if(target.empty()) return "";

	Assimp::Exporter exporter;
	const size_t formatCount = exporter.GetExportFormatCount();
	const char* aliases[][2] = { // preferred ids for common extensions
		{ "glb", "glb2" }, { "gltf", "gltf2" }, { "dae", "collada" }, { "json", "assjson" }
	};
	for(unsigned a = 0; a < sizeof(aliases) / sizeof(aliases[0]); a++)
		if(target == aliases[a][0])
			for(size_t f = 0; f < formatCount; f++)
				if(std::string(aliases[a][1]) == exporter.GetExportFormatDescription(f)->id) return aliases[a][1];

	for(size_t f = 0; f < formatCount; f++) // exact id match
		if(target == toLower(exporter.GetExportFormatDescription(f)->id)) return exporter.GetExportFormatDescription(f)->id;
	for(size_t f = 0; f < formatCount; f++) // file extension match
		if(target == toLower(exporter.GetExportFormatDescription(f)->fileExtension)) return exporter.GetExportFormatDescription(f)->id;
	return "";
}

std::string getExportFormatExt(const std::string& formatId){
	Assimp::Exporter exporter;
	for(size_t f = 0; f < exporter.GetExportFormatCount(); f++)
		if(formatId == exporter.GetExportFormatDescription(f)->id) return exporter.GetExportFormatDescription(f)->fileExtension;
	return "";
}

std::vector<std::string> getExportFormats(){
	std::vector<std::string> formats;
	Assimp::Exporter exporter;
	for(size_t f = 0; f < exporter.GetExportFormatCount(); f++){
		const aiExportFormatDesc* desc = exporter.GetExportFormatDescription(f);
		std::string line = desc->id;
		line.resize(std::max<size_t>(line.size() + 1, 12), ' ');
		line += std::string(".") + desc->fileExtension;
		line.resize(std::max<size_t>(line.size() + 1, 22), ' ');
		formats.push_back(line + desc->description);
	}
	return formats;
}

Geo_Model_ExportResult exportModelItems(const std::string& filePath, const std::vector<Geo_Model_ExportItem>& items, const std::string& format, const Geo_Model_ExportOpts& opts){
	Geo_Model_ExportResult result;

	std::string formatId = getExportFormatId(format);
	if(formatId.empty()){
		result.message = "Unsupported export format: " + format;
		return result;
	}

	std::vector<aiMesh*> meshes;
	std::vector<std::string> names;
	Export_Buffer mergeBuffer;
	for(std::vector<Geo_Model_ExportItem>::const_iterator item = items.begin(); item != items.end(); item++){
		if(opts.merge) appendItem(mergeBuffer, *item, opts, &result.droppedFaces);
		else {
			Export_Buffer buffer;
			if(appendItem(buffer, *item, opts, &result.droppedFaces) > 0){
				meshes.push_back(genAssimp_mesh(buffer, item->name, opts));
				names.push_back(item->name);
			}
		}
	}
	if(opts.merge && !mergeBuffer.indices.empty()){
		std::string name = (items.size() == 1)? items.front().name : "merged";
		meshes.push_back(genAssimp_mesh(mergeBuffer, name, opts));
		names.push_back(name);
	}
	if(meshes.empty()){
		result.message = "No valid triangles to export";
		return result;
	}

	aiScene* scene = new aiScene(); // owns and frees all meshes, materials, and nodes
	scene->mNumMeshes = meshes.size();
	scene->mMeshes = new aiMesh*[scene->mNumMeshes];
	std::copy(meshes.begin(), meshes.end(), scene->mMeshes);

	aiMaterial* material = new aiMaterial();
	aiString materialName("Geo_Material");
	aiColor3D diffuseColor(0.8F, 0.8F, 0.8F);
	material->AddProperty(&materialName, AI_MATKEY_NAME);
	material->AddProperty(&diffuseColor, 1, AI_MATKEY_COLOR_DIFFUSE);
	scene->mNumMaterials = 1;
	scene->mMaterials = new aiMaterial*[1];
	scene->mMaterials[0] = material;

	scene->mRootNode = new aiNode("Geometron");
	scene->mRootNode->mNumChildren = meshes.size();
	scene->mRootNode->mChildren = new aiNode*[meshes.size()];
	for(unsigned m = 0; m < meshes.size(); m++){
		aiNode* node = new aiNode(names[m]);
		node->mParent = scene->mRootNode;
		node->mNumMeshes = 1;
		node->mMeshes = new unsigned[1];
		node->mMeshes[0] = m;
		scene->mRootNode->mChildren[m] = node;

		result.vertexCount += meshes[m]->mNumVertices;
		result.faceCount += meshes[m]->mNumFaces;
	}
	result.meshCount = meshes.size();

	Assimp::Exporter exporter;
	result.success = exporter.Export(scene, formatId, filePath) == aiReturn_SUCCESS;
	result.message = (result.success)? "Exported " + filePath + " as " + formatId : std::string("Export failed: ") + exporter.GetErrorString();

	delete scene;
	return result;
}

// #endif
