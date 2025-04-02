#pragma once

#include "VectorMath.h"
#include "Color.h"
#include <cstdint>

// Emission Properties and other particle structs

__declspec(align(16)) struct FluidEmissionProperties
{
    XMFLOAT3 LastEmitPosW;
    float EmitSpeed;
    XMFLOAT3 EmitPosW;
    float FloorHeight;
    XMFLOAT3 EmitDirW;
    float Restitution;
    XMFLOAT3 EmitRightW;
    float EmitterVelocitySensitivity;
    XMFLOAT3 EmitUpW;
    std::uint32_t MaxParticles;
    XMFLOAT3 Gravity;
    std::uint32_t TextureID;
    XMFLOAT3 EmissiveColor;
    float pad1;
    XMUINT4 RandIndex[64];
};

FluidEmissionProperties* CreateFluidEmissionProperties();

struct FluidSpawnData
{
    float AgeRate;
    float RotationSpeed;
    float StartSize;
    float EndSize;
    XMFLOAT3 Velocity; float Mass;
    XMFLOAT3 SpreadOffset; float Random;
    Color StartColor;
    Color EndColor;
};


struct FluidMotion
{
    XMFLOAT3 Position;
    XMFLOAT3 PredictedPosition;
    float Mass;
    float Density;
    XMFLOAT3 Velocity;
    float Age;
    float Rotation;
    std::uint32_t ResetDataIndex;
    std::uint32_t LocationHash;
    std::uint32_t SortKey;
};

struct FluidVertex
{
    XMFLOAT3 Position;
    XMFLOAT4 Color;
    float Size;
    std::uint32_t TextureID;
};

struct FluidScreenData
{
    float Corner[2];
    float RcpSize[2];
    float Color[4];
    float Depth;
    float TextureIndex;
    float TextureLevel;
    std::uint32_t Bounds;
};

