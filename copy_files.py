from pathlib import Path
import shutil

SOURCE_ROOT = Path(r"C:\O3DE_FILES\o3de_fork")
DEST_ROOT = Path(r"C:\O3DE_FILES\o3de - particle")

files = [
    r"o3de\Gems\Atom\Tools\AtomToolsFramework\Code\Include\AtomToolsFramework\Graph\GraphView.h",
    r"o3de\Gems\GraphCanvas\Code\Source\Components\Nodes\NodeFrameGraphicsWidget.cpp",
    r"o3de\Gems\GraphModel\Code\Include\GraphModel\Integration\Helpers.h",
    r"o3de\Gems\GraphModel\Code\Include\GraphModel\Integration\GraphController.h",
    r"o3de\Gems\GraphModel\Code\Source\Integration\GraphController.cpp",
    r"o3de\Gems\Atom\Tools\AtomToolsFramework\Code\Source\Graph\GraphView.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\emotionfx_files.cmake",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Source\AnimGraphObjectFactory.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Source\BlendTreeRerouteNode.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Source\BlendTreeRerouteNode.h",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendGraphViewWidget.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendGraphViewWidget.h",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendGraphWidget.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendGraphWidget.h",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendTreeVisualNode.cpp",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\BlendTreeVisualNode.h",
    r"o3de\Gems\EMotionFX\Code\EMotionFX\Tools\EMotionStudio\Plugins\StandardPlugins\Source\AnimGraph\ContextMenu.cpp",
    r"o3de\Gems\LandscapeCanvas\Assets\LandscapeCanvas\StyleSheet\graphcanvas_style.json",
    r"o3de\Gems\LandscapeCanvas\Code\landscapecanvas_editor_static_files.cmake",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\MainWindow.cpp",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\MainWindow.h",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\Menus\NodeContextMenu.cpp",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\Menus\NodeContextMenu.h",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\Nodes\BaseNode.h",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\Nodes\RerouteNode.cpp",
    r"o3de\Gems\LandscapeCanvas\Code\Source\Editor\Nodes\RerouteNode.h",
    r"o3de\Gems\LandscapeCanvas\Code\Source\LandscapeCanvasSystemComponent.cpp",
    r"o3de\Gems\ScriptCanvas\Assets\ScriptCanvas\StyleSheet\graphcanvas_style.json",
    r"o3de\Gems\ScriptCanvas\Code\Editor\View\Windows\MainWindow.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Editor\View\Windows\ScriptCanvasContextMenus.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Editor\View\Windows\ScriptCanvasContextMenus.h",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Core\Contracts\RestrictedNodeContract.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Core\Node.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Core\Node.h",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Grammar\AbstractCodeModel.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Grammar\ParsingUtilities.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Grammar\ParsingUtilitiesScriptEventExtension.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Libraries\Core\CoreLibrary.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Libraries\Core\Reroute.cpp",
    r"o3de\Gems\ScriptCanvas\Code\Include\ScriptCanvas\Libraries\Core\Reroute.h",
    r"o3de\Gems\ScriptCanvas\Code\scriptcanvasgem_common_files.cmake",
    r"o3de\Gems\ScriptCanvas\Code\scriptcanvasgem_headers.cmake",
]

copied = 0
missing = 0
failed = 0

for relative_path in files:
    source = SOURCE_ROOT / relative_path
    destination = DEST_ROOT / relative_path

    if not source.exists():
        print(f"[MISSING] {source}")
        missing += 1
        continue

    try:
        destination.parent.mkdir(parents=True, exist_ok=True)

        shutil.copy2(source, destination)

        print(f"[COPIED] {source}")
        print(f"      -> {destination}")

        copied += 1

    except Exception as error:
        print(f"[ERROR] {source}")
        print(f"        {error}")
        failed += 1

print()
print("=" * 60)
print(f"Copied : {copied}")
print(f"Missing: {missing}")
print(f"Failed : {failed}")
print("=" * 60)