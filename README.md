# Geometron

A geometry tool supporting primitive types, generative forms, and imported 3D models

###  Enviornment

The external dependencies being built are mainly included to deal with various codecs.

1. Assimp for loading 3D models from a wide range of formats

###  Project Structure:

1. **core** contains core functionality to work with basic 3D types
2. **plugin** contains extended functionality to generate or import complex 3D types
3. **support** contains definitions for vectors, matrices, and types used for physics
4. **exporter** contains Geo_Exporter, a command line tool that encodes a Geo_Mesh or Geo_Construct into a 3D file
7. **assets** contains 3D model files other resources
8. **scripts** contains build scripts used in the project

###  Geo_Exporter

Built alongside GEOLIB when Assimp is available (`SUPPORT_MODELS`). Generates a preset mesh, optionally arranges it inside a construct, and writes it in any format Assimp can export.

```
Geo_Exporter --mesh hexcone -o cone.obj
Geo_Exporter --mesh orb --segments 24,24 --radius 2 -f glb
Geo_Exporter --mesh quad3d --construct grid --count 4,4,2 --spacing 1.5 -o grid.fbx
Geo_Exporter --mesh circlecone --construct chain --count 6 --direction 0,0,1.2 -f stl
```

Run `Geo_Exporter --help` for every option, `--list` for preset meshes and constructs, and `--formats` for the available formats.
