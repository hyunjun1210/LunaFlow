#pragma once
#include "Core/Serialization.h"

namespace LunaFlow {
enum class ParticlePreset { Custom, Fire, Smoke, Magic, Snow, Rain, Explosion };
struct ParticleSample {
    XMFLOAT3 position;
    XMFLOAT4 color;
    float size=0;
    double birth=0; // seconds relative to the effect start
};
ParticleSettings MakeParticleSettings(ParticlePreset);
EffectData MakeParticleEffect(ParticlePreset,uint32_t bone,const std::string& clipId,double duration);
EffectData GenerateParticleEffect(const std::string& prompt,uint32_t bone,const std::string& clipId,double duration);
void ValidateParticles(const ParticleSettings&);
Json EncodeParticles(const ParticleSettings&);
ParticleSettings DecodeParticles(const Json&);
// Pure evaluation makes pause, scrubbing and replay independent of frame rate.
std::vector<ParticleSample> SampleParticles(const EffectData&,double time);
}
