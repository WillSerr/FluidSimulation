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
    influenceFactor = influenceFactor * influenceFactor * influenceFactor; //curve smoothed towards zero
        
    return influenceFactor;
}

[RootSignature(Particle_RootSig)]
[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= MaxParticles)
        return;

    ParticleMotion ParticleState = g_InputBuffer[DTid.x];
    ParticleSpawnData rd = g_ResetData[ParticleState.ResetDataIndex];
    
    //Update position
    float StepSize = gElapsedTime;
    
    const float influenceRadius = 5;
    
    //The total force being enacted upon the volume of fluid, basically compression
    float density = 0;
    
    for (int i = 0; i < MaxParticles; ++i)
    {
        if (i == DTid.x)
        {
            continue;
        }
        
        float influenceFactor = calculateInfluence(ParticleState.PredictedPosition, g_InputBuffer[i].PredictedPosition, influenceRadius);        
               
        density += influenceFactor;
    }
    ParticleState.Density = max(0.00001, density);
    
    
    // The spawn dispatch might be simultaneously adding particles as well.  It's possible to overflow.
    uint index = g_OutputBuffer.IncrementCounter();
    if (index >= MaxParticles)
        return;

    g_OutputBuffer[index] = ParticleState;
}
