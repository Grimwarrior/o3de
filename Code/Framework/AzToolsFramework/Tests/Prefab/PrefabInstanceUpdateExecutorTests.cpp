/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Console/IConsole.h>
#include <AzToolsFramework/Entity/EditorEntityHelpers.h>
#include <AzToolsFramework/Prefab/PrefabFocusPublicInterface.h>
#include <Prefab/PrefabTestFixture.h>

namespace UnitTest
{
    // Covers ed_prefabInstanceUpdateBatchSize and ed_prefabDeferInstanceUpdatesInFocusMode.
    class PrefabInstanceUpdateExecutorTests : public PrefabTestFixture
    {
    protected:
        void SetUpEditorFixtureImpl() override
        {
            PrefabTestFixture::SetUpEditorFixtureImpl();

            m_prefabFocusPublicInterface = AZ::Interface<PrefabFocusPublicInterface>::Get();
            ASSERT_TRUE(m_prefabFocusPublicInterface);

            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->GetCvarValue("ed_prefabInstanceUpdateBatchSize", m_ed_prefabInstanceUpdateBatchSize);
                console->GetCvarValue("ed_prefabDeferInstanceUpdatesInFocusMode", m_ed_prefabDeferInstanceUpdatesInFocusMode);
            }

            // Level with three CarPrefab instances, each holding a Wheel entity.
            AZ::IO::Path engineRootPath;
            m_settingsRegistryInterface->Get(engineRootPath.Native(), AZ::SettingsRegistryMergeUtils::FilePathKey_EngineRootFolder);
            AZ::IO::Path carPrefabFilepath(engineRootPath);
            carPrefabFilepath.Append("CarPrefab");

            AZ::EntityId wheelEntityId = CreateEditorEntityUnderRoot(WheelEntityName);
            m_carContainerIds[0] = CreateEditorPrefab(carPrefabFilepath, { wheelEntityId });
            m_wheelEntityAlias = FindEntityAliasInInstance(m_carContainerIds[0], WheelEntityName);
            m_carContainerIds[1] = InstantiateEditorPrefab(carPrefabFilepath, GetRootContainerEntityId());
            m_carContainerIds[2] = InstantiateEditorPrefab(carPrefabFilepath, GetRootContainerEntityId());
        }

        void TearDownEditorFixtureImpl() override
        {
            if (AZ::Interface<AZ::IConsole>::Get())
            {
                SetBatchSize(m_ed_prefabInstanceUpdateBatchSize);
                SetDeferInFocusMode(m_ed_prefabDeferInstanceUpdatesInFocusMode);
            }

            PrefabTestFixture::TearDownEditorFixtureImpl();
        }

        void SetBatchSize(int batchSize)
        {
            AZ::Interface<AZ::IConsole>::Get()->PerformCommand(
                AZStd::string::format("ed_prefabInstanceUpdateBatchSize %d", batchSize).c_str());
        }

        void SetDeferInFocusMode(bool defer)
        {
            AZ::Interface<AZ::IConsole>::Get()->PerformCommand(
                AZStd::string::format("ed_prefabDeferInstanceUpdatesInFocusMode %s", defer ? "true" : "false").c_str());
        }

        Instance& GetCarInstance(int carIndex)
        {
            return m_instanceEntityMapperInterface->FindOwningInstance(m_carContainerIds[carIndex])->get();
        }

        AZ::EntityId GetWheelEntityId(int carIndex)
        {
            return GetCarInstance(carIndex).GetEntityId(m_wheelEntityAlias);
        }

        AZStd::string GetWheelName(int carIndex)
        {
            AZ::Entity* wheelEntity = AzToolsFramework::GetEntityById(GetWheelEntityId(carIndex));
            return wheelEntity ? wheelEntity->GetName() : AZStd::string();
        }

        // Wheel edits only reach the car template while a car is focused; at level focus they are per-instance overrides.
        void FocusOnCar(int carIndex)
        {
            m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_carContainerIds[carIndex]);
            PropagateAllTemplateChanges();
        }

        // Renames the wheel and records the change, without running propagation.
        void RenameWheelWithoutPropagation(int carIndex, const AZStd::string& newName)
        {
            AZ::EntityId wheelEntityId = GetWheelEntityId(carIndex);
            AZ::Entity* wheelEntity = AzToolsFramework::GetEntityById(wheelEntityId);
            ASSERT_TRUE(wheelEntity);
            wheelEntity->SetName(newName);
            m_prefabPublicInterface->GenerateUndoNodesForEntityChangeAndUpdateCache(wheelEntityId, m_undoStack->GetTop());
        }

        int CountWheelsNamed(const AZStd::string& name)
        {
            int count = 0;
            for (int carIndex = 0; carIndex < CarCount; ++carIndex)
            {
                count += (GetWheelName(carIndex) == name) ? 1 : 0;
            }
            return count;
        }

        static constexpr int CarCount = 3;
        inline static const char* WheelEntityName = "Wheel";
        inline static const char* RenamedWheelEntityName = "RenamedWheel";

        PrefabFocusPublicInterface* m_prefabFocusPublicInterface = nullptr;
        AZ::EntityId m_carContainerIds[CarCount];
        EntityAlias m_wheelEntityAlias;
        int m_ed_prefabInstanceUpdateBatchSize = 0;
        bool m_ed_prefabDeferInstanceUpdatesInFocusMode = false;
    };

    TEST_F(PrefabInstanceUpdateExecutorTests, BatchSizeZeroUpdatesAllInstancesInOneTick)
    {
        SetBatchSize(0);
        FocusOnCar(0);

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        PropagateAllTemplateChanges();

        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, BatchSizeSpreadsInstanceUpdatesOverTicks)
    {
        SetBatchSize(1);
        FocusOnCar(0);

        // The edited instance is up to date straight away, the two other instances are queued.
        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), 1);

        PropagateAllTemplateChanges();
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), 2);

        PropagateAllTemplateChanges();
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), 3);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, FlushIgnoresBatchSize)
    {
        SetBatchSize(1);
        FocusOnCar(0);

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        m_instanceUpdateExecutorInterface->UpdateTemplateInstancesInQueue(/*flush=*/true);

        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, FocusChangeFlushesOnNextTick)
    {
        SetBatchSize(1);
        FocusOnCar(0);

        // Two instances are queued; leaving prefab edit mode updates both on the next tick despite the batch size.
        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        m_prefabFocusPublicInterface->FocusOnOwningPrefab(GetRootContainerEntityId());
        PropagateAllTemplateChanges();

        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, DeferInFocusModeHoldsBackInstancesOutsideFocusUntilFocusChanges)
    {
        SetDeferInFocusMode(true);

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_carContainerIds[0]);
        PropagateAllTemplateChanges();

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        for (int tick = 0; tick < 3; ++tick)
        {
            PropagateAllTemplateChanges();
        }

        // Only the focused instance has the change, the others wait.
        EXPECT_EQ(GetWheelName(0), RenamedWheelEntityName);
        EXPECT_EQ(GetWheelName(1), WheelEntityName);
        EXPECT_EQ(GetWheelName(2), WheelEntityName);
        EXPECT_FALSE(m_instanceUpdateExecutorInterface->IsInstanceUpdateDeferred(GetCarInstance(0)));
        EXPECT_TRUE(m_instanceUpdateExecutorInterface->IsInstanceUpdateDeferred(GetCarInstance(1)));
        EXPECT_TRUE(m_instanceUpdateExecutorInterface->IsInstanceUpdateDeferred(GetCarInstance(2)));

        // Leaving prefab edit mode brings them up to date.
        m_prefabFocusPublicInterface->FocusOnOwningPrefab(GetRootContainerEntityId());
        PropagateAllTemplateChanges();

        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
        EXPECT_FALSE(m_instanceUpdateExecutorInterface->IsInstanceUpdateDeferred(GetCarInstance(1)));
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, DeferInFocusModeFlushUpdatesHeldBackInstances)
    {
        SetDeferInFocusMode(true);

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_carContainerIds[0]);
        PropagateAllTemplateChanges();

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        PropagateAllTemplateChanges();
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), 1);

        // Undo/redo and entering game mode flush like this.
        m_instanceUpdateExecutorInterface->UpdateTemplateInstancesInQueue(/*flush=*/true);
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, DeferInFocusModeDiscardsChangesToHeldBackInstances)
    {
        SetDeferInFocusMode(true);

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(m_carContainerIds[0]);
        PropagateAllTemplateChanges();

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        PropagateAllTemplateChanges();

        // A write to a stale instance would patch the template from its old state; it must not reach the template.
        RenameWheelWithoutPropagation(1, "StaleWrite");

        m_prefabFocusPublicInterface->FocusOnOwningPrefab(GetRootContainerEntityId());
        PropagateAllTemplateChanges();

        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }

    TEST_F(PrefabInstanceUpdateExecutorTests, DeferInFocusModeTurnedOffReleasesHeldBackInstances)
    {
        SetDeferInFocusMode(true);
        FocusOnCar(0);

        RenameWheelWithoutPropagation(0, RenamedWheelEntityName);
        PropagateAllTemplateChanges();
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), 1);

        // Still in prefab edit mode: the held-back instances update on the next tick once the setting is off.
        SetDeferInFocusMode(false);
        PropagateAllTemplateChanges();
        EXPECT_EQ(CountWheelsNamed(RenamedWheelEntityName), CarCount);
    }
} // namespace UnitTest
