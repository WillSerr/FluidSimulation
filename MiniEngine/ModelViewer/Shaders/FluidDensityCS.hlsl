#include "FluidUpdateCommon.hlsli"
#include "FluidUtility.hlsli"

cbuffer CB0 : register(b0)
{
    float gElapsedTime;
};

StructuredBuffer<ParticleSpawnData> g_ResetData : register(t0);
StructuredBuffer<ParticleMotion> g_InputBuffer : register(t1);
StructuredBuffer<uint> g_SortKeys : register(t2);
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
    
    const float influenceRadius = InfluenceRadius;
    
    //The total force being enacted upon the volume of fluid, basically compression
    float density = 0;
    
    for (int i = 0; i < 27; ++i)
    {
        float3 neighbourPos = offsets3D[i] * SimCellSize;
        uint neighbourHash = BasicPositionHash(ParticleState.Position + neighbourPos, SimCellSize);
        uint neighbourKey = HashToLookupKey(neighbourHash);
        int inputBufferIndex = g_SortKeys[neighbourKey];
        
        while (inputBufferIndex < MaxParticles)
        {            
            //Stop when exiting the bucket
            if (g_InputBuffer[inputBufferIndex].SortKey != neighbourKey)
            {
                break;
            }
            
            //Must increment index before skipping to prevent endless loop
            float influenceFactor = calculateInfluence(ParticleState.PredictedPosition, g_InputBuffer[inputBufferIndex].PredictedPosition, influenceRadius);
            inputBufferIndex++;
            
            //Skip particles from non-neighbour cells in the bucket
            if (g_InputBuffer[inputBufferIndex-1].LocationHash != neighbourHash)
            {
                continue;
            }
            //Skip over self
            if (inputBufferIndex-1 == DTid.x)
            {
                continue;
            }
            
            
            density += influenceFactor;
        }
    }
    
    
    //for (int i = 0; i < MaxParticles; ++i)
    //{
    //    if (i == DTid.x)
    //    {
    //        continue;
    //    }
        
    //    float influenceFactor = calculateInfluence(ParticleState.PredictedPosition, g_InputBuffer[i].PredictedPosition, influenceRadius);        
               
    //    density += influenceFactor;
    //}
    ParticleState.Density = max(0.00001, density);
    

    g_OutputBuffer[DTid.x] = ParticleState;
}
