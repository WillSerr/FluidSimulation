#pragma once

#include "pch.h"
#include "GpuBuffer.h"
#include "FluidEffectProperties.h"
#include "FluidShaderStructs.h"

class FluidEffect
{
public:
    FluidEffect(FluidEffectProperties& effectProperties);
    void LoadDeviceResources(ID3D12Device* device);
    void Update(ComputeContext& CompContext, float timeDelta);
    void Init(ComputeContext& CompContext);
    float GetLifetime() { return m_EffectProperties.TotalActiveLifetime; }
    float GetElapsedTime() { return m_ElapsedTime; }
    void Reset();

private:

    void SpawnFluid(ComputeContext& CompContext);
    void ComputeFluidAdvection(ComputeContext& CompContext);
    void ComputeFluidDensity(ComputeContext& CompContext);
    void ComputeFluidVelocity(ComputeContext& CompContext);

    StructuredBuffer m_StateBuffers[2];
    uint32_t m_CurrentStateBuffer;

    //ParticleSystem::SpawnDataBuffer
    StructuredBuffer m_RandomStateBuffer;

    IndirectArgsBuffer m_DispatchIndirectArgs;
    IndirectArgsBuffer m_DrawIndirectArgs;

    FluidEffectProperties m_EffectProperties;
    FluidEffectProperties m_OriginalEffectProperties;
    float m_ElapsedTime;
    UINT m_effectID;

    bool m_initialised = false;
};

