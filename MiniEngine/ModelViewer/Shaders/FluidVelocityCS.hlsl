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
        

    return influenceFactor / (InfluenceRadius * InfluenceRadius * InfluenceRadius); //Should do max(...) / InfluenceRadius instead
}

float calculatePressure(float Density)
{    
    float TargetDensity = 1;
    float PressureMultiplier = 1.5;
    float densityDelta = Density - TargetDensity;
    return densityDelta * PressureMultiplier;
}

// obtain the pressure between self and other
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
    
    const float influenceRadius = InfluenceRadius;
    const float futureStep = 1.f / 120.f; //Length to predict position into the future
    
    float3 totalInfluenceForce = float3(0.f, 0.f, 0.f);
    
    for (int i = 0; i < 27; ++i)
    {
        
        float3 neighbourPos = offsets3D[i] * SimCellSize;
        uint neighbourHash = BasicPositionHash(ParticleState.Position + neighbourPos, SimCellSize);
        uint neighbourKey = HashToLookupKey(neighbourHash);
        int inputBufferIndex = g_SortKeys[neighbourKey];
        
        while (inputBufferIndex < MaxParticles)
        {
            //Must increment index before skipping to prevent endless loop
            ParticleMotion Other = g_InputBuffer[inputBufferIndex];
            inputBufferIndex++;
            
            //Stop when exiting the bucket
            if (Other.SortKey != neighbourKey)
            {
                break;
            }
            //Skip particles from non-neighbour cells in the bucket
            if (Other.LocationHash != neighbourHash)
            {
                continue;
            }
            //Skip over self
            if (inputBufferIndex-1 == DTid.x)
            {
                continue;
            }

            
            float3 deltaPos = ParticleState.PredictedPosition - Other.PredictedPosition;
        
            float deltaSqrDistance = dot(deltaPos, deltaPos);
        
            //Causes a marked performance increase despite warp branching
            if (deltaSqrDistance > (influenceRadius * influenceRadius)) //Skip calculations if other is outside of influence radius
                continue;
        
            //Unstick 2 overlapping particles (only really possible when spawned overlapping)
            if (deltaSqrDistance == 0)
            {
                totalInfluenceForce += float3(0.f, 0.1f, 0.f) * sign(DTid.x - i) * influenceRadius; //One up, one down
                continue;
            }
        
            float influenceFactor = calculateInfluence(ParticleState.PredictedPosition, Other.PredictedPosition, influenceRadius);
        
            float3 deltaDirection = normalize(deltaPos);
        
            float sharedPressure = calculateSharedPressure(ParticleState.Density, Other.Density);
            
            totalInfluenceForce += sharedPressure * deltaDirection * influenceFactor;
            
            
            
        }
    }
    
    //for (int i = 0; i < MaxParticles; ++i)
    //{
    //    if (i == DTid.x)
    //    {
    //        continue;
    //    }
            
    //    float3 deltaPos = ParticleState.PredictedPosition - g_InputBuffer[i].PredictedPosition;
        
    //    float deltaSqrDistance = dot(deltaPos, deltaPos);
        
    ////Causes a marked performance increase despite warp branching
    //    if (deltaSqrDistance > (influenceRadius * influenceRadius)) //Skip calculations if other is outside of influence radius
    //        continue;
        
    ////Unstick 2 overlapping particles
    //    if (deltaSqrDistance == 0)
    //    {
    //        totalInfluenceForce += float3(0.f, 0.1f, 0.f) * sign(DTid.x - i) * influenceRadius; //One up, one down
    //        continue;
    //    }
        

       
    ////Could cache the future positions to prevent wasting cycles re-calculating
    //    float influenceFactor = calculateInfluence(ParticleState.Position + (ParticleState.Velocity * futureStep), g_InputBuffer[i].Position + (g_InputBuffer[i].Velocity * futureStep), influenceRadius);
        
    //    float3 deltaDirection = normalize(deltaPos);
        
    //    float sharedPressure = calculateSharedPressure(ParticleState.Density, g_InputBuffer[i].Density);
    //    totalInfluenceForce += sharedPressure * deltaDirection * influenceFactor; // * 1.f / g_InputBuffer[i].Density; // / g_InputBuffer[i].Density; //Missing Pressure(g_InputBuffer[i].Position), mass / g_InputBuffer[i].Density

    //}
    
    float3 RightVector = cross(normalize(float3(ParticleState.Position.x, 0.f, ParticleState.Position.z)), float3(0.f, -1.f, 0.f));
    float SwirlForce = max(0, 100 - length(ParticleState.Position.xz)) / 50;
    
    //float3 UpVector = float3(0, 1.f, 0);
    //float heightFactor = max(0, 40 - ParticleState.Position.y) / 40;
    //heightFactor = heightFactor * heightFactor * heightFactor;
    //float SwirlForce = max(0, 20 - length(ParticleState.Position.xz)) * heightFactor * 10;
    
    ParticleState.Velocity += (totalInfluenceForce + float3(0.f, -9.8f, 0.f) + (RightVector * SwirlForce)) * gElapsedTime;
    
    
    g_OutputBuffer[DTid.x] = ParticleState;
    
}
