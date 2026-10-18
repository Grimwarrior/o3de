/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include "Interaction/ViewportInteraction.h"

class HierarchyWidget;

namespace AZ
{
    class Entity;
    class EntityId;
    class Vector2;
} // namespace AZ

namespace ViewportHelpers
{
    struct SelectedAnchors;
    struct ElementEdges;
    struct GizmoParts;
} // namespace ViewportHelpers

class ViewportElement
{
public:
    // Used to determine what the cursor is hovering over
    static bool PickElementEdges(
        const AZ::Entity* element, const AZ::Vector2& point, float distance, ViewportHelpers::ElementEdges& outEdges);
    static bool PickAnchors(
        const AZ::Entity* element, const AZ::Vector2& point, const AZ::Vector2& iconSize, ViewportHelpers::SelectedAnchors& outAnchors);
    static bool PickAxisGizmo(
        const AZ::Entity* element,
        ViewportInteraction::CoordinateSystem coordinateSystem,
        ViewportInteraction::InteractionMode interactionMode,
        const AZ::Vector2& point,
        const AZ::Vector2& iconSize,
        ViewportHelpers::GizmoParts& outGizmoParts);
    static bool PickCircleGizmo(
        const AZ::Entity* element, const AZ::Vector2& point, const AZ::Vector2& iconSize, ViewportHelpers::GizmoParts& outGizmoParts);
    static bool PickPivot(const AZ::Entity* element, const AZ::Vector2& point, const AZ::Vector2& iconSize);

    static void ResizeDirectly(
        HierarchyWidget* hierarchy,
        const AZ::EntityId& canvasId,
        const ViewportHelpers::ElementEdges& grabbedEdges,
        AZ::Entity* element,
        const AZ::Vector3& mouseTranslation);
    static void ResizeByGizmo(
        HierarchyWidget* hierarchy,
        const AZ::EntityId& canvasId,
        const ViewportHelpers::GizmoParts& grabbedGizmoParts,
        const AZ::EntityId& activeElementId,
        AZ::Entity* element,
        const AZ::Vector3& mouseTranslation);
    static void Rotate(
        HierarchyWidget* hierarchy,
        const AZ::EntityId& canvasId,
        const AZ::Vector2& lastMouseDragPos,
        const AZ::EntityId& activeElementId,
        AZ::Entity* element,
        const AZ::Vector2& mousePosition);
    static void MoveAnchors(
        const ViewportHelpers::SelectedAnchors& grabbedAnchors,
        const UiTransform2dInterface::Anchors& startAnchors,
        const AZ::Vector2& startMouseDragPos,
        AZ::Entity* element,
        const AZ::Vector2& mousePosition,
        bool adjustOffsets);
    static void MovePivot(const AZ::Vector2& lastMouseDragPos, AZ::Entity* element, const AZ::Vector2& mousePosition);

    //! Capture the elements a Scale gizmo drag will change, and the point each one scales around.
    //! The Selection pivot mode only scales the top level elements since their children follow them.
    static AZStd::vector<ViewportElementScaleStart> BeginScale(
        const LyShine::EntityArray& selectedElements,
        const LyShine::EntityArray& topLevelSelectedElements,
        ViewportInteraction::ScalePivotMode pivotMode);

    //! Scale the captured elements by the factor, measured from the values they had when the drag started
    static void Scale(const AZStd::vector<ViewportElementScaleStart>& startStates, const AZ::Vector2& factor);
};
