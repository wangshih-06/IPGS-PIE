cmake_minimum_required(VERSION 3.16)
project(PlantSimulationSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Widgets OpenGL Network REQUIRED)
find_package(OpenGL REQUIRED)

# Eigen is vendored because it is header-only.
set(EIGEN_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/eigen)
if(NOT EXISTS ${EIGEN_INCLUDE_DIR}/Eigen/Dense)
    message(FATAL_ERROR "Eigen not found. Place Eigen in third_party/eigen")
endif()

add_library(PlantDataModel STATIC
    src/Plant/PlantModel.cpp
    include/Plant/PlantModel.h
    include/Plant/PlantTypes.h
    include/Algorithm/PlantNode.h
)
target_include_directories(PlantDataModel PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantDataModel PUBLIC Qt5::Core)

add_library(PlantLSystem STATIC
    src/Algorithm/LSystem.cpp
    src/Algorithm/TurtleInterpreter.cpp
    src/Algorithm/PlantSkeletonPresets.cpp
    include/Algorithm/LSystem.h
    include/Algorithm/TurtleInterpreter.h
    include/Algorithm/PlantSkeletonPresets.h
)
target_include_directories(PlantLSystem PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantLSystem PUBLIC PlantDataModel Qt5::Core)

add_library(PlantImplicitSurface STATIC
    src/Implicit/MetaballField.cpp
    include/Implicit/MetaballField.h
)
target_include_directories(PlantImplicitSurface PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantImplicitSurface PUBLIC PlantDataModel Qt5::Core)


# 第13周：质量点—PBD 植物物理模型
add_library(PlantPhysics STATIC
    src/Physics/PlantPhysicsSolver.cpp
    include/Physics/PlantPhysicsSolver.h
)
target_include_directories(PlantPhysics PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantPhysics PUBLIC PlantDataModel Qt5::Core)
# 第9/11周：生长时间模型 + 环境与向光性/向地性参数
add_library(PlantGrowth STATIC
    src/Engine/GrowthTimeline.cpp
    src/Engine/GrowthClock.cpp
    src/Engine/DynamicBranchingSystem.cpp
    src/Engine/EnvironmentParams.cpp
    src/Engine/GrowthEventManager.cpp
    src/Engine/GrowthKeyframeStore.cpp
    src/Engine/GrowthDataRecorder.cpp
    include/Engine/DynamicBranchingSystem.h
    include/Engine/EnvironmentParams.h
    include/Engine/GrowthEventManager.h
    include/Engine/GrowthKeyframeStore.h
    include/Engine/GrowthDataRecorder.h
    include/Engine/GrowthTimeline.h
    include/Engine/GrowthClock.h
    include/Engine/GrowthStateReport.h
)
target_include_directories(PlantGrowth PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantGrowth PUBLIC PlantDataModel Qt5::Core)

add_library(PlantGeometry STATIC
    src/Geometry/MarchingCubes.cpp
    src/Geometry/MeshProcessing.cpp
    src/Geometry/LeafGenerator.cpp
    src/Geometry/MeshExporter.cpp
    include/Geometry/MarchingCubes.h
    include/Geometry/MarchingCubesTables.h
    include/Geometry/MeshProcessing.h
    include/Geometry/LeafGenerator.h
    include/Geometry/MeshExporter.h
)
target_include_directories(PlantGeometry PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantGeometry PUBLIC PlantImplicitSurface Qt5::Core)

add_executable(PlantSimulationSystem
    src/main.cpp
    src/Engine/EngineWindow.cpp
    src/Engine/EngineWindow.h
    src/Engine/SimulationEngine.cpp
    include/Engine/SimulationEngine.h
    src/Networking/WebSocketServer.cpp
    src/Rendering/Camera.cpp
    src/Rendering/Light.h
    src/Rendering/Mesh.cpp
    src/Rendering/Shader.cpp
    src/Rendering/Renderer.cpp
)
target_include_directories(PlantSimulationSystem PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${EIGEN_INCLUDE_DIR}
)
target_link_libraries(PlantSimulationSystem PRIVATE
    PlantLSystem
    PlantImplicitSurface
    PlantGrowth
    PlantPhysics
    PlantGeometry
    Qt5::Widgets
    Qt5::OpenGL
    Qt5::Network
    OpenGL::GL
)

add_executable(PlantStructureDemo
    src/Tools/PlantStructureDemo.cpp
)
target_link_libraries(PlantStructureDemo PRIVATE PlantDataModel Qt5::Core)

add_executable(LSystemSkeletonDemo
    src/Tools/LSystemSkeletonDemo.cpp
)
target_link_libraries(LSystemSkeletonDemo PRIVATE PlantLSystem PlantDataModel Qt5::Core)

add_executable(MetaballFieldDemo
    src/Tools/MetaballFieldDemo.cpp
)
target_link_libraries(MetaballFieldDemo PRIVATE
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Widgets
    Qt5::Core
)

add_executable(MarchingCubesDemo
    src/Tools/MarchingCubesDemo.cpp
)
target_link_libraries(MarchingCubesDemo PRIVATE
    PlantGeometry
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(PlantModelExportDemo
    src/Tools/PlantModelExportDemo.cpp
)
target_link_libraries(PlantModelExportDemo PRIVATE
    PlantGeometry
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(GrowthTimelineDemo
    src/Tools/GrowthTimelineDemo.cpp
)
target_link_libraries(GrowthTimelineDemo PRIVATE
    PlantGrowth
    PlantImplicitSurface
    PlantLSystem
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(GrowthSurfaceCheck
    src/Tools/GrowthSurfaceCheck.cpp
)
target_link_libraries(GrowthSurfaceCheck PRIVATE
    PlantGrowth
    PlantImplicitSurface
    PlantLSystem
    PlantGeometry
    PlantDataModel
    Qt5::Gui
    Qt5::Core
)

add_executable(DynamicBranchingDemo
    src/Tools/DynamicBranchingDemo.cpp
)
target_link_libraries(DynamicBranchingDemo PRIVATE
    PlantGrowth
    PlantDataModel
    Qt5::Core
)


# 第13周：质量点、固定根节点与长度约束自动化验证 CLI
add_executable(PlantPhysicsDemo
    src/Tools/PlantPhysicsDemo.cpp
)
target_link_libraries(PlantPhysicsDemo PRIVATE
    PlantPhysics
    PlantDataModel
    Qt5::Core
)
# 第11周：向光性与向地性自动化验证 CLI
add_executable(TropismDemo
    src/Tools/TropismDemo.cpp
)
target_link_libraries(TropismDemo PRIVATE
    PlantGrowth
    PlantDataModel
    Qt5::Core
)

if(MSVC)
    target_compile_options(PlantDataModel PRIVATE /utf-8)
    target_compile_options(PlantLSystem PRIVATE /utf-8)
    target_compile_options(PlantImplicitSurface PRIVATE /utf-8)
    target_compile_options(PlantPhysics PRIVATE /utf-8)
    target_compile_options(PlantGrowth PRIVATE /utf-8)
    target_compile_options(PlantSimulationSystem PRIVATE /utf-8)
    target_compile_options(PlantStructureDemo PRIVATE /utf-8)
    target_compile_options(LSystemSkeletonDemo PRIVATE /utf-8)
    target_compile_options(MetaballFieldDemo PRIVATE /utf-8)
    target_compile_options(MarchingCubesDemo PRIVATE /utf-8)
    target_compile_options(PlantModelExportDemo PRIVATE /utf-8)
    target_compile_options(GrowthTimelineDemo PRIVATE /utf-8)
    target_compile_options(GrowthSurfaceCheck PRIVATE /utf-8)
    target_compile_options(DynamicBranchingDemo PRIVATE /utf-8)
    target_compile_options(PlantPhysicsDemo PRIVATE /utf-8)
    target_compile_options(TropismDemo PRIVATE /utf-8)
    target_compile_options(PlantGeometry PRIVATE /utf-8)
endif()
