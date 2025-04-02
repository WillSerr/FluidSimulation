#include "FluidEffect.h"

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  Julia Careaga
//             James Stanard
//

#include "pch.h"
#include "FluidEffect.h"
#include "CommandContext.h"
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "FluidEffectManager.h"
#include "GameInput.h"
#include "Math/Random.h"

using namespace Math;
using namespace FluidEffectManager;

namespace FluidEffectManager
{
    extern ComputePSO s_FluidSpawnCS;
    extern ComputePSO s_FluidUpdateCS;
    extern ComputePSO s_FluidAdvectionCS;
    extern ComputePSO s_FluidSortByLocationCS;
    extern ComputePSO s_FluidDensityCS;
    extern ComputePSO s_FluidVelocityCS;
    extern ComputePSO s_FluidDispatchIndirectArgsCS;
    extern StructuredBuffer SpriteVertexBuffer;
    extern RandomNumberGenerator s_RNG;
}

FluidEffect::FluidEffect(FluidEffectProperties& effectProperties)
{
    m_ElapsedTime = 0.0;
    m_EffectProperties = effectProperties;
}

inline static Color RandColor(Color c0, Color c1)
{
    return Color(1.f, 1.f, 1.f);
}

inline static XMFLOAT3 RandSpread(const XMFLOAT3& s)
{

    return XMFLOAT3(0.f, 1.f, 0.f);

}

void FluidEffect::LoadDeviceResources(ID3D12Device* device)
{
    (device); // Currently unused.  May be useful with multi-adapter support.

    m_OriginalEffectProperties = m_EffectProperties; //In case we want to reset

    //Fill particle spawn data buffer
    FluidSpawnData* pSpawnData = (FluidSpawnData*)_malloca(m_EffectProperties.EmitProperties.MaxParticles * sizeof(FluidSpawnData));

    for (UINT i = 0; i < m_EffectProperties.EmitProperties.MaxParticles; i++)
    {
        FluidSpawnData& SpawnData = pSpawnData[i];
        SpawnData.AgeRate = 1.0f;// / s_RNG.NextFloat(m_EffectProperties.LifeMinMax.x, m_EffectProperties.LifeMinMax.y); s_RNG Removed from fluid
        float horizontalAngle = 3;// s_RNG.NextFloat(XM_2PI);
        float horizontalVelocity = 3;// s_RNG.NextFloat(m_EffectProperties.Velocity.GetX(), m_EffectProperties.Velocity.GetY());
        SpawnData.Velocity.x = horizontalVelocity * cos(horizontalAngle);
        SpawnData.Velocity.y = 3;//s_RNG.NextFloat(m_EffectProperties.Velocity.GetZ(), m_EffectProperties.Velocity.GetW());
        SpawnData.Velocity.z = horizontalVelocity * sin(horizontalAngle);

        SpawnData.SpreadOffset = RandSpread(m_EffectProperties.Spread);

        SpawnData.StartSize = 5;// s_RNG.NextFloat(m_EffectProperties.Size.GetX(), m_EffectProperties.Size.GetY());
        SpawnData.EndSize = 5;// s_RNG.NextFloat(m_EffectProperties.Size.GetZ(), m_EffectProperties.Size.GetW());
        SpawnData.StartColor = RandColor(m_EffectProperties.MinStartColor, m_EffectProperties.MaxStartColor);
        SpawnData.EndColor = RandColor(m_EffectProperties.MinEndColor, m_EffectProperties.MaxEndColor);
        SpawnData.Mass = 5;// s_RNG.NextFloat(m_EffectProperties.MassMinMax.x, m_EffectProperties.MassMinMax.y);
        SpawnData.RotationSpeed = 5;// s_RNG.NextFloat(); //todo
        SpawnData.Random = 2;// s_RNG.NextFloat();
    }

    m_RandomStateBuffer.Create(L"ParticleSystem::SpawnDataBuffer", m_EffectProperties.EmitProperties.MaxParticles, sizeof(FluidSpawnData), pSpawnData);
    _freea(pSpawnData);

    m_StateBuffers[0].Create(L"ParticleSystem::Buffer0", m_EffectProperties.EmitProperties.MaxParticles, sizeof(FluidMotion));
    m_StateBuffers[1].Create(L"ParticleSystem::Buffer1", m_EffectProperties.EmitProperties.MaxParticles, sizeof(FluidMotion));
    m_CurrentStateBuffer = 0;

    m_LookupMap.Create(L"FluidSystemSpacialHashing::LookupMap", 1024, sizeof(std::uint32_t));
    m_TempIndexArray.Create(L"FluidSystemSpacialHashing::TempIndexArray", m_EffectProperties.EmitProperties.MaxParticles, sizeof(std::uint32_t));

    //DispatchIndirect args buffer / number of thread groups
    __declspec(align(16)) UINT DispatchIndirectData[3] = { 0, 1, 1 };
    m_DispatchIndirectArgs.Create(L"ParticleSystem::DispatchIndirectArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS), DispatchIndirectData);

}

void FluidEffect::Update(ComputeContext& CompContext, float timeDelta)
{
    if (timeDelta == 0.0f)
        return;



    m_ElapsedTime += timeDelta;
    m_EffectProperties.EmitProperties.LastEmitPosW = m_EffectProperties.EmitProperties.EmitPosW;

    ////Not Implemented on either fluid or particle effects
    //m_EffectProperties.EmitProperties.EmitPosW = XMFLOAT3(ComputeConstants.EmitPosW.x + 1.0f * float(GameInput::IsPressed(GameInput::kBButton)), ComputeConstants.EmitPosW.y + 1.0f * float(GameInput::IsPressed(GameInput::kYButton)), ComputeConstants.EmitPosW.z - 1.0f * float(GameInput::IsPressed(GameInput::kAButton)));//
    //m_EffectProperties.EmitProperties.EmitPosW.x += m_EffectProperties.DirectionIncrement.x;
    //m_EffectProperties.EmitProperties.EmitPosW.y += m_EffectProperties.DirectionIncrement.y;
    //m_EffectProperties.EmitProperties.EmitPosW.z += m_EffectProperties.DirectionIncrement.z;


    //------------------- Update alive particles

    //CPU side random num gen (removed from fluid)
    for (uint32_t i = 0; i < 64; i++)
    {
        UINT random = 1;// (UINT)s_RNG.NextInt(m_EffectProperties.EmitProperties.MaxParticles - 1);
        m_EffectProperties.EmitProperties.RandIndex[i].x = random;
    }

    ComputeFluidAdvection(CompContext);
    ComputeFluidLookupMap(CompContext);
    //CompContext.SetConstants(0, timeDelta);
    ComputeFluidDensity(CompContext);
    ComputeFluidVelocity(CompContext);

    if (!m_initialised) {
        SpawnFluid(CompContext);
        m_initialised = true;
    }
    //SpawnFluid(CompContext);
}

void FluidEffect::Init(ComputeContext& CompContext)
{
    //SpawnFluid(CompContext);
}


void FluidEffect::Reset()
{
    m_EffectProperties = m_OriginalEffectProperties;
}

void FluidEffect::SpawnFluid(ComputeContext& CompContext)
{
    // Spawn to replace dead ones 
    CompContext.SetPipelineState(s_FluidSpawnCS);
    CompContext.SetDynamicDescriptor(4, 0, m_RandomStateBuffer.GetSRV());
    UINT NumSpawnThreads = m_EffectProperties.EmitProperties.MaxParticles;
    CompContext.Dispatch((NumSpawnThreads + 63) / 64, 1, 1);

    // Output number of thread groups into m_DispatchIndirectArgs	
    CompContext.SetPipelineState(s_FluidDispatchIndirectArgsCS);
    CompContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.SetDynamicDescriptor(4, 0, m_StateBuffers[m_CurrentStateBuffer].GetCounterSRV(CompContext)); //Input: number of particles
    CompContext.SetDynamicDescriptor(3, 1, m_DispatchIndirectArgs.GetUAV()); //Output:  m_DispatchIndirectArgs = (number of particles,1,1)
    CompContext.Dispatch(1, 1, 1);

}

void FluidEffect::ComputeFluidAdvection(ComputeContext& CompContext)
{
    CompContext.SetDynamicConstantBufferView(2, sizeof(FluidEmissionProperties), &m_EffectProperties.EmitProperties);
    

    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.SetDynamicDescriptor(4, 0, m_RandomStateBuffer.GetSRV()); //g_ResetData
    CompContext.SetDynamicDescriptor(4, 1, m_StateBuffers[m_CurrentStateBuffer].GetSRV()); //g_InputBuffer. m_CurrentStateBuffer = double buffered particle motion data

    m_CurrentStateBuffer ^= 1; //Bitwise XOR swaps between 0 and 1

    CompContext.SetPipelineState(s_FluidAdvectionCS); //Load in the shader pipeline
    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_UNORDERED_ACCESS); //Transition g_OutputBuffer as UAV (Input is an SRV)
    CompContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    CompContext.SetDynamicDescriptor(3, 2, m_StateBuffers[m_CurrentStateBuffer].GetUAV()); //g_OutputBuffer
    CompContext.DispatchIndirect(m_DispatchIndirectArgs, 0);

    CompContext.InsertUAVBarrier(m_StateBuffers[m_CurrentStateBuffer]);
}

//int firstbithigh(uint32_t number) {    
//    int counter = -1;
//    while (number > 1) {
//        counter++;
//        number = number >> 1;
//    }
//    return counter;
//}
//
//int countbits(uint32_t number) 
//{
//    int counter = 0;
//    for (int i = 0; i < 32; ++i) {
//        counter += 1 & (number >> i);
//    }
//    return counter;
//}

void FluidEffect::ComputeFluidLookupMap(ComputeContext& CompContext)
{
    CompContext.SetDynamicConstantBufferView(2, sizeof(FluidEmissionProperties), &m_EffectProperties.EmitProperties);
    {
    //This was an attempt to spread the sort over multiple frames
    ////Calculate sort depth for this frame
    //{
    //    SortDepth = SortDepth >> 7;

    //    if (SortDepth < 256) {
    //        uint32_t ParticleCount = m_EffectProperties.EmitProperties.MaxParticles;
    //        uint32_t NextPow2 = countbits(ParticleCount) <= 1 ? ParticleCount : (2u << firstbithigh(ParticleCount));
    //        SortDepth = NextPow2;
    //    }
    //}


    //CompContext.SetConstants(0, SortDepth);
    }

    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.SetDynamicDescriptor(4, 1, m_StateBuffers[m_CurrentStateBuffer].GetSRV()); //g_InputBuffer. m_CurrentStateBuffer = double buffered particle motion data

    m_CurrentStateBuffer ^= 1; //Bitwise XOR swaps between 0 and 1

    CompContext.ResetCounter(m_TempIndexArray);
    

    CompContext.SetPipelineState(s_FluidSortByLocationCS); //Load in the shader pipeline
    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_UNORDERED_ACCESS); //Work directly on the active buffer
    CompContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    CompContext.TransitionResource(m_LookupMap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CompContext.TransitionResource(m_TempIndexArray, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    CompContext.SetDynamicDescriptor(3, 1, m_LookupMap.GetUAV()); //g_SortKeys
    CompContext.SetDynamicDescriptor(3, 2, m_StateBuffers[m_CurrentStateBuffer].GetUAV()); //g_OutputBuffer
    CompContext.SetDynamicDescriptor(3, 3, m_TempIndexArray.GetUAV()); //g_OutputBuffer
    CompContext.Dispatch(1,1, 1);

    CompContext.InsertUAVBarrier(m_StateBuffers[m_CurrentStateBuffer]);
    CompContext.InsertUAVBarrier(m_LookupMap);
    CompContext.InsertUAVBarrier(m_TempIndexArray);
}

void FluidEffect::ComputeFluidDensity(ComputeContext& CompContext)
{
    CompContext.SetDynamicConstantBufferView(2, sizeof(FluidEmissionProperties), &m_EffectProperties.EmitProperties);

    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.TransitionResource(m_LookupMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.SetDynamicDescriptor(4, 0, m_RandomStateBuffer.GetSRV()); //g_ResetData
    CompContext.SetDynamicDescriptor(4, 1, m_StateBuffers[m_CurrentStateBuffer].GetSRV()); //g_InputBuffer. m_CurrentStateBuffer = double buffered particle motion data
    CompContext.SetDynamicDescriptor(4, 2, m_LookupMap.GetSRV()); //g_SortKeys

    m_CurrentStateBuffer ^= 1; //Bitwise XOR swaps between 0 and 1

    CompContext.SetPipelineState(s_FluidDensityCS); //Load in the shader pipeline
    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_UNORDERED_ACCESS); //Transition g_OutputBuffer as UAV (Input is an SRV)
    CompContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    CompContext.SetDynamicDescriptor(3, 2, m_StateBuffers[m_CurrentStateBuffer].GetUAV()); //g_OutputBuffer
    CompContext.DispatchIndirect(m_DispatchIndirectArgs, 0);

    CompContext.InsertUAVBarrier(m_StateBuffers[m_CurrentStateBuffer]);
}

void FluidEffect::ComputeFluidVelocity(ComputeContext& CompContext)
{
    CompContext.SetDynamicConstantBufferView(2, sizeof(FluidEmissionProperties), &m_EffectProperties.EmitProperties);

    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.TransitionResource(m_LookupMap, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    CompContext.SetDynamicDescriptor(4, 0, m_RandomStateBuffer.GetSRV()); //g_ResetData
    CompContext.SetDynamicDescriptor(4, 1, m_StateBuffers[m_CurrentStateBuffer].GetSRV()); //g_InputBuffer. m_CurrentStateBuffer = double buffered particle motion data
    CompContext.SetDynamicDescriptor(4, 2, m_LookupMap.GetSRV()); //g_SortKeys

    m_CurrentStateBuffer ^= 1; //Bitwise XOR swaps between 0 and 1

    CompContext.SetPipelineState(s_FluidVelocityCS); //Load in the shader pipeline
    CompContext.TransitionResource(m_StateBuffers[m_CurrentStateBuffer], D3D12_RESOURCE_STATE_UNORDERED_ACCESS); //Transition g_OutputBuffer as UAV (Input is an SRV)
    CompContext.TransitionResource(m_DispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    CompContext.SetDynamicDescriptor(3, 2, m_StateBuffers[m_CurrentStateBuffer].GetUAV()); //g_OutputBuffer
    CompContext.DispatchIndirect(m_DispatchIndirectArgs, 0);

    CompContext.InsertUAVBarrier(m_StateBuffers[m_CurrentStateBuffer]);
}
