
#include "pch.h"
#include "FluidShaderStructs.h"

FluidEmissionProperties* CreateFluidEmissionProperties()
{
    FluidEmissionProperties* emitProps = new FluidEmissionProperties;
    ZeroMemory(emitProps, sizeof(*emitProps));
    emitProps->EmitPosW = emitProps->LastEmitPosW = XMFLOAT3(0.0, 0.0, 0.0);
    emitProps->EmitDirW = XMFLOAT3(0.0, 0.0, 1.0);
    emitProps->EmitRightW = XMFLOAT3(1.0, 0.0, 0.0);
    emitProps->EmitUpW = XMFLOAT3(0.0, 1.0, 0.0);
    emitProps->Restitution = 0.6;
    emitProps->FloorHeight = -0.7;
    emitProps->EmitSpeed = 1.0;
    emitProps->Gravity = XMFLOAT3(0, -5, 0);
    emitProps->MaxParticles = 131072;
    return emitProps;
};
