/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/EBus/EBus.h>

namespace OpenParticle
{
    //! Mesh materials that use the particle distortion shader aren't seen by the feature processor, so they ask for the distortion passes here.
    class ParticleDistortionRequests
        : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        //! Keeps the scene-copy and distortion passes running for at least this many seconds from now.
        virtual void KeepDistortionActive(float seconds) = 0;
    };

    using ParticleDistortionRequestBus = AZ::EBus<ParticleDistortionRequests>;
}
