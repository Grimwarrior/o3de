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

                editContext->Enum<ColorOverLifeOverrideMode>(QT_TRANSLATE_NOOP("OpenParticleSystem", "Color Over Lifetime Mode"), "")
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Gradient"), ColorOverLifeOverrideMode::Gradient)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Random Between Two Colors"), ColorOverLifeOverrideMode::RandomColors)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Random Between Two Gradients"), ColorOverLifeOverrideMode::RandomGradients);

                editContext->Enum<LoopOverride>(QT_TRANSLATE_NOOP("OpenParticleSystem", "Loop Override"), "")
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Use asset"), LoopOverride::Asset)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "On"), LoopOverride::On)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Off"), LoopOverride::Off);

                editContext->Class<ParticleOverrideSettings>(QT_TRANSLATE_NOOP("OpenParticleSystem", "ParticleOverrideSettings"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->UIElement(AZ::Edit::UIHandlers::Button, "", QT_TRANSLATE_NOOP("OpenParticleSystem", "Turn every override off and go back to the asset's values"))
                        ->Attribute(AZ::Edit::Attributes::ButtonText, QT_TRANSLATE_NOOP("OpenParticleSystem", "Reset All Overrides"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, &ParticleOverrideSettings::OnResetAllPressed)
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
                    ->UIElement(AZ::Edit::UIHandlers::Button, "", QT_TRANSLATE_NOOP("OpenParticleSystem", "Turn this group's overrides off and go back to the asset's values"))
                        ->Attribute(AZ::Edit::Attributes::ButtonText, QT_TRANSLATE_NOOP("OpenParticleSystem", "Reset"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, &EmitterOverrideSettings::OnResetPressed)
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &EmitterOverrideSettings::m_loop, QT_TRANSLATE_NOOP("OpenParticleSystem", "Loop"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "A non-looping emitter plays once"))
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideEmitterSpeed, QT_TRANSLATE_NOOP("OpenParticleSystem", "Emitter Speed"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the emitter's own Playback Speed from the Particle Editor; it multiplies with the system Playback Speed above"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &EmitterOverrideSettings::m_emitterSpeed, QT_TRANSLATE_NOOP("OpenParticleSystem", "Speed"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "1 is normal speed and 0.5 is slow motion"))
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::m_overrideEmitterSpeed)
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.05f)
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
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &EmitterOverrideSettings::m_overrideColorOverLife, QT_TRANSLATE_NOOP("OpenParticleSystem", "Color Over Lifetime"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Override the colour and alpha gradient over each particle's life; "
                            "only applies to emitters that have the Color Over Lifetime module"))
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &EmitterOverrideSettings::m_colorOverLifeMode, QT_TRANSLATE_NOOP("OpenParticleSystem", "Mode"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Gradient: every particle follows the keys. Random Between Two Colors: each particle keeps "
                            "one colour between Color A and Color B. Random Between Two Gradients: each particle follows its own blend of Keys "
                            "and Second Keys. A particle's blend is fixed for its life."))
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::ShowsColorOverLifeMode)
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::EntireTree)
                    ->DataElement(AZ_CRC_CE("ParticleColorKeyGradient"), &EmitterOverrideSettings::m_colorOverLife, QT_TRANSLATE_NOOP("OpenParticleSystem", "Keys"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "Gradient stops; the first key holds before its time and the last after. "
                            "Click the gradient to edit the keys in the gradient editor"))
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::ShowsColorKeys)
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                        ->Attribute(AZ::Edit::Attributes::IndexedChildNameLabelOverride, &EmitterOverrideSettings::GetColorKeyLabel)
                    ->DataElement(AZ_CRC_CE("ParticleColorKeyGradient"), &EmitterOverrideSettings::m_secondColorOverLife, QT_TRANSLATE_NOOP("OpenParticleSystem", "Second Keys"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "The other gradient; each particle blends between the two by its own fixed amount"))
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::ShowsSecondColorKeys)
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                        ->Attribute(AZ::Edit::Attributes::IndexedChildNameLabelOverride, &EmitterOverrideSettings::GetSecondColorKeyLabel)
                    ->DataElement(AZ::Edit::UIHandlers::Color, &EmitterOverrideSettings::m_randomColorA, QT_TRANSLATE_NOOP("OpenParticleSystem", "Color A"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "One end of the range each particle's colour is picked from"))
                        ->Attribute("AlphaChannel", true)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::ShowsRandomColors)
                    ->DataElement(AZ::Edit::UIHandlers::Color, &EmitterOverrideSettings::m_randomColorB, QT_TRANSLATE_NOOP("OpenParticleSystem", "Color B"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "The other end of the range each particle's colour is picked from"))
                        ->Attribute("AlphaChannel", true)
                        ->Attribute(AZ::Edit::Attributes::Visibility, &EmitterOverrideSettings::ShowsRandomColors)
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

                editContext->Class<ParticleColorKey>(QT_TRANSLATE_NOOP("OpenParticleSystem", "ParticleColorKey"), "")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                        ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &ParticleColorKey::m_time, QT_TRANSLATE_NOOP("OpenParticleSystem", "Time"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "0 is the particle's birth and 1 its death"))
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                        ->Attribute(AZ::Edit::Attributes::Step, 0.01f)
                        ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->DataElement(AZ::Edit::UIHandlers::Color, &ParticleColorKey::m_color, QT_TRANSLATE_NOOP("OpenParticleSystem", "Color"), "")
                        ->Attribute("AlphaChannel", true)
                    ->DataElement(AZ::Edit::UIHandlers::ComboBox, &ParticleColorKey::m_blend, QT_TRANSLATE_NOOP("OpenParticleSystem", "Blend"),
                            QT_TRANSLATE_NOOP("OpenParticleSystem", "How this key blends into the next key; Step holds this colour until the next key"));

                editContext->Enum<ColorKeyBlend>(QT_TRANSLATE_NOOP("OpenParticleSystem", "Color Key Blend"), "")
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Linear"), ColorKeyBlend::Linear)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Step"), ColorKeyBlend::Step)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Ease In"), ColorKeyBlend::EaseIn)
                    ->Value(QT_TRANSLATE_NOOP("OpenParticleSystem", "Ease Out"), ColorKeyBlend::EaseOut);
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

    // Written into the authored Playback Speed override, which the controller keeps when the preview's asset is reloaded. Only the
    // Particle Editor's own preview entity calls this, and that entity is never saved.
    void EditorParticleComponent::SetPreviewPlaybackSpeed(float speed)
    {
        ParticleOverrideSettings& overrides = m_controller.m_configuration.m_overrides;
        overrides.m_playbackSpeed = AZStd::max(speed, 0.0f);
        overrides.m_overridePlaybackSpeed = overrides.m_playbackSpeed != 1.0f;
        m_controller.ApplyAuthoredOverrides();
    }

    AZ::u32 EditorParticleComponent::OnConfigurationChanged()
    {
        const ParticleComponentConfig& config = m_controller.GetConfiguration();
        if (config.m_particleAsset.GetId() == m_activatedConfig.m_particleAsset.GetId() && config.m_enable == m_activatedConfig.m_enable &&
            config.m_autoPlay == m_activatedConfig.m_autoPlay && config.m_followActiveCamera == m_activatedConfig.m_followActiveCamera)
        {
            // Only the overrides changed, so the running effect takes them without restarting while a value is dragged.
            RefreshAssetDefaults();
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
        bool changed = false;
        if (!assetId.IsValid())
        {
            changed = !emitters.empty();
            emitters.clear();
            m_assetValues.clear();
        }
        // The built asset has no emitter names, so they come from the source file, whose emitter order is the runtime index.
        else if (ReadEmitterAssetValues(assetId, m_assetValues))
        {
            AZStd::vector<EmitterOverrideSettings> synced;
            for (const EmitterAssetValues& emitter : m_assetValues)
            {
                const auto existing = AZStd::find_if(
                    emitters.begin(), emitters.end(),
                    [&emitter](const EmitterOverrideSettings& entry) { return entry.m_emitterName == emitter.m_name; });
                EmitterOverrideSettings& entry = synced.emplace_back(existing != emitters.end() ? *existing : EmitterOverrideSettings{});
                entry.m_emitterName = emitter.m_name;
                entry.m_emitterIndex = static_cast<AZ::u32>(synced.size() - 1);
            }

            changed = synced.size() != emitters.size();
            for (size_t i = 0; !changed && i < synced.size(); ++i)
            {
                changed = synced[i].m_emitterName != emitters[i].m_emitterName || synced[i].m_emitterIndex != emitters[i].m_emitterIndex;
            }
            emitters = AZStd::move(synced);
        }

        RefreshAssetDefaults();
        BindResetHandlers();
        return changed;
    }

    namespace
    {
        void SeedFromAsset(EmitterOverrideSettings& settings, const EmitterAssetValues& asset)
        {
            if (!settings.m_overrideEmitterSpeed && asset.m_emitterSpeed)
            {
                settings.m_emitterSpeed = *asset.m_emitterSpeed;
            }
            if (!settings.m_overrideSpawnRate && asset.m_spawnRate)
            {
                settings.m_spawnRate = *asset.m_spawnRate;
            }
            if (!settings.m_overrideLifetime && asset.m_lifetime)
            {
                settings.m_lifetime = *asset.m_lifetime;
            }
            if (!settings.m_overrideSize && asset.m_size)
            {
                settings.m_size = *asset.m_size;
            }
            if (!settings.m_overrideColor && asset.m_color)
            {
                settings.m_color = *asset.m_color;
            }
            if (!settings.m_overrideColorOverLife && asset.m_hasColorOverLife)
            {
                settings.m_colorOverLifeMode = asset.m_colorOverLifeMode;
                settings.m_colorOverLife = asset.m_colorOverLife;
                settings.m_secondColorOverLife = asset.m_secondColorOverLife;
                settings.m_randomColorA = asset.m_randomColorA;
                settings.m_randomColorB = asset.m_randomColorB;
            }
            if (!settings.m_overrideForce && asset.m_force)
            {
                settings.m_force = *asset.m_force;
            }
            if (!settings.m_overrideFlipbookFps && asset.m_flipbookFps)
            {
                settings.m_flipbookFps = *asset.m_flipbookFps;
            }
        }

        // All Emitters starts each value from the first emitter that has it.
        EmitterAssetValues FirstOfEach(const AZStd::vector<EmitterAssetValues>& emitters)
        {
            EmitterAssetValues first;
            for (const EmitterAssetValues& emitter : emitters)
            {
                first.m_emitterSpeed = first.m_emitterSpeed ? first.m_emitterSpeed : emitter.m_emitterSpeed;
                first.m_spawnRate = first.m_spawnRate ? first.m_spawnRate : emitter.m_spawnRate;
                first.m_lifetime = first.m_lifetime ? first.m_lifetime : emitter.m_lifetime;
                first.m_size = first.m_size ? first.m_size : emitter.m_size;
                first.m_color = first.m_color ? first.m_color : emitter.m_color;
                first.m_force = first.m_force ? first.m_force : emitter.m_force;
                first.m_flipbookFps = first.m_flipbookFps ? first.m_flipbookFps : emitter.m_flipbookFps;
                if (!first.m_hasColorOverLife && emitter.m_hasColorOverLife)
                {
                    first.m_hasColorOverLife = true;
                    first.m_colorOverLifeMode = emitter.m_colorOverLifeMode;
                    first.m_colorOverLife = emitter.m_colorOverLife;
                    first.m_secondColorOverLife = emitter.m_secondColorOverLife;
                    first.m_randomColorA = emitter.m_randomColorA;
                    first.m_randomColorB = emitter.m_randomColorB;
                }
            }
            return first;
        }
    } // namespace

    // Values whose override is off have no effect, so they can follow the asset without an undo step.
    void EditorParticleComponent::RefreshAssetDefaults()
    {
        ParticleOverrideSettings& overrides = m_controller.m_configuration.m_overrides;
        SeedFromAsset(overrides.m_allEmitters, FirstOfEach(m_assetValues));
        for (EmitterOverrideSettings& emitter : overrides.m_emitters)
        {
            if (emitter.m_emitterIndex < m_assetValues.size())
            {
                SeedFromAsset(emitter, m_assetValues[emitter.m_emitterIndex]);
            }
        }
    }

    // A button's ChangeNotify runs after the Inspector has recorded its undo step (and in the DPE after this component's own
    // ChangeNotify), so the Reset buttons call back here to make their change, undo step and refresh themselves.
    void EditorParticleComponent::BindResetHandlers()
    {
        ParticleOverrideSettings& overrides = m_controller.m_configuration.m_overrides;
        overrides.m_resetAll = [this]() { ResetOverrides(ResetEverything); };
        overrides.m_allEmitters.m_reset = [this]() { ResetOverrides(ResetAllEmitters); };
        for (EmitterOverrideSettings& emitter : overrides.m_emitters)
        {
            const int index = static_cast<int>(emitter.m_emitterIndex);
            emitter.m_reset = [this, index]() { ResetOverrides(index); };
        }
    }

    void EditorParticleComponent::ResetOverrides(int target)
    {
        AzToolsFramework::ScopedUndoBatch undoBatch("Reset Particle Overrides");

        ParticleOverrideSettings& overrides = m_controller.m_configuration.m_overrides;
        if (target == ResetEverything)
        {
            overrides.m_overridePlaybackSpeed = false;
            overrides.m_playbackSpeed = 1.0f;
            overrides.m_allEmitters.ClearToggles();
            for (EmitterOverrideSettings& emitter : overrides.m_emitters)
            {
                emitter.ClearToggles();
            }
        }
        else if (target == ResetAllEmitters)
        {
            overrides.m_allEmitters.ClearToggles();
        }
        else
        {
            for (EmitterOverrideSettings& emitter : overrides.m_emitters)
            {
                if (static_cast<int>(emitter.m_emitterIndex) == target)
                {
                    emitter.ClearToggles();
                }
            }
        }
        RefreshAssetDefaults();
        undoBatch.MarkEntityDirty(GetEntityId());

        m_controller.ApplyAuthoredOverrides();
        AzToolsFramework::ToolsApplicationNotificationBus::Broadcast(
            &AzToolsFramework::ToolsApplicationEvents::InvalidatePropertyDisplay, AzToolsFramework::Refresh_EntireTree);
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
