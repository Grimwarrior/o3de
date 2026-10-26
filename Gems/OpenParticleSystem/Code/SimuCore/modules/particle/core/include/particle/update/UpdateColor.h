/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include "particle/core/Particle.h"

namespace SimuCore::ParticleCore {
    // How Color Over Lifetime colours a particle. The random modes give each particle its own blend, fixed for its life.
    enum class ColorOverLifeMode : AZ::u8 {
        GRADIENT = 0,
        RANDOM_COLORS,    // a fixed colour between randomColorA and randomColorB
        RANDOM_GRADIENTS  // a blend between currentColor and secondColor, following both over the particle's life
    };

    struct UpdateColor {
        using DataType = UpdateColor;
        static void Execute(const UpdateColor* data, const UpdateInfo& info, Particle& particle);
        static void UpdateDistPtr(UpdateColor* data, const Distribution& distribution);

        ValueObjColor currentColor { { 1.f, 1.f, 1.f, 1.f } };
        ValueObjColor secondColor { { 1.f, 1.f, 1.f, 1.f } };
        AZ::Color randomColorA { 1.f, 1.f, 1.f, 1.f };
        AZ::Color randomColorB { 0.f, 0.f, 0.f, 1.f };
        ColorOverLifeMode mode = ColorOverLifeMode::GRADIENT;
    };
}

