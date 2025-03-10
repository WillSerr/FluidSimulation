#include "FluidUpdateCommon.hlsli"
#include "FluidUtility.hlsli"

StructuredBuffer< ParticleSpawnData > g_ResetData : register( t0 );
RWStructuredBuffer< ParticleMotion > g_OutputBuffer : register( u2 );

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    uint index = g_OutputBuffer.IncrementCounter();
    if (index >= MaxParticles)
        return;
    
    uint ResetDataIndex = RandIndex[DTid.x].x; 
    ParticleSpawnData rd  = g_ResetData[ResetDataIndex];
        
    float3 emitterVelocity = EmitPosW - LastEmitPosW; 
    float3 randDir = rd.Velocity.x * EmitRightW + rd.Velocity.y * EmitUpW + rd.Velocity.z * EmitDirW;
    float3 newVelocity = emitterVelocity * EmitterVelocitySensitivity + randDir;
    
    const float HalfBounds = 100;
    const float BoundsHeight = 100;
    
    //40x40x40 cube of particles, Max particles 64k
    float3 adjustedPosition = EmitPosW + float3((index % 40) * 5.f - HalfBounds,
        (int(index / 1600) % 40) * 5.f - HalfBounds + BoundsHeight,
        (int(index / 40) % 40) * 5.f - HalfBounds);

    ParticleMotion newParticle;
    newParticle.Position = adjustedPosition;    
    newParticle.Rotation = 0.0;
    newParticle.Velocity = newVelocity + EmitDirW * EmitSpeed;
    newParticle.PredictedPosition = adjustedPosition + ((newVelocity + EmitDirW * EmitSpeed) * 0.083);
    newParticle.Mass = 25; 
    newParticle.Density = 0.0f;
    newParticle.Age = 0.0;
    newParticle.ResetDataIndex = ResetDataIndex; 
    g_OutputBuffer[index] = newParticle;
}
