/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
 
#pragma once

#include <OpenParticleSystem/ParticleComponent.h>
#include <AzToolsFramework/ToolsComponents/EditorComponentAdapter.h>
#include <AzFramework/Asset/AssetCatalogBus.h>
#include <AzToolsFramework/ToolsComponents/EditorVisibilityBus.h>
#include <OpenParticleSystem/ParticleComponentConfig.h>

#include <Editor/EditorParticleAssetValues.h>
#include <Editor/EditorParticleRequestBus.h>

namespace OpenParticle
{
    class EditorParticleComponent
        : public AzToolsFramework::Components::
              EditorComponentAdapter<ParticleComponentController, ParticleComponent, ParticleComponentConfig>
        , private AzToolsFramework::EditorVisibilityNotificationBus::Handler
        , private EditorParticleRequestBus::Handler
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        using BaseClass =
            AzToolsFramework::Components::EditorComponentAdapter<ParticleComponentController, ParticleComponent, ParticleComponentConfig>;

        AZ_EDITOR_COMPONENT(EditorParticleComponent, EditorParticleComponentTypeId, BaseClass);
        static constexpr const char* const EditorParticleComponentTypeId = "{d0b6b04d-12b0-40b2-80cf-4f9b264b5074}";

        static void Reflect(AZ::ReflectContext* context);

        EditorParticleComponent() = default;
        explicit EditorParticleComponent(const ParticleComponentConfig& config)
            : BaseClass(config)
        {
        }

        ~EditorParticleComponent() = default;

        void Activate() override;
        void Deactivate() override;

        void SetParticleAsset(AZ::Data::Asset<ParticleAsset> particleAsset, bool inParticleEditor) override;
        void SetMaterialDiffuseMap(AZ::u32 emitterIndex, AZStd::string mapPath) override;

    protected:
        AZ::u32 OnConfigurationChanged() override;
        static void OpenParticleEditor(const AZ::Data::AssetId& assetId, const AZ::Data::AssetType& assetType);

    private:
        // AzToolsFramework::EditorEntityVisibilityNotificationBus::Handler overrides
        void OnEntityVisibilityChanged(bool visibility) override;

        // AzFramework::AssetCatalogEventBus::Handler overrides
        void OnCatalogAssetChanged(const AZ::Data::AssetId& assetId) override;

        //! Gives the per-emitter overrides one entry per emitter in the .particle file, keeping authored values by emitter name.
        //! Returns true when the list changed.
        bool SyncEmitterOverrides();

        //! Sets every value whose override is off to what the asset plays, so turning an override on starts from it.
        void RefreshAssetDefaults();

        //! Gives the override groups the callbacks their Reset buttons call.
        void BindResetHandlers();

        static constexpr int ResetEverything = -2;
        static constexpr int ResetAllEmitters = -1;
        //! Turns overrides off as one undo step: everything, the All Emitters group, or the emitter with that index.
        void ResetOverrides(int target);

        //! The configuration the controller was last activated with, so an edit that only touches overrides can skip the restart.
        ParticleComponentConfig m_activatedConfig;

        //! What the asset plays per emitter, read with the emitter list.
        AZStd::vector<EmitterAssetValues> m_assetValues;
    };
} // namespace OpenParticle
