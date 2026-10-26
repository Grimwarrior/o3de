/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Memory/SystemAllocator.h>

#include <QWidget>
#include <QMenuBar>
#include <QMainWindow>
#include <QLabel>
#include <QPointer>

#include <Editor/EditorParticleSystemComponentRequestBus.h>
#include <Document/ParticleDocument.h>
#include <OpenParticleSystemEditorWindowRequests.h>
#include <AzQtComponents/Components/StyledDockWidget.h>
#include <AzQtComponents/Components/FancyDocking.h>
#include <AzQtComponents/Components/DockMainWindow.h>
#include <AzToolsFramework/ActionManager/ActionManagerRegistrationNotificationBus.h>

namespace OpenParticleSystemEditor
{
    class EffectorInspector;
    class OpenParticleSystemEditorWindow
        : public AzQtComponents::DockMainWindow
        , public EditorWindowRequestsBus::Handler
        , public OpenParticle::EditorParticleOpenParticleRequestsBus::Handler
        , private AzToolsFramework::ActionManagerRegistrationNotificationBus::Handler
    {
        Q_OBJECT
    public:
        AZ_CLASS_ALLOCATOR(OpenParticleSystemEditorWindow, AZ::SystemAllocator);

        explicit OpenParticleSystemEditorWindow(QWidget* parent = nullptr);
        ~OpenParticleSystemEditorWindow() override;

    private:
        void SetupMenu();

        //! Adds the File menu and joins the action context its hotkeys use; false while the Action Manager has not registered them.
        bool AttachActionManagerItems();

        // AzToolsFramework::ActionManagerRegistrationNotificationBus
        void OnPostActionManagerRegistrationHook() override;
        void SetupDocking();
        void SetupCentral();
        void SetDockWidget(const AZStd::string& name, QWidget* widget);
        void SetTabWidget(const AZStd::string& path);

        // ParticleEditorWindowRequestsBus
        void ActivateWindow() override;
        void OpenDocument(const AZStd::string& path) override;
        void CreateParticleFile(const AZStd::string& path) override;
        void NewDocument() override;
        void OpenDocumentFromBrowser() override;
        void SaveDocument() override;
        void SaveDocumentAs() override;
        void SaveAllDocuments() override;

        // EditorParticleOpenParticleRequestsBus
        void OpenParticleFile(const AZStd::string& sourcePath) override;
        bool SaveDialog();

        void closeEvent(QCloseEvent* event) override;
        bool RaiseOpendEmitter(const QString& path);
        void SetStatusMessage(const QString& message);
        void SetDockWidgetVisible(const AZStd::string& name, bool visible);
        void SetEmitterDockWidget(const AZStd::string& name, QWidget* widget, AZ::u32 area);
        bool AddDockWidget(const AZStd::string& name, QWidget* widget, AZ::u32 area);
        void Checked(const QString& name);
        void UnChecked(const QString& name);
        void RemoveDockWidget(const AZStd::string& name);
        AZStd::vector<AZStd::string> GetDockWidgetNames() const;

        QMenuBar* m_menuBar = nullptr;

        bool m_opened = false;
        AZStd::unique_ptr<ParticleDocument> m_document;
        AZStd::unordered_map<AZStd::string, AZStd::unique_ptr<ParticleDocument>> m_tabWidgetDocument;

        QLabel* m_statusMessage = {};
        QMenu* m_menuFile = {};
        QMenu* m_menuView = {};

        AZStd::unordered_map<AZStd::string, QDockWidget*> m_dockWidgets;
        AZStd::unordered_map<AZStd::string, QAction*> m_dockActions;

        AzQtComponents::FancyDocking* m_fancyDockingManager = nullptr;
        QPointer<AzQtComponents::DockTabWidget> m_emitterTabWidget = nullptr;
        EffectorInspector* m_effectorInspector = nullptr;
        AZStd::string m_currentTabName;
    };
} // namespace OpenParticleSystemEditor
