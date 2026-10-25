/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/Memory/SystemAllocator.h>
#include <AzToolsFramework/UI/PropertyEditor/PropertyEditorAPI.h>
#include <OpenParticleSystem/ParticleOverrides.h>

#include <QWidget>

namespace OpenParticleSystemEditor
{
    class GradientColorDialog;
    class GradientWidget;

    //! Shows a list of colour keys as a gradient strip; clicking the strip edits the keys in the gradient editor.
    class PropertyColorKeyGradientCtrl
        : public QWidget
    {
        Q_OBJECT
    public:
        AZ_CLASS_ALLOCATOR(PropertyColorKeyGradientCtrl, AZ::SystemAllocator, 0);

        explicit PropertyColorKeyGradientCtrl(QWidget* parent = nullptr);

        void SetKeys(const AZStd::vector<OpenParticle::ParticleColorKey>& keys);
        const AZStd::vector<OpenParticle::ParticleColorKey>& GetKeys() const;

    signals:
        void valueChanged();

    private slots:
        void OpenGradientColorDialog();

    private:
        GradientColorDialog* m_gradientColorDialog = nullptr;
        GradientWidget* m_gradientWidget = nullptr;
        //! Sorted by time, as the gradient editor returns them.
        AZStd::vector<OpenParticle::ParticleColorKey> m_keys;
    };

    //! Handles the "ParticleColorKeyGradient" UI handler on an AZStd::vector<ParticleColorKey>. The list's own rows and
    //! add/remove buttons stay, so the strip is a second way to edit the same keys.
    class PropertyColorKeyGradientHandler
        : QObject
        , public AzToolsFramework::PropertyHandler<AZStd::vector<OpenParticle::ParticleColorKey>, PropertyColorKeyGradientCtrl>
    {
        Q_OBJECT
    public:
        AZ_CLASS_ALLOCATOR(PropertyColorKeyGradientHandler, AZ::SystemAllocator, 0);

        AZ::u32 GetHandlerName() const override
        {
            return AZ_CRC_CE("ParticleColorKeyGradient");
        }
        bool AutoDelete() const override
        {
            return false;
        }

        QWidget* CreateGUI(QWidget* parent) override;
        void ConsumeAttribute(
            PropertyColorKeyGradientCtrl* GUI, AZ::u32 attrib, AzToolsFramework::PropertyAttributeReader* attrValue, const char* debugName) override;
        void WriteGUIValuesIntoProperty(
            size_t index, PropertyColorKeyGradientCtrl* GUI, property_t& instance, AzToolsFramework::InstanceDataNode* node) override;
        bool ReadValuesIntoGUI(
            size_t index, PropertyColorKeyGradientCtrl* GUI, const property_t& instance, AzToolsFramework::InstanceDataNode* node) override;
    };

    void RegisterColorKeyGradientPropertyHandler();
    void UnregisterColorKeyGradientPropertyHandler();
} // namespace OpenParticleSystemEditor
