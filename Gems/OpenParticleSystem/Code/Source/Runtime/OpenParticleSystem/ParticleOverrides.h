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
#include <AzCore/RTTI/TypeInfo.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/function/function_template.h>
#include <AzCore/std/string/string.h>

namespace AZ
{
    class ReflectContext;
}

namespace SimuCore::ParticleCore
{
    class ParticleCurve;
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

    //! How a colour key blends into the next key.
    enum class ColorKeyBlend : AZ::u8
    {
        Linear,
        Step,
        EaseIn,
        EaseOut
    };

    //! One stop of a colour-over-lifetime gradient; time 0 is a particle's birth and 1 its death.
    struct ParticleColorKey
    {
        AZ_TYPE_INFO(OpenParticle::ParticleColorKey, "{3F1B7C52-8E2D-4A61-9C0B-5D7E2A94F318}");
        static void Reflect(AZ::ReflectContext* context);

        float m_time = 0.0f;
        AZ::Color m_color = AZ::Color::CreateOne();
        ColorKeyBlend m_blend = ColorKeyBlend::Linear;
    };

    //! How a Color Over Lifetime override colours particles; the random modes give each particle its own blend, fixed for its life.
    enum class ColorOverLifeOverrideMode : AZ::u8
    {
        Gradient,
        RandomColors,
        RandomGradients
    };

    //! What a Color Over Lifetime override plays. Keys are sorted by time.
    struct ColorOverLifeOverride
    {
        ColorOverLifeOverrideMode m_mode = ColorOverLifeOverrideMode::Gradient;
        AZStd::vector<ParticleColorKey> m_keys;
        AZStd::vector<ParticleColorKey> m_secondKeys;
        AZ::Color m_colorA = AZ::Color::CreateOne();
        AZ::Color m_colorB = AZ::Color(0.0f, 0.0f, 0.0f, 1.0f);
    };

    //! Curves a colour-over-lifetime override added to one simulation, per emitter: the first and second gradient, one curve per
    //! channel. The simulation owns them, and every later apply rewrites their keys, so they are made once.
    using OverrideCurves = AZStd::unordered_map<AZ::u32, AZStd::array<AZStd::array<SimuCore::ParticleCore::ParticleCurve*, 4>, 2>>;

    struct EmitterOverrides
    {
        OverrideValue<bool> m_loop;
        //! The emitter's own time scale; it multiplies with the system's playback speed.
        OverrideValue<float> m_emitterSpeed;
        OverrideValue<float> m_spawnRate;
        OverrideValue<float> m_lifetime;
        OverrideValue<float> m_flipbookFps;
        OverrideValue<AZ::Vector3> m_size;
        OverrideValue<AZ::Vector3> m_force;
        OverrideValue<AZ::Color> m_color;
        OverrideValue<ColorOverLifeOverride> m_colorOverLife;
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

        //! True when a value set in previous is no longer set here. Effector data is written in place, so the asset value
        //! only comes back by rebuilding the simulation.
        bool Drops(const ParticleOverrides& previous) const;

        //! Writes the emitter overrides into the simulation; the playback speed is read by OpenParticle::ParticleSystem.
        //! curves must belong to this simulation and start empty for a new one.
        void Apply(SimuCore::ParticleCore::ParticleSystem& system, OverrideCurves& curves) const;

        EmitterOverrides m_all;
        AZStd::unordered_map<AZ::u32, EmitterOverrides> m_emitters;
        OverrideValue<float> m_playbackSpeed;
    };

    enum class LoopOverride : AZ::u8
    {
        Asset,
        On,
        Off
    };

    //! Values authored in the Particle component card for one emitter, or for every emitter. A value applies only while its
    //! toggle is on.
    struct EmitterOverrideSettings
    {
        AZ_TYPE_INFO(OpenParticle::EmitterOverrideSettings, "{6DA4933E-E4C8-49A9-B2CB-6FB2B9F3D758}");
        static void Reflect(AZ::ReflectContext* context);

        void AddTo(EmitterOverrides& overrides) const;

        //! Turns every override off. Values are kept; the editor sets them back to the asset's.
        void ClearToggles();

        static AZStd::vector<ParticleColorKey> DefaultColorOverLife();

        AZStd::string GetColorKeyLabel(int index) const;
        AZStd::string GetSecondColorKeyLabel(int index) const;
        AZ::u32 OnResetPressed();

        bool ShowsColorOverLifeMode() const
        {
            return m_overrideColorOverLife;
        }
        bool ShowsColorKeys() const
        {
            return m_overrideColorOverLife && m_colorOverLifeMode != ColorOverLifeOverrideMode::RandomColors;
        }
        bool ShowsSecondColorKeys() const
        {
            return m_overrideColorOverLife && m_colorOverLifeMode == ColorOverLifeOverrideMode::RandomGradients;
        }
        bool ShowsRandomColors() const
        {
            return m_overrideColorOverLife && m_colorOverLifeMode == ColorOverLifeOverrideMode::RandomColors;
        }

        //! Set by the editor component to reset this group with undo; not serialized, and only the Reset button calls it.
        AZStd::function<void()> m_reset;

        //! The emitter's name in the .particle file and its position there, which is its index at runtime. The editor
        //! re-matches entries by name when the asset changes.
        AZStd::string m_emitterName;
        AZ::u32 m_emitterIndex = 0;

        LoopOverride m_loop = LoopOverride::Asset;
        bool m_overrideEmitterSpeed = false;
        float m_emitterSpeed = 1.0f;
        bool m_overrideSpawnRate = false;
        float m_spawnRate = 10.0f;
        bool m_overrideLifetime = false;
        float m_lifetime = 1.0f;
        bool m_overrideSize = false;
        AZ::Vector3 m_size = AZ::Vector3::CreateOne();
        bool m_overrideColor = false;
        AZ::Color m_color = AZ::Color::CreateOne();
        bool m_overrideColorOverLife = false;
        ColorOverLifeOverrideMode m_colorOverLifeMode = ColorOverLifeOverrideMode::Gradient;
        AZStd::vector<ParticleColorKey> m_colorOverLife = DefaultColorOverLife();
        AZStd::vector<ParticleColorKey> m_secondColorOverLife = DefaultColorOverLife();
        AZ::Color m_randomColorA = AZ::Color::CreateOne();
        AZ::Color m_randomColorB = AZ::Color(0.0f, 0.0f, 0.0f, 1.0f);
        bool m_overrideForce = false;
        AZ::Vector3 m_force = AZ::Vector3(0.0f, 0.0f, -9.8f);
        bool m_overrideFlipbookFps = false;
        float m_flipbookFps = 30.0f;
    };

    //! The overrides a Particle component starts with; scripts can still change them through ParticleRequestBus.
    struct ParticleOverrideSettings
    {
        AZ_TYPE_INFO(OpenParticle::ParticleOverrideSettings, "{05240E48-5718-4AB4-84B2-793BD600209D}");
        static void Reflect(AZ::ReflectContext* context);

        //! A per-emitter value wins over the same value set for every emitter.
        ParticleOverrides ToOverrides() const;

        AZStd::string GetEmitterLabel(int index) const;
        AZ::u32 OnResetAllPressed();

        //! Set by the editor component to reset every override with undo; not serialized, and only the Reset button calls it.
        AZStd::function<void()> m_resetAll;

        bool m_overridePlaybackSpeed = false;
        float m_playbackSpeed = 1.0f;
        EmitterOverrideSettings m_allEmitters;
        AZStd::vector<EmitterOverrideSettings> m_emitters;
    };
}

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(OpenParticle::LoopOverride, "{9888BBD9-10B1-42EF-96B7-C0CE3942686B}");
    AZ_TYPE_INFO_SPECIALIZE(OpenParticle::ColorKeyBlend, "{B2D64A1E-7C39-4F85-A0E3-61C9D8F25B47}");
    AZ_TYPE_INFO_SPECIALIZE(OpenParticle::ColorOverLifeOverrideMode, "{1F0BD104-29DF-4E6A-AF80-3E7B1E3C2D28}");
}
