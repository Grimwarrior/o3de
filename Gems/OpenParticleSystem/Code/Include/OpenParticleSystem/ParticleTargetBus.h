/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/std/string/string.h>

namespace OpenParticle
{
    //! Lets a gameplay component on a character (the address) tell the Particle Motion Events component where a named particle entity is when it is not
    //! a descendant of the character, e.g. the Trail entity of the weapon the character holds.
    class ParticleTargetRequests
        : public AZ::ComponentBus
    {
    public:
        //! The entity with this name that belongs to the character's current equipment; invalid when there is none.
        virtual AZ::EntityId FindParticleEntity(const AZStd::string& name) = 0;
    };

    using ParticleTargetRequestBus = AZ::EBus<ParticleTargetRequests>;
}
