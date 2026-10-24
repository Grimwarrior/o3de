/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/string/string_view.h>

namespace OpenParticleSystemEditor::ActionIdentifiers
{
    // Hotkeys of this context only fire while the Particle Editor has focus, so they can share keys with the main Editor
    inline constexpr AZStd::string_view ParticleEditorActionContext = "o3de.context.particleEditor";

    inline constexpr AZStd::string_view FileMenu = "o3de.menu.particleEditor.file";

    inline constexpr AZStd::string_view NewAction = "o3de.action.particleEditor.file.new";
    inline constexpr AZStd::string_view OpenAction = "o3de.action.particleEditor.file.open";
    inline constexpr AZStd::string_view SaveAction = "o3de.action.particleEditor.file.save";
    inline constexpr AZStd::string_view SaveAsAction = "o3de.action.particleEditor.file.saveAs";
    inline constexpr AZStd::string_view SaveAllAction = "o3de.action.particleEditor.file.saveAll";
} // namespace OpenParticleSystemEditor::ActionIdentifiers
