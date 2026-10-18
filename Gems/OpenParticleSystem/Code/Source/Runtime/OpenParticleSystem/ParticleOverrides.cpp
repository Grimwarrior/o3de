/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ParticleOverrides.h"

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
}
