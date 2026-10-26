/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Editor/EditorParticleAssetValues.h>

#include <API/EditorAssetSystemAPI.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>

namespace OpenParticle
{
    namespace
    {
        using JsonValue = rapidjson::Value;

        const JsonValue* FindMember(const JsonValue& object, const char* name)
        {
            if (!object.IsObject())
            {
                return nullptr;
            }
            const auto member = object.FindMember(name);
            return member != object.MemberEnd() ? &member->value : nullptr;
        }

        float ReadFloat(const JsonValue* value, float fallback)
        {
            return (value != nullptr && value->IsNumber()) ? value->GetFloat() : fallback;
        }

        AZStd::string_view ReadString(const JsonValue* value)
        {
            return (value != nullptr && value->IsString()) ? AZStd::string_view(value->GetString(), value->GetStringLength()) : "";
        }

        // The file's curves and random ranges, which value objects point into with 1-based indices.
        struct SourceDistributions
        {
            const JsonValue* m_curves = nullptr;
            const JsonValue* m_randoms = nullptr;
        };

        const JsonValue* FindDistribution(const JsonValue* list, int index)
        {
            if (list == nullptr || !list->IsArray() || index < 1 || index > static_cast<int>(list->Size()))
            {
                return nullptr;
            }
            return &(*list)[static_cast<rapidjson::SizeType>(index - 1)];
        }

        // Matches SimuCore's ParticleCurve::GetInterpVal.
        float InterpWeight(AZStd::string_view mode, float t)
        {
            if (mode == "Step")
            {
                return 0.0f;
            }
            if (mode == "CubicIn")
            {
                return t * t * t;
            }
            if (mode == "CubicOut")
            {
                return (t - 1.0f) * (t - 1.0f) * (t - 1.0f) + 1.0f;
            }
            if (mode == "SineIn")
            {
                return sinf((t - 1.0f) * AZ::Constants::HalfPi) + 1.0f;
            }
            if (mode == "SineOut")
            {
                return sinf(t * AZ::Constants::HalfPi);
            }
            if (mode == "CircleIn")
            {
                return 1.0f - sqrtf(1.0f - t * t);
            }
            if (mode == "CircleOut")
            {
                return sqrtf((2.0f - t) * t);
            }
            return t;
        }

        // A key point's mode shapes the segment that ends at it, as in ParticleCurve::CalcCurveValue.
        float SampleCurve(const JsonValue& curve, float time)
        {
            const float valueFactor = ReadFloat(FindMember(curve, "valueFactor"), 1.0f);
            const JsonValue* keys = FindMember(curve, "keyPoint");
            if (keys == nullptr || !keys->IsArray() || keys->Empty())
            {
                return 0.0f;
            }
            const JsonValue& first = (*keys)[0];
            if (time <= ReadFloat(FindMember(first, "time"), 0.0f))
            {
                return ReadFloat(FindMember(first, "value"), 0.0f) * valueFactor;
            }
            for (rapidjson::SizeType index = 1; index < keys->Size(); ++index)
            {
                const JsonValue& left = (*keys)[index - 1];
                const JsonValue& right = (*keys)[index];
                const float leftTime = ReadFloat(FindMember(left, "time"), 0.0f);
                const float rightTime = ReadFloat(FindMember(right, "time"), 0.0f);
                if (time < rightTime)
                {
                    const float leftValue = ReadFloat(FindMember(left, "value"), 0.0f);
                    const float rightValue = ReadFloat(FindMember(right, "value"), 0.0f);
                    const float t = rightTime > leftTime ? (time - leftTime) / (rightTime - leftTime) : 0.0f;
                    const float weight = InterpWeight(ReadString(FindMember(right, "interpMode")), t);
                    return (leftValue + weight * (rightValue - leftValue)) * valueFactor;
                }
            }
            return ReadFloat(FindMember((*keys)[keys->Size() - 1], "value"), 0.0f) * valueFactor;
        }

        bool IsUniform(const JsonValue& object)
        {
            const JsonValue* uniform = FindMember(object, "isUniform");
            return uniform != nullptr && uniform->IsBool() && uniform->GetBool();
        }

        // A uniform value object drives every channel from its first one.
        rapidjson::SizeType ChannelSlot(const JsonValue& object, int channel)
        {
            return IsUniform(object) ? 0 : static_cast<rapidjson::SizeType>(channel);
        }

        //! The curve a channel of a value object is bound to, or null when it plays its constant.
        const JsonValue* FindChannelCurve(const JsonValue& object, int channel, const SourceDistributions& distributions)
        {
            if (ReadString(FindMember(object, "distType")) != "Curve")
            {
                return nullptr;
            }
            const JsonValue* indices = FindMember(object, "distIndex");
            const rapidjson::SizeType slot = ChannelSlot(object, channel);
            if (indices == nullptr || !indices->IsArray() || slot >= indices->Size() || !(*indices)[slot].IsNumber())
            {
                return nullptr;
            }
            return FindDistribution(distributions.m_curves, (*indices)[slot].GetInt());
        }

        // A channel without a bound distribution plays its constant, the same as at runtime.
        float ReadChannel(const JsonValue& object, int channel, const SourceDistributions& distributions, float time)
        {
            const rapidjson::SizeType slot = ChannelSlot(object, channel);
            float constant = 0.0f;
            if (const JsonValue* data = FindMember(object, "dataValue"))
            {
                if (data->IsNumber())
                {
                    constant = data->GetFloat();
                }
                else if (data->IsArray() && slot < data->Size())
                {
                    constant = ReadFloat(&(*data)[slot], 0.0f);
                }
            }

            const AZStd::string_view distType = ReadString(FindMember(object, "distType"));
            const JsonValue* indices = FindMember(object, "distIndex");
            if (distType.empty() || distType == "Constant" || indices == nullptr || !indices->IsArray() || slot >= indices->Size() ||
                !(*indices)[slot].IsNumber())
            {
                return constant;
            }
            const int index = (*indices)[slot].GetInt();
            if (distType == "Curve")
            {
                if (const JsonValue* curve = FindDistribution(distributions.m_curves, index))
                {
                    return SampleCurve(*curve, time);
                }
            }
            else if (distType == "Random")
            {
                if (const JsonValue* random = FindDistribution(distributions.m_randoms, index))
                {
                    return 0.5f * (ReadFloat(FindMember(*random, "min"), constant) + ReadFloat(FindMember(*random, "max"), constant));
                }
            }
            return constant;
        }

        const JsonValue* FindValueObject(const JsonValue& emitter, const char* module, const char* field)
        {
            const JsonValue* moduleValue = FindMember(emitter, module);
            return moduleValue != nullptr ? FindMember(*moduleValue, field) : nullptr;
        }

        AZ::Vector3 ReadVector3(const JsonValue& object, const SourceDistributions& distributions)
        {
            return AZ::Vector3(
                ReadChannel(object, 0, distributions, 0.0f), ReadChannel(object, 1, distributions, 0.0f), ReadChannel(object, 2, distributions, 0.0f));
        }

        AZ::Color ReadColor(const JsonValue& object, const SourceDistributions& distributions, float time)
        {
            return AZ::Color(
                ReadChannel(object, 0, distributions, time), ReadChannel(object, 1, distributions, time),
                ReadChannel(object, 2, distributions, time), ReadChannel(object, 3, distributions, time));
        }

        ColorKeyBlend ToBlend(AZStd::string_view mode)
        {
            if (mode == "Step")
            {
                return ColorKeyBlend::Step;
            }
            if (mode == "CubicIn" || mode == "SineIn" || mode == "CircleIn")
            {
                return ColorKeyBlend::EaseIn;
            }
            if (mode == "CubicOut" || mode == "SineOut" || mode == "CircleOut")
            {
                return ColorKeyBlend::EaseOut;
            }
            return ColorKeyBlend::Linear;
        }

        // The gradient gets a key wherever any channel's curve has one, each sampled from all four channels. A key's blend comes
        // from the first channel curve with a key where the next gradient key sits, since that key's mode shapes the segment.
        AZStd::vector<ParticleColorKey> ReadColorGradient(const JsonValue& object, const SourceDistributions& distributions)
        {
            constexpr float sameTime = 0.0001f;
            AZStd::vector<float> times;
            AZStd::vector<const JsonValue*> curves;
            for (int channel = 0; channel < 4; ++channel)
            {
                const JsonValue* curve = FindChannelCurve(object, channel, distributions);
                curves.push_back(curve);
                const JsonValue* keys = curve != nullptr ? FindMember(*curve, "keyPoint") : nullptr;
                if (keys == nullptr || !keys->IsArray())
                {
                    continue;
                }
                for (const auto& key : keys->GetArray())
                {
                    times.push_back(AZ::GetClamp(ReadFloat(FindMember(key, "time"), 0.0f), 0.0f, 1.0f));
                }
            }
            if (times.empty())
            {
                times = { 0.0f, 1.0f };
            }
            AZStd::sort(times.begin(), times.end());
            times.erase(
                AZStd::unique(times.begin(), times.end(), [](float left, float right) { return right - left < sameTime; }), times.end());

            AZStd::vector<ParticleColorKey> gradient;
            for (size_t index = 0; index < times.size(); ++index)
            {
                ParticleColorKey key;
                key.m_time = times[index];
                key.m_color = ReadColor(object, distributions, times[index]);
                if (index + 1 < times.size())
                {
                    for (const JsonValue* curve : curves)
                    {
                        const JsonValue* keys = curve != nullptr ? FindMember(*curve, "keyPoint") : nullptr;
                        if (keys == nullptr || !keys->IsArray())
                        {
                            continue;
                        }
                        const auto next = AZStd::find_if(
                            keys->Begin(), keys->End(),
                            [nextTime = times[index + 1]](const JsonValue& point)
                            {
                                return AZ::GetAbs(ReadFloat(FindMember(point, "time"), -1.0f) - nextTime) < sameTime;
                            });
                        if (next != keys->End())
                        {
                            key.m_blend = ToBlend(ReadString(FindMember(*next, "interpMode")));
                            break;
                        }
                    }
                }
                gradient.push_back(key);
            }
            return gradient;
        }

        // AZ::Color is stored as an array of four numbers.
        AZ::Color ReadColorArray(const JsonValue* value, const AZ::Color& fallback)
        {
            if (value == nullptr || !value->IsArray() || value->Size() < 4)
            {
                return fallback;
            }
            return AZ::Color(
                ReadFloat(&(*value)[0], fallback.GetR()), ReadFloat(&(*value)[1], fallback.GetG()), ReadFloat(&(*value)[2], fallback.GetB()),
                ReadFloat(&(*value)[3], fallback.GetA()));
        }

        ColorOverLifeOverrideMode ToOverrideMode(AZStd::string_view mode)
        {
            if (mode == "RandomColors")
            {
                return ColorOverLifeOverrideMode::RandomColors;
            }
            if (mode == "RandomGradients")
            {
                return ColorOverLifeOverrideMode::RandomGradients;
            }
            return ColorOverLifeOverrideMode::Gradient;
        }

        EmitterAssetValues ReadEmitter(const JsonValue& emitter, const SourceDistributions& distributions)
        {
            EmitterAssetValues values;
            values.m_name = ReadString(FindMember(emitter, "name"));
            // Files from before the emitter speed setting play at 1.
            values.m_emitterSpeed = ReadFloat(FindValueObject(emitter, "config", "playbackSpeed"), 1.0f);
            if (const JsonValue* object = FindValueObject(emitter, "OpenParticle::EmitSpawn", "spawnRateObject"))
            {
                values.m_spawnRate = ReadChannel(*object, 0, distributions, 0.0f);
            }
            if (const JsonValue* object = FindValueObject(emitter, "OpenParticle::SpawnLifetime", "lifeTimeObject"))
            {
                values.m_lifetime = ReadChannel(*object, 0, distributions, 0.0f);
            }
            if (const JsonValue* object = FindValueObject(emitter, "OpenParticle::SpawnSize", "sizeObject"))
            {
                values.m_size = ReadVector3(*object, distributions);
            }
            if (const JsonValue* object = FindValueObject(emitter, "OpenParticle::UpdateConstForce", "forceObject"))
            {
                values.m_force = ReadVector3(*object, distributions);
            }
            if (const JsonValue* subUv = FindMember(emitter, "OpenParticle::UpdateSubUv"))
            {
                if (const JsonValue* fps = FindMember(*subUv, "framePerSecond"); fps != nullptr && fps->IsNumber())
                {
                    values.m_flipbookFps = fps->GetFloat();
                }
            }
            if (const JsonValue* updateColor = FindMember(emitter, "OpenParticle::UpdateColor"))
            {
                values.m_hasColorOverLife = true;
                // Files from before the mode play the gradient.
                values.m_colorOverLifeMode = ToOverrideMode(ReadString(FindMember(*updateColor, "mode")));
                if (const JsonValue* object = FindMember(*updateColor, "currentColorObject"))
                {
                    values.m_colorOverLife = ReadColorGradient(*object, distributions);
                }
                if (const JsonValue* object = FindMember(*updateColor, "secondColorObject"))
                {
                    values.m_secondColorOverLife = ReadColorGradient(*object, distributions);
                }
                values.m_randomColorA = ReadColorArray(FindMember(*updateColor, "randomColorA"), values.m_randomColorA);
                values.m_randomColorB = ReadColorArray(FindMember(*updateColor, "randomColorB"), values.m_randomColorB);
            }
            if (const JsonValue* object = FindValueObject(emitter, "OpenParticle::SpawnColor", "startColorObject"))
            {
                values.m_color = ReadColor(*object, distributions, 0.0f);
            }
            else if (!values.m_colorOverLife.empty())
            {
                values.m_color = values.m_colorOverLife.front().m_color;
            }
            return values;
        }
    } // namespace

    bool ReadEmitterAssetValues(const AZ::Data::AssetId& assetId, AZStd::vector<EmitterAssetValues>& emitters)
    {
        bool foundSource = false;
        AZ::Data::AssetInfo sourceInfo;
        AZStd::string watchFolder;
        AzToolsFramework::AssetSystemRequestBus::BroadcastResult(
            foundSource, &AzToolsFramework::AssetSystemRequestBus::Events::GetSourceInfoBySourceUUID, assetId.m_guid, sourceInfo, watchFolder);
        if (!foundSource)
        {
            return false;
        }
        const AZ::IO::Path sourcePath = AZ::IO::Path(watchFolder) / AZ::IO::Path(sourceInfo.m_relativePath);
        const auto document = AZ::JsonSerializationUtils::ReadJsonFile(sourcePath.Native());
        if (!document.IsSuccess() || !document.GetValue().IsObject())
        {
            return false;
        }
        const JsonValue& root = document.GetValue();
        const JsonValue* emitterArray = FindMember(root, "emitters");
        if (emitterArray == nullptr || !emitterArray->IsArray())
        {
            return false;
        }

        SourceDistributions distributions;
        if (const JsonValue* distribution = FindMember(root, "distribution"))
        {
            distributions.m_curves = FindMember(*distribution, "curves");
            distributions.m_randoms = FindMember(*distribution, "randoms");
        }

        emitters.clear();
        for (const auto& emitter : emitterArray->GetArray())
        {
            emitters.push_back(ReadEmitter(emitter, distributions));
        }
        return true;
    }
} // namespace OpenParticle
