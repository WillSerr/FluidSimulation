#include "FluidEffects.h"
#include "TextureManager.h"

using namespace std;
using namespace Graphics;
using namespace Math;

namespace FluidEffects
{
    map<wstring, uint32_t> s_TextureArrayLookup;
    vector<TextureRef> s_TextureReferences;

    uint32_t GetTextureIndex(const wstring& name)
    {
        // Look for the texture already being assigned an index.  If it's not found,
        // load the texture and assign it an index.
        auto iter = s_TextureArrayLookup.find(name);

        if (iter != s_TextureArrayLookup.end())
        {
            return iter->second;
        }
        else
        {
            uint32_t index = (uint32_t)s_TextureArrayLookup.size();
            s_TextureArrayLookup[name] = index;

            // Load the texture and register it with the effect manager
            TextureRef texture = TextureManager::LoadDDSFromFile(name, kMagenta2D, true);
            s_TextureReferences.push_back(texture);
            FluidEffectManager::RegisterTexture(index, *texture.Get());

            return index;
        }
    }

    void InstantiateEffect(FluidEffectProperties& effectProperties)
    {
        effectProperties.EmitProperties.TextureID = FluidEffects::GetTextureIndex(effectProperties.TexturePath);
        FluidEffectManager::InstantiateEffect(effectProperties);
    }
}