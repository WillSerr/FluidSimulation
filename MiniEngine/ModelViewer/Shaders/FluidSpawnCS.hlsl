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
    
    
    const float Capacity = HalfBounds / InfluenceRadius;
    
    //40x40x40 cube of particles, Max particles 64k
    float3 adjustedPosition = EmitPosW + float3((index % Capacity) * InfluenceDiameter - HalfBounds,
        (int(index / (Capacity * Capacity)) % Capacity) * InfluenceDiameter - HalfBounds + BoundsHeight,
        (int(index / Capacity) % Capacity) * InfluenceDiameter - HalfBounds);

    ParticleMotion newParticle;
    newParticle.Position = adjustedPosition;    
    newParticle.Rotation = 0.0;
    newParticle.Velocity = newVelocity + EmitDirW * EmitSpeed;
    newParticle.PredictedPosition = adjustedPosition + ((newVelocity + EmitDirW * EmitSpeed) * 0.083);
    newParticle.Mass = 25; 
    newParticle.Density = 0.0f;
    newParticle.Age = 0.0;
    newParticle.ResetDataIndex = ResetDataIndex;
    newParticle.LocationHash = BasicPositionHash(newParticle.PredictedPosition, 5);
    newParticle.SortKey = HashToLookupKey(newParticle.LocationHash);
    
    g_OutputBuffer[index] = newParticle;
}
