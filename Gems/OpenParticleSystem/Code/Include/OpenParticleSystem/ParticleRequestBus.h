/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
 
#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector3.h>

namespace OpenParticle
{
    class ParticleRequest
        : public AZ::ComponentBus
    {
    public:
        //! Triggers the particle system to start playing.
        virtual void Play() = 0;

        //! Pauses the particle system simulation.
        virtual void Pause() = 0;

        //! Stops the particle system simulation.
        virtual void Stop() = 0;

        //! Sets the visibility state of the particle system.
        //! @param visible  If true, the particle system will be rendered. If false, it will remain hidden until visibility is restored.
        virtual void SetVisibility(bool visible) = 0;

        //! Retrieves the current visibility state of the particle system.
        //! \return true if the particle system is visible; false otherwise.
        virtual bool GetVisibility() const = 0;

        //! True while the simulation is playing, false when paused, stopped or not yet started.
        virtual bool IsPlaying() const = 0;

        //! Seconds the system has played since it last started, at its playback speed.
        virtual float GetPlaybackTime() const = 0;

        //! Seconds until the last emitter stops emitting, at each emitter's own speed; live particles can outlast it.
        virtual float GetPlaybackDuration() const = 0;

        //! True when an emitter loops, so the system plays on past its duration.
        virtual bool IsLooping() const = 0;

        //! Restarts the system and simulates it up to seconds; it keeps playing if it was, otherwise it stays paused there.
        virtual void SetPlaybackTime(float seconds) = 0;

        //! Pauses the system and simulates seconds more, such as one frame.
        virtual void Step(float seconds) = 0;

        //! Runtime overrides write into the instance's own effector data (no per-particle cost) and survive Stop/Play; emitterIndex -1 means every emitter.

        //! Turns emitter looping on or off; a non-looping emitter plays once and Stop then Play replays it.
        virtual void SetLooping(int emitterIndex, bool loop) = 0;

        //! Particles spawned per second, replacing any curve or random value from the asset.
        virtual void SetSpawnRate(int emitterIndex, float particlesPerSecond) = 0;

        //! Lifetime in seconds for particles spawned from now on.
        virtual void SetLifetime(int emitterIndex, float seconds) = 0;

        //! Size of particles spawned from now on.
        virtual void SetSize(int emitterIndex, const AZ::Vector3& size) = 0;

        //! Particle colour; an alpha curve from the asset is kept and only rgb is replaced.
        virtual void SetColor(int emitterIndex, const AZ::Color& color) = 0;

        //! Constant force such as gravity or wind.
        virtual void SetForce(int emitterIndex, const AZ::Vector3& force) = 0;

        //! Flipbook (SubUV) playback rate in frames per second.
        virtual void SetFlipbookFps(int emitterIndex, float framesPerSecond) = 0;

        //! Simulation speed for the whole system, 1 is normal and 0.5 is slow motion.
        virtual void SetPlaybackSpeed(float speed) = 0;

        //! Drops every script override, going back to the overrides set on the component (or the asset values), by restarting the system.
        virtual void ClearOverrides() = 0;
    };

    using ParticleRequestBus = AZ::EBus<ParticleRequest>;
}
