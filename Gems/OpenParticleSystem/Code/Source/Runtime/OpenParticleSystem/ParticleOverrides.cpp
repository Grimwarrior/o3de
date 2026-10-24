/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ParticleOverrides.h"

#include <AzCore/Serialization/SerializeContext.h>

#include <particle/core/ParticleEmitter.h>
#include <particle/core/ParticleSystem.h>
#include <particle/emit/ParticleEmit.h>
#include <particle/spawn/SpawnColor.h>
#include <particle/spawn/SpawnLifetime.h>
#include <particle/spawn/SpawnSize.h>
#include <particle/update/UpdateColor.h>
#include <particle/update/UpdateForce.h>
#include <particle/update/UpdateSubUv.h>

namespace OpenParticle
{
    namespace
    {
        namespace Core = SimuCore::ParticleCore;

        // Effector data lives in the instance's own data pool, so writing it never touches the shared asset.
        template<typename Effector, typename InfoList>
        Effector* FindData(const InfoList& list)
        {
            const auto name = Core::TypeInfo<Effector>::Name();
            for (const auto& info : list)
            {
                if (AZStd::string_view(info.effector->Name()) == name)
                {
                    return reinterpret_cast<Effector*>(info.data);
                }
            }
            return nullptr;
        }

        template<typename T, AZ::u32 N>
        void SetConstant(Core::ValueObject<T, N>& object, const T& value)
        {
            object.dataValue = value;
            object.distType = Core::DistributionType::CONSTANT;
            object.isUniform = false;
        }

        // A channel without a bound distribution keeps its constant value, so a colour curve on alpha survives an rgb override.
        void SetColorRgb(Core::ValueObjColor& object, const AZ::Color& color)
        {
            if (object.distType == Core::DistributionType::CONSTANT || object.isUniform)
            {
                SetConstant(object, color);
                return;
            }
            object.dataValue = AZ::Color(color.GetR(), color.GetG(), color.GetB(), object.dataValue.GetA());
            for (AZ::u32 channel = 0; channel < 3; ++channel)
            {
                object.distributions[channel] = nullptr;
            }
        }

        void ApplyToEmitter(Core::ParticleEmitter& emitter, const EmitterOverrides& o)
        {
            if (o.m_loop.m_set)
            {
                emitter.GetEmitterConfig()->loop = o.m_loop.m_value;
            }
            if (o.m_spawnRate.m_set)
            {
                if (auto* data = FindData<Core::EmitSpawn>(emitter.GetEmitEffectors()))
                {
                    SetConstant(data->spawnRate, o.m_spawnRate.m_value);
                }
            }
            if (o.m_lifetime.m_set)
            {
                if (auto* data = FindData<Core::SpawnLifetime>(emitter.GetSpawnEffectors()))
                {
                    SetConstant(data->lifeTime, o.m_lifetime.m_value);
                }
            }
            if (o.m_size.m_set)
            {
                if (auto* data = FindData<Core::SpawnSize>(emitter.GetSpawnEffectors()))
                {
                    SetConstant(data->size, o.m_size.m_value);
                }
            }
            if (o.m_color.m_set)
            {
                if (auto* data = FindData<Core::SpawnColor>(emitter.GetSpawnEffectors()))
                {
                    SetColorRgb(data->startColor, o.m_color.m_value);
                }
                if (auto* data = FindData<Core::UpdateColor>(emitter.GetUpdateEffectors()))
                {
                    SetColorRgb(data->currentColor, o.m_color.m_value);
                }
            }
            if (o.m_force.m_set)
            {
                if (auto* data = FindData<Core::UpdateConstForce>(emitter.GetUpdateEffectors()))
                {
                    SetConstant(data->force, o.m_force.m_value);
                }
            }
            if (o.m_flipbookFps.m_set)
            {
                if (auto* data = FindData<Core::UpdateSubUv>(emitter.GetUpdateEffectors()))
                {
                    data->framePerSecond = static_cast<AZ::u32>(AZStd::max(0.0f, o.m_flipbookFps.m_value) + 0.5f);
                }
            }
        }

        bool HasAny(const EmitterOverrides& o)
        {
            return (o.m_loop.m_set || o.m_spawnRate.m_set || o.m_lifetime.m_set || o.m_flipbookFps.m_set || o.m_size.m_set ||
                     o.m_force.m_set || o.m_color.m_set);
        }

        template<typename T>
        bool Dropped(const OverrideValue<T>& before, const OverrideValue<T>& after)
        {
            return before.m_set && !after.m_set;
        }

        bool DropsAny(const EmitterOverrides& before, const EmitterOverrides& after)
        {
            return (Dropped(before.m_loop, after.m_loop) || Dropped(before.m_spawnRate, after.m_spawnRate) ||
                    Dropped(before.m_lifetime, after.m_lifetime) || Dropped(before.m_flipbookFps, after.m_flipbookFps) ||
                    Dropped(before.m_size, after.m_size) || Dropped(before.m_force, after.m_force) ||
                    Dropped(before.m_color, after.m_color));
        }
    } // namespace

    bool ParticleOverrides::IsEmpty() const
    {
        if (m_playbackSpeed.m_set || HasAny(m_all))
        {
            return false;
        }
        for (const auto& entry : m_emitters)
        {
            if (HasAny(entry.second))
            {
                return false;
            }
        }
        return true;
    }

    void ParticleOverrides::Apply(Core::ParticleSystem& system) const
    {
        const auto emitters = system.GetAllEmitters();
        for (const auto& [emitterId, emitter] : emitters)
        {
            ApplyToEmitter(*emitter, m_all);
            if (auto found = m_emitters.find(emitterId); found != m_emitters.end())
            {
                ApplyToEmitter(*emitter, found->second);
            }
        }
    }

    bool ParticleOverrides::Drops(const ParticleOverrides& previous) const
    {
        if (DropsAny(previous.m_all, m_all))
        {
            return true;
        }
        for (const auto& [emitterIndex, before] : previous.m_emitters)
        {
            const auto found = m_emitters.find(emitterIndex);
            if (DropsAny(before, found != m_emitters.end() ? found->second : EmitterOverrides{}))
            {
                return true;
            }
        }
        return false;
    }

    void EmitterOverrideSettings::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Enum<LoopOverride>()
                ->Value("Asset", LoopOverride::Asset)
                ->Value("On", LoopOverride::On)
                ->Value("Off", LoopOverride::Off);

            serializeContext->Class<EmitterOverrideSettings>()
                ->Version(0)
                ->Field("EmitterName", &EmitterOverrideSettings::m_emitterName)
                ->Field("EmitterIndex", &EmitterOverrideSettings::m_emitterIndex)
                ->Field("Loop", &EmitterOverrideSettings::m_loop)
                ->Field("OverrideSpawnRate", &EmitterOverrideSettings::m_overrideSpawnRate)
                ->Field("SpawnRate", &EmitterOverrideSettings::m_spawnRate)
                ->Field("OverrideLifetime", &EmitterOverrideSettings::m_overrideLifetime)
                ->Field("Lifetime", &EmitterOverrideSettings::m_lifetime)
                ->Field("OverrideSize", &EmitterOverrideSettings::m_overrideSize)
                ->Field("Size", &EmitterOverrideSettings::m_size)
                ->Field("OverrideColor", &EmitterOverrideSettings::m_overrideColor)
                ->Field("Color", &EmitterOverrideSettings::m_color)
                ->Field("OverrideForce", &EmitterOverrideSettings::m_overrideForce)
                ->Field("Force", &EmitterOverrideSettings::m_force)
                ->Field("OverrideFlipbookFps", &EmitterOverrideSettings::m_overrideFlipbookFps)
                ->Field("FlipbookFps", &EmitterOverrideSettings::m_flipbookFps);
        }
    }

    void EmitterOverrideSettings::AddTo(EmitterOverrides& overrides) const
    {
        if (m_loop != LoopOverride::Asset)
        {
            overrides.m_loop.Set(m_loop == LoopOverride::On);
        }
        if (m_overrideSpawnRate)
        {
            overrides.m_spawnRate.Set(m_spawnRate);
        }
        if (m_overrideLifetime)
        {
            overrides.m_lifetime.Set(m_lifetime);
        }
        if (m_overrideSize)
        {
            overrides.m_size.Set(m_size);
        }
        if (m_overrideColor)
        {
            overrides.m_color.Set(m_color);
        }
        if (m_overrideForce)
        {
            overrides.m_force.Set(m_force);
        }
        if (m_overrideFlipbookFps)
        {
            overrides.m_flipbookFps.Set(m_flipbookFps);
        }
    }

    void ParticleOverrideSettings::Reflect(AZ::ReflectContext* context)
    {
        EmitterOverrideSettings::Reflect(context);

        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ParticleOverrideSettings>()
                ->Version(0)
                ->Field("OverridePlaybackSpeed", &ParticleOverrideSettings::m_overridePlaybackSpeed)
                ->Field("PlaybackSpeed", &ParticleOverrideSettings::m_playbackSpeed)
                ->Field("AllEmitters", &ParticleOverrideSettings::m_allEmitters)
                ->Field("Emitters", &ParticleOverrideSettings::m_emitters);
        }
    }

    ParticleOverrides ParticleOverrideSettings::ToOverrides() const
    {
        ParticleOverrides overrides;
        if (m_overridePlaybackSpeed)
        {
            overrides.m_playbackSpeed.Set(m_playbackSpeed);
        }
        m_allEmitters.AddTo(overrides.m_all);
        for (const auto& emitter : m_emitters)
        {
            emitter.AddTo(overrides.m_emitters[emitter.m_emitterIndex]);
        }
        return overrides;
    }

    AZStd::string ParticleOverrideSettings::GetEmitterLabel(int index) const
    {
        if (index >= 0 && index < static_cast<int>(m_emitters.size()) && !m_emitters[index].m_emitterName.empty())
        {
            return m_emitters[index].m_emitterName;
        }
        return AZStd::string::format("Emitter %d", index);
    }
}
