--------------------------------------------------------------------------------------
--
-- Copyright (c) Contributors to the Open 3D Engine Project.
-- For complete copyright and license terms please see the LICENSE at the root of this distribution.
--
-- SPDX-License-Identifier: Apache-2.0 OR MIT
--
--
--
----------------------------------------------------------------------------------------------------

-- The distortion draw of a ribbon wakes the frame copy of the distortion pass, so it only exists while the material warps anything.
function GetMaterialPropertyDependencies()
    return { "distortion.strength" }
end

function Process(context)
    if context:HasShaderWithTag("RibbonDistortion") then
        local shader = context:GetShaderByTag("RibbonDistortion")
        shader:SetEnabled(context:GetMaterialPropertyValue_float("distortion.strength") > 0.0)
    end
end
