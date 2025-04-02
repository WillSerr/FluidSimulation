#include "FluidUpdateCommon.hlsli"
#include "FluidUtility.hlsli"

#define THREADCOUNT 256


StructuredBuffer<ParticleMotion> g_InputBuffer : register(t1);

RWStructuredBuffer<uint> g_SortKeys : register(u1);
RWStructuredBuffer<ParticleMotion> g_OutputBuffer : register(u2);
RWStructuredBuffer<uint> g_TempIndexBuffer : register(u3);

void PopulateTempBuffer(uint DTix, uint NumElements, uint NumThreads)
{
    for (uint i = DTix; i < NumElements; i += NumThreads)
    {
        g_TempIndexBuffer[i] = i;
    }
}

void BitonicSort(uint GI, uint NumElements, uint NextPow2, uint NumThreads)
{
    for (uint k = 2; k <= NextPow2; k *= 2)
    {
        // Align NumElements to the next power of k
        NumElements = (NumElements + k - 1) & ~(k - 1);

        for (uint j = k / 2; j > 0; j /= 2)
        {
            // Loop over all N/2 unique element pairs
            for (uint i = GI; i < NumElements / 2; i += NumThreads)
            {
                uint Index1 = InsertZeroBit(i, j);
                uint Index2 = Index1 | j;

                //magic of bitonic sort, this is race free so long as the next round of checks doesnt happen at the same time
                uint A = g_InputBuffer[g_TempIndexBuffer[Index1]].SortKey;
                uint B = g_InputBuffer[g_TempIndexBuffer[Index2]].SortKey;
                
                
                if ((A < B) != ((Index1 & k) == 0))
                {
                    uint temp = g_TempIndexBuffer[Index1];
                    g_TempIndexBuffer[Index1] = g_TempIndexBuffer[Index2];
                    g_TempIndexBuffer[Index2] = temp;
                }
                

            }

            //stop the next round of checks happening at the same time
            GroupMemoryBarrierWithGroupSync();
        }
    }
}

void MoveResources(uint DTid, uint NumElements, uint NumThreads)
{
    for (uint i = DTid; i < NumElements; i += NumThreads)
    {
        g_OutputBuffer[i] = g_InputBuffer[g_TempIndexBuffer[i]];
    }
}

void PopulateLookupMap(uint DTid, uint NumThreads)
{
    for (int i = DTid; i < MaxParticles; i += NumThreads)
    {
        uint SelfKey = g_OutputBuffer[i].SortKey;
        uint OtherKey = i == 0 ? 0 : g_OutputBuffer[i - 1].SortKey;
        
        
        if (SelfKey != OtherKey)
        {
            g_SortKeys[SelfKey] = i;
        }

    }
}


[RootSignature(Particle_RootSig)]
[numthreads(THREADCOUNT, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint ParticleCount = MaxParticles;
    
    // Compute the next power of two for the bitonic sort
    uint NextPow2 = countbits(ParticleCount) <= 1 ? ParticleCount : (2u << firstbithigh(ParticleCount));
    
    PopulateTempBuffer(DTid.x, ParticleCount, THREADCOUNT);
    
    // Sort the particles by key in ascending order.
    BitonicSort(DTid.x, ParticleCount, NextPow2, THREADCOUNT);
    
    MoveResources(DTid.x, ParticleCount, THREADCOUNT);
    
    GroupMemoryBarrierWithGroupSync();
    
    PopulateLookupMap(DTid.x, THREADCOUNT);

}
