/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "particle/core/ParticleHelper.h"
#include "particle/update/UpdateSubUv.h"

namespace SimuCore::ParticleCore {
    void UpdateSubUv::Execute(const UpdateSubUv* data, [[maybe_unused]] const UpdateInfo& info, Particle& particle)
    {
        if (data->spawnOnly) {
            if (particle.spawnSubUVDone) { return; }
            particle.subUVFrame = data->IndexByEventOrder ? particle.parentEventIdx % data->frameNum
                                                          : Random::RandomRange(0, data->frameNum);
            particle.spawnSubUVDone = true;
            return;
        }

        if (data->frameNum == 0) {
            particle.subUVFrame = 0;
        } else {
            AZ::u32 currentFrame = static_cast<AZ::u32>(std::floor(data->framePerSecond * particle.currentLife));
            if (data->randomStartFrames > 0) {
                // the start frame is hashed from the particle id, so it stays fixed for the particle's life without extra state
                AZ::u64 h = particle.id * 0x9E3779B97F4A7C15ull;
                h ^= h >> 29;
                currentFrame += static_cast<AZ::u32>(h % (static_cast<AZ::u64>(data->randomStartFrames) + 1));
            }
            particle.subUVFrame = currentFrame % data->frameNum;
        }
    }

    void UpdateSubUv::UpdateDistPtr(const UpdateSubUv* data, const Distribution& distribution)
    {
        (void)data;
        (void)distribution;
    }
}
