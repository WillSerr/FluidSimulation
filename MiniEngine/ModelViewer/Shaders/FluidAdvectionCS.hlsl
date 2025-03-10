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
    
    //Redundant
    ParticleState.Age += gElapsedTime * rd.AgeRate;
    
    
    //Update position
    float StepSize = gElapsedTime;        
    ParticleState.Position += ParticleState.Velocity * StepSize;
    
    //ParticleState.Velocity -= ParticleState.Velocity * 0.3 * StepSize; //'air' resistance
    
    const float HalfBounds = 100;
    const float BoundsHeight = 100;
    const float DampingFactor = 0.5;
    const float futureStep = 1.f / 120.f; //Length to predict position into the future
    
    //Keep particles contained withing bounds
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
    
    ParticleState.PredictedPosition = ParticleState.Position + (ParticleState.Velocity * futureStep);
    
    
    // The spawn dispatch might be simultaneously adding particles as well.  It's possible to overflow.
    uint index = g_OutputBuffer.IncrementCounter();
    if (index >= MaxParticles)
        return;

    g_OutputBuffer[index] = ParticleState;

    //
    // Generate a sprite vertex
    //

    float Speed = length(ParticleState.Velocity);
    float ColourVal = min(1, Speed / 40.f) / 0.6f;
    
    ParticleVertex Sprite;

    Sprite.Position = ParticleState.Position;
    Sprite.TextureID = TextureID;

    // Set size and color
    Sprite.Size = 1.f;
    Sprite.Color = float4(0.4f + ColourVal, 0.4f + ColourVal, 1.f, 1.f);

    g_VertexBuffer[g_VertexBuffer.IncrementCounter()] = Sprite;
}
