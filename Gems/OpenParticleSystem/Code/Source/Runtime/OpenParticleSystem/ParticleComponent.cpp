/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ParticleComponent.h"

#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <OpenParticleSystem/ParticleFeatureProcessor.h>

namespace OpenParticle
{

    ParticleComponent::ParticleComponent(const ParticleComponentConfig& config)
        : BaseClass(config)
    {
    }

    void ParticleComponent::Reflect(AZ::ReflectContext* context)
    {
        BaseClass::Reflect(context);
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ParticleComponent, BaseClass>()->Version(0);
        }

        if (AZ::BehaviorContext* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->EBus<ParticleRequestBus>("ParticleRequestBus")
                ->Attribute(AZ::Script::Attributes::Module, "OpenParticleSystem")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                ->Event("Play", &ParticleRequestBus::Events::Play)
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Notify particle system to play")
                ->Event("Pause", &ParticleRequestBus::Events::Pause)
                    ->Attribute(AZ::Script::Attributes::ToolTip,  "Notify particle system to pause simulation and keep rendering")
                ->Event("Stop", &ParticleRequestBus::Events::Stop)
                    ->Attribute(AZ::Script::Attributes::ToolTip,  "Notify particle system to stop simulation and rendering")
                ->Event("SetVisible", &ParticleRequestBus::Events::SetVisibility)
                    ->Attribute(AZ::Script::Attributes::Deprecated, true)
                    ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::ExcludeFlags::List)
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Visibility modification no longer needed in game mode")
                ->Event("GetVisible", &ParticleRequestBus::Events::GetVisibility)
                    ->Attribute(AZ::Script::Attributes::Deprecated, true)
                    ->Attribute(AZ::Script::Attributes::ExcludeFrom, AZ::Script::Attributes::ExcludeFlags::List)
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Visibility modification no longer needed in game mode")
                                ->VirtualProperty("Visible", "GetVisible", "SetVisible")
                ->Event("SetLooping", &ParticleRequestBus::Events::SetLooping,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Loop") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Turn emitter looping on or off, a non-looping emitter plays once")
                ->Event("SetSpawnRate", &ParticleRequestBus::Events::SetSpawnRate,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Rate", "Particles per second") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the spawn rate, replaces any curve or random value")
                ->Event("SetLifetime", &ParticleRequestBus::Events::SetLifetime,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Seconds") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the lifetime of particles spawned from now on")
                ->Event("SetSize", &ParticleRequestBus::Events::SetSize,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Size") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the size of particles spawned from now on")
                ->Event("SetColor", &ParticleRequestBus::Events::SetColor,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Color") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the particle colour, an alpha curve from the asset is kept")
                ->Event("SetForce", &ParticleRequestBus::Events::SetForce,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Force") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the constant force such as gravity or wind")
                ->Event("SetFlipbookFps", &ParticleRequestBus::Events::SetFlipbookFps,
                    { AZ::BehaviorParameterOverrides("Emitter", "Emitter index in the asset, -1 for all"), AZ::BehaviorParameterOverrides("Fps", "Frames per second") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Override the flipbook (SubUV) frame rate")
                ->Event("SetPlaybackSpeed", &ParticleRequestBus::Events::SetPlaybackSpeed,
                    { AZ::BehaviorParameterOverrides("Speed", "1 is normal, 0.5 is slow motion") })
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Scale the simulation speed of the whole system")
                ->Event("ClearOverrides", &ParticleRequestBus::Events::ClearOverrides)
                    ->Attribute(AZ::Script::Attributes::ToolTip, "Drop every override and restore the asset values by restarting the system");

            behaviorContext->Class<ParticleComponent>()->RequestBus("ParticleRequestBus");
        }
    }
} // namespace OpenParticle
