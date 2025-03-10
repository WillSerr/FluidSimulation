

#include "pch.h"
#include "BufferManager.h"
#include "BitonicSort.h"
#include "Camera.h"
#include "ColorBuffer.h"
#include "CommandContext.h"
#include "GameCore.h"
#include "GraphicsCore.h"


//#include "Math/Matrix4.h"

#include "FluidEffectManager.h"
#include "FluidEffect.h"
#include "FluidEffectProperties.h"

#include <mutex>

#include "CompiledShaders/FluidSpawnCS.h"
#include "CompiledShaders/FluidUpdateCS.h"
#include "CompiledShaders/FluidAdvectionCS.h"
#include "CompiledShaders/FluidDensityCS.h"
#include "CompiledShaders/FluidVelocityCS.h"
#include "CompiledShaders/FluidDispatchIndirectArgsCS.h"
#include "CompiledShaders/FluidFinalDispatchIndirectArgsCS.h"
#include "CompiledShaders/FluidLargeBinCullingCS.h"
#include "CompiledShaders/FluidBinCullingCS.h"

#include "CompiledShaders/FluidTileRenderCS.h"
#include "CompiledShaders/FluidTileRenderFastCS.h"
#include "CompiledShaders/FluidTileRenderSlowDynamicCS.h"
#include "CompiledShaders/FluidTileRenderFastDynamicCS.h"
#include "CompiledShaders/FluidTileRenderSlowLowResCS.h"
#include "CompiledShaders/FluidTileRenderFastLowResCS.h"

#include "CompiledShaders/FluidTileRender2CS.h"
#include "CompiledShaders/FluidTileRenderFast2CS.h"
#include "CompiledShaders/FluidTileRenderSlowDynamic2CS.h"
#include "CompiledShaders/FluidTileRenderFastDynamic2CS.h"
#include "CompiledShaders/FluidTileRenderSlowLowRes2CS.h"
#include "CompiledShaders/FluidTileRenderFastLowRes2CS.h"

#include "CompiledShaders/FluidTileCullingCS.h"
#include "CompiledShaders/FluidDepthBoundsCS.h"

#include "CompiledShaders/FluidSortIndirectArgsCS.h"
#include "CompiledShaders/FluidPreSortCS.h"
#include "CompiledShaders/FluidPS.h"
#include "CompiledShaders/FluidVS.h"
#include "CompiledShaders/FluidNoSortVS.h"

#define EFFECTS_ERROR uint32_t(0xFFFFFFFF)

#define MAX_TOTAL_PARTICLES 0x40000		// 256k (18-bit indices)
#define MAX_PARTICLES_PER_BIN 1024
#define BIN_SIZE_X 128
#define BIN_SIZE_Y 64
#define TILE_SIZE 16

// It's good to have 32 tiles per bin to maximize the tile culling phase
#define TILES_PER_BIN_X (BIN_SIZE_X / TILE_SIZE)
#define TILES_PER_BIN_Y (BIN_SIZE_Y / TILE_SIZE)
#define TILES_PER_BIN (TILES_PER_BIN_X * TILES_PER_BIN_Y)

using namespace Graphics;
using namespace Math;
using namespace FluidEffectManager;

namespace FluidEffectManager
{
    BoolVar Enable("Graphics/Particle Effects/Enable", true);
    BoolVar EnableSpriteSort("Graphics/Particle Effects/Sort Sprites", true);
    BoolVar EnableTiledRendering("Graphics/Particle Effects/Tiled Rendering", true);
    BoolVar PauseSim("Graphics/Particle Effects/Pause Simulation", false);
    const char* ResolutionLabels[] = { "High-Res", "Low-Res", "Dynamic" };
    EnumVar TiledRes("Graphics/Particle Effects/Tiled Sample Rate", 2, 3, ResolutionLabels);
    NumVar DynamicResLevel("Graphics/Particle Effects/Dynamic Resolution Cutoff", 0.0f, -4.0f, 4.0f, 0.5f);
    NumVar MipBias("Graphics/Particle Effects/Mip Bias", 0.0f, -4.0f, 4.0f, 0.5f);

    ComputePSO s_FluidSpawnCS(L"Fluids: Fluid Spawn CS");
    ComputePSO s_FluidUpdateCS(L"Fluids: Fluid Update CS");
    ComputePSO s_FluidAdvectionCS(L"Fluids: Fluid Advection CS");
    ComputePSO s_FluidDensityCS(L"Fluids: Fluid Density CS");
    ComputePSO s_FluidVelocityCS(L"Fluids: Fluid Velocity CS");
    ComputePSO s_FluidDispatchIndirectArgsCS(L"Fluids: Fluid Dispatch Indirect Args CS");

    StructuredBuffer SpriteVertexBuffer;

    UINT s_ReproFrame = 0;//201;
    //RandomNumberGenerator s_RNG;
}

struct CBChangesPerView
{
    Matrix4 gInvView;
    Matrix4 gViewProj;

    float gVertCotangent;
    float gAspectRatio;
    float gRcpFarZ;
    float gInvertZ;

    float gBufferWidth;
    float gBufferHeight;
    float gRcpBufferWidth;
    float gRcpBufferHeight;

    uint32_t gBinsPerRow;
    uint32_t gTileRowPitch;
    uint32_t gTilesPerRow;
    uint32_t gTilesPerCol;
};

namespace
{
    ComputePSO s_FluidFinalDispatchIndirectArgsCS(L"Fluids: Fluid Final Dispatch Indirect Args CS");
    ComputePSO s_FluidLargeBinCullingCS(L"Fluids: Fluid Large Bin Culling CS");
    ComputePSO s_FluidBinCullingCS(L"Fluids: Fluid Bin Culling CS");
    ComputePSO s_FluidTileCullingCS(L"Fluids: Fluid Tile Culling CS");
    ComputePSO s_FluidTileRenderSlowCS[3];	// High-Res, Low-Res, Dynamic-Res
    ComputePSO s_FluidTileRenderFastCS[3]; 	// High-Res, Low-Res, Dynamic-Res (disable depth tests)
    ComputePSO s_FluidDepthBoundsCS(L"Fluids: Fluid Depth Bounds CS");
    GraphicsPSO s_NoTileRasterizationPSO[2];
    ComputePSO s_FluidSortIndirectArgsCS(L"Fluids: Fluid Sort Indirect Args CS");
    ComputePSO s_FluidPreSortCS(L"Fluids: Fluid Pre Sort CS");

    RootSignature RootSig;

    StructuredBuffer SpriteIndexBuffer;
    IndirectArgsBuffer SortIndirectArgs;

    IndirectArgsBuffer DrawIndirectArgs;
    IndirectArgsBuffer FinalDispatchIndirectArgs;
    StructuredBuffer VisibleParticleBuffer;
    StructuredBuffer BinParticles[2];
    ByteAddressBuffer BinCounters[2];
    StructuredBuffer TileCounters;
    ByteAddressBuffer TileHitMasks;

    StructuredBuffer TileDrawPackets;
    StructuredBuffer TileFastDrawPackets;
    IndirectArgsBuffer TileDrawDispatchIndirectArgs;

    CBChangesPerView s_ChangesPerView;

    GpuResource TextureArray;
    D3D12_CPU_DESCRIPTOR_HANDLE TextureArraySRV;
    std::vector<std::wstring> TextureNameArray;

    std::vector<std::unique_ptr<FluidEffect>> ParticleEffectsPool;
    std::vector<FluidEffect*> FluidEffectsActive;

    static bool s_InitComplete = false;
    UINT TotalElapsedFrames;

    void SetFinalBuffers(ComputeContext& CompContext)
    {
        CompContext.SetPipelineState(s_FluidFinalDispatchIndirectArgsCS);

        CompContext.TransitionResource(SpriteVertexBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
        CompContext.TransitionResource(FinalDispatchIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CompContext.TransitionResource(DrawIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CompContext.SetDynamicDescriptor(3, 0, FinalDispatchIndirectArgs.GetUAV());
        CompContext.SetDynamicDescriptor(3, 1, DrawIndirectArgs.GetUAV());
        CompContext.SetDynamicDescriptor(4, 0, SpriteVertexBuffer.GetCounterSRV(CompContext));

        CompContext.Dispatch(1, 1, 1);
    }

    void RenderTiles(ComputeContext& CompContext, ColorBuffer& ColorTarget, ColorBuffer& LinearDepth)
    {
        size_t ScreenWidth = ColorTarget.GetWidth();
        size_t ScreenHeight = ColorTarget.GetHeight();

        ASSERT(ColorTarget.GetFormat() == DXGI_FORMAT_R32_UINT || g_bTypedUAVLoadSupport_R11G11B10_FLOAT,
            "Without typed UAV loads, tiled particles must render to a R32_UINT buffer");

        {
            ScopedTimer _p(L"Compute Depth Bounds", CompContext);

            CompContext.SetPipelineState(s_FluidDepthBoundsCS);

            CompContext.TransitionResource(LinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(g_MinMaxDepth8, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(g_MinMaxDepth16, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(g_MinMaxDepth32, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.SetDynamicDescriptor(3, 0, g_MinMaxDepth8.GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, g_MinMaxDepth16.GetUAV());
            CompContext.SetDynamicDescriptor(3, 2, g_MinMaxDepth32.GetUAV());
            CompContext.SetDynamicDescriptor(4, 0, LinearDepth.GetSRV());

            CompContext.Dispatch2D(ScreenWidth, ScreenHeight, 32, 32);
        }

        {
            ScopedTimer _p(L"Culling & Sorting", CompContext);

            CompContext.ResetCounter(VisibleParticleBuffer);

            // The first step inserts each particle into all of the large bins it intersects.  Large bins
            // are 512x256.
            CompContext.SetPipelineState(s_FluidLargeBinCullingCS);
            CompContext.SetConstants(0, 5, 4);

            CompContext.TransitionResource(SpriteVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(FinalDispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            CompContext.TransitionResource(BinParticles[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(BinCounters[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(VisibleParticleBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            CompContext.SetDynamicDescriptor(3, 0, BinParticles[0].GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, BinCounters[0].GetUAV());
            CompContext.SetDynamicDescriptor(3, 2, VisibleParticleBuffer.GetUAV());
            CompContext.SetDynamicDescriptor(4, 0, SpriteVertexBuffer.GetSRV());
            CompContext.SetDynamicDescriptor(4, 1, SpriteVertexBuffer.GetCounterSRV(CompContext));

            CompContext.DispatchIndirect(FinalDispatchIndirectArgs);

            // The second step refines the binning by inserting particles into the appropriate small bins.
            // Small bins are 128x64.
            CompContext.SetPipelineState(s_FluidBinCullingCS);
            CompContext.SetConstants(0, 3, 2);

            CompContext.TransitionResource(VisibleParticleBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(BinParticles[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(BinCounters[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(BinParticles[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(BinCounters[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            CompContext.SetDynamicDescriptor(3, 0, BinParticles[1].GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, BinCounters[1].GetUAV());
            CompContext.SetDynamicDescriptor(4, 0, VisibleParticleBuffer.GetSRV());
            CompContext.SetDynamicDescriptor(4, 1, BinParticles[0].GetSRV());
            CompContext.SetDynamicDescriptor(4, 2, BinCounters[0].GetSRV());

            CompContext.Dispatch2D(ScreenWidth, ScreenHeight, 4 * BIN_SIZE_X, 4 * BIN_SIZE_Y);

            // The final sorting step will perform a bitonic sort on each bin's particles (front to
            // back).  Afterward, it will generate a bitmap for each tile indicating which of the bin's
            // particles occupy the tile.  This allows each tile to iterate over a sorted list of particles
            // ignoring the ones that do not intersect.
            CompContext.SetPipelineState(s_FluidTileCullingCS);

            CompContext.FillBuffer(TileDrawDispatchIndirectArgs, 0, 0, sizeof(uint32_t));
            CompContext.FillBuffer(TileDrawDispatchIndirectArgs, 12, 0, sizeof(uint32_t));

            CompContext.TransitionResource(BinParticles[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(TileHitMasks, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(TileDrawPackets, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(TileFastDrawPackets, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(TileDrawDispatchIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(BinParticles[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(BinCounters[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(g_MinMaxDepth8, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(g_MinMaxDepth16, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(g_MinMaxDepth32, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            CompContext.SetDynamicDescriptor(3, 0, BinParticles[0].GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, TileHitMasks.GetUAV());
            CompContext.SetDynamicDescriptor(3, 2, TileDrawPackets.GetUAV());
            CompContext.SetDynamicDescriptor(3, 3, TileFastDrawPackets.GetUAV());
            CompContext.SetDynamicDescriptor(3, 4, TileDrawDispatchIndirectArgs.GetUAV());

            CompContext.SetDynamicDescriptor(4, 0, BinParticles[1].GetSRV());
            CompContext.SetDynamicDescriptor(4, 1, BinCounters[1].GetSRV());
            CompContext.SetDynamicDescriptor(4, 2, TILE_SIZE == 16 ? g_MinMaxDepth16.GetSRV() : g_MinMaxDepth32.GetSRV());
            CompContext.SetDynamicDescriptor(4, 3, VisibleParticleBuffer.GetSRV());

            CompContext.Dispatch2D(ScreenWidth, ScreenHeight, BIN_SIZE_X, BIN_SIZE_Y);
        }

        {
            ScopedTimer _p(L"Tiled Rendering", CompContext);

            CompContext.TransitionResource(TileDrawDispatchIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            CompContext.TransitionResource(ColorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(LinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(BinParticles[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(TileHitMasks, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(TileDrawPackets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(TileFastDrawPackets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(TextureArray, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

            CompContext.SetDynamicDescriptor(3, 0, ColorTarget.GetUAV());

            D3D12_CPU_DESCRIPTOR_HANDLE SRVs[] =
            {
                VisibleParticleBuffer.GetSRV(),
                TileHitMasks.GetSRV(),
                TextureArraySRV,
                LinearDepth.GetSRV(),
                BinParticles[0].GetSRV(),
                TileDrawPackets.GetSRV(),
                TileFastDrawPackets.GetSRV(),
                (TILE_SIZE == 16 ? g_MinMaxDepth16.GetSRV() : g_MinMaxDepth32.GetSRV()),
            };
            CompContext.SetDynamicDescriptors(4, 0, _countof(SRVs), SRVs);

            CompContext.SetConstants(0, (float)DynamicResLevel, (float)MipBias);

            CompContext.SetPipelineState(s_FluidTileRenderSlowCS[TiledRes]);
            CompContext.DispatchIndirect(TileDrawDispatchIndirectArgs, 0);

            CompContext.SetPipelineState(s_FluidTileRenderFastCS[TiledRes]);
            CompContext.DispatchIndirect(TileDrawDispatchIndirectArgs, 12);
        }
    }

    void RenderSprites(GraphicsContext& GrContext, ColorBuffer& ColorTarget, DepthBuffer& DepthTarget, ColorBuffer& LinearDepth)
    {
        if (EnableSpriteSort)
        {
            ScopedTimer _p(L"Sort Particles", GrContext);
            ComputeContext& CompContext = GrContext.GetComputeContext();
            CompContext.SetRootSignature(RootSig);

            CompContext.SetDynamicConstantBufferView(1, sizeof(CBChangesPerView), &s_ChangesPerView);

            CompContext.SetPipelineState(s_FluidSortIndirectArgsCS);
            CompContext.TransitionResource(SpriteVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            CompContext.TransitionResource(SortIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.TransitionResource(DrawIndirectArgs, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.InsertUAVBarrier(DrawIndirectArgs);
            CompContext.SetDynamicDescriptor(3, 0, SortIndirectArgs.GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, DrawIndirectArgs.GetUAV());
            CompContext.Dispatch(1, 1, 1);

            CompContext.SetPipelineState(s_FluidPreSortCS);
            CompContext.TransitionResource(SortIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            CompContext.TransitionResource(SpriteIndexBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            CompContext.InsertUAVBarrier(DrawIndirectArgs);
            CompContext.SetDynamicDescriptor(3, 0, SpriteIndexBuffer.GetUAV());
            CompContext.SetDynamicDescriptor(3, 1, DrawIndirectArgs.GetUAV());
            CompContext.SetDynamicDescriptor(4, 0, SpriteVertexBuffer.GetSRV());
            CompContext.SetDynamicDescriptor(4, 1, SpriteVertexBuffer.GetCounterSRV(CompContext));
            CompContext.DispatchIndirect(SortIndirectArgs, 0);

            CompContext.InsertUAVBarrier(SpriteIndexBuffer);

            BitonicSort::Sort(CompContext, SpriteIndexBuffer, SpriteVertexBuffer.GetCounterBuffer(), 0, true, false);
        }

        D3D12_RECT scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = (LONG)ColorTarget.GetWidth();
        scissor.bottom = (LONG)ColorTarget.GetHeight();

        D3D12_VIEWPORT viewport;
        viewport.TopLeftX = 0.0;
        viewport.TopLeftY = 0.0;
        viewport.Width = (float)ColorTarget.GetWidth();
        viewport.Height = (float)ColorTarget.GetHeight();
        viewport.MinDepth = 0.0;
        viewport.MaxDepth = 1.0;

        GrContext.SetRootSignature(RootSig);
        GrContext.SetPipelineState(s_NoTileRasterizationPSO[EnableSpriteSort ? 0 : 1]);
        GrContext.TransitionResource(SpriteVertexBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        GrContext.TransitionResource(DrawIndirectArgs, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
        GrContext.TransitionResource(TextureArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        GrContext.TransitionResource(LinearDepth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        GrContext.TransitionResource(SpriteIndexBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        GrContext.SetDynamicDescriptor(4, 0, SpriteVertexBuffer.GetSRV());
        GrContext.SetDynamicDescriptor(4, 1, TextureArraySRV);
        GrContext.SetDynamicDescriptor(4, 2, LinearDepth.GetSRV());
        GrContext.SetDynamicDescriptor(4, 3, SpriteIndexBuffer.GetSRV());
        GrContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        GrContext.TransitionResource(ColorTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
        GrContext.TransitionResource(DepthTarget, D3D12_RESOURCE_STATE_DEPTH_READ);
        GrContext.SetRenderTarget(ColorTarget.GetRTV(), DepthTarget.GetDSV_DepthReadOnly());
        GrContext.SetViewportAndScissor(viewport, scissor);
        GrContext.DrawIndirect(DrawIndirectArgs);
    }

} // {anonymous} namespace

//---------------------------------------------------------------------
//
//	Initialize
//
//---------------------------------------------------------------------

void FluidEffectManager::Initialize(uint32_t MaxDisplayWidth, uint32_t MaxDisplayHeight)
{
    D3D12_SAMPLER_DESC SamplerBilinearBorderDesc = SamplerPointBorderDesc;
    SamplerBilinearBorderDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;

    RootSig.Reset(5, 3);
    RootSig.InitStaticSampler(0, SamplerBilinearBorderDesc);
    RootSig.InitStaticSampler(1, SamplerPointBorderDesc);
    RootSig.InitStaticSampler(2, SamplerPointClampDesc);
    RootSig[0].InitAsConstants(0, 3);
    RootSig[1].InitAsConstantBuffer(1);
    RootSig[2].InitAsConstantBuffer(2);
    RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 8);
    RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 10);
    RootSig.Finalize(L"Fluid Effects");

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(RootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();
    CreatePSO(s_FluidSpawnCS, g_pFluidSpawnCS);
    CreatePSO(s_FluidUpdateCS, g_pFluidUpdateCS);
    CreatePSO(s_FluidAdvectionCS, g_pFluidAdvectionCS);
    CreatePSO(s_FluidDensityCS, g_pFluidDensityCS);
    CreatePSO(s_FluidVelocityCS, g_pFluidVelocityCS);
    CreatePSO(s_FluidDispatchIndirectArgsCS, g_pFluidDispatchIndirectArgsCS);
    CreatePSO(s_FluidFinalDispatchIndirectArgsCS, g_pFluidFinalDispatchIndirectArgsCS);

    CreatePSO(s_FluidLargeBinCullingCS, g_pFluidLargeBinCullingCS);
    CreatePSO(s_FluidBinCullingCS, g_pFluidBinCullingCS);
    CreatePSO(s_FluidTileCullingCS, g_pFluidTileCullingCS);
    if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        CreatePSO(s_FluidTileRenderSlowCS[0], g_pFluidTileRender2CS);
        CreatePSO(s_FluidTileRenderFastCS[0], g_pFluidTileRenderFast2CS);
        CreatePSO(s_FluidTileRenderSlowCS[1], g_pFluidTileRenderSlowLowRes2CS);
        CreatePSO(s_FluidTileRenderFastCS[1], g_pFluidTileRenderFastLowRes2CS);
        CreatePSO(s_FluidTileRenderSlowCS[2], g_pFluidTileRenderSlowDynamic2CS);
        CreatePSO(s_FluidTileRenderFastCS[2], g_pFluidTileRenderFastDynamic2CS);
    }
    else
    {
        CreatePSO(s_FluidTileRenderSlowCS[0], g_pFluidTileRenderCS);
        CreatePSO(s_FluidTileRenderFastCS[0], g_pFluidTileRenderFastCS);
        CreatePSO(s_FluidTileRenderSlowCS[1], g_pFluidTileRenderSlowLowResCS);
        CreatePSO(s_FluidTileRenderFastCS[1], g_pFluidTileRenderFastLowResCS);
        CreatePSO(s_FluidTileRenderSlowCS[2], g_pFluidTileRenderSlowDynamicCS);
        CreatePSO(s_FluidTileRenderFastCS[2], g_pFluidTileRenderFastDynamicCS);
    }
    CreatePSO(s_FluidDepthBoundsCS, g_pFluidDepthBoundsCS);
    CreatePSO(s_FluidSortIndirectArgsCS, g_pFluidSortIndirectArgsCS);
    CreatePSO(s_FluidPreSortCS, g_pFluidPreSortCS);

#undef CreatePSO

    //VSPS Render, no tiles.
    s_NoTileRasterizationPSO[0].SetRootSignature(RootSig);
    s_NoTileRasterizationPSO[0].SetRasterizerState(RasterizerTwoSided);
    s_NoTileRasterizationPSO[0].SetDepthStencilState(DepthStateReadOnly);
    s_NoTileRasterizationPSO[0].SetBlendState(BlendPreMultiplied);
    s_NoTileRasterizationPSO[0].SetInputLayout(0, nullptr);
    s_NoTileRasterizationPSO[0].SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    s_NoTileRasterizationPSO[0].SetRenderTargetFormat(g_SceneColorBuffer.GetFormat(), g_SceneDepthBuffer.GetFormat());
    s_NoTileRasterizationPSO[0].SetVertexShader(g_pFluidVS, sizeof(g_pFluidVS));
    s_NoTileRasterizationPSO[0].SetPixelShader(g_pFluidPS, sizeof(g_pFluidPS));
    s_NoTileRasterizationPSO[0].Finalize();

    s_NoTileRasterizationPSO[1] = s_NoTileRasterizationPSO[0];
    s_NoTileRasterizationPSO[1].SetVertexShader(g_pFluidNoSortVS, sizeof(g_pFluidNoSortVS));
    s_NoTileRasterizationPSO[1].Finalize();

    __declspec(align(16)) UINT InitialDrawIndirectArgs[4] = { 4, 0, 0, 0 };
    DrawIndirectArgs.Create(L"FluidEffectManager::DrawIndirectArgs", 1, sizeof(D3D12_DRAW_ARGUMENTS), InitialDrawIndirectArgs);
    __declspec(align(16)) UINT InitialDispatchIndirectArgs[6] = { 0, 1, 1, 0, 1, 1 };
    FinalDispatchIndirectArgs.Create(L"FluidEffectManager::FinalDispatchIndirectArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS), InitialDispatchIndirectArgs);
    SpriteVertexBuffer.Create(L"FluidEffectManager::SpriteVertexBuffer", MAX_TOTAL_PARTICLES, sizeof(FluidVertex));
    VisibleParticleBuffer.Create(L"FluidEffectManager::VisibleParticleBuffer", MAX_TOTAL_PARTICLES, sizeof(FluidScreenData));
    SpriteIndexBuffer.Create(L"FluidEffectManager::SpriteIndexBuffer", MAX_TOTAL_PARTICLES, sizeof(UINT));
    SortIndirectArgs.Create(L"FluidEffectManager::SortIndirectArgs", 1, sizeof(D3D12_DISPATCH_ARGUMENTS));
    TileDrawDispatchIndirectArgs.Create(L"FluidEffectManager::DrawPackets_IArgs", 2, sizeof(D3D12_DISPATCH_ARGUMENTS), InitialDispatchIndirectArgs);

    const uint32_t LargeBinsPerRow = DivideByMultiple(MaxDisplayWidth, 4 * BIN_SIZE_X);
    const uint32_t LargeBinsPerCol = DivideByMultiple(MaxDisplayHeight, 4 * BIN_SIZE_Y);
    const uint32_t BinsPerRow = LargeBinsPerRow * 4;
    const uint32_t BinsPerCol = LargeBinsPerCol * 4;
    const uint32_t MaxParticlesPerLargeBin = MAX_PARTICLES_PER_BIN * 16;
    const uint32_t ParticleBinCapacity = LargeBinsPerRow * LargeBinsPerCol * MaxParticlesPerLargeBin;
    const uint32_t TilesPerRow = DivideByMultiple(MaxDisplayWidth, TILE_SIZE);
    const uint32_t TilesPerCol = DivideByMultiple(MaxDisplayHeight, TILE_SIZE);

    // Padding is necessary to eliminate bounds checking when outputting data to bins or tiles.
    const uint32_t PaddedTilesPerRow = AlignUp(TilesPerRow, TILES_PER_BIN_X * 4);
    const uint32_t PaddedTilesPerCol = AlignUp(TilesPerCol, TILES_PER_BIN_Y * 4);

    BinParticles[0].Create(L"FluidEffectManager::BinParticles[0]", ParticleBinCapacity, sizeof(UINT));
    BinParticles[1].Create(L"FluidEffectManager::BinParticles[1]", ParticleBinCapacity, sizeof(UINT));
    BinCounters[0].Create(L"FluidEffectManager::LargeBinCounters", LargeBinsPerRow * LargeBinsPerCol, sizeof(UINT));
    BinCounters[1].Create(L"FluidEffectManager::BinCounters", BinsPerRow * BinsPerCol, sizeof(UINT));
    TileCounters.Create(L"FluidEffectManager::TileCounters", PaddedTilesPerRow * PaddedTilesPerCol, sizeof(UINT));
    TileHitMasks.Create(L"FluidEffectManager::TileHitMasks", PaddedTilesPerRow * PaddedTilesPerCol, MAX_PARTICLES_PER_BIN / 8);
    TileDrawPackets.Create(L"FluidEffectManager::DrawPackets", TilesPerRow * TilesPerCol, sizeof(UINT));
    TileFastDrawPackets.Create(L"FluidEffectManager::FastDrawPackets", TilesPerRow * TilesPerCol, sizeof(UINT));

    D3D12_RESOURCE_DESC TexDesc = {};
    TexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    TexDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
    TexDesc.Width = 64;
    TexDesc.Height = 64;
    TexDesc.DepthOrArraySize = 16;
    TexDesc.MipLevels = 4;
    TexDesc.SampleDesc.Count = 1;
    TexDesc.Alignment = 0x10000;

    D3D12_HEAP_PROPERTIES HeapProps = {};
    HeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    HeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    HeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    HeapProps.CreationNodeMask = 1;
    HeapProps.VisibleNodeMask = 1;

    ID3D12Resource* tex = nullptr;
    ASSERT_SUCCEEDED(g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE,
        &TexDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, MY_IID_PPV_ARGS(&tex)));
    tex->SetName(L"Fluid TexArray");
    TextureArray = GpuResource(tex, D3D12_RESOURCE_STATE_COPY_DEST);
    tex->Release();

    D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SRVDesc.Format = DXGI_FORMAT_BC3_UNORM_SRGB;
    SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.MipLevels = 4;
    SRVDesc.Texture2DArray.ArraySize = 16;

    TextureArraySRV = AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_Device->CreateShaderResourceView(TextureArray.GetResource(), &SRVDesc, TextureArraySRV);

   // if (s_ReproFrame > 0)
        //s_RNG.SetSeed(1);

    TotalElapsedFrames = 0;
    s_InitComplete = true;
}

void FluidEffectManager::RegisterTexture(uint32_t index, const Texture& texture)
{
    CommandContext::InitializeTextureArraySlice(TextureArray, index, (GpuResource&)texture);
}

void FluidEffectManager::Shutdown(void)
{
    ClearAll();

    SpriteVertexBuffer.Destroy();
    DrawIndirectArgs.Destroy();
    FinalDispatchIndirectArgs.Destroy();
    SpriteVertexBuffer.Destroy();
    VisibleParticleBuffer.Destroy();
    SpriteIndexBuffer.Destroy();
    SortIndirectArgs.Destroy();
    TileDrawDispatchIndirectArgs.Destroy();

    BinParticles[0].Destroy();
    BinParticles[1].Destroy();
    BinCounters[0].Destroy();
    BinCounters[1].Destroy();
    TileCounters.Destroy();
    TileHitMasks.Destroy();
    TileDrawPackets.Destroy();
    TileFastDrawPackets.Destroy();
    TextureArray.Destroy();
}

//Returns index into Active
EffectHandle FluidEffectManager::InstantiateEffect(FluidEffectProperties& effectProperties)
{
    if (!s_InitComplete)
        return EFFECTS_ERROR;

    static std::mutex s_InstantiateNewEffectMutex;
    s_InstantiateNewEffectMutex.lock();
    FluidEffect* newEffect = new FluidEffect(effectProperties);
    ParticleEffectsPool.emplace_back(newEffect);
    FluidEffectsActive.push_back(newEffect);
    s_InstantiateNewEffectMutex.unlock();

    EffectHandle index = (EffectHandle)FluidEffectsActive.size() - 1;
    FluidEffectsActive[index]->LoadDeviceResources(Graphics::g_Device);

    return index;
}

//---------------------------------------------------------------------
//
//	Update
//
//---------------------------------------------------------------------

void FluidEffectManager::Update(ComputeContext& Context, float timeDelta)
{
    ScopedTimer _fluidProf(L"Fluid Update", Context);

    if (!Enable || !s_InitComplete || FluidEffectsActive.size() == 0)
        return;

    if (++TotalElapsedFrames == s_ReproFrame)
        PauseSim = true;

    if (PauseSim)
        return;

    Context.ResetCounter(SpriteVertexBuffer);

    if (FluidEffectsActive.size() == 0)
        return;

    Context.SetRootSignature(RootSig);
    Context.SetConstants(0, timeDelta);
    Context.TransitionResource(SpriteVertexBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Context.SetDynamicDescriptor(3, 0, SpriteVertexBuffer.GetUAV());

    for (UINT i = 0; i < FluidEffectsActive.size(); ++i)
    {
        FluidEffectsActive[i]->Update(Context, timeDelta);

        ////Causes Thread crash
        //if (FluidEffectsActive[i]->GetLifetime() <= FluidEffectsActive[i]->GetElapsedTime())
        //{
        //    //Erase from vector
        //    auto iter = FluidEffectsActive.begin() + i;
        //    static std::mutex s_EraseFluidMutex;
        //    s_EraseFluidMutex.lock();
        //    FluidEffectsActive.erase(iter);
        //    s_EraseFluidMutex.unlock();
        //}
    }

    SetFinalBuffers(Context);
}


//---------------------------------------------------------------------
//
//	Render
//
//---------------------------------------------------------------------

void FluidEffectManager::Render(CommandContext& Context, const Camera& Camera, ColorBuffer& ColorTarget, DepthBuffer& DepthTarget, ColorBuffer& LinearDepth)
{
    if (!Enable || !s_InitComplete || FluidEffectsActive.size() == 0)
        return;

    uint32_t Width = (uint32_t)ColorTarget.GetWidth();
    uint32_t Height = (uint32_t)ColorTarget.GetHeight();

    ASSERT(
        Width == DepthTarget.GetWidth() &&
        Height == DepthTarget.GetHeight() &&
        Width == LinearDepth.GetWidth() &&
        Height == LinearDepth.GetHeight(),
        "There is a mismatch in buffer dimensions for rendering Fluids"
    );

    ScopedTimer _prof(L"Fluid Render", Context);

    uint32_t BinsPerRow = 4 * DivideByMultiple(Width, 4 * BIN_SIZE_X);

    s_ChangesPerView.gViewProj = Camera.GetViewProjMatrix();
    s_ChangesPerView.gInvView = Invert(Camera.GetViewMatrix());
    float HCot = Camera.GetProjMatrix().GetX().GetX();
    float VCot = Camera.GetProjMatrix().GetY().GetY();
    s_ChangesPerView.gVertCotangent = VCot;
    s_ChangesPerView.gAspectRatio = HCot / VCot;
    s_ChangesPerView.gRcpFarZ = 1.0f / Camera.GetFarClip();
    s_ChangesPerView.gInvertZ = Camera.GetNearClip() / (Camera.GetFarClip() - Camera.GetNearClip());
    s_ChangesPerView.gBufferWidth = (float)Width;
    s_ChangesPerView.gBufferHeight = (float)Height;
    s_ChangesPerView.gRcpBufferWidth = 1.0f / Width;
    s_ChangesPerView.gRcpBufferHeight = 1.0f / Height;
    s_ChangesPerView.gBinsPerRow = BinsPerRow;
    s_ChangesPerView.gTileRowPitch = BinsPerRow * TILES_PER_BIN_X;
    s_ChangesPerView.gTilesPerRow = DivideByMultiple(Width, TILE_SIZE);
    s_ChangesPerView.gTilesPerCol = DivideByMultiple(Height, TILE_SIZE);

    // For now, UAV load support for R11G11B10 is required to read-modify-write the color buffer, but
    // the compositing could be deferred.
    WARN_ONCE_IF(EnableTiledRendering && !g_bTypedUAVLoadSupport_R11G11B10_FLOAT,
        "Unable to composite tiled particles without support for R11G11B10F UAV loads");
    EnableTiledRendering = EnableTiledRendering && g_bTypedUAVLoadSupport_R11G11B10_FLOAT;

    if (EnableTiledRendering)
    {
        ComputeContext& CompContext = Context.GetComputeContext();
        CompContext.TransitionResource(ColorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CompContext.TransitionResource(BinCounters[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        CompContext.TransitionResource(BinCounters[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

        CompContext.ClearUAV(BinCounters[0]);
        CompContext.ClearUAV(BinCounters[1]);
        CompContext.SetRootSignature(RootSig);
        CompContext.SetDynamicConstantBufferView(1, sizeof(CBChangesPerView), &s_ChangesPerView);

        RenderTiles(CompContext, ColorTarget, LinearDepth);

        CompContext.InsertUAVBarrier(ColorTarget);
    }
    else
    {
        GraphicsContext& GrContext = Context.GetGraphicsContext();
        GrContext.SetRootSignature(RootSig);
        GrContext.SetDynamicConstantBufferView(1, sizeof(CBChangesPerView), &s_ChangesPerView);
        RenderSprites(GrContext, ColorTarget, DepthTarget, LinearDepth);
    }

}

//---------------------------------------------------------------------
//
//	Clean up
//
//---------------------------------------------------------------------

void FluidEffectManager::ClearAll()
{
    FluidEffectsActive.clear();
    ParticleEffectsPool.clear();
    TextureNameArray.clear();
}

void FluidEffectManager::ResetEffect(EffectHandle EffectID)
{
    if (!s_InitComplete || FluidEffectsActive.size() == 0 || PauseSim || EffectID >= FluidEffectsActive.size())
        return;

    FluidEffectsActive[EffectID]->Reset();
}


float FluidEffectManager::GetCurrentLife(EffectHandle EffectID)
{
    if (!s_InitComplete || FluidEffectsActive.size() == 0 || PauseSim || EffectID >= FluidEffectsActive.size())
        return -1.0;

    return FluidEffectsActive[EffectID]->GetElapsedTime();
}