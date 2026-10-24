/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Serialization/SerializeContext.h>
#include <OpenParticleSystemEditorSystemComponent.h>
#include <AzCore/Name/Name.h>

#include <Atom/RHI/Factory.h>
#include <AzCore/Utils/Utils.h>
#include <AzToolsFramework/API/ViewPaneOptions.h>
#include <AzToolsFramework/ActionManager/Action/ActionManagerInterface.h>
#include <AzToolsFramework/ActionManager/HotKey/HotKeyManagerInterface.h>
#include <AzToolsFramework/ActionManager/Menu/MenuManagerInterface.h>
#include <OpenParticleSystemEditor/Window/OpenParticleSystemEditorWindow.h>
#include <OpenParticleSystemEditor/Window/OpenParticleSystemEditorActionIdentifiers.h>
#include <LyViewPaneNames.h>

namespace OpenParticleSystem
{
    void OpenParticleSystemEditorSystemComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<OpenParticleSystemEditorSystemComponent, OpenParticleSystemSystemComponent>()
                ->Version(0);
        }
    }

    OpenParticleSystemEditorSystemComponent::OpenParticleSystemEditorSystemComponent() = default;

    OpenParticleSystemEditorSystemComponent::~OpenParticleSystemEditorSystemComponent() = default;

    void OpenParticleSystemEditorSystemComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        BaseSystemComponent::GetProvidedServices(provided);
        provided.push_back(AZ_CRC_CE("OpenParticleSystemEditorService"));
    }

    void OpenParticleSystemEditorSystemComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        BaseSystemComponent::GetIncompatibleServices(incompatible);
        incompatible.push_back(AZ_CRC_CE("OpenParticleSystemEditorService"));
    }

    void OpenParticleSystemEditorSystemComponent::GetRequiredServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        BaseSystemComponent::GetRequiredServices(required);
    }

    void OpenParticleSystemEditorSystemComponent::GetDependentServices([[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
        BaseSystemComponent::GetDependentServices(dependent);
    }

    void OpenParticleSystemEditorSystemComponent::Activate()
    {
        OpenParticleSystemSystemComponent::Activate();
        AzToolsFramework::EditorEvents::Bus::Handler::BusConnect();

        AzToolsFramework::EditorMenuNotificationBus::Handler::BusConnect();
        AzToolsFramework::ActionManagerRegistrationNotificationBus::Handler::BusConnect();
    }

    void OpenParticleSystemEditorSystemComponent::Deactivate()
    {
        AzToolsFramework::ActionManagerRegistrationNotificationBus::Handler::BusDisconnect();
        AzToolsFramework::EditorEvents::Bus::Handler::BusDisconnect();
        OpenParticleSystemSystemComponent::Deactivate();

        AzToolsFramework::EditorMenuNotificationBus::Handler::BusDisconnect();
    }

    void OpenParticleSystemEditorSystemComponent::OnResetToolMenuItems()
    {
    }

    void OpenParticleSystemEditorSystemComponent::NotifyRegisterViews()
    {
        QtViewOptions options;
        options.canHaveMultipleInstances = false;
        options.isPreview = true;
        options.showInMenu = true;
        options.showOnToolsToolbar = true;
        options.toolbarIcon = ":/OpenParticleSystem/Icons/Particle.svg";

        AzToolsFramework::RegisterViewPane<OpenParticleSystemEditor::OpenParticleSystemEditorWindow>(
            "(Preview) Particle Editor", LyViewPane::CategoryTools, options);
    }

    void OpenParticleSystemEditorSystemComponent::OnActionContextRegistrationHook()
    {
        if (auto actionManagerInterface = AZ::Interface<AzToolsFramework::ActionManagerInterface>::Get())
        {
            AzToolsFramework::ActionContextProperties contextProperties;
            contextProperties.m_name = "O3DE Particle Editor";
            actionManagerInterface->RegisterActionContext(
                OpenParticleSystemEditor::ActionIdentifiers::ParticleEditorActionContext, contextProperties);
        }
    }

    void OpenParticleSystemEditorSystemComponent::OnMenuRegistrationHook()
    {
        if (auto menuManagerInterface = AZ::Interface<AzToolsFramework::MenuManagerInterface>::Get())
        {
            AzToolsFramework::MenuProperties menuProperties;
            menuProperties.m_name = "&File";
            menuManagerInterface->RegisterMenu(OpenParticleSystemEditor::ActionIdentifiers::FileMenu, menuProperties);
        }
    }

    void OpenParticleSystemEditorSystemComponent::OnActionRegistrationHook()
    {
        auto actionManagerInterface = AZ::Interface<AzToolsFramework::ActionManagerInterface>::Get();
        auto hotKeyManagerInterface = AZ::Interface<AzToolsFramework::HotKeyManagerInterface>::Get();
        if (!actionManagerInterface || !hotKeyManagerInterface)
        {
            return;
        }

        using WindowBus = OpenParticleSystemEditor::EditorWindowRequestsBus;
        struct FileAction
        {
            AZStd::string_view m_identifier;
            const char* m_name;
            const char* m_description;
            const char* m_hotKey;
            void (OpenParticleSystemEditor::EditorWindowRequests::*m_request)();
        };
        // The window answers the requests while it is open; with the pane closed they do nothing
        const FileAction fileActions[] = {
            { OpenParticleSystemEditor::ActionIdentifiers::NewAction, "New...", "Create a new particle file", "Ctrl+N",
              &OpenParticleSystemEditor::EditorWindowRequests::NewDocument },
            { OpenParticleSystemEditor::ActionIdentifiers::OpenAction, "Open...", "Open a particle file", "Ctrl+O",
              &OpenParticleSystemEditor::EditorWindowRequests::OpenDocumentFromBrowser },
            { OpenParticleSystemEditor::ActionIdentifiers::SaveAction, "Save", "Save the current particle", "Ctrl+S",
              &OpenParticleSystemEditor::EditorWindowRequests::SaveDocument },
            { OpenParticleSystemEditor::ActionIdentifiers::SaveAsAction, "Save As...", "Save the current particle under a new name",
              "Ctrl+Shift+S", &OpenParticleSystemEditor::EditorWindowRequests::SaveDocumentAs },
            { OpenParticleSystemEditor::ActionIdentifiers::SaveAllAction, "Save All", "Save every open particle with changes", "",
              &OpenParticleSystemEditor::EditorWindowRequests::SaveAllDocuments },
        };

        for (const FileAction& fileAction : fileActions)
        {
            AzToolsFramework::ActionProperties actionProperties;
            actionProperties.m_name = fileAction.m_name;
            actionProperties.m_description = fileAction.m_description;
            actionProperties.m_category = "Particle Editor";
            actionProperties.m_menuVisibility = AzToolsFramework::ActionVisibility::AlwaysShow;

            const auto request = fileAction.m_request;
            [[maybe_unused]] auto outcome = actionManagerInterface->RegisterAction(
                OpenParticleSystemEditor::ActionIdentifiers::ParticleEditorActionContext,
                fileAction.m_identifier,
                actionProperties,
                [request]()
                {
                    WindowBus::Broadcast(request);
                });
            AZ_Error("OpenParticleSystem", outcome.IsSuccess(), "Failed to register action %.*s", AZ_STRING_ARG(fileAction.m_identifier));

            if (fileAction.m_hotKey[0] != '\0')
            {
                hotKeyManagerInterface->SetActionHotKey(fileAction.m_identifier, fileAction.m_hotKey);
            }
        }
    }

    void OpenParticleSystemEditorSystemComponent::OnMenuBindingHook()
    {
        auto menuManagerInterface = AZ::Interface<AzToolsFramework::MenuManagerInterface>::Get();
        if (!menuManagerInterface)
        {
            return;
        }

        const AZStd::string fileMenu(OpenParticleSystemEditor::ActionIdentifiers::FileMenu);
        menuManagerInterface->AddActionToMenu(fileMenu, OpenParticleSystemEditor::ActionIdentifiers::NewAction, 100);
        menuManagerInterface->AddActionToMenu(fileMenu, OpenParticleSystemEditor::ActionIdentifiers::OpenAction, 200);
        menuManagerInterface->AddSeparatorToMenu(fileMenu, 300);
        menuManagerInterface->AddActionToMenu(fileMenu, OpenParticleSystemEditor::ActionIdentifiers::SaveAction, 400);
        menuManagerInterface->AddActionToMenu(fileMenu, OpenParticleSystemEditor::ActionIdentifiers::SaveAsAction, 500);
        menuManagerInterface->AddActionToMenu(fileMenu, OpenParticleSystemEditor::ActionIdentifiers::SaveAllAction, 600);
    }

} // namespace OpenParticleSystem
