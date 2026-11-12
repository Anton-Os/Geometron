# Geometron

A geometry tool supporting primitive types, generative forms, and imported 3D models

###  Enviornment

The external dependencies being built are mainly included to deal with various codecs.

1. Assimp for loading 3D models from a wide range of formats
2. OpenVDB (optional, `SUPPORT_ALGOFORMS`) for converting between meshes and volume grids inside of plugin/form
3. OpenMVG (optional, `SUPPORT_PHOTOGRAM`) for cameras solved from photos inside of plugin/recon

###  Project Structure:

1. **core** contains core functionality to work with basic 3D types
2. **plugin** contains extended functionality to generate or import complex 3D types
3. **support** contains definitions for vectors, matrices, and types used for physics
4. **exporter** contains Geo_Exporter, a command line tool that encodes a Geo_Mesh or Geo_Construct into a 3D file
7. **assets** contains 3D model files other resources
8. **scripts** contains build scripts used in the project

###  Plugins

Sources inside of **plugin/form** and **plugin/recon** are left out of GEOLIB by default and need no external libraries.

- `SUPPORT_FORMS` builds plugin/form, include `form/Geo_Forms.hpp`
  - `Geo_Freeform` culls a lattice of points, each point spawns vertices or indices from the points beside it
  - `Geo_Voxelform` culls a grid of cells, each cell gets a primitive, a copy of a mesh, or geometry from a callback
  - `Geo_Composite` combines two or more closed meshes as a union, difference, intersection, or xor
  - `Geo_Recursive` builds Menger sponges, Koch shapes, and custom self similar forms
- `SUPPORT_RECON` builds plugin/recon, include `recon/Geo_Heightmap.hpp` or `recon/Geo_Scanmap.hpp`
  - `Geo_Heightmap` raises a grid from heights, optionally closed into a solid
  - `Geo_Scanmap` rebuilds a mesh from outlines or depths seen from cameras around a subject

`SUPPORT_ALGOFORMS` and `SUPPORT_PHOTOGRAM` build the same sources, then define `GEO_ENABLE_OPENVDB` and `GEO_ENABLE_OPENMVG` once the libraries are found. Routines that take `Rasteron_Image`, `Rasteron_Queue`, or `Rasteron_Heightmap` only exist in sources compiled with `RASTERON_H` or `TOPL_ENABLE_TEXTURES` defined, Geometron itself does not link against Rasteron.

###  Geo_Exporter

Built alongside GEOLIB when Assimp is available (`SUPPORT_MODELS`). Generates a preset mesh, optionally arranges it inside a construct, and writes it in any format Assimp can export.

```
Geo_Exporter --mesh hexcone -o cone.obj
Geo_Exporter --mesh orb --segments 24,24 --radius 2 -f glb
Geo_Exporter --mesh quad3d --construct grid --count 4,4,2 --spacing 1.5 -o grid.fbx
Geo_Exporter --mesh circlecone --construct chain --count 6 --direction 0,0,1.2 -f stl
```

Run `Geo_Exporter --help` for every option, `--list` for preset meshes and constructs, and `--formats` for the available formats.
