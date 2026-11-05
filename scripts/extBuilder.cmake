# extBuilder.cmake
#
# Fetches, configures, builds and installs an external CMake project *while Geometron is being configured*,
# so the dependency is already installed when find_package() runs. (ExternalProject_Add only builds at
# build time, which is why the old scripts needed a second configure before OpenVDB/OpenMVG were found.)
#
# Every dependency is built with the same generator, platform (-A), toolset (-T) and compilers as Geometron,
# and installed into GEO_DEPS_PREFIX. Each dependency is rebuilt only when its recipe changes: a stamp file per
# configuration records what was built.
#
# Multi-config generators (Visual Studio) build every configuration in GEO_DEPS_CONFIGS (Release;Debug by default)
# into the same prefix, so a Debug Geometron links the Debug dependencies (/MDd) and a Release Geometron links the
# Release ones (/MD); mixing those runtimes crashes on Windows.

include_guard(GLOBAL)

find_package(Git REQUIRED)

get_property(GEO_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

set(GEO_DEPS_DIR "${CMAKE_BINARY_DIR}/DEPS" CACHE PATH
    "Directory where external dependencies are downloaded and built. Point several build trees at one short path (e.g. C:/geodeps) to share builds and avoid Windows path-length limits")
set(GEO_DEPS_PREFIX "${GEO_DEPS_DIR}/install" CACHE PATH "Install prefix for the external dependencies")

if(GEO_IS_MULTI_CONFIG)
    set(GEO_DEPS_CONFIGS "Release;Debug" CACHE STRING
        "Configurations the dependencies are built in; include every configuration you build Geometron in (RelWithDebInfo and MinSizeRel use Release)")
elseif(CMAKE_BUILD_TYPE) # single-config generators (Ninja, Makefiles) follow the Geometron build type
    set(GEO_DEPS_CONFIGS "${CMAKE_BUILD_TYPE}")
else()
    set(GEO_DEPS_CONFIGS "Release")
endif()

cmake_host_system_information(RESULT _geo_cores QUERY NUMBER_OF_LOGICAL_CORES)
cmake_host_system_information(RESULT _geo_memory_mb QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR _geo_memory_jobs "${_geo_memory_mb} / 4096") # some OpenVDB files need ~4 GB each to compile
if(_geo_memory_jobs LESS 1)
    set(_geo_memory_jobs 1)
endif()
if(_geo_memory_jobs LESS _geo_cores)
    set(_geo_cores ${_geo_memory_jobs})
endif()
set(GEO_DEPS_PARALLEL "" CACHE STRING "Parallel jobs used to build the dependencies (empty = logical cores, limited to one job per 4 GB of RAM)")
if(NOT GEO_DEPS_PARALLEL)
    set(GEO_DEPS_JOBS ${_geo_cores})
else()
    set(GEO_DEPS_JOBS ${GEO_DEPS_PARALLEL})
endif()

# Release-type Geometron configurations link the Release build of imported dependencies
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release;")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;Release;")

list(PREPEND CMAKE_PREFIX_PATH "${GEO_DEPS_PREFIX}")

# Arguments shared by every dependency so the binaries are ABI compatible with Geometron
function(_geo_common_args out_var)
    set(args
        -G "${CMAKE_GENERATOR}"
        "-DCMAKE_INSTALL_PREFIX:PATH=${GEO_DEPS_PREFIX}"
        "-DCMAKE_PREFIX_PATH:PATH=${GEO_DEPS_PREFIX}"
        "-DCMAKE_INSTALL_LIBDIR:STRING=lib" # keeps lib/ instead of lib64/ on some Linux distributions
        "-DCMAKE_POLICY_VERSION_MINIMUM:STRING=3.5" # lets CMake 4.x configure older bundled projects
        "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON" # static dependencies end up inside the GEOLIB shared library
        "-DBUILD_TESTING:BOOL=OFF"
    )
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(CMAKE_GENERATOR_INSTANCE)
        list(APPEND args "-DCMAKE_GENERATOR_INSTANCE:INTERNAL=${CMAKE_GENERATOR_INSTANCE}")
    endif()
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND args "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "Visual Studio") # Ninja / Makefiles must use the same compilers
        foreach(lang C CXX)
            if(CMAKE_${lang}_COMPILER)
                list(APPEND args "-DCMAKE_${lang}_COMPILER:FILEPATH=${CMAKE_${lang}_COMPILER}")
            endif()
        endforeach()
        if(CMAKE_MAKE_PROGRAM)
            list(APPEND args "-DCMAKE_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}")
        endif()
    endif()
    if(CMAKE_MSVC_RUNTIME_LIBRARY)
        list(APPEND args "-DCMAKE_MSVC_RUNTIME_LIBRARY:STRING=${CMAKE_MSVC_RUNTIME_LIBRARY}")
    endif()
    if(NOT GEO_IS_MULTI_CONFIG)
        list(APPEND args "-DCMAKE_BUILD_TYPE:STRING=${GEO_DEPS_CONFIGS}")
    endif()
    set(${out_var} "${args}" PARENT_SCOPE)
endfunction()

function(_geo_run step_name)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command_line)
        message(FATAL_ERROR "[deps] ${step_name} failed (exit ${result}). See the output above.\nCommand: ${command_line}")
    endif()
endfunction()

# geo_build_dependency(<name>
#     GIT_REPOSITORY <url> GIT_TAG <commit-or-tag>
#     [SOURCE_SUBDIR <dir>]          # where the top CMakeLists.txt lives inside the repository
#     [CMAKE_ARGS <args>...])        # project-specific cache settings
function(geo_build_dependency name)
    cmake_parse_arguments(DEP "" "GIT_REPOSITORY;GIT_TAG;SOURCE_SUBDIR" "CMAKE_ARGS" ${ARGN})

    string(TOLOWER "${name}" lname)
    set(src_dir "${GEO_DEPS_DIR}/src/${lname}")
    set(bin_dir "${GEO_DEPS_DIR}/build/${lname}")
    set(stamp_dir "${GEO_DEPS_PREFIX}/.geo_stamps")

    _geo_common_args(common_args)
    set(configure_args ${common_args} ${DEP_CMAKE_ARGS})

    # Everything that affects the binaries goes into the recipe signature (job counts do not)
    set(recipe_args ${configure_args})
    list(FILTER recipe_args EXCLUDE REGEX "^-DMSVC_MP_THREAD_COUNT")
    string(SHA1 recipe "${DEP_GIT_REPOSITORY}|${DEP_GIT_TAG}|${DEP_SOURCE_SUBDIR}|${recipe_args}|${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}")

    set(all_built TRUE)
    foreach(cfg IN LISTS GEO_DEPS_CONFIGS)
        set(stamp "${stamp_dir}/${lname}-${cfg}.stamp")
        set(stamp_value "")
        if(EXISTS "${stamp}")
            file(READ "${stamp}" stamp_value)
        endif()
        if(NOT stamp_value STREQUAL recipe)
            set(all_built FALSE)
        endif()
    endforeach()
    if(all_built)
        message(STATUS "[deps] ${name}: up to date (${GEO_DEPS_CONFIGS})")
        return()
    endif()

    message(STATUS "[deps] ${name}: building ${DEP_GIT_TAG} (${GEO_DEPS_CONFIGS}) - this runs once and can take a while")

    # 1. Fetch the exact commit/tag (shallow) plus submodules
    set(fetched_file "${src_dir}/.geo_fetched")
    set(fetched "")
    if(EXISTS "${fetched_file}")
        file(READ "${fetched_file}" fetched)
    endif()
    if(NOT fetched STREQUAL "${DEP_GIT_REPOSITORY}@${DEP_GIT_TAG}")
        file(REMOVE_RECURSE "${src_dir}")
        file(MAKE_DIRECTORY "${src_dir}")
        _geo_run("${name} git init" "${GIT_EXECUTABLE}" -C "${src_dir}" init -q)
        _geo_run("${name} git remote" "${GIT_EXECUTABLE}" -C "${src_dir}" remote add origin "${DEP_GIT_REPOSITORY}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${src_dir}" fetch --depth 1 origin "${DEP_GIT_TAG}" RESULT_VARIABLE fetch_result)
        if(NOT fetch_result EQUAL 0) # some servers refuse shallow fetches by commit id
            _geo_run("${name} git fetch" "${GIT_EXECUTABLE}" -C "${src_dir}" fetch --tags origin)
            _geo_run("${name} git checkout" "${GIT_EXECUTABLE}" -C "${src_dir}" -c advice.detachedHead=false checkout "${DEP_GIT_TAG}")
        else()
            _geo_run("${name} git checkout" "${GIT_EXECUTABLE}" -C "${src_dir}" -c advice.detachedHead=false checkout FETCH_HEAD)
        endif()
        _geo_run("${name} git submodules" "${GIT_EXECUTABLE}" -C "${src_dir}" submodule update --init --recursive)
        file(WRITE "${fetched_file}" "${DEP_GIT_REPOSITORY}@${DEP_GIT_TAG}")
    endif()

    # 2. Configure (fresh when the recipe changed)
    set(source_root "${src_dir}")
    if(DEP_SOURCE_SUBDIR)
        set(source_root "${src_dir}/${DEP_SOURCE_SUBDIR}")
    endif()
    set(configured_file "${bin_dir}/.geo_configured")
    set(configured "")
    if(EXISTS "${configured_file}")
        file(READ "${configured_file}" configured)
    endif()
    if(NOT configured STREQUAL recipe)
        file(REMOVE_RECURSE "${bin_dir}")
        file(MAKE_DIRECTORY "${bin_dir}")
        _geo_run("${name} configure" "${CMAKE_COMMAND}" -S "${source_root}" -B "${bin_dir}" ${configure_args})
        file(WRITE "${configured_file}" "${recipe}")
    endif()

    # 3. Build + install every configuration
    file(MAKE_DIRECTORY "${stamp_dir}")
    if(NOT GEO_IS_MULTI_CONFIG) # one configuration owns the installed files, forget any other
        file(GLOB old_stamps "${stamp_dir}/${lname}-*.stamp")
        if(old_stamps)
            file(REMOVE ${old_stamps})
        endif()
    endif()
    foreach(cfg IN LISTS GEO_DEPS_CONFIGS)
        message(STATUS "[deps] ${name}: building ${cfg}")
        _geo_run("${name} build (${cfg})" "${CMAKE_COMMAND}" --build "${bin_dir}" --config ${cfg} --target install --parallel ${GEO_DEPS_JOBS})
        file(WRITE "${stamp_dir}/${lname}-${cfg}.stamp" "${recipe}")
    endforeach()
endfunction()

# Shared libraries (.dll on Windows, .so/.dylib elsewhere) installed by the dependencies; refreshed every configure
function(geo_dependency_runtime_files out_var)
    if(WIN32)
        file(GLOB files "${GEO_DEPS_PREFIX}/bin/*.dll")
    elseif(APPLE)
        file(GLOB files "${GEO_DEPS_PREFIX}/lib/*.dylib")
    else()
        file(GLOB files "${GEO_DEPS_PREFIX}/lib/*.so*")
    endif()
    set(${out_var} "${files}" PARENT_SCOPE)
endfunction()

# Copies the dependency DLLs next to a target after it builds so it runs straight from the build tree (Windows)
function(geo_copy_dependency_runtime target)
    if(NOT WIN32)
        return()
    endif()
    geo_dependency_runtime_files(dlls)
    if(dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dlls} $<TARGET_FILE_DIR:${target}>
            COMMENT "Copying dependency DLLs next to ${target}"
            VERBATIM
        )
    endif()
endfunction()
