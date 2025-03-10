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

StructuredBuffer< ParticleSpawnData > g_ResetData : register( t0 );
StructuredBuffer< ParticleMotion > g_InputBuffer : register( t1 );
RWStructuredBuffer< ParticleVertex > g_VertexBuffer : register( u0 );
RWStructuredBuffer< ParticleMotion > g_OutputBuffer : register( u2 );

float calculateInfluence(float3 SelfPos, float3 OtherPos, float InfluenceRadius)
{
    float3 deltaPos = SelfPos - OtherPos;
    float deltaLength = length(deltaPos);
        
    //Repulsion
    float influenceFactor = max(0, InfluenceRadius - deltaLength);
    influenceFactor = influenceFactor * influenceFactor * influenceFactor;
        

    return influenceFactor;
}

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    if (DTid.x >= MaxParticles)
        return;

    ParticleMotion ParticleState = g_InputBuffer[ DTid.x ];
    ParticleSpawnData rd = g_ResetData[ ParticleState.ResetDataIndex ];

    //// Update age.  If normalized age exceeds 1, the particle does not renew its lease on life.
    ParticleState.Age += gElapsedTime * rd.AgeRate;
    //if (ParticleState.Age >= 1.0)
    //    return;
    
    
    ///-------------------------------------
    ///
    ///Need to run seperate Compute shaders for each stage of simulation
    ///
    ///-------------------------------------
    
    
    //Update position
    float StepSize = gElapsedTime;
    
    
    ParticleState.Position += ParticleState.Velocity * StepSize;
    
    //ParticleState.Velocity -= ParticleState.Velocity * 0.3 * StepSize; //Residual momentum loss
    
    const float HalfBounds = 200;
    const float BoundsHeight = 500;
    const float DampingFactor = 0.5;
    
    if (abs(ParticleState.Position.x) > HalfBounds)
    {
        ParticleState.Position.x = sign(ParticleState.Position.x) * (HalfBounds - (abs(ParticleState.Position.x) - HalfBounds) * DampingFactor);
        ParticleState.Velocity.x *= -1.f * DampingFactor;
    }
    
    if (abs(ParticleState.Position.y - BoundsHeight) > HalfBounds)
    {
        ParticleState.Position.y = BoundsHeight + sign(ParticleState.Position.y - BoundsHeight) * (HalfBounds - (abs(ParticleState.Position.y - BoundsHeight) - HalfBounds) * DampingFactor);
        ParticleState.Velocity.y *= -1.f * DampingFactor;
    }
    
    if (abs(ParticleState.Position.z) > HalfBounds)
    {
        ParticleState.Position.z = sign(ParticleState.Position.z) * (HalfBounds - (abs(ParticleState.Position.z) - HalfBounds) * DampingFactor);
        ParticleState.Velocity.z *= -1.f * DampingFactor;
    }

    const float RepulsionFactor = 2;
    const float influenceRadius = 30;
    const float futureStep = 1.f / 120.f; //Length to predict position into the future
    
    //const float TensionFactor = 0.1;
    //const float TensionRadius = 40;
    float3 totalInfluenceForce = float3(0.f, 0.f, 0.f);
    
    //The total force being enacted upon the volume of fluid
    //float density = 0;
    
    for (int i = 0; i < MaxParticles; ++i)
    {
        if (i == DTid.x)
        {
            continue;
        }
            
        
        float3 deltaPos = ParticleState.Position - (g_InputBuffer[i].Position + (g_InputBuffer[i].Velocity * futureStep));
        
       
        //Could cache the future positions to prevent wasting cycles re-calculating
        float influenceFactor = calculateInfluence(ParticleState.Position, g_InputBuffer[i].Position + (g_InputBuffer[i].Velocity * futureStep), influenceRadius);
        
        float3 deltaDirection = normalize(deltaPos);
        
        //Needs cached to obtain density(Other);
        //density += influenceFactor;
        
        
        
        totalInfluenceForce += deltaDirection * influenceFactor; //Missing Pressure(Density(other)), mass / Density(other)

    }
    ParticleState.Velocity += totalInfluenceForce * StepSize;
    //ParticleState.Velocity += float3(0.f, -9.8f, 0.f) * 10 * StepSize; // Gravity * StepSize * ParticleState.Mass;
    
    
    //ParticleState.Velocity += ParticleState.Velocity * -0.1 * StepSize;
    
    // The spawn dispatch will be simultaneously adding particles as well.  It's possible to overflow.
    uint index = g_OutputBuffer.IncrementCounter();    
    if (index >= MaxParticles)
        return;

    g_OutputBuffer[index] = ParticleState;

    //
    // Generate a sprite vertex
    //

    ParticleVertex Sprite;

    Sprite.Position = ParticleState.Position;
    Sprite.TextureID = TextureID;

    // Update size and color
    Sprite.Size = 10.f; //lerp(rd.StartSize, rd.EndSize, ParticleState.Age);
    Sprite.Color = float4(1.f, 1.f, 1.f, 1.f); //lerp(rd.StartColor, rd.EndColor, ParticleState.Age);

    //// ...Originally from Reflex...
    //// Use a trinomial to smoothly fade in a particle at birth and fade it out at death.
    //Sprite.Color *= ParticleState.Age * (1.0 - ParticleState.Age) * (1.0 - ParticleState.Age) * 6.7;

    g_VertexBuffer[ g_VertexBuffer.IncrementCounter() ] = Sprite;
}
