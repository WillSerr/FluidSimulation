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
// Author:  James Stanard 
//          Julia Careaga
//

#include "FluidUpdateCommon.hlsli"
#include "FluidUtility.hlsli"

cbuffer CB0 : register(b0)
{
    float gElapsedTime;
};

StructuredBuffer<ParticleSpawnData> g_ResetData : register(t0);
StructuredBuffer<ParticleMotion> g_InputBuffer : register(t1);
RWStructuredBuffer<ParticleVertex> g_VertexBuffer : register(u0);
RWStructuredBuffer<ParticleMotion> g_OutputBuffer : register(u2);

float calculateInfluence(float3 SelfPos, float3 OtherPos, float InfluenceRadius)
{
    float3 deltaPos = SelfPos - OtherPos;
    float deltaLength = length(deltaPos);
        
    //Repulsion
    float influenceFactor = max(0, InfluenceRadius - deltaLength);
    influenceFactor = influenceFactor * influenceFactor * influenceFactor;
        

    return influenceFactor / (InfluenceRadius * InfluenceRadius * InfluenceRadius);
}

float calculatePressure(float Density)
{    
    float TargetDensity = 1;
    float PressureMultiplier = 1.5;
    float densityDelta = Density - TargetDensity;
    return densityDelta * PressureMultiplier;
}

float calculateSharedPressure(float DensitySelf, float DensityOther)
{
    float pressureSelf = calculatePressure(DensitySelf);
    float pressureOther = calculatePressure(DensityOther);
    return (pressureSelf * pressureOther) / 2;
}

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= MaxParticles)
        return;

    ParticleMotion ParticleState = g_InputBuffer[DTid.x];
    ParticleSpawnData rd = g_ResetData[ParticleState.ResetDataIndex];        
    
    const float influenceRadius = 5;
    const float futureStep = 1.f / 120.f; //Length to predict position into the future
    
    float3 totalInfluenceForce = float3(0.f, 0.f, 0.f);
    
    
    for (int i = 0; i < MaxParticles; ++i)
    {
        if (i == DTid.x)
        {
            continue;
        }
            
        float3 deltaPos = ParticleState.PredictedPosition - g_InputBuffer[i].PredictedPosition;
        
        float deltaSqrDistance = dot(deltaPos, deltaPos);
        
        //Causes a marked performance increase despite warp branching
        if (deltaSqrDistance > (influenceRadius * influenceRadius)) //Skip calculations if other is outside of influence radius
            continue;
        
        //Unstick 2 overlapping particles
        if (deltaSqrDistance == 0)
        {
            totalInfluenceForce += float3(0.f, 0.1f, 0.f) * sign(DTid.x - i) * influenceRadius;
            continue;
        }
        

       
        //Could cache the future positions to prevent wasting cycles re-calculating
        float influenceFactor = calculateInfluence(ParticleState.Position + (ParticleState.Velocity * futureStep), g_InputBuffer[i].Position + (g_InputBuffer[i].Velocity * futureStep), influenceRadius);
        
        float3 deltaDirection = normalize(deltaPos);        
        
        float sharedPressure = calculateSharedPressure(ParticleState.Density, g_InputBuffer[i].Density);
        totalInfluenceForce += sharedPressure * deltaDirection * influenceFactor;// * 1.f / g_InputBuffer[i].Density; // / g_InputBuffer[i].Density; //Missing Pressure(g_InputBuffer[i].Position), mass / g_InputBuffer[i].Density

    }
    
    float3 RightVector = cross(normalize(float3(ParticleState.Position.x, 0.f, ParticleState.Position.z)), float3(0.f, -1.f, 0.f));
    float SwirlForce = max(0, 50 - length(ParticleState.Position.xz)) / 10;
    
    //float3 UpVector = float3(0, 1.f, 0);
    //float heightFactor = max(0, 40 - ParticleState.Position.y) / 40;
    //heightFactor = heightFactor * heightFactor * heightFactor;
    //float SwirlForce = max(0, 20 - length(ParticleState.Position.xz)) * heightFactor * 10;
    
    ParticleState.Velocity += (totalInfluenceForce + float3(0.f, -9.8f, 0.f) + (RightVector * SwirlForce)) * gElapsedTime;
    
    
    // The spawn dispatch will be simultaneously adding particles as well.  It's possible to overflow.
    uint index = g_OutputBuffer.IncrementCounter();
    if (index >= MaxParticles)
        return;

    g_OutputBuffer[index] = ParticleState;
    
}
