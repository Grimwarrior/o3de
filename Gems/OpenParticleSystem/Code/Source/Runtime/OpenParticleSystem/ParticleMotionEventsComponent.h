/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <Integration/AnimationBus.h>

namespace OpenParticle
{
    // What a motion event does to a Particle component.
    enum class MotionEventParticleAction : AZ::u8
    {
        None,
        Play,
        Restart,
        Stop,
        Pause
    };

    // One motion event name and what it does to the Particle component on the target entity.
    struct ParticleMotionEventEntry
    {
        AZ_TYPE_INFO(ParticleMotionEventEntry, "{7A5D91C2-3B64-4E0F-9C1D-5E2B8F4A6D13}");
        AZ_CLASS_ALLOCATOR(ParticleMotionEventEntry, AZ::SystemAllocator);

        static void Reflect(AZ::ReflectContext* context);
        AZStd::string GetLabel() const;

        // The event's subject (the name in the Motion Events editor).
        AZStd::string m_eventName;
        // Only events with this parameter react; empty = any.
        AZStd::string m_parameter;
        // The entity with the Particle component, by name: found among the character's equipment (Particle Target bus), then among this entity's descendants.
        AZStd::string m_particleEntityName;
        // The entity with the Particle component when no name is given; empty = this entity.
        AZ::EntityId m_particleEntity;
        // For a ranged event: its start. A one-shot event only has a start.
        MotionEventParticleAction m_onStart = MotionEventParticleAction::Restart;
        // For a ranged event: its end.
        MotionEventParticleAction m_onEnd = MotionEventParticleAction::Stop;
    };

    // Plays, restarts, pauses or stops Particle components from the motion events of this entity's actor, e.g. a trail from the start to the end of a swing.
    class ParticleMotionEventsComponent
        : public AZ::Component
        , private EMotionFX::Integration::ActorNotificationBus::Handler
    {
    public:
        AZ_COMPONENT(ParticleMotionEventsComponent, "{C3E8A41D-6F27-4B95-8A0E-1D7B52F9E6A4}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

    protected:
        void Activate() override;
        void Deactivate() override;

    private:
        // ActorNotificationBus
        void OnMotionEvent(EMotionFX::Integration::MotionEvent motionEvent) override;

        void Apply(const ParticleMotionEventEntry& entry, MotionEventParticleAction action) const;
        AZ::EntityId ResolveTarget(const ParticleMotionEventEntry& entry) const;

        AZStd::vector<ParticleMotionEventEntry> m_entries;
        // Events from clips that are fading out (weight below this) are ignored, so a blend does not fire twice.
        float m_minWeight = 0.5f;
    };
} // namespace OpenParticle

namespace AZ
{
    AZ_TYPE_INFO_SPECIALIZE(OpenParticle::MotionEventParticleAction, "{E4B27D68-91A3-4C5F-B0D2-6A8F13C79E05}");
}
