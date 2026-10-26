/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/optional.h>
#include <AzCore/std/string/string.h>
#include <OpenParticleSystem/ParticleOverrides.h>

namespace OpenParticle
{
    //! What a .particle source file plays for one emitter, so an override can start from it. A value is empty when the emitter
    //! has no module for it. Curves and random ranges come down to one number: a curve's value at its start and a range's middle.
    struct EmitterAssetValues
    {
        AZStd::string m_name;
        AZStd::optional<float> m_emitterSpeed;
        AZStd::optional<float> m_spawnRate;
        AZStd::optional<float> m_lifetime;
        AZStd::optional<AZ::Vector3> m_size;
        //! The spawn colour, or the start of the lifetime gradient when the emitter has no spawn colour.
        AZStd::optional<AZ::Color> m_color;
        AZStd::optional<AZ::Vector3> m_force;
        AZStd::optional<float> m_flipbookFps;
        //! The Color Over Lifetime module's settings; the rest are only read when m_hasColorOverLife.
        bool m_hasColorOverLife = false;
        ColorOverLifeOverrideMode m_colorOverLifeMode = ColorOverLifeOverrideMode::Gradient;
        AZStd::vector<ParticleColorKey> m_colorOverLife;
        AZStd::vector<ParticleColorKey> m_secondColorOverLife;
        AZ::Color m_randomColorA = AZ::Color::CreateOne();
        AZ::Color m_randomColorB = AZ::Color(0.0f, 0.0f, 0.0f, 1.0f);
    };

    //! Reads the emitters of the source .particle behind assetId in file order, which is their runtime index.
    //! Returns false when the source file can't be found or read, leaving emitters untouched.
    bool ReadEmitterAssetValues(const AZ::Data::AssetId& assetId, AZStd::vector<EmitterAssetValues>& emitters);
} // namespace OpenParticle
