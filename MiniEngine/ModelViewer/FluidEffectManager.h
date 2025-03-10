#pragma once

#include "FluidEffectProperties.h"
#include "FluidEffect.h"
#include "CommandContext.h"
#include "Math/Random.h" //Removed? for fluid

namespace Math
{
    class Camera;
}


namespace FluidEffectManager
{
    void Initialize(uint32_t MaxDisplayWidth, uint32_t MaxDisplayHeight);
    void Shutdown();
    void ClearAll();

    void Update(ComputeContext& Context, float timeDelta);

    typedef uint32_t EffectHandle;
    EffectHandle InstantiateEffect(FluidEffectProperties& effectProperties);
    void Update(ComputeContext& Context, float timeDelta);
    void Render(CommandContext& Context, const Math::Camera& Camera, ColorBuffer& ColorTarget, DepthBuffer& DepthTarget, ColorBuffer& LinearDepth);
    void ResetEffect(EffectHandle EffectID);
    float GetCurrentLife(EffectHandle EffectID);
    void RegisterTexture(uint32_t index, const Texture& texture);

    extern BoolVar Enable;
    extern BoolVar PauseSim;
    extern BoolVar EnableTiledRendering;
    extern bool Reproducible; //If you want to repro set to true. When true, effect uses the same set of random numbers each run
    extern UINT ReproFrame;
} // namespace ParticleEffectManager
