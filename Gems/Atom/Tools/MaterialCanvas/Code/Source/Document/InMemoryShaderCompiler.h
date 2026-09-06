/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <Atom/RHI.Edit/ShaderPlatformInterface.h>
#include <Atom/RPI.Reflect/Material/MaterialTypeAsset.h>
#include <Atom/RPI.Reflect/Shader/ShaderAsset.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzCore/std/utils.h>

namespace MaterialCanvas
{
    //! Builds a MaterialTypeAsset in process from the intermediate material type the Asset Processor already produced, which is what
    //! the FinalStage job does for 300 ms of Asset Processor time and 16 ms of actual work.
    //!
    //! Returns an invalid asset on any failure, having reported why. Callers are expected to fall back to the asset system path,
    //! because every reason this can fail is a reason the assets are not ready yet rather than a reason they never will be.
    //!
    //! @param materialTypeSourcePath the abstract .materialtype the canvas wrote. The intermediate is located from it the same way
    //! MaterialBuilder locates it, through MaterialUtils::PredictIntermediateMaterialTypeSourcePath.
    AZ::Data::Asset<AZ::RPI::MaterialTypeAsset> CreateInMemoryMaterialTypeAsset(const AZStd::string& materialTypeSourcePath);

    //! One entry point of a shader: the function name azslc and DXC are pointed at, and which hardware stage it is.
    struct InMemoryShaderEntryPoint
    {
        AZStd::string m_name;
        AZ::RHI::ShaderHardwareStage m_stage = AZ::RHI::ShaderHardwareStage::Invalid;
    };

    //! Builds a ShaderAsset in process from AZSL source, by compiling it and then cloning @sourceShaderAsset with the result.
    //!
    //! Cloning rather than building from nothing is what makes this tractable. A ShaderAsset carries far more than byte code -- SRG
    //! layouts, a pipeline layout descriptor, input and output contracts, render states, the shader option group layout -- and
    //! assembling all of that is what ShaderAssetBuilder::ProcessJob does. ShaderAssetCreator::Clone copies every one of those from
    //! an existing asset and replaces only the variants, so the only thing that has to be produced here is the root variant's byte
    //! code. Nothing is reimplemented and nothing can drift.
    //!
    //! The price is the reason this cannot be used unconditionally: everything Clone copies describes the shader's *interface*, and
    //! it comes from @sourceShaderAsset rather than from the source just compiled. That is correct for an edit that changes only
    //! shader code -- the overwhelmingly common case while authoring a graph, and the one worth making fast -- and wrong for an edit
    //! that adds a resource, changes an SRG or changes the shader options. The caller must therefore treat a null return as "use the
    //! Asset Processor", not as an error, and this function returns null whenever it can tell the interface has moved.
    //!
    //! @param azslPath a .azsl or an already preprocessed .azslin. Given a .azsl the platform header is prepended and MCPP runs first.
    //! @param sourceShaderAsset the asset to clone. Its AssetId is kept, so the result can be handed to
    //!        ShaderCollection::Item::TryReplaceShaderAsset, which only accepts an asset whose id matches the one it is replacing.
    //! @param entryPoints the shader's entry points, in any order.
    //!
    //! Runs the compilers as child processes, so it must not be called on the main thread.
    AZ::Data::Asset<AZ::RPI::ShaderAsset> CreateInMemoryShaderAsset(
        const AZStd::string& azslPath,
        const AZ::Data::Asset<AZ::RPI::ShaderAsset>& sourceShaderAsset,
        const AZStd::vector<InMemoryShaderEntryPoint>& entryPoints);

    //! One shader of a material type, paired with everything needed to rebuild it in process.
    struct InMemoryShaderRequest
    {
        AZ::Data::Asset<AZ::RPI::ShaderAsset> m_sourceShaderAsset; //!< What to clone, and whose AssetId the result keeps.
        AZStd::string m_azslPath;                                  //!< The intermediate .azsl the material pipeline stage wrote.
        AZStd::string m_shaderPath;                                //!< The matching .shader, for asking the Asset Processor to rebuild.
        AZStd::vector<InMemoryShaderEntryPoint> m_entryPoints;     //!< Read from the matching intermediate .shader.
    };

    //! Works out which shaders a material type is built from and where their sources are, so they can be recompiled in process.
    //!
    //! The sources are the intermediate .azsl and .shader that MaterialTypeBuilder's pipeline stage writes. They exist well before
    //! the shader assets themselves do -- the pipeline stage runs first and takes about 350 ms, the shader jobs follow -- which is
    //! the whole opportunity: everything needed to build the shader is on disk long before the Asset Processor has finished
    //! building it.
    //!
    //! Returns an empty list when anything is missing, which is the normal state before the pipeline stage has run for this edit.
    AZStd::vector<InMemoryShaderRequest> CollectInMemoryShaderRequests(
        const AZ::Data::Asset<AZ::RPI::MaterialTypeAsset>& materialTypeAsset, const AZStd::string& materialTypeSourcePath);

    //! Recompiles every request and hands back the shaders that succeeded, keyed by the AssetId they replace.
    //!
    //! Runs the compilers as child processes, so it must not be called on the main thread.
    AZStd::vector<AZStd::pair<AZ::Data::AssetId, AZ::Data::Asset<AZ::RPI::ShaderAsset>>> CompileInMemoryShaders(
        const AZStd::vector<InMemoryShaderRequest>& requests);
} // namespace MaterialCanvas
