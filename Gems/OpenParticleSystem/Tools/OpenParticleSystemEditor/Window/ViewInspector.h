/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/std/string/string.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzToolsFramework/UI/PropertyEditor/ReflectedPropertyEditor.hxx>
#include <AzToolsFramework/UI/PropertyEditor/PropertyEditorAPI_Internals.h>
#include <Viewport/OpenParticleViewportWidget.h>

#include <QWidget>
#include <QMenu>
#include <QPoint>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QToolBar>
#include <QAction>
#include <QToolButton>
#include <LightingPresetMenu.h>

class QLabel;
class QSlider;
class QTimer;

namespace OpenParticleSystemEditor
{
    class ViewInspector
        : public QWidget
        , public OpenParticleViewportNotificationBus::Handler
    {
        Q_OBJECT;

    public:
        AZ_CLASS_ALLOCATOR(ViewInspector, AZ::SystemAllocator, 0);

        explicit ViewInspector(QWidget* parent = nullptr);
        ~ViewInspector();

    Q_SIGNALS:

    private:
        void CreatTopBar();
        void AddSpeedControl(QHBoxLayout* layout);
        void CreatePlayerProgressBar();
        void CreatePlayerAction();

        //! The player bar drives only the Particle Editor's preview entity, never particles in the level.
        AZ::EntityId GetPreviewEntityId() const;
        void UpdatePlayerBar();

        // OpenParticleViewportNotificationBus::Handler overrides...
        void OnGridEnabledChanged([[maybe_unused]] bool enable) override;
        void OnAlternateSkyboxEnabledChanged([[maybe_unused]] bool enable) override;
        void OnDisplayMapperOperationTypeChanged(AZ::Render::DisplayMapperOperationType operationType) override;

        OpenParticleViewportWidget* m_viewPort = nullptr;
        QAction* m_toggleGrid = {};
        QAction* m_toggleAlternateSkybox = {};
        AZStd::unordered_map<AZ::Render::DisplayMapperOperationType, QAction*> m_operationActions;
        QToolButton* m_skyBtn;
        QVBoxLayout* m_verticalLayout = nullptr;

        QSlider* m_timeSlider = nullptr;
        QLabel* m_timeLabel = nullptr;
        QAction* m_playAction = nullptr;
        QAction* m_loopAction = nullptr;
        QTimer* m_playerTimer = nullptr;
        float m_duration = 0.0f; // the preview's duration when the bar last updated, which scales the slider
        bool m_resumeAfterScrub = false;
    };
} // namespace OpenParticleSystemEditor
