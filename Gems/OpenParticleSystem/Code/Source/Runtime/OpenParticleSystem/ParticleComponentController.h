/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/NonUniformScaleBus.h>
#include <AzCore/Component/TransformBus.h>

#include <OpenParticleSystem/ParticleRequestBus.h>
#include <OpenParticleSystem/ParticleFeatureProcessorInterface.h>

#include <OpenParticleSystem/ParticleComponentConfig.h>
#include <OpenParticleSystem/ParticleOverrides.h>

namespace OpenParticle
{
    class ParticleComponentController final
        : private AZ::TransformNotificationBus::Handler
        , private ParticleRequestBus::Handler
        , private AzFramework::AssetCatalogEventBus::Handler
    {
    public:
        friend class EditorParticleComponent;

        AZ_TYPE_INFO(OpenParticle::ParticleComponentController, "{a5b42514-594d-40a8-aca6-7f98f2d913c4}");
        static void Reflect(AZ::ReflectContext* context);

        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        ParticleComponentController() = default;
        explicit ParticleComponentController(const ParticleComponentConfig& config);

        ~ParticleComponentController() override = default;

        void OnTransformChanged(const AZ::Transform& local, const AZ::Transform& world) override;

        void SetVisible(bool visible);

        void Activate(AZ::EntityId entityId);
        void Deactivate();
        void SetConfiguration(const ParticleComponentConfig& config);
        const ParticleComponentConfig& GetConfiguration() const;

    private:
        void Play() override;
        void Pause() override;
        void Stop() override;
        void SetVisibility(bool visible) override;
        bool GetVisibility() const override;
        bool IsPlaying() const override;
        float GetPlaybackTime() const override;
        float GetPlaybackDuration() const override;
        bool IsLooping() const override;
        void SetPlaybackTime(float seconds) override;
        void Step(float seconds) override;
        void SetLooping(int emitterIndex, bool loop) override;
        void SetSpawnRate(int emitterIndex, float particlesPerSecond) override;
        void SetLifetime(int emitterIndex, float seconds) override;
        void SetSize(int emitterIndex, const AZ::Vector3& size) override;
        void SetColor(int emitterIndex, const AZ::Color& color) override;
        void SetForce(int emitterIndex, const AZ::Vector3& force) override;
        void SetFlipbookFps(int emitterIndex, float framesPerSecond) override;
        void SetPlaybackSpeed(float speed) override;
        void ClearOverrides() override;

        template<typename Edit>
        void EditOverrides(Edit&& edit);

        //! Swaps in the overrides authored in the configuration; scripts' overrides are dropped.
        void ApplyAuthoredOverrides();

        //! Rebuilds the running simulation with the same play status, which brings back asset values an override replaced.
        void Restart();

        void SetParticleAsset(AZ::Data::Asset<ParticleAsset> particleAsset, bool inParticleEditor);
        void SetParticleAssetByPath(AZStd::string path);
        AZStd::string GetParticleAssetPath() const;

        void SetMaterialDiffuseMap(AZ::u32 emitterIndex, AZStd::string mapPath);
        void OnNonUniformScaleChange(const AZ::Vector3& nonUniformScale);

        void Register();

        void Deregister();

        // AzFramework::AssetCatalogEventBus::Handler
        void OnCatalogAssetChanged(const AZ::Data::AssetId&) override;

        ParticleComponentConfig m_configuration;
        AZ::EntityId m_entityId;
        AZ::TransformInterface* m_transformInterface = nullptr;

        AZ::NonUniformScaleChangedEvent::Handler m_nonUniformScaleChangedHandler{
            [&](const AZ::Vector3& nonUniformScale) { OnNonUniformScaleChange(nonUniformScale); }};

        ParticleFeatureProcessorInterface* m_featureProcessor = nullptr;
        ParticleFeatureProcessorInterface::ParticleHandle m_particleHandle;
        bool m_isVisible = true;
        ParticleOverrides m_overrides;
    };
}
