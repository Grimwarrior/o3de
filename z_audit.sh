#!/bin/bash
# Marker audit. Each line is  path|marker|description
# A marker is a fixed string that only exists because of an edit made in these sessions.
cd "$(dirname "$0")" || exit 1

CHECKS='
Code/Tools/CMakeLists.txt|O3DE_BUILD_AZSLC_FROM_SOURCE|opt-in azslc-from-source guard
Code/Tools/CMakeLists.txt|add_subdirectory(ShaderCompiler)|guarded ShaderCompiler subdir
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphCompiler.h|SetProductionOutputRequested|save/edit signal setter
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphCompiler.h|IsProductionOutputRequested|save/edit signal getter
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphCompiler.h|IsProductionOutputStale|staleness virtual
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphCompiler.h|m_productionOutputRequested|latch member
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocumentRequestBus.h|QueueApplyGraph|Apply bus method
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocumentRequestBus.h|IsApplyGraphNeeded|Apply-needed bus method
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocument.h|m_compileProductionOutputQueued|production latch member
Gems/Atom/Tools/AtomToolsFramework/Code/Include/AtomToolsFramework/Graph/GraphDocument.h|QueueApplyGraph|Apply override decl
Gems/Atom/Tools/AtomToolsFramework/Code/Source/Graph/GraphDocument.cpp|m_compileProductionOutputQueued|production latch use
Gems/Atom/Tools/AtomToolsFramework/Code/Source/Graph/GraphDocument.cpp|SetProductionOutputRequested|latch handed to compiler
Gems/Atom/Tools/AtomToolsFramework/Code/Source/Graph/GraphDocument.cpp|GraphDocument::QueueApplyGraph|Apply impl
Gems/Atom/Tools/AtomToolsFramework/Code/Source/Graph/GraphDocument.cpp|GraphDocument::IsApplyGraphNeeded|Apply-needed impl
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|enum class OutputSet|preview/production enum
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|PreviewOutputRootFolderName|preview root name
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|PreviewOutputRootRelativePath|preview root path
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|IsPreviewOutputEnabled|preview toggle
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|IsInMemoryPreviewMaterialEnabled|in-memory toggle
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|IsProductionOutputStale|staleness override
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|m_productionOutputStale|staleness member
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.h|m_currentOutputSet|current set member
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|GetOutputSetsForThisCompile|which sets to write
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|ExportOutputSetForCurrentNode|per-set export
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|DeleteStalePreviewOutputForCurrentNode|preview cleanup
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|RecordProductionOutputStaleness|staleness record
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|GetPreviewOutputFolderForGraph|preview folder mirror
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|GetProjectPath().c_str()|FixedMaxPathString fix
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/MaterialGraphCompiler.cpp|ProductionMaterialPipelines|production pipeline override
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/InMemoryShaderCompiler.h|RunInMemoryShaderSpike|shader spike decl
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/InMemoryShaderCompiler.cpp|EmitFullData|azslc call
Gems/Atom/Tools/MaterialCanvas/Code/Source/Document/InMemoryShaderCompiler.cpp|ParseSrgPopulateSrgData|reflection parse
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.h|ApplyInMemoryMaterial|in-memory apply decl
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.h|GetGeneratedFilePath|generated path helper
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.cpp|m_materialInstancePreCreated|foreign material hand-off
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.cpp|GetImageAssetReference|image property fix
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.cpp|ModificationTime|freshness guard
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasViewportContent.cpp|CreateMaterialTypeAsset|material type build
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasMainWindow.cpp|QueueApplyGraph|Apply action wiring
Gems/Atom/Tools/MaterialCanvas/Code/Source/Window/MaterialCanvasMainWindow.cpp|insertAction|hand-written QAction
Gems/Atom/Tools/MaterialCanvas/Code/CMakeLists.txt|InMemoryShaderCompiler|new files in build
Gems/Atom/Tools/MaterialCanvas/Code/CMakeLists.txt|AtomShader.Static|shader static lib dep
Gems/Atom/Asset/Shader/Code/CMakeLists.txt|PUBLIC|Source made PUBLIC
Gems/Atom/RPI/Code/Source/RPI.Builders/Material/MaterialBuilderUtils.h|JobPhaseTimer|phase timer class
Gems/Atom/RPI/Code/Source/RPI.Builders/Material/MaterialBuilderUtils.h|Mark(|phase mark
Gems/Atom/RPI/Code/Source/RPI.Public/Material/MaterialSystem.cpp|m_materialTypeAssetHint|crash fix
'

pass=0; fail=0
printf '%-6s %-44s %s\n' "STATUS" "MARKER" "FILE"
printf '%s\n' "-------------------------------------------------------------------------------------------------------"
while IFS='|' read -r path marker desc; do
    [ -z "$path" ] && continue
    if [ ! -f "$path" ]; then
        printf '%-6s %-44s %s\n' "NOFILE" "$marker" "$path"
        fail=$((fail+1)); continue
    fi
    if grep -qF -- "$marker" "$path"; then
        pass=$((pass+1))
    else
        printf '%-6s %-44s %s   (%s)\n' "GONE" "$marker" "$path" "$desc"
        fail=$((fail+1))
    fi
done <<< "$CHECKS"

printf '\n%s\n' "-------------------------------------------------------------------------------------------------------"
printf 'markers present: %s   markers missing: %s\n' "$pass" "$fail"
