# Geometron

A geometry tool supporting primitive types, generative forms, and imported 3D models

###  Enviornment

The external dependencies being built are mainly included to deal with various codecs.

1. Assimp for loading 3D models from a wide range of formats
2. OpenVDB (with oneTBB) for algorithmic formations, enabled with `SUPPORT_ALGOFORMS`
3. OpenMVG for photographic reconstruction, enabled with `SUPPORT_PHOTOGRAM`

OpenVDB, oneTBB and OpenMVG are downloaded and built automatically the first time CMake configures with their option on (`scripts/extBuilder.cmake`). They are built with the same generator and compiler as Geometron and installed into `build/DEPS/install`, so the first configure takes a while (OpenVDB alone can take 30+ minutes); later configures reuse them. OpenMVG uses its bundled Eigen, Ceres, cereal, Clp/Osi, lemon and jpeg/png/tiff/zlib, so nothing needs to be installed by hand.

Requirements: CMake 3.24+, Git, and Visual Studio 2022 (MSVC 19.30+) on Windows. Build for x64.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DSUPPORT_ALGOFORMS=ON -DSUPPORT_PHOTOGRAM=ON
cmake --build build --config Release
build\Release\Geo_DepCheck.exe
```

Useful cache options:

* `GEO_DEPS_DIR` puts downloads and builds somewhere else; a short path such as `C:/geodeps` avoids Windows path-length limits and can be shared by several build folders
* `GEO_DEPS_CONFIGS` (Visual Studio) lists the configurations the dependencies are built in, `Release;Debug` by default; drop `Debug` to halve the first build
* `GEO_DEPS_PARALLEL` sets the number of parallel jobs; by default it uses every core but at most one job per 4 GB of RAM, because some OpenVDB files need about that much to compile
* `GEO_BUILD_DEPENDENCIES=OFF` uses your own installs instead (for example a vcpkg toolchain, `OpenVDB_ROOT` or `OpenMVG_DIR`)

`Geo_DepCheck` meshes an OpenVDB sphere and runs an OpenMVG triangulation plus image codec round trip; it exits with 0 when everything works (also runnable with `ctest`). The required DLLs are copied next to the executables after each build and installed into `bin`.

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
