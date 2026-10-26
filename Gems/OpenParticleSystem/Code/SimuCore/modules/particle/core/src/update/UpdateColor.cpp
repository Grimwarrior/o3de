/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "particle/update/UpdateColor.h"
#include "particle/core/ParticleHelper.h"

namespace SimuCore::ParticleCore {
    namespace {
        // A value in [0, 1) that stays the same for a particle's whole life, from a hash of its unique id, so the random
        // modes need nothing stored per particle.
        float ParticleBlend(AZ::u64 id)
        {
            auto x = static_cast<AZ::u32>(id ^ (id >> 32));
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return static_cast<float>(x >> 8) * (1.f / 16777216.f);
        }
    }

    void UpdateColor::Execute(const UpdateColor* data, const UpdateInfo& info, Particle& particle)
    {
        switch (data->mode) {
            case ColorOverLifeMode::RANDOM_COLORS:
                particle.color = data->randomColorA.Lerp(data->randomColorB, ParticleBlend(particle.id));
                break;
            case ColorOverLifeMode::RANDOM_GRADIENTS: {
                const AZ::Color first = CalcDistributionTickValue(data->currentColor, info.baseInfo, particle);
                const AZ::Color second = CalcDistributionTickValue(data->secondColor, info.baseInfo, particle);
                particle.color = first.Lerp(second, ParticleBlend(particle.id));
                break;
            }
            default:
                particle.color = CalcDistributionTickValue(data->currentColor, info.baseInfo, particle);
                break;
        }
    }

    void UpdateColor::UpdateDistPtr(UpdateColor* data, const Distribution& distribution)
    {
        UpdateDistributionPtr(data->currentColor, distribution);
        UpdateDistributionPtr(data->secondColor, distribution);
    }
}