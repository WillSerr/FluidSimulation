#pragma once


#include "FluidShaderStructs.h"
#include "Color.h"
#include <string>

struct FluidEffectProperties
{
    FluidEffectProperties()
    {
        MinStartColor = Color(0.8f, 0.8f, 1.0f);
        MaxStartColor = Color(0.9f, 0.9f, 1.0f);
        MinEndColor = Color(1.0f, 1.0f, 1.0f);
        MaxEndColor = Color(1.0f, 1.0f, 1.0f);
        EmitProperties = *CreateFluidEmissionProperties(); //Properties passed to the shader
        EmitRate = 200;
        LifeMinMax = XMFLOAT2(1.0f, 2.0f);
        MassMinMax = XMFLOAT2(0.5f, 1.0f);
        Size = Math::Vector4(0.07f, 0.7f, 0.8f, 0.8f); // (Start size min, Start size max, End size min, End size max) 		
        Spread = XMFLOAT3(0.5f, 1.5f, 0.1f);
        TexturePath = L"FluidScene/BallSprit.dds";
        TotalActiveLifetime = 20.0;
        Velocity = Math::Vector4(0.5, 3.0, -0.5, 3.0); // (X velocity min, X velocity max, Y velocity min, Y velocity max)
    };


    Color MinStartColor;
    Color MaxStartColor;
    Color MinEndColor;
    Color MaxEndColor;
    FluidEmissionProperties  EmitProperties;
    float EmitRate;
    XMFLOAT2 LifeMinMax;
    XMFLOAT2 MassMinMax;
    Math::Vector4 Size;
    XMFLOAT3 Spread;
    std::wstring TexturePath;
    float TotalActiveLifetime;
    Math::Vector4 Velocity;

};


