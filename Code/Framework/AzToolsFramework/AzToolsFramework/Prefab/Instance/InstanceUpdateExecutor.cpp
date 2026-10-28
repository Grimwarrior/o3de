/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzToolsFramework/Prefab/Instance/InstanceUpdateExecutor.h>

#include <AzCore/Component/TickBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Memory/AllocatorManager.h>
#include <AzToolsFramework/Entity/EditorEntityContextBus.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/Entity/PrefabEditorEntityOwnershipInterface.h>
#include <AzToolsFramework/API/ToolsApplicationAPI.h>
#include <AzToolsFramework/Prefab/Instance/Instance.h>
#include <AzToolsFramework/Prefab/Instance/InstanceDomGeneratorInterface.h>
#include <AzToolsFramework/Prefab/Instance/TemplateInstanceMapperInterface.h>
#include <AzToolsFramework/Prefab/Instance/InstanceEntityMapperInterface.h>
#include <AzToolsFramework/Prefab/PrefabDomUtils.h>
#include <AzToolsFramework/Prefab/PrefabFocusInterface.h>
#include <AzToolsFramework/Prefab/PrefabInstanceUtils.h>
#include <AzToolsFramework/Prefab/PrefabPublicInterface.h>
#include <AzToolsFramework/Prefab/PrefabPublicNotificationBus.h>
#include <AzToolsFramework/Prefab/PrefabSystemComponentInterface.h>
#include <AzToolsFramework/Prefab/Template/Template.h>
#include <AzToolsFramework/Prefab/PrefabPublicRequestBus.h>

// Spreads prefab instance rebuilds over several ticks; flushes (undo/redo, game mode, focus changes) still rebuild all.
AZ_CVAR(
    int,
    ed_prefabInstanceUpdateBatchSize,
    0,
    nullptr,
    AZ::ConsoleFunctorFlags::DontReplicate | AZ::ConsoleFunctorFlags::DontDuplicate,
    "Number of prefab instances rebuilt per Editor tick after a prefab edit. 0 rebuilds all queued instances in one tick.");

// Holds back instances outside the focused prefab until the focus changes or a flush; edits to them are discarded meanwhile.
AZ_CVAR(
    bool,
    ed_prefabDeferInstanceUpdatesInFocusMode,
    false,
    nullptr,
    AZ::ConsoleFunctorFlags::DontReplicate | AZ::ConsoleFunctorFlags::DontDuplicate,
    "If set, other instances of a prefab being edited in prefab edit mode are only rebuilt when leaving prefab edit mode.");

namespace AzToolsFramework
{
    namespace Prefab
    {
        InstanceUpdateExecutor::InstanceUpdateExecutor(int instanceCountToUpdateInBatch)
            : m_instanceCountToUpdateInBatch(instanceCountToUpdateInBatch)
            , m_GameModeEventHandler(
                  [this](GameModeState state)
                  {
                      m_shouldPausePropagation = (state == GameModeState::Started);
                  })
        {
        }

        void InstanceUpdateExecutor::RegisterInstanceUpdateExecutorInterface()
        {
            AZ::Interface<InstanceUpdateExecutorInterface>::Register(this);
            ToolsApplicationNotificationBus::Handler::BusConnect();

            m_prefabSystemComponentInterface = AZ::Interface<PrefabSystemComponentInterface>::Get();
            AZ_Assert(m_prefabSystemComponentInterface != nullptr,
                "Prefab - InstanceUpdateExecutor::RegisterInstanceUpdateExecutorInterface - "
                "Prefab System Component Interface could not be found. "
                "Check that it is being correctly initialized.");

            m_templateInstanceMapperInterface = AZ::Interface<TemplateInstanceMapperInterface>::Get();
            AZ_Assert(m_templateInstanceMapperInterface != nullptr,
                "Prefab - InstanceUpdateExecutor::RegisterInstanceUpdateExecutorInterface - "
                "Template Instance Mapper Interface could not be found. "
                "Check that it is being correctly initialized.");

            m_instanceDomGeneratorInterface = AZ::Interface<InstanceDomGeneratorInterface>::Get();
            AZ_Assert(m_instanceDomGeneratorInterface != nullptr,
                "Prefab - InstanceUpdateExecutor::RegisterInstanceUpdateExecutorInterface - "
                "Instance Dom Generator Interface could not be found. "
                "Check that it is being correctly initialized.");

            m_instanceEntityMapperInterface = AZ::Interface<InstanceEntityMapperInterface>::Get();
            AZ_Assert(
                m_instanceEntityMapperInterface != nullptr,
                "Prefab - InstanceUpdateExecutor::RegisterInstanceUpdateExecutorInterface - "
                "Instance Entity Mapper Interface could not be found. "
                "Check that it is being correctly initialized.");

        }

        void InstanceUpdateExecutor::UnregisterInstanceUpdateExecutorInterface()
        {
            ToolsApplicationNotificationBus::Handler::BusDisconnect();

            m_GameModeEventHandler.Disconnect();

            m_instanceDomGeneratorInterface = nullptr;
            m_templateInstanceMapperInterface = nullptr;
            m_prefabSystemComponentInterface = nullptr;
            m_instanceEntityMapperInterface = nullptr;

            AZ::Interface<InstanceUpdateExecutorInterface>::Unregister(this);
        }

        void InstanceUpdateExecutor::AddInstanceToQueue(InstanceOptionalReference instance)
        {
            if (!instance.has_value())
            {
                AZ_Warning(
                    "Prefab", false, "InstanceUpdateExecutor::AddInstanceToQueue - "
                    "Caller tried to insert null instance into the queue.");

                return;
            }

            AddInstanceToQueue(&(instance->get()));
        }

        void InstanceUpdateExecutor::AddInstanceToQueue(Instance* instance)
        {
            // Skip the insertion into queue if the instance is already present in the set.
            if (m_uniqueInstancesForPropagation.emplace(instance).second)
            {
                m_instancesUpdateQueue.emplace_back(instance);
            }
        }

        void InstanceUpdateExecutor::AddTemplateInstancesToQueue(TemplateId instanceTemplateId, InstanceOptionalConstReference instanceToExclude)
        {
            auto findInstancesResult =
                m_templateInstanceMapperInterface->FindInstancesOwnedByTemplate(instanceTemplateId);
            if (!findInstancesResult.has_value())
            {
                AZ_Warning("Prefab", false,
                    "InstanceUpdateExecutor::AddTemplateInstancesToQueue - "
                    "Could not find Template with Id '%llu' in Template Instance Mapper.",
                    instanceTemplateId);

                return;
            }

            const Instance* instanceToExcludePtr = nullptr;
            if (instanceToExclude.has_value())
            {
                instanceToExcludePtr = &(instanceToExclude->get());
            }

            for (auto instance : findInstancesResult->get())
            {
                if (instance != instanceToExcludePtr)
                {
                    AddInstanceToQueue(instance);
                }
            }
        }

        void InstanceUpdateExecutor::RemoveTemplateInstanceFromQueue(Instance* instance)
        {
            // Skip the removal from the queue if the instance is not present in the set.
            if (m_uniqueInstancesForPropagation.erase(instance))
            {
                auto isInstance = [instance](Instance* entry)
                {
                    return entry == instance;
                };
                AZStd::erase_if(m_instancesUpdateQueue, isInstance);
                AZStd::erase_if(m_deferredInstancesUpdateQueue, isInstance);
            }
        }

        int InstanceUpdateExecutor::GetInstanceCountToUpdateInBatch() const
        {
            if (m_instanceCountToUpdateInBatch > 0)
            {
                return m_instanceCountToUpdateInBatch;
            }
            return AZStd::max(static_cast<int>(ed_prefabInstanceUpdateBatchSize), 0);
        }

        const Instance* InstanceUpdateExecutor::GetFocusedInstanceToDeferAround() const
        {
            if (!ed_prefabDeferInstanceUpdatesInFocusMode)
            {
                return nullptr;
            }

            auto* prefabFocusInterface = AZ::Interface<PrefabFocusInterface>::Get();
            if (!prefabFocusInterface)
            {
                return nullptr;
            }

            AzFramework::EntityContextId editorEntityContextId = AzFramework::EntityContextId::CreateNull();
            EditorEntityContextRequestBus::BroadcastResult(editorEntityContextId, &EditorEntityContextRequests::GetEditorEntityContextId);

            InstanceOptionalReference focusedInstance = prefabFocusInterface->GetFocusedPrefabInstance(editorEntityContextId);
            // When the level itself is focused every instance is inside the focus, so there is nothing to defer.
            if (!focusedInstance.has_value() || !focusedInstance->get().HasParentInstance())
            {
                return nullptr;
            }
            return &focusedInstance->get();
        }

        bool InstanceUpdateExecutor::IsOutsideFocus(const Instance& instance, const Instance& focusedInstance)
        {
            return !PrefabInstanceUtils::IsDescendantInstance(instance, focusedInstance) &&
                !PrefabInstanceUtils::IsDescendantInstance(focusedInstance, instance);
        }

        void InstanceUpdateExecutor::RequeueDeferredInstances()
        {
            while (!m_deferredInstancesUpdateQueue.empty())
            {
                m_instancesUpdateQueue.push_front(m_deferredInstancesUpdateQueue.back());
                m_deferredInstancesUpdateQueue.pop_back();
            }
        }

        bool InstanceUpdateExecutor::IsInstanceUpdateDeferred(const Instance& instance) const
        {
            const Instance* focusedInstance = GetFocusedInstanceToDeferAround();
            if (!focusedInstance || m_uniqueInstancesForPropagation.empty())
            {
                return false;
            }

            // A queued ancestor outside the focus also leaves this instance stale.
            for (const Instance* current = &instance; current;
                 current = current->HasParentInstance() ? &current->GetParentInstance()->get() : nullptr)
            {
                if (m_uniqueInstancesForPropagation.find(const_cast<Instance*>(current)) != m_uniqueInstancesForPropagation.end() &&
                    IsOutsideFocus(*current, *focusedInstance))
                {
                    return true;
                }
            }
            return false;
        }

        void InstanceUpdateExecutor::LazyConnectGameModeEventHandler()
        {
            PrefabEditorEntityOwnershipInterface* prefabEditorEntityOwnershipInterface =
                AZ::Interface<PrefabEditorEntityOwnershipInterface>::Get();
            
            if (!m_GameModeEventHandler.IsConnected() && prefabEditorEntityOwnershipInterface)
            {
                prefabEditorEntityOwnershipInterface->RegisterGameModeEventHandler(m_GameModeEventHandler);
            }
        }

        bool InstanceUpdateExecutor::UpdateTemplateInstancesInQueue(bool flush)
        {
            AZ_PROFILE_FUNCTION(AzToolsFramework);

            LazyConnectGameModeEventHandler();

            bool isUpdateSuccessful = true;
            bool updatedInstances = false;
            if (!m_updatingTemplateInstancesInQueue && !m_shouldPausePropagation)
            {
                m_updatingTemplateInstancesInQueue = true;

                flush = flush || m_flushOnNextUpdate;
                m_flushOnNextUpdate = false;

                // Held-back instances were queued first, so they go back to the front.
                const bool deferOutsideFocus = !flush && GetFocusedInstanceToDeferAround() != nullptr;
                if (!deferOutsideFocus)
                {
                    RequeueDeferredInstances();
                }

                const int batchSize = GetInstanceCountToUpdateInBatch();
                const bool shouldLimitQueueSize = !flush && batchSize > 0;

                const int instanceCountToUpdateInBatch = shouldLimitQueueSize ?
                     AZStd::min(aznumeric_cast<int>(m_instancesUpdateQueue.size()), batchSize)
                    : aznumeric_cast<int>(m_instancesUpdateQueue.size());

                TemplateId currentTemplateId = InvalidTemplateId;
                TemplateReference currentTemplateReference = AZStd::nullopt;

                if (instanceCountToUpdateInBatch > 0)
                {
                    // Notify Propagation has begun
                    PrefabPublicNotificationBus::Broadcast(&PrefabPublicNotifications::OnPrefabInstancePropagationBegin);

                    EntityIdList selectedEntityIds;
                    ToolsApplicationRequestBus::BroadcastResult(selectedEntityIds, &ToolsApplicationRequests::GetSelectedEntities);

                    // Turn the list of EntityIDs into actual entity alias paths so that we can check if any of the
                    // selected entities were replaced (their EntityId would change but their alias would remain the same)

                    // map of "alias" -> "old entity id" for all selected entities
                    AZStd::unordered_map<AliasPath, AZ::EntityId> selectedEntityAliasPaths; 

                    selectedEntityAliasPaths.reserve(selectedEntityIds.size());
                    for (const auto& selectedEntityId : selectedEntityIds)
                    {
                        auto owningInstance = m_instanceEntityMapperInterface->FindOwningInstance(selectedEntityId);
                        if (owningInstance.has_value())
                        {
                            auto entityAlias = owningInstance->get().GetEntityAlias(selectedEntityId);
                            if (entityAlias.has_value())
                            {
                                AliasPath absoluteEntityPath = owningInstance->get().GetAbsoluteInstanceAliasPath();
                                absoluteEntityPath.Append(entityAlias.value());
                                selectedEntityAliasPaths[absoluteEntityPath] = selectedEntityId;
                            }
                        }
                    }

                    // Process all instances in the queue, capped to the batch size.
                    // Even though we potentially initialized the batch size to the queue, it's possible for the queue size to shrink
                    // during instance processing if the instance gets deleted and it was queued multiple times.  To handle this, we
                    // make sure to end the loop once the queue is empty, regardless of what the initial size was.
                    // Held-back instances do not count towards the batch.
                    for (int i = 0; (i < instanceCountToUpdateInBatch) && !m_instancesUpdateQueue.empty();)
                    {
                        Instance* instanceToUpdate = m_instancesUpdateQueue.front();
                        m_instancesUpdateQueue.pop_front();
                        AZ_Assert(instanceToUpdate != nullptr, "Invalid instance on update queue.");

                        // Looked up each time: rebuilding an ancestor may replace the focused instance.
                        const Instance* focusedInstance = deferOutsideFocus ? GetFocusedInstanceToDeferAround() : nullptr;
                        if (focusedInstance && IsOutsideFocus(*instanceToUpdate, *focusedInstance))
                        {
                            // Stays in m_uniqueInstancesForPropagation so it is neither queued twice nor edited while stale.
                            m_deferredInstancesUpdateQueue.push_back(instanceToUpdate);
                            continue;
                        }

                        ++i;
                        updatedInstances = true;
                        m_uniqueInstancesForPropagation.erase(instanceToUpdate);

                        TemplateId instanceTemplateId = instanceToUpdate->GetTemplateId();
                        if (currentTemplateId != instanceTemplateId)
                        {
                            currentTemplateId = instanceTemplateId;
                            currentTemplateReference = m_prefabSystemComponentInterface->FindTemplate(currentTemplateId);
                            if (!currentTemplateReference.has_value())
                            {
                                AZ_Error(
                                    "Prefab", false,
                                    "InstanceUpdateExecutor::UpdateTemplateInstancesInQueue - "
                                    "Could not find Template using Id '%llu'. Unable to update Instance.",
                                    currentTemplateId);

                                // Remove the instance from update queue if its corresponding template couldn't be found
                                isUpdateSuccessful = false;
                                continue;
                            }
                        }

                        auto findInstancesResult = m_templateInstanceMapperInterface->FindInstancesOwnedByTemplate(instanceTemplateId);
                        AZ_Assert(
                            findInstancesResult.has_value(), "Prefab Instances corresponding to template with id %llu couldn't be found.",
                            instanceTemplateId);

                        if (findInstancesResult == AZStd::nullopt ||
                            findInstancesResult->get().find(instanceToUpdate) == findInstancesResult->get().end())
                        {
                            // Since nested instances get reconstructed during propagation, remove any nested instance that no longer
                            // maps to a template.
                            isUpdateSuccessful = false;
                            continue;
                        }

                        // Gets a copy of instance DOM from focused or root prefab template.
                        PrefabDom instanceDom;
                        m_instanceDomGeneratorInterface->GetInstanceDomFromTemplate(instanceDom, *instanceToUpdate);

                        if (!instanceDom.IsObject())
                        {
                            AZ_Assert(
                                false,
                                "InstanceUpdateExecutor::UpdateTemplateInstancesInQueue - "
                                "Could not load Instance DOM from the given Instance.");

                            isUpdateSuccessful = false;
                            continue;
                        }

                        // Loads instance object from the generated instance DOM.
                        EntityList newEntities;
                        if (PrefabDomUtils::LoadInstanceFromPrefabDom(*instanceToUpdate, newEntities, instanceDom,
                            PrefabDomUtils::LoadFlags::UseSelectiveDeserialization))
                        {
                            Template& currentTemplate = currentTemplateReference->get();
                            instanceToUpdate->GetNestedInstances([&](AZStd::unique_ptr<Instance>& nestedInstance) 
                            {
                                if (!nestedInstance || nestedInstance->GetLinkId() != InvalidLinkId)
                                {
                                    return;
                                }

                                for (auto linkId : currentTemplate.GetLinks())
                                {
                                    LinkReference nestedLink = m_prefabSystemComponentInterface->FindLink(linkId);
                                    if (!nestedLink.has_value())
                                    {
                                        continue;
                                    }

                                    if (nestedLink->get().GetInstanceName() == nestedInstance->GetInstanceAlias())
                                    {
                                        nestedInstance->SetLinkId(linkId);
                                        break;
                                    }
                                }
                            });

                            AzToolsFramework::EditorEntityContextRequestBus::Broadcast(
                                &AzToolsFramework::EditorEntityContextRequests::HandleEntitiesAdded, newEntities);

                            if (!selectedEntityAliasPaths.empty())
                            {
                                // was anything in the instance that updated selected?
                                AliasPath instancePath = instanceToUpdate->GetAbsoluteInstanceAliasPath();

                                // Loop over all new entities and check if their alias path matches
                                // any of the selected entity alias paths.  If it does, update the entityId
                                // in the seelcted entity list to the new entityId:
                                auto updateSelectionCallback = [&](const AZ::Entity& entity)
                                {
                                    AliasPath entityPath = instancePath;
                                    auto alias = instanceToUpdate->GetEntityAlias(entity.GetId());
                                    if (alias.has_value())
                                    {
                                        // The alias always has a value, because we just created one:
                                        entityPath.Append(alias.value());

                                        if (auto found = selectedEntityAliasPaths.find(entityPath); found != selectedEntityAliasPaths.end())
                                        {
                                            const AZ::EntityId& oldEntityId = found->second;
                                            AZStd::replace(selectedEntityIds.begin(), selectedEntityIds.end(), oldEntityId, entity.GetId());
                                            found->second = entity.GetId();
                                        }
                                    }
                                    return true; // returning true in this callback means keep iterating.
                                };

                                instanceToUpdate->GetConstEntities(updateSelectionCallback);
                            }

                            if (!m_isRootPrefabInstanceLoaded &&
                                instanceToUpdate->GetTemplateSourcePath() == m_rootPrefabInstanceSourcePath)
                            {
                                PrefabPublicNotificationBus::Broadcast(&PrefabPublicNotifications::OnRootPrefabInstanceLoaded);
                                m_isRootPrefabInstanceLoaded = true;
                            }
                        }
                    }

                    // Notify Propagation has ended, then update selection (which is frozen during propagation, so this order matters)
                    PrefabPublicNotificationBus::Broadcast(&PrefabPublicNotifications::OnPrefabInstancePropagationEnd);

                    // eliminate any dead entity Ids and then reselect the entities that were replaced during propagation.
                    AZStd::erase_if(selectedEntityIds, [](const AZ::EntityId& entityId) { return !GetEntityById(entityId); });
                    ToolsApplicationRequestBus::Broadcast(&ToolsApplicationRequests::SetSelectedEntities, selectedEntityIds);
                }

                m_updatingTemplateInstancesInQueue = false;
            }
            
            // this logic avoids calling garbage collect on every frame when no instances have been updated or when 
            // we are still processing the update queue (if in a mode where the processing queue is emptied slowly over time).
            if ( (updatedInstances) && (m_instancesUpdateQueue.empty()) )
            {
                AZ::AllocatorManager::Instance().GarbageCollect();
                AZ_MALLOC_TRIM(0);
            }

            return isUpdateSuccessful;
        }

        bool InstanceUpdateExecutor::IsUpdatingTemplateInstancesInQueue() const
        {
            return m_updatingTemplateInstancesInQueue;
        }

        void InstanceUpdateExecutor::FlushOnNextUpdate()
        {
            m_flushOnNextUpdate = true;
        }

        void InstanceUpdateExecutor::QueueRootPrefabLoadedNotificationForNextPropagation()
        {
            m_isRootPrefabInstanceLoaded = false;

            PrefabEditorEntityOwnershipInterface* prefabEditorEntityOwnershipInterface =
                AZ::Interface<PrefabEditorEntityOwnershipInterface>::Get();
            m_rootPrefabInstanceSourcePath =
                prefabEditorEntityOwnershipInterface->GetRootPrefabInstance()->get().GetTemplateSourcePath();
        }

        void InstanceUpdateExecutor::SetShouldPauseInstancePropagation(bool shouldPausePropagation)
        {
            m_shouldPausePropagation = shouldPausePropagation;
        }

        void InstanceUpdateExecutor::AfterUndoRedo()
        {
            // After undo/redo, the instance update queue may have been populated.  Make sure that the queue is processed,
            // so that deletions/creations are complete before control returns to the application.
            // Note that this signal only comes from when a user actually initiates an undo or redo (or a test script does
            // on behalf of simulating a user), and not from when undo or redo operations are being performed as part of
            // building the undo stack during an op.  If this ever causes a perf issue, we can relax this requirement.
            if (!m_shouldPausePropagation)
            {
                UpdateTemplateInstancesInQueue(true);
            }
        }
    }
}
