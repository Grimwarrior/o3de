/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/unordered_map.h>

namespace SimuCore::ParticleCore
{
    class ParticleSystem;
}

namespace OpenParticle
{
    template<typename T>
    struct OverrideValue
    {
        void Set(const T& value)
        {
            m_value = value;
            m_set = true;
        }

        T m_value{};
        bool m_set = false;
    };

    struct EmitterOverrides
    {
        OverrideValue<bool> m_loop;
        OverrideValue<float> m_spawnRate;
        OverrideValue<float> m_lifetime;
        OverrideValue<float> m_flipbookFps;
        OverrideValue<AZ::Vector3> m_size;
        OverrideValue<AZ::Vector3> m_force;
        OverrideValue<AZ::Color> m_color;
    };

    //! Script-set values that are written into a particle instance's own effector data, so the per-particle update path is untouched.
    struct ParticleOverrides
    {
        //! emitterIndex -1 targets every emitter and clears the same field on the per-emitter entries so the latest call wins.
        template<typename T>
        void Set(int emitterIndex, OverrideValue<T> EmitterOverrides::*field, const T& value)
        {
            if (emitterIndex < 0)
            {
                (m_all.*field).Set(value);
                for (auto& entry : m_emitters)
                {
                    (entry.second.*field).m_set = false;
                }
                return;
            }
            (m_emitters[static_cast<AZ::u32>(emitterIndex)].*field).Set(value);
        }

        bool IsEmpty() const;

        //! Writes the emitter overrides into the simulation; the playback speed is read by OpenParticle::ParticleSystem.
        void Apply(SimuCore::ParticleCore::ParticleSystem& system) const;

        EmitterOverrides m_all;
        AZStd::unordered_map<AZ::u32, EmitterOverrides> m_emitters;
        OverrideValue<float> m_playbackSpeed;
    };
}
