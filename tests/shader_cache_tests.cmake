# Exercise the actual engine CMake regeneration with no GPU or shader compiler.
if(NOT ROOT OR NOT TEST_DIR OR NOT TEST_DIR MATCHES "/shader-cache-tests$")
 message(FATAL_ERROR "ROOT and TEST_DIR are required")
endif()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run)
set(TEST_DIR "${TEST_DIR}/${run}")
set(source "${TEST_DIR}/source")
set(build "${TEST_DIR}/build")
file(MAKE_DIRECTORY "${source}")
file(COPY "${ROOT}/CMakeLists.txt" "${ROOT}/src" "${ROOT}/include" "${ROOT}/cmake" "${ROOT}/shaders" "${ROOT}/platform" DESTINATION "${source}")
file(APPEND "${source}/CMakeLists.txt" "\nadd_custom_target(retrato_shader_cache_probe DEPENDS \"\${CMAKE_CURRENT_BINARY_DIR}/generated/spirv_sources.hpp\")\n")
set(arguments -S "${source}" -B "${build}" -G "${GENERATOR}"
 "-DCMAKE_CXX_COMPILER=${CXX}" "-DCMAKE_C_COMPILER=${CC}"
 "-DVulkan_INCLUDE_DIR=${VULKAN_INCLUDE}" "-DGLSLANG=${CMAKE_COMMAND}"
 -DBUILD_SHARED_LIBS=OFF -DEMBER_VULKAN=ON -DEMBER_OPENGL=OFF
 -DEMBER_VIEWER=OFF -DEMBER_APP=OFF -DEMBER_STARTER=OFF -DEMBER_LUA=OFF
 -DEMBER_RMLUI=OFF -DEMBER_JOLT=OFF -DEMBER_NOVA=OFF -DEMBER_IMPORTERS=OFF
 -DEMBER_EXAMPLES=OFF -DEMBER_TOOLS=OFF -DEMBER_TESTS=OFF -DEMBER_VFX=OFF)
if(PLATFORM)
 list(APPEND arguments -A "${PLATFORM}")
endif()
if(TOOLSET)
 list(APPEND arguments -T "${TOOLSET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0 OR NOT EXISTS "${build}/generated/spirv_sources.hpp")
 message(FATAL_ERROR "Valid precompiled shader configuration failed: ${out}\n${err}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release --target retrato_shader_cache_probe RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT result EQUAL 0)
 message(FATAL_ERROR "Valid cached shader build failed: ${out}\n${err}")
endif()
# A new source timestamp must trigger reconfiguration and a compiler attempt.
# CMake is deliberately supplied as a fake glslang: -V must fail, rather than
# quietly using the old SPIR-V. No shader-tool installation is needed for this test.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1)
file(APPEND "${source}/shaders/scene.vert" "\n// invalidation regression\n")
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --config Release --target retrato_shader_cache_probe RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(result EQUAL 0)
 message(FATAL_ERROR "Edited GLSL reused stale SPIR-V without regeneration")
endif()
message(STATUS "PASS: cached SPIR-V works; a GLSL-only edit triggers regeneration")
