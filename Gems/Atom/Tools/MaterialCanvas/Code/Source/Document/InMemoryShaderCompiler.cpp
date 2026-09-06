/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/RHI.Edit/Utils.h>
#include <Atom/RPI.Reflect/Material/MaterialPropertiesLayout.h>
#include <Atom/RPI.Reflect/Material/MaterialTypeAsset.h>
#include <Atom/RPI.Edit/Material/MaterialUtils.h>
#include <Atom/RPI.Edit/Material/MaterialTypeSourceData.h>
#include <Atom/RPI.Reflect/Shader/ShaderOptionGroupLayout.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/IO/Path/Path.h>
#include <AzCore/StringFunc/StringFunc.h>
#include <AzCore/Serialization/Json/JsonUtils.h>
#include <AzCore/std/chrono/chrono.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/parallel/thread.h>
#include <AzCore/Utils/Utils.h>
#include <Document/InMemoryShaderCompiler.h>

// From Atom_Asset_Shader.Static. These are the same types the Shader Asset Builder uses, included exactly as the builder includes
// them: the point of the spike is that a tool can call them, so wrapping them would defeat it.
#include <Editor/AzslCompiler.h>
#include <Editor/CommonFiles/Preprocessor.h>
#include <Editor/ShaderBuilderUtility.h>

#include <Atom/RPI.Edit/Shader/ShaderVariantAssetCreator.h>
#include <Atom/RPI.Reflect/Shader/ShaderAssetCreator.h>

#if defined(AZ_PLATFORM_WINDOWS)
// The DX12 ShaderPlatformInterface, from Atom_RHI_DX12.Builders.Static. Its constructor is public and
// CompilePlatformInternal ignores the PlatformInfo it is handed, so a tool can drive the same DXC invocation the Shader Asset
// Builder drives without standing up any builder context. See shader_dependencies_windows.cmake for why this is Windows only.
#include <RHI.Builders/ShaderPlatformInterface.h>
#endif

namespace MaterialCanvas
{
    namespace
    {
        // Copied from a "ShaderPlatformInterface: Executing" line in a Material Canvas job log, so that this measures the work the
        // Asset Processor measures. --full is what makes one invocation produce the HLSL and every reflection document together;
        // asking for them one switch at a time would be several azslc runs and would report a cost the real pipeline never pays.
        const AZStd::vector<AZStd::string> AzslcArguments = {
            "--full", "--Zpr", "--W1", "--strip-unused-srgs", "--root-const=128", "--sc-options", "--namespace=dx"
        };

        // Reconstructed from the "Preprocessor: builder ..." line of a Material Canvas job log. -C keeps comments and -+ enables C++
        // mode; the six defines are the preview pipeline's fidelity reductions, which have to be here or MCPP expands code the real
        // preview shader never sees and the measurement is of a different shader.
        const AZStd::vector<AZStd::string> PreprocessorArguments = {
            "-C", "-+",
            "-DENABLE_AREA_LIGHTS=0", "-DENABLE_DECALS=0", "-DENABLE_SHADOWS=0",
            "-DENABLE_SHADER_DEBUGGING=0", "-DENABLE_LIGHT_CULLING=0", "-DENABLE_ACESCC_COLOR_SPACE=0"
        };

        // The engine and project ShaderLib roots the builder puts on the include path, plus the folder holding the input so that a
        // generated shader can find the azsli files generated beside it.
        AZStd::vector<AZStd::string> BuildIncludePaths(const AZStd::string& inputPath)
        {
            const AZStd::string enginePath = AZ::Utils::GetEnginePath().c_str();
            const AZStd::string projectPath = AZ::Utils::GetProjectPath().c_str();

            AZStd::string inputFolder = inputPath;
            AZ::StringFunc::Path::StripFullName(inputFolder);

            return {
                inputFolder,
                projectPath,
                projectPath + "/ShaderLib",
                enginePath + "/Gems/Atom/RHI/Assets/ShaderLib",
                enginePath + "/Gems/Atom/Feature/Common/Assets/ShaderLib",
                enginePath + "/Gems/Atom/RPI/Assets/ShaderLib",
                enginePath + "/Gems",
            };
        }

        double MillisecondsSince(const AZStd::chrono::steady_clock::time_point& start)
        {
            return AZStd::chrono::duration<double, AZStd::milli>(AZStd::chrono::steady_clock::now() - start).count();
        }

        size_t CountLines(const AZStd::string& path)
        {
            const auto contents = AZ::Utils::ReadFile(path);
            if (!contents.IsSuccess())
            {
                return 0;
            }
            // Counted by hand rather than with a standard algorithm: AzCore's AZStd/algorithm.h does not alias std::count, and
            // reaching for <algorithm> here to save three lines would pull the std namespace into a file that otherwise lives
            // entirely in AZStd.
            size_t lineCount = 0;
            for (const char character : contents.GetValue())
            {
                lineCount += (character == '\n') ? 1 : 0;
            }
            return lineCount;
        }
    } // namespace
} // namespace MaterialCanvas

namespace MaterialCanvas
{
} // namespace MaterialCanvas

namespace MaterialCanvas
{
    AZ::Data::Asset<AZ::RPI::MaterialTypeAsset> CreateInMemoryMaterialTypeAsset(const AZStd::string& materialTypeSourcePath)
    {
        const AZStd::string intermediatePath =
            AZ::RPI::MaterialUtils::PredictIntermediateMaterialTypeSourcePath(materialTypeSourcePath);
        if (intermediatePath.empty())
        {
            return {};
        }

        auto sourceDataOutcome = AZ::RPI::MaterialUtils::LoadMaterialTypeSourceData(intermediatePath);
        if (!sourceDataOutcome.IsSuccess())
        {
            // Expected while PipelineStage has not run yet for this edit. The caller falls back to the asset system, which waits.
            return {};
        }

        const AZ::RPI::MaterialTypeSourceData intermediateSourceData = sourceDataOutcome.TakeValue();

        // CreateMaterialTypeAsset asserts rather than returning a failure on the abstract format, so this has to be checked here.
        if (intermediateSourceData.GetFormat() != AZ::RPI::MaterialTypeSourceData::Format::Direct)
        {
            return {};
        }

        // Warnings are not elevated to errors: the intermediate can legitimately reference a shader whose asset is still building,
        // and the caller's fallback handles that better than a hard failure would.
        auto materialTypeAssetOutcome =
            intermediateSourceData.CreateMaterialTypeAsset(AZ::Uuid::CreateRandom(), intermediatePath, false);
        if (!materialTypeAssetOutcome.IsSuccess())
        {
            return {};
        }

        return materialTypeAssetOutcome.TakeValue();
    }
} // namespace MaterialCanvas

namespace MaterialCanvas
{
    AZ::Data::Asset<AZ::RPI::ShaderAsset> CreateInMemoryShaderAsset(
        [[maybe_unused]] const AZStd::string& azslPath,
        [[maybe_unused]] const AZ::Data::Asset<AZ::RPI::ShaderAsset>& sourceShaderAsset,
        [[maybe_unused]] const AZStd::vector<InMemoryShaderEntryPoint>& entryPoints)
    {
#if !defined(AZ_PLATFORM_WINDOWS)
        // No ShaderPlatformInterface to drive off Windows; see shader_dependencies_windows.cmake.
        return {};
#else
        using namespace AZ::ShaderBuilder;

        auto decline = [](const char* reason)
        {
            // Not an error. Every way this can fail is a way of saying "let the Asset Processor do it", and the caller is expected
            // to have that path available.
            AZ_TracePrintf("MaterialCanvas", "In-memory shader asset declined: %s\n", reason);
            return AZ::Data::Asset<AZ::RPI::ShaderAsset>{};
        };

        if (!sourceShaderAsset.IsReady() || entryPoints.empty())
        {
            return decline("no source shader asset to clone, or no entry points");
        }

        auto fileIO = AZ::IO::FileIOBase::GetInstance();
        if (!fileIO || !fileIO->Exists(azslPath.c_str()))
        {
            return decline("the AZSL source does not exist");
        }

        // Outside every asset scan folder, so nothing written here is ever seen by the Asset Processor.
        const AZ::IO::Path tempFolder = AZ::IO::Path(AZ::Utils::GetProjectPath()) / "user" / "MaterialCanvasInMemoryShaders";
        if (!fileIO->Exists(tempFolder.c_str()) && !fileIO->CreatePath(tempFolder.c_str()))
        {
            return decline("the temp folder could not be created");
        }

        // ------------------------------------------------------------------------------------------------------------------
        // MCPP and azslc. Identical to the spike above, which exists to measure exactly this.
        // ------------------------------------------------------------------------------------------------------------------

        AZStd::string azslcInputPath = azslPath;

        if (!azslPath.ends_with(".azslin"))
        {
            AZ::RHI::PrependArguments prependArguments;
            prependArguments.m_sourceFile = azslPath.c_str();
            prependArguments.m_prependFile = "Builders/ShaderHeaders/Platform/Windows/DX12/AzslcHeader.azsli";
            prependArguments.m_addSuffixToFileName = "dx12";
            prependArguments.m_destinationFolder = tempFolder.c_str();

            const AZStd::string prependedPath = AZ::RHI::PrependFile(prependArguments);
            if (prependedPath == azslPath)
            {
                return decline("the platform AZSL header could not be prepended");
            }

            PreprocessorData preprocessorOutput;
            if (!PreprocessFile(
                    prependedPath,
                    preprocessorOutput,
                    AppendIncludePathsToArgumentList(PreprocessorArguments, BuildIncludePaths(azslPath)),
                    true))
            {
                return decline("MCPP rejected the input");
            }

            azslcInputPath = ShaderBuilderUtility::DumpPreprocessedCode(
                "MaterialCanvas", preprocessorOutput.code, tempFolder.Native(), AZ::IO::Path(azslPath).Stem().Native(), "dx12");
            if (azslcInputPath.empty())
            {
                return decline("the preprocessed code could not be written out for azslc");
            }
        }

        const AzslCompiler azslc(azslcInputPath, tempFolder.Native());

        AZ::IO::Path azslcOutputPath = tempFolder / AZ::IO::Path(azslcInputPath).Stem();
        azslcOutputPath.ReplaceExtension(".hlsl");

        const auto emitOutcome = azslc.EmitFullData(AzslcArguments, azslcOutputPath.Native());
        if (!emitOutcome.IsSuccess())
        {
            return decline("azslc rejected the input");
        }

        const ShaderBuilderUtility::AzslSubProducts::Paths& products = emitOutcome.GetValue();

        // ------------------------------------------------------------------------------------------------------------------
        // The interface guard. Clone copies the SRG layouts, pipeline layout and contracts from the source asset, so this is only
        // valid while the shader the graph now describes has the same interface as the one that asset was built from.
        //
        // Checked through the shader option group layout, which is the part of the interface that a Material Canvas edit is most
        // likely to move: adding a node that introduces a shader option changes the option list, and a material built against the
        // old layout would then index options that are not there. Its hash covers the option names, types, order and bit layout.
        //
        // This does not cover every possible interface change -- an SRG gaining a texture would not move the option layout -- so
        // it is a guard rather than a proof. The remaining exposure is bounded by what the Asset Processor is doing concurrently:
        // a structural edit changes the generated .shader and .azsl too, so the Asset Processor rebuilds the real asset and the
        // catalog notification replaces whatever this produced.
        // ------------------------------------------------------------------------------------------------------------------

        AZ::RPI::Ptr<AZ::RPI::ShaderOptionGroupLayout> shaderOptionGroupLayout = AZ::RPI::ShaderOptionGroupLayout::Create();
        {
            const auto optionsDocument =
                AZ::JsonSerializationUtils::ReadJsonFile(products[ShaderBuilderUtility::AzslSubProducts::options]);
            if (!optionsDocument.IsSuccess())
            {
                return decline("the --options document could not be read back");
            }

            bool usesSpecializationConstants = false;
            if (!azslc.ParseOptionsPopulateOptionGroupLayout(optionsDocument.GetValue(), shaderOptionGroupLayout, usesSpecializationConstants))
            {
                return decline("ParseOptionsPopulateOptionGroupLayout rejected the --options document");
            }
        }

        const AZ::RPI::ShaderOptionGroupLayout* sourceOptionGroupLayout = sourceShaderAsset->GetShaderOptionGroupLayout();
        if (!sourceOptionGroupLayout || sourceOptionGroupLayout->GetHash() != shaderOptionGroupLayout->GetHash())
        {
            return decline("the shader option layout has changed, so the existing asset is not safe to clone");
        }

        // ------------------------------------------------------------------------------------------------------------------
        // DXC, one invocation per entry point, then the root variant those stages make up.
        // ------------------------------------------------------------------------------------------------------------------

        AZ::DX12::ShaderPlatformInterface dx12ShaderPlatformInterface(0);

        AZ::RHI::ShaderBuildArguments shaderBuildArguments;
        shaderBuildArguments.m_dxcArguments = { "-Zpr", "-enable-16bit-types", "-O1" };

        const AZ::Data::Asset<AZ::RPI::ShaderVariantAsset> sourceRootVariant = sourceShaderAsset->GetRootVariantAsset();
        if (!sourceRootVariant.IsReady())
        {
            return decline("the source shader asset has no root variant to match");
        }

        AZ::RPI::ShaderVariantAssetCreator variantCreator;
        variantCreator.Begin(
            sourceRootVariant.GetId(), sourceRootVariant->GetShaderVariantId(), AZ::RPI::RootShaderVariantStableId, false);

        // The entry points are independent compiles of the same HLSL, so they run together and the shorter one costs nothing.
        // Measured sequentially here: DXC and dxsc were 71 + 20 for the vertex stage against 261 + 36 for the pixel stage, so
        // overlapping them hides the whole vertex stage. This is the same change ShaderVariantAssetBuilder already carries; only
        // CompilePlatformInternal runs in parallel, and everything touching the variant creator happens afterwards in order.
        AZStd::vector<AZ::RHI::ShaderPlatformInterface::StageDescriptor> descriptors(entryPoints.size());
        AZStd::vector<bool> compiled(entryPoints.size(), false);

        auto compileEntryPoint = [&](size_t index)
        {
            const AssetBuilderSDK::PlatformInfo platformInfo;
            compiled[index] = dx12ShaderPlatformInterface.CompilePlatformInternal(
                platformInfo,
                products[ShaderBuilderUtility::AzslSubProducts::hlsl],
                entryPoints[index].m_name,
                entryPoints[index].m_stage,
                tempFolder.Native(),
                descriptors[index],
                shaderBuildArguments,
                // Specialization constants, matching what the Asset Processor does for this shader.
                //
                // azslc is run with --sc-options above, so the HLSL it emits addresses its shader options through specialization
                // constants rather than baking them in, and the DXIL that comes out of DXC still holds the sentinel values. dxsc
                // patches those and writes the offsets json that CreateShaderStageFunction needs; CompilePlatformInternal only
                // runs it when told to. Passing false here produced a shader that ran on sentinels instead of the material's
                // actual option values -- visible as geometry stretched along every axis, because the vertex stage read nonsense.
                true);
        };

        {
            AZStd::vector<AZStd::thread> compileThreads;
            compileThreads.reserve(entryPoints.size() - 1);
            for (size_t i = 1; i < entryPoints.size(); ++i)
            {
                compileThreads.emplace_back([&compileEntryPoint, i]() { compileEntryPoint(i); });
            }

            compileEntryPoint(0);

            for (AZStd::thread& compileThread : compileThreads)
            {
                compileThread.join();
            }
        }

        for (size_t i = 0; i < entryPoints.size(); ++i)
        {
            if (!compiled[i])
            {
                return decline("DXC rejected an entry point");
            }

            AZ::RHI::Ptr<AZ::RHI::ShaderStageFunction> shaderStageFunction =
                dx12ShaderPlatformInterface.CreateShaderStageFunction(descriptors[i]);
            variantCreator.SetShaderFunction(AZ::RHI::ToRHIShaderStage(entryPoints[i].m_stage), shaderStageFunction);
        }

        AZ::Data::Asset<AZ::RPI::ShaderVariantAsset> rootVariantAsset;
        if (!variantCreator.End(rootVariantAsset) || !rootVariantAsset)
        {
            return decline("the root shader variant could not be created");
        }

        // ------------------------------------------------------------------------------------------------------------------
        // Clone, keeping the source asset's id so the result can replace it in a material type's shader collection.
        // ------------------------------------------------------------------------------------------------------------------

        AZ::RPI::ShaderAssetCreator::ShaderSupervariants supervariants;
        {
            AZ::RPI::ShaderAssetCreator::ShaderSupervariant supervariant;
            // The default supervariant, whose name is empty. Material Canvas preview shaders declare no others, and Clone requires
            // the incoming list to have one entry per supervariant on the source asset.
            supervariant.m_name = AZ::Name{};
            supervariant.m_rootVariantAssets.push_back({ dx12ShaderPlatformInterface.GetAPIType(), rootVariantAsset });
            supervariants.push_back(AZStd::move(supervariant));
        }

        AZStd::vector<AZ::RHI::ShaderPlatformInterface*> platformInterfaces = { &dx12ShaderPlatformInterface };

        AZ::RPI::ShaderAssetCreator shaderAssetCreator;
        shaderAssetCreator.Clone(sourceShaderAsset.GetId(), *sourceShaderAsset.Get(), supervariants, platformInterfaces);

        AZ::Data::Asset<AZ::RPI::ShaderAsset> shaderAsset;
        if (!shaderAssetCreator.End(shaderAsset) || !shaderAsset)
        {
            return decline("ShaderAssetCreator::Clone did not produce an asset");
        }

        return shaderAsset;
#endif
    }
} // namespace MaterialCanvas

namespace MaterialCanvas
{
    AZStd::vector<InMemoryShaderRequest> CollectInMemoryShaderRequests(
        const AZ::Data::Asset<AZ::RPI::MaterialTypeAsset>& materialTypeAsset, const AZStd::string& materialTypeSourcePath)
    {
        AZStd::vector<InMemoryShaderRequest> requests;

        if (!materialTypeAsset || materialTypeSourcePath.empty())
        {
            return requests;
        }

        // The intermediate .azsl and .shader sit next to the intermediate material type, which is where the pipeline stage put all
        // three. Deriving the folder from that path rather than guessing keeps this working wherever the intermediate tree lives.
        const AZStd::string intermediateMaterialTypePath =
            AZ::RPI::MaterialUtils::PredictIntermediateMaterialTypeSourcePath(materialTypeSourcePath);
        if (intermediateMaterialTypePath.empty())
        {
            return requests;
        }

        AZ::IO::Path intermediateFolder{ intermediateMaterialTypePath };
        intermediateFolder = intermediateFolder.ParentPath();

        auto fileIO = AZ::IO::FileIOBase::GetInstance();
        if (!fileIO)
        {
            return requests;
        }

        size_t shaderItemCount = 0;
        size_t notReadyCount = 0;
        size_t sourcesMissingCount = 0;

        // Both collections, not just the general one.
        //
        // A material type built through a material pipeline keeps its shaders in that pipeline's payload; the general collection is
        // for shaders that apply whatever pipeline is in use, and for a Material Canvas preview material type it is empty. Walking
        // only the general collection therefore finds nothing at all, silently -- which is exactly what an earlier attempt at this
        // did when it subscribed to "the shader assets this material depends on" and got an empty list.
        auto collectFrom = [&](const AZ::RPI::ShaderCollection& shaderCollection)
        {
            for (const auto& shaderItem : shaderCollection)
            {
                ++shaderItemCount;

                const AZ::Data::Asset<AZ::RPI::ShaderAsset>& shaderAsset = shaderItem.GetShaderAsset();
                if (!shaderAsset.IsReady())
                {
                    ++notReadyCount;
                    continue;
                }

                // The shader asset's name is the stem the pipeline stage used for all three files it wrote: ShaderAssetBuilder sets
                // it from the .shader source file name.
                const AZStd::string shaderStem = shaderAsset->GetName().GetStringView();
                if (shaderStem.empty())
                {
                    continue;
                }

                // Already collected from another pipeline payload. The same shader can appear in more than one collection, and
                // compiling it twice would be wasted work and two assets racing to replace the same id.
                const bool alreadyCollected = AZStd::any_of(
                    requests.begin(),
                    requests.end(),
                    [&shaderAsset](const InMemoryShaderRequest& existing)
                    {
                        return existing.m_sourceShaderAsset.GetId() == shaderAsset.GetId();
                    });
                if (alreadyCollected)
                {
                    continue;
                }

                const AZ::IO::Path azslPath = intermediateFolder / (shaderStem + ".azsl");
                const AZ::IO::Path shaderPath = intermediateFolder / (shaderStem + ".shader");

                if (!fileIO->Exists(azslPath.c_str()) || !fileIO->Exists(shaderPath.c_str()))
                {
                    // The pipeline stage has not written this edit's sources yet. Normal, and the caller waits.
                    ++sourcesMissingCount;
                    continue;
                }

                const auto shaderSourceDataOutcome =
                    AZ::ShaderBuilder::ShaderBuilderUtility::LoadShaderDataJson(shaderPath.Native(), false);
                if (!shaderSourceDataOutcome.IsSuccess())
                {
                    continue;
                }

                InMemoryShaderRequest request;
                request.m_sourceShaderAsset = shaderAsset;
                request.m_azslPath = azslPath.Native();
                request.m_shaderPath = shaderPath.Native();

                for (const auto& entryPoint : shaderSourceDataOutcome.GetValue().m_programSettings.m_entryPoints)
                {
                    // ShaderSourceData speaks RPI::ShaderStageType; the ShaderPlatformInterface speaks RHI::ShaderHardwareStage.
                    // ToAssetBuilderShaderType is the same conversion ShaderVariantAssetBuilder makes at this boundary.
                    InMemoryShaderEntryPoint inMemoryEntryPoint;
                    inMemoryEntryPoint.m_name = entryPoint.m_name;
                    inMemoryEntryPoint.m_stage = AZ::ShaderBuilder::ShaderBuilderUtility::ToAssetBuilderShaderType(entryPoint.m_type);
                    request.m_entryPoints.push_back(AZStd::move(inMemoryEntryPoint));
                }

                if (!request.m_entryPoints.empty())
                {
                    requests.push_back(AZStd::move(request));
                }
            }
        };

        collectFrom(materialTypeAsset->GetGeneralShaderCollection());

        for (const auto& [materialPipelineName, materialPipelinePayload] : materialTypeAsset->GetMaterialPipelinePayloads())
        {
            collectFrom(materialPipelinePayload.m_shaderCollection);
        }

        AZ_TracePrintf(
            "MaterialCanvas",
            "In-memory shader collection: %zu shader item(s), %zu not loaded, %zu missing sources, %zu request(s).\n",
            shaderItemCount, notReadyCount, sourcesMissingCount, requests.size());

        return requests;
    }

    AZStd::vector<AZStd::pair<AZ::Data::AssetId, AZ::Data::Asset<AZ::RPI::ShaderAsset>>> CompileInMemoryShaders(
        const AZStd::vector<InMemoryShaderRequest>& requests)
    {
        AZStd::vector<AZStd::pair<AZ::Data::AssetId, AZ::Data::Asset<AZ::RPI::ShaderAsset>>> compiled;

        for (const InMemoryShaderRequest& request : requests)
        {
            const AZ::Data::Asset<AZ::RPI::ShaderAsset> shaderAsset =
                CreateInMemoryShaderAsset(request.m_azslPath, request.m_sourceShaderAsset, request.m_entryPoints);

            if (shaderAsset)
            {
                compiled.emplace_back(request.m_sourceShaderAsset.GetId(), shaderAsset);
            }
        }

        return compiled;
    }
} // namespace MaterialCanvas
