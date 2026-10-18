/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ParticleMotionEventsComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <OpenParticleSystem/ParticleRequestBus.h>
#include <OpenParticleSystem/ParticleTargetBus.h>

namespace OpenParticle
{
    void ParticleMotionEventEntry::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Enum<MotionEventParticleAction>()
                ->Value("None", MotionEventParticleAction::None)
                ->Value("Play", MotionEventParticleAction::Play)
                ->Value("Restart", MotionEventParticleAction::Restart)
                ->Value("Stop", MotionEventParticleAction::Stop)
                ->Value("Pause", MotionEventParticleAction::Pause)
                ->Value("StopSpawning", MotionEventParticleAction::StopSpawning);

            serializeContext->Class<ParticleMotionEventEntry>()
                ->Version(0)
                ->Field("EventName", &ParticleMotionEventEntry::m_eventName)
                ->Field("Parameter", &ParticleMotionEventEntry::m_parameter)
                ->Field("ParticleEntityName", &ParticleMotionEventEntry::m_particleEntityName)
                ->Field("ParticleEntity", &ParticleMotionEventEntry::m_particleEntity)
                ->Field("OnStart", &ParticleMotionEventEntry::m_onStart)
                ->Field("OnEnd", &ParticleMotionEventEntry::m_onEnd);

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Enum<MotionEventParticleAction>("Motion Event Particle Action", "What a motion event does to a Particle component.")
                    ->Value("None", MotionEventParticleAction::None)
                    ->Value("Play", MotionEventParticleAction::Play)
                    ->Value("Restart", MotionEventParticleAction::Restart)
                    ->Value("Stop", MotionEventParticleAction::Stop)
                    ->Value("Pause", MotionEventParticleAction::Pause)
                    ->Value("StopSpawning", MotionEventParticleAction::StopSpawning);

                editContext->Class<ParticleMotionEventEntry>("Particle Motion Event", "A motion event and what it does to a Particle component.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::NameLabelOverride, &ParticleMotionEventEntry::GetLabel)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventEntry::m_eventName, "Event Name",
                        "The subject of the motion event, as named in the Motion Events editor.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventEntry::m_parameter, "Parameter",
                        "Only events with this parameter react; empty reacts to any.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventEntry::m_particleEntityName, "Particle Entity Name",
                        "Name of the entity with the Particle component, looked up when the event fires: first in the character's equipment (the held weapon's Trail), then among this entity's children. Empty uses Particle Entity.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventEntry::m_particleEntity, "Particle Entity",
                        "The entity with the Particle component when no name is given; empty uses this entity.")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ParticleMotionEventEntry::m_onStart, "On Start",
                        "What the event's start does (a one-shot event only has a start). Restart replays from the beginning; Stop Spawning lets the particles already out fade by their own lifetime.")
                        ->EnumAttribute(MotionEventParticleAction::None, "None")
                        ->EnumAttribute(MotionEventParticleAction::Play, "Play")
                        ->EnumAttribute(MotionEventParticleAction::Restart, "Restart")
                        ->EnumAttribute(MotionEventParticleAction::Stop, "Stop")
                        ->EnumAttribute(MotionEventParticleAction::Pause, "Pause")
                        ->EnumAttribute(MotionEventParticleAction::StopSpawning, "Stop Spawning")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ParticleMotionEventEntry::m_onEnd, "On End",
                        "What the end of a ranged event does.")
                        ->EnumAttribute(MotionEventParticleAction::None, "None")
                        ->EnumAttribute(MotionEventParticleAction::Play, "Play")
                        ->EnumAttribute(MotionEventParticleAction::Restart, "Restart")
                        ->EnumAttribute(MotionEventParticleAction::Stop, "Stop")
                        ->EnumAttribute(MotionEventParticleAction::Pause, "Pause")
                        ->EnumAttribute(MotionEventParticleAction::StopSpawning, "Stop Spawning");
            }
        }
    }

    AZStd::string ParticleMotionEventEntry::GetLabel() const
    {
        return m_eventName.empty() ? AZStd::string("(no event)") : m_eventName;
    }

    void ParticleMotionEventsComponent::Reflect(AZ::ReflectContext* context)
    {
        ParticleMotionEventEntry::Reflect(context);

        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ParticleMotionEventsComponent, AZ::Component>()
                ->Version(0)
                ->Field("Entries", &ParticleMotionEventsComponent::m_entries)
                ->Field("MinWeight", &ParticleMotionEventsComponent::m_minWeight);

            if (auto editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ParticleMotionEventsComponent>("Particle Motion Events",
                    "Plays, restarts, pauses or stops Particle components from the motion events of this entity's actor, e.g. a trail from the start to the end of a swing.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::Category, "Graphics/Effects")
                        ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventsComponent::m_entries, "Events",
                        "The motion events to react to.")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleMotionEventsComponent::m_minWeight, "Min Weight",
                        "Events from a clip playing with less weight than this are ignored. 0 keeps every event; raise it to skip clips that are fading in or out, but an event at the very start of a clip then needs the clip to have blended in first.")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f);
            }
        }
    }

    void ParticleMotionEventsComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("ParticleMotionEventsService"));
    }

    void ParticleMotionEventsComponent::Activate()
    {
        EMotionFX::Integration::ActorNotificationBus::Handler::BusConnect(GetEntityId());
    }

    void ParticleMotionEventsComponent::Deactivate()
    {
        m_spawnStopped.clear();
        EMotionFX::Integration::ActorNotificationBus::Handler::BusDisconnect();
    }

    void ParticleMotionEventsComponent::OnMotionEvent(EMotionFX::Integration::MotionEvent motionEvent)
    {
        if (!motionEvent.m_eventTypeName)
        {
            return;
        }

        const AZStd::string_view subject = motionEvent.m_eventTypeName;
        const AZStd::string_view parameter = motionEvent.m_parameter ? AZStd::string_view(motionEvent.m_parameter) : AZStd::string_view();
        for (const ParticleMotionEventEntry& entry : m_entries)
        {
            if (entry.m_eventName != subject || (!entry.m_parameter.empty() && entry.m_parameter != parameter))
            {
                continue;
            }
            if (motionEvent.m_globalWeight < m_minWeight)
            {
                AZ_TracePrintf("Particle", "Particle Motion Events: '%s' ignored, clip weight %.2f is below Min Weight %.2f", entry.m_eventName.c_str(),
                    motionEvent.m_globalWeight, m_minWeight);
                continue;
            }
            Apply(entry, motionEvent.m_isEventStart ? entry.m_onStart : entry.m_onEnd);
        }
    }

    AZ::EntityId ParticleMotionEventsComponent::ResolveTarget(const ParticleMotionEventEntry& entry) const
    {
        if (entry.m_particleEntityName.empty())
        {
            return entry.m_particleEntity.IsValid() ? entry.m_particleEntity : GetEntityId();
        }

        AZ::EntityId found;
        ParticleTargetRequestBus::EventResult(found, GetEntityId(), &ParticleTargetRequestBus::Events::FindParticleEntity, entry.m_particleEntityName);
        if (found.IsValid())
        {
            return found;
        }

        AZStd::vector<AZ::EntityId> descendants;
        AZ::TransformBus::EventResult(descendants, GetEntityId(), &AZ::TransformBus::Events::GetAllDescendants);
        for (const AZ::EntityId& descendant : descendants)
        {
            AZStd::string name;
            AZ::ComponentApplicationBus::BroadcastResult(name, &AZ::ComponentApplicationRequests::GetEntityName, descendant);
            if (name == entry.m_particleEntityName)
            {
                return descendant;
            }
        }
        return AZ::EntityId();
    }

    void ParticleMotionEventsComponent::ClearSpawnStop(AZ::EntityId target)
    {
        // The zeroed spawn rate is an override and would survive Stop and Play, so it is dropped before the particle plays again.
        if (m_spawnStopped.erase(target) > 0)
        {
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::ClearOverrides);
        }
    }

    void ParticleMotionEventsComponent::Apply(const ParticleMotionEventEntry& entry, MotionEventParticleAction action)
    {
        const AZ::EntityId target = ResolveTarget(entry);
        if (!target.IsValid())
        {
            return;
        }
        switch (action)
        {
        case MotionEventParticleAction::Play:
            ClearSpawnStop(target);
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::Play);
            break;
        case MotionEventParticleAction::Restart:
            ClearSpawnStop(target);
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::Stop);
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::Play);
            break;
        case MotionEventParticleAction::Stop:
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::Stop);
            break;
        case MotionEventParticleAction::Pause:
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::Pause);
            break;
        case MotionEventParticleAction::StopSpawning:
            ParticleRequestBus::Event(target, &ParticleRequestBus::Events::SetSpawnRate, -1, 0.0f);
            m_spawnStopped.insert(target);
            break;
        case MotionEventParticleAction::None:
        default:
            break;
        }
    }
} // namespace OpenParticle
