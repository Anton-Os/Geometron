#ifndef GEO_MODEL_H

#ifdef TOPL_ENABLE_MODELS
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#endif

#include "Geo_Model_Node.hpp"

#include "Geo_Construct.hpp"

#ifdef TOPL_ENABLE_TEXTURES
unsigned linedTopl_Sampler_callback(double x, double y){ return (cos(y * 20) < 0.5)? 0xAA00FF00 : 0xAAFF00FF; }
#endif

class Geo_Model3D : public Geo_Construct<Geo_Model3D> {
public:
    Geo_Model3D(const std::string& prefix, const std::string& filePath) : _filePath(filePath), Geo_Construct(prefix){ // Non-Configured Constructor
        init();
    }
#ifdef TOPL_ENABLE_MODELS
    Geo_Model3D(const std::string& prefix, const std::string& filePath, Topl_Scene* scene) : _filePath(filePath), Geo_Construct(prefix){ // Configured Constructor
        init();
        configure(scene);
    }

    ~Geo_Model3D(){
        if(_nodes != nullptr){
            for(unsigned n = 0; n < _nodeCount; n++) delete(*(_nodes + n));
            free(_nodes);
            _nodes = nullptr;
        }
    }

    // Exports a single mesh, format is an Assimp format id or file extension (e.g. obj, stl, ply, fbx, glb, dae)
    static Geo_Model_ExportResult exportModel(const std::string& filePath, const Geo_Mesh* mesh, const std::string& format = "obj", const Geo_Model_ExportOpts& opts = Geo_Model_ExportOpts()){
        return exportModelItems(filePath, { Geo_Model_ExportItem("mesh", mesh) }, format, opts);
    }

    // Exports every visible actor of a construct as its own mesh, actor position, rotation, and scale are baked in
    template <typename ConstructType>
    static Geo_Model_ExportResult exportModel(const std::string& filePath, Geo_Construct<ConstructType>* construct, const std::string& format = "obj", const Geo_Model_ExportOpts& opts = Geo_Model_ExportOpts()){
        std::vector<Geo_Model_ExportItem> items;
        for(unsigned n = 0; n < construct->getActorCount(); n++){
            Geo_Actor* actor = construct->getGeoActor(n);
            if(actor == nullptr || actor->getMesh() == nullptr || !actor->isShown) continue;

            Geo_Model_ExportItem item(construct->getPrefix() + ((actor->getName() != DEFAULT_ACTOR_NAME)? actor->getName() : std::to_string(n + 1)), actor->getMesh());
            item.position = *actor->getPos();
            item.rotation = *actor->getRot();
            item.scale = *actor->getSize();
            items.push_back(item);
        }
        return exportModelItems(filePath, items, format, opts);
    }

    void init() override {
        const unsigned aiFlags = aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType;

        const aiScene* aiScene = importer.ReadFile(_filePath, aiFlags);
        if(aiScene == nullptr || aiScene->mRootNode == nullptr) return; // file could not be loaded

        _nodeCount = aiScene->mRootNode->mNumChildren;
        _nodes = (Geo_Model_Node**)malloc(_nodeCount * sizeof(Geo_Model_Node*));
        for (unsigned n = 0; n < _nodeCount; n++) // iterates through all nodes
            *(_nodes + n) = new Geo_Model_Node(aiScene, aiScene->mRootNode->mChildren[n]);

        for (unsigned n = 0; n < _nodeCount; n++) {
            Geo_Model_Node* currentNode = *(_nodes + n);
            if (currentNode->getMesh()->getMeshCount() > NO_NODE_COUNT) {
                _geoNodes.push_back(currentNode);
                _geoActors.push_back(Geo_Actor(*currentNode)); // replace the _geoNodes member
#ifdef TOPL_ENABLE_TEXTURES
                _samplerMap.insert({ _geoNodes[n], Topl_Sampler_2D() });
                _samplerMap.at(_geoNodes[n]) = Topl_Sampler_Color(RAND_COLOR()); // .setColorImage(RAND_COLOR()); // .setImage(modelImg);
#endif
            }
        }
    }

    void configure(Topl_Scene* scene) override {
        for (unsigned n = 0; n < _geoNodes.size(); n++) {
                scene->addGeometry(getPrefix() + _geoActors[n].getName(), _geoNodes[n]);
#ifdef TOPL_ENABLE_TEXTURES
                scene->addTexture(getPrefix() + _geoActors[n].getName(), &_samplerMap.at(_geoNodes[n]));
#endif
        }
    }

    unsigned getActorCount() const { return _geoNodes.size(); }
    // Geo_Actor* getGeoActor(unsigned short a){ return (Geo_Actor*)&_geoNodes[a]; }
    
    void shift(Vec3f vec) { // replace Geo_Construct shift()
		for (std::vector<Geo_Model_Node*>::iterator n = _geoNodes.begin(); n != _geoNodes.end(); n++)
			(*n)->updatePos({ vec });
	}
    void rotateAll(Vec3f angles){ // piecewise rotation of all actors
        for(unsigned g = 0; g < _geoNodes.size(); g++) _geoNodes[g]->updateRot(angles);
    }
protected:
	std::vector<Geo_Model_Node*> _geoNodes; // geometry nodes only
#ifdef TOPL_ENABLE_TEXTURES
    Topl_Sampler_2D* getImgAt(unsigned short i){ return (i < _geoNodes.size())? &_samplerMap[_geoNodes[i]] : nullptr; }
    std::map<const Geo_Model_Node*, Topl_Sampler_2D> _samplerMap; // child backgrounds
#endif
	// TODO: include animation and other relevant data
private:
    Assimp::Importer importer;

    Geo_Model_Node** _nodes = nullptr; // all nodes data
    unsigned _nodeCount = 0; // all nodes count
#else
    void configure(Topl_Scene* scene) override {  }
#endif
    std::string _filePath; // nodes are read from file
};

#define GEO_MODEL_H
#endif
