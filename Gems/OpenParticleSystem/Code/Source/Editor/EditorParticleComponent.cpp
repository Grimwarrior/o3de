/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "EditorParticleComponent.h"
#include "EditorParticleSystemComponentRequestBus.h"
#include <OpenParticleSystem/ParticleFeatureProcessorInterface.h>

#include <API/EditorAssetSystemAPI.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Utils/Utils.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzFramework/Translation/TranslationDef.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>

namespace OpenParticle
{
    void EditorParticleComponent::Reflect(AZ::ReflectContext* context)
    {
        BaseClass::Reflect(context);

        auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context);
        if (serializeContext != nullptr)
        {
            serializeContext->Class<EditorParticleComponent, BaseClass>()->Version(0);

            AZ::EditContext* editContext = serializeContext->GetEditContext();
            if (editContext != nullptr)
            {
                editContext->Class<EditorParticleComponent>(QT_TRANSLATE_NOOP("OpenParticleSystem", "Particle"), QT_TRANSLATE_NOOP("OpenParticleSystem", "Particle System"))
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Particle System")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC("Game", 0x232b318c))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->Attribute(AZ::Edit::Attributes::PrimaryAssetType, AZ::AzTypeInfo<ParticleAsset>::Uuid());

                editContext->Class<ParticleComponentController>(QT_TRANSLATE_NOOP("OpenParticleSystem", "ParticleComponentController"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleComponentController::m_configuration, QT_TRANSLATE_NOOP("OpenParticleSystem", "Configuration"), "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly);

                editContext->Class<ParticleComponentConfig>(QT_TRANSLATE_NOOP("OpenParticleSystem", "ParticleComponentConfig"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &ParticleComponentConfig::m_enable, QT_TRANSLATE_NOOP("OpenParticleSystem", "Enable"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Control whether this particle effect is enabled, can only be changed in edit mode"))
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &ParticleComponentConfig::m_autoPlay, QT_TRANSLATE_NOOP("OpenParticleSystem", "AutoPlay"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Control whether this particle effect auto played after loaded, (e.g. loaded in editor or game beginning)"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleComponentConfig::m_particleAsset, QT_TRANSLATE_NOOP("OpenParticleSystem", "Asset"), QT_TRANSLATE_NOOP("OpenParticleSystem", "Particle Asset"))
                        ->Attribute("BrowseIcon", ":/stylesheet/img/UI20/browse-edit-select-files.svg")
                        ->Attribute("EditButton", "")
                        ->Attribute("EditDescription", "Open in Particle Editor")
                        ->Attribute("EditCallback", &EditorParticleComponent::OpenParticleEditor)

                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleComponentConfig::m_followActiveCamera, QT_TRANSLATE_NOOP("OpenParticleSystem", "FollowActiveCamera"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Particles always generated around active camera and absolute position of particle system will be ignored, global space "
                            "used forcibly."))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleComponentConfig::m_overrides, QT_TRANSLATE_NOOP("OpenParticleSystem", "Overrides"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Values that replace the asset's values for this entity only; the .particle file is not changed"));

                editContext->Enum<LoopOverride>(QT_TRANSLATE_NOOP("OpenParticleSystem", "Loop Override"), "")
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Use asset"), LoopOverride::Asset)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "On"), LoopOverride::On)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Off"), LoopOverride::Off);

                editContext->Class<ParticleOverrideSettings>(QT_TRANSLATE_NOOP("OpenParticleSystem", "ParticleOverrideSettings"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &ParticleOverrideSettings::m_overridePlaybackSpeed, QT_TRANSLATE_NOOP("OpenParticleSystem", "Playback Speed"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the simulation speed of the whole system"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleOverrideSettings::m_playbackSpeed, QT_TRANSLATE_NOOP("OpenParticleSystem", "Speed"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "1 is normal speed and 0.5 is slow motion"))
                        ->Attribute(AZ::Edit::Attributes::Visibility, &ParticleOverrideSettings::m_overridePlaybackSpeed)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.05f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleOverrideSettings::m_allEmitters, QT_TRANSLATE_NOOP("OpenParticleSystem", "All Emitters"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Values for every emitter; a value set on one emitter below wins"))
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &ParticleOverrideSettings::m_emitters, QT_TRANSLATE_NOOP("OpenParticleSystem", "Emitters"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Values for one emitter, listed from the particle asset"))
                        ->Attribute(AZ::Edit::Attributes::ContainerCanBeModified, false)
                        ->Attribute(AZ::Edit::Attributes::ContainerReorderAllow, false)
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                        ->Attribute(AZ::Edit::Attributes::IndexedChildNameLabelOverride, &ParticleOverrideSettings::GetEmitterLabel);

                editContext->Class<EmitterOverrideSettings>(QT_TRANSLATE_NOOP("OpenParticleSystem", "EmitterOverrideSettings"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &EmitterOverrideSettings::m_loop, QT_TRANSLATE_NOOP("OpenParticleSystem", "Loop"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "A non-looping emitter plays once"))
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideSpawnRate, QT_TRANSLATE_NOOP("OpenParticleSystem", "Spawn Rate"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the particles spawned per second, replacing any curve or random value from the asset"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_spawnRate, QT_TRANSLATE_NOOP("OpenParticleSystem", "Particles Per Second"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideSpawnRate)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideLifetime, QT_TRANSLATE_NOOP("OpenParticleSystem", "Lifetime"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the lifetime of particles spawned from now on"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_lifetime, QT_TRANSLATE_NOOP("OpenParticleSystem", "Seconds"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideLifetime)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Suffix, " s")
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideSize, QT_TRANSLATE_NOOP("OpenParticleSystem", "Size"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the size of particles spawned from now on"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_size, QT_TRANSLATE_NOOP("OpenParticleSystem", "Start Size"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideSize)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideColor, QT_TRANSLATE_NOOP("OpenParticleSystem", "Color"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the particle colour; an alpha curve from the asset is kept and only rgb is replaced"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Color, &EmitterOverrideSettings::m_color, QT_TRANSLATE_NOOP("OpenParticleSystem", "RGB"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideColor)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideForce, QT_TRANSLATE_NOOP("OpenParticleSystem", "Force"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the emitter's constant force such as gravity or wind"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_force, QT_TRANSLATE_NOOP("OpenParticleSystem", "Constant Force"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideForce)
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideFlipbookFps, QT_TRANSLATE_NOOP("OpenParticleSystem", "Flipbook FPS"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the flipbook (SubUV) playback rate"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_flipbookFps, QT_TRANSLATE_NOOP("OpenParticleSystem", "Frames Per Second"), "")
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideFlipbookFps)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f);
            }
        }

        if (AZ::BehaviorContext* behaviorContext = azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behaviorContext->ConstantProperty("EditorParticleComponentTypeId", BehaviorConstant(AZ::Uuid(EditorParticleComponentTypeId)))
                ->Attribute(AZ::Script::Attributes::Module, "render")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Automation);

            behaviorContext->EBus<EditorParticleRequestBus>("EditorParticleRequestBus")
                ->Attribute(AZ::Script::Attributes::Module, "OpenParticleSystem")
                ->Attribute(AZ::Script::Attributes::Scope, AZ::Script::Attributes::ScopeFlags::Common)
                    ->Event("SetMaterialDiffuseMap", &EditorParticleRequestBus::Events::SetMaterialDiffuseMap)
                    ->Attribute(AZ::Script::Attributes::ToolTip,  "Set new diffuse map for current particle system")
                ;

            behaviorContext->Class<EditorParticleComponent>()->RequestBus("EditorParticleRequestBus");
        }
    }

    void EditorParticleComponent::Activate()
    {
        const AZ::EntityId entityId = GetEntityId();
        const AZ::EntityComponentIdPair entityComponentIdPair{ entityId, GetId() };
        SyncEmitterOverrides();
        BaseClass::Activate();
        m_activatedConfig = m_controller.GetConfiguration();
        AzToolsFramework::EditorVisibilityNotificationBus::Handler::BusConnect(entityId);
        EditorParticleRequestBus::Handler::BusConnect(entityId);
        AzFramework::AssetCatalogEventBus::Handler::BusConnect();
    }

    void EditorParticleComponent::Deactivate()
    {
        const AZ::EntityId entityId = GetEntityId();
        AzFramework::AssetCatalogEventBus::Handler::BusDisconnect();
        EditorParticleRequestBus::Handler::BusDisconnect(entityId);
        AzToolsFramework::EditorVisibilityNotificationBus::Handler::BusDisconnect();
        BaseClass::Deactivate();
    }

    void EditorParticleComponent::SetParticleAsset(AZ::Data::Asset<ParticleAsset> particleAsset, bool inParticleEditor)
    {
        m_controller.SetParticleAsset(particleAsset, inParticleEditor);
    }

    void EditorParticleComponent::SetMaterialDiffuseMap(AZ::u32 emitterIndex, AZStd::string mapPath)
    {
        m_controller.SetMaterialDiffuseMap(emitterIndex, mapPath);
    }

    AZ::u32 EditorParticleComponent::OnConfigurationChanged()
    {
        const ParticleComponentConfig& config = m_controller.GetConfiguration();
        if (config.m_particleAsset.GetId() == m_activatedConfig.m_particleAsset.GetId() && config.m_enable == m_activatedConfig.m_enable &&
            config.m_autoPlay == m_activatedConfig.m_autoPlay && config.m_followActiveCamera == m_activatedConfig.m_followActiveCamera)
        {
            // Only the overrides changed, so the running effect takes them without restarting while a value is dragged.
            m_controller.ApplyAuthoredOverrides();
            return AZ::Edit::PropertyRefreshLevels::None;
        }

        const bool emittersChanged = SyncEmitterOverrides();
        BaseClass::OnConfigurationChanged();
        m_activatedConfig = m_controller.GetConfiguration();
        return emittersChanged ? AZ::Edit::PropertyRefreshLevels::EntireTree : AZ::Edit::PropertyRefreshLevels::None;
    }

    void EditorParticleComponent::OnCatalogAssetChanged(const AZ::Data::AssetId& assetId)
    {
        if (assetId != m_controller.GetConfiguration().m_particleAsset.GetId() || !SyncEmitterOverrides())
        {
            return;
        }
        m_controller.ApplyAuthoredOverrides();
        AzToolsFramework::ToolsApplicationNotificationBus::Broadcast(
            &AzToolsFramework::ToolsApplicationEvents::InvalidatePropertyDisplay, AzToolsFramework::Refresh_EntireTree);
    }

    bool EditorParticleComponent::SyncEmitterOverrides()
    {
        auto& emitters = m_controller.m_configuration.m_overrides.m_emitters;
        const AZ::Data::AssetId assetId = m_controller.m_configuration.m_particleAsset.GetId();
        if (!assetId.IsValid())
        {
            const bool hadEmitters = !emitters.empty();
            emitters.clear();
            return hadEmitters;
        }

        // The built asset has no emitter names, so they come from the source file, whose emitter order is the runtime index.
        bool foundSource = false;
        AZ::Data::AssetInfo sourceInfo;
        AZStd::string watchFolder;
        AzToolsFramework::AssetSystemRequestBus::BroadcastResult(
            foundSource, &AzToolsFramework::AssetSystemRequestBus::Events::GetSourceInfoBySourceUUID, assetId.m_guid, sourceInfo, watchFolder);
        if (!foundSource)
        {
            return false;
        }
        const AZ::IO::Path sourcePath = AZ::IO::Path(watchFolder) / AZ::IO::Path(sourceInfo.m_relativePath);
        const auto document = AZ::JsonSerializationUtils::ReadJsonFile(sourcePath.Native());
        if (!document.IsSuccess() || !document.GetValue().IsObject())
        {
            return false;
        }
        const auto emitterArray = document.GetValue().FindMember("emitters");
        if (emitterArray == document.GetValue().MemberEnd() || !emitterArray->value.IsArray())
        {
            return false;
        }

        AZStd::vector<EmitterOverrideSettings> synced;
        for (const auto& emitter : emitterArray->value.GetArray())
        {
            AZStd::string name;
            if (emitter.IsObject())
            {
                if (const auto member = emitter.FindMember("name"); member != emitter.MemberEnd() && member->value.IsString())
                {
                    name = member->value.GetString();
                }
            }
            const auto existing = AZStd::find_if(
                emitters.begin(), emitters.end(), [&name](const EmitterOverrideSettings& entry) { return entry.m_emitterName == name; });
            EmitterOverrideSettings& entry = synced.emplace_back(existing != emitters.end() ? *existing : EmitterOverrideSettings{});
            entry.m_emitterName = name;
            entry.m_emitterIndex = static_cast<AZ::u32>(synced.size() - 1);
        }

        bool changed = synced.size() != emitters.size();
        for (size_t i = 0; !changed && i < synced.size(); ++i)
        {
            changed = synced[i].m_emitterName != emitters[i].m_emitterName || synced[i].m_emitterIndex != emitters[i].m_emitterIndex;
        }
        emitters = AZStd::move(synced);
        return changed;
    }

    void EditorParticleComponent::OnEntityVisibilityChanged(bool visibility)
    {
        m_controller.SetVisible(visibility);
    }

    void EditorParticleComponent::OpenParticleEditor(const AZ::Data::AssetId& assetId, const AZ::Data::AssetType&)
    {
        if (assetId.IsValid())
        {
            bool foundSourceInfo = false;
            AZStd::string folderFoundIn;
            AZ::Data::AssetInfo assetInfo;
            AzToolsFramework::AssetSystemRequestBus::BroadcastResult(foundSourceInfo, &AzToolsFramework::AssetSystemRequestBus::Events::GetSourceInfoBySourceUUID, assetId.m_guid, assetInfo, folderFoundIn);
            if (foundSourceInfo)
            {
                const AZ::IO::Path assetFullPath = AZ::IO::Path(folderFoundIn) / AZ::IO::Path(assetInfo.m_relativePath);
                EditorParticleSystemComponentRequestBus::Broadcast(&EditorParticleSystemComponentRequestBus::Events::OpenParticleEditor, assetFullPath.c_str());
            }
            else
            {
                AZ_Warning("EditorParticleComponent", false, "Could not find particle editor asset");
            }
        }
    }
} // namespace OpenParticle
