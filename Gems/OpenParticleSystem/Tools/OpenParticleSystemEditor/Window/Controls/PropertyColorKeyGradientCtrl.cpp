/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Window/Controls/PropertyColorKeyGradientCtrl.h>

#include <AzCore/std/sort.h>
#include <AzQtComponents/Utilities/Conversions.h>
#include <Window/Controls/GradientColorDialog.h>
#include <Window/Controls/GradientWidget.h>

#include <QHBoxLayout>
#include <QPainter>

namespace OpenParticleSystemEditor
{
    namespace
    {
        PropertyColorKeyGradientHandler* g_colorKeyGradientHandler = nullptr;

        // The gradient editor works in 8-bit colour, so keys are clamped for display.
        QColor ToDisplayColor(const AZ::Color& color)
        {
            return QColor::fromRgbF(
                AZ::GetClamp(color.GetR(), 0.0f, 1.0f), AZ::GetClamp(color.GetG(), 0.0f, 1.0f), AZ::GetClamp(color.GetB(), 0.0f, 1.0f),
                AZ::GetClamp(color.GetA(), 0.0f, 1.0f));
        }

        QGradientStops ToStops(const AZStd::vector<OpenParticle::ParticleColorKey>& keys)
        {
            QGradientStops stops;
            for (const auto& key : keys)
            {
                stops.push_back(QGradientStop(AZ::GetClamp(key.m_time, 0.0f, 1.0f), ToDisplayColor(key.m_color)));
            }
            return stops;
        }
    } // namespace

    PropertyColorKeyGradientCtrl::PropertyColorKeyGradientCtrl(QWidget* parent)
        : QWidget(parent)
    {
        static const int minColorEditWidth = 60;
        static const int defaultHeight = 18;

        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setAlignment(Qt::AlignLeft);
        layout->setContentsMargins(2, 0, 2, 0);
        setLayout(layout);

        m_gradientWidget = new GradientWidget(this);
        m_gradientWidget->AddGradient(new QLinearGradient(0, 0, 1, 0), QPainter::CompositionMode::CompositionMode_Source);
        m_gradientWidget->setMinimumWidth(minColorEditWidth);
        // The strip has no size hint, and the Inspector's rows size widgets by their hint, so without a fixed height it gets none.
        m_gradientWidget->setFixedHeight(defaultHeight);
        m_gradientWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_gradientWidget->setToolTip(tr("Click to edit the keys in the gradient editor"));
        layout->addWidget(m_gradientWidget);

        connect(m_gradientWidget, SIGNAL(Clicked()), this, SLOT(OpenGradientColorDialog()));
    }

    void PropertyColorKeyGradientCtrl::SetKeys(const AZStd::vector<OpenParticle::ParticleColorKey>& keys)
    {
        m_keys = keys;
        AZStd::stable_sort(
            m_keys.begin(), m_keys.end(),
            [](const OpenParticle::ParticleColorKey& left, const OpenParticle::ParticleColorKey& right) { return left.m_time < right.m_time; });
        m_gradientWidget->SetKeys(0, ToStops(m_keys));
    }

    const AZStd::vector<OpenParticle::ParticleColorKey>& PropertyColorKeyGradientCtrl::GetKeys() const
    {
        return m_keys;
    }

    void PropertyColorKeyGradientCtrl::OpenGradientColorDialog()
    {
        if (m_gradientColorDialog == nullptr)
        {
            m_gradientColorDialog = new GradientColorDialog(this);
        }
        m_gradientColorDialog->SetGradient(ToStops(m_keys));
        m_gradientColorDialog->AddKeyEnabled(true);
        if (m_gradientColorDialog->exec() != QDialog::Accepted)
        {
            return;
        }

        // The editor only knows time and 8-bit colour. A stop keeps the blend of the key it came from, and that key's own colour
        // when the stop's colour is unchanged, so colours above 1 survive. With the same number of stops the keys are matched in
        // order, which follows a dragged key; otherwise by time.
        const QGradientStops stops = m_gradientColorDialog->GetGradient();
        const bool sameCount = static_cast<size_t>(stops.size()) == m_keys.size();
        AZStd::vector<OpenParticle::ParticleColorKey> edited;
        for (int index = 0; index < stops.size(); ++index)
        {
            const QGradientStop& stop = stops[index];
            OpenParticle::ParticleColorKey key;
            key.m_time = static_cast<float>(stop.first);
            key.m_color = AzQtComponents::fromQColor(stop.second);

            const OpenParticle::ParticleColorKey* source = nullptr;
            if (sameCount)
            {
                source = &m_keys[static_cast<size_t>(index)];
            }
            else
            {
                for (const auto& previous : m_keys)
                {
                    if (AZ::GetAbs(previous.m_time - key.m_time) < 0.001f)
                    {
                        source = &previous;
                        break;
                    }
                }
            }
            if (source != nullptr)
            {
                key.m_blend = source->m_blend;
                if (ToDisplayColor(source->m_color).rgba() == stop.second.rgba())
                {
                    key.m_color = source->m_color;
                }
            }
            edited.push_back(key);
        }

        SetKeys(edited);
        emit valueChanged();
    }

    QWidget* PropertyColorKeyGradientHandler::CreateGUI(QWidget* parent)
    {
        PropertyColorKeyGradientCtrl* newCtrl = aznew PropertyColorKeyGradientCtrl(parent);
        connect(newCtrl, &PropertyColorKeyGradientCtrl::valueChanged, this, [newCtrl]()
            {
                using namespace AzToolsFramework;
                PropertyEditorGUIMessages::Bus::Broadcast(&PropertyEditorGUIMessages::RequestWrite, newCtrl);
                PropertyEditorGUIMessages::Bus::Broadcast(&PropertyEditorGUIMessages::OnEditingFinished, newCtrl);
                // The number of keys may have changed, so the key rows under the strip are rebuilt.
                PropertyEditorGUIMessages::Bus::Broadcast(
                    &PropertyEditorGUIMessages::RequestRefresh, PropertyModificationRefreshLevel::Refresh_EntireTree);
            });
        return newCtrl;
    }

    void PropertyColorKeyGradientHandler::ConsumeAttribute(
        [[maybe_unused]] PropertyColorKeyGradientCtrl* GUI,
        [[maybe_unused]] AZ::u32 attrib,
        [[maybe_unused]] AzToolsFramework::PropertyAttributeReader* attrValue,
        [[maybe_unused]] const char* debugName)
    {
    }

    void PropertyColorKeyGradientHandler::WriteGUIValuesIntoProperty(
        [[maybe_unused]] size_t index,
        PropertyColorKeyGradientCtrl* GUI,
        property_t& instance,
        [[maybe_unused]] AzToolsFramework::InstanceDataNode* node)
    {
        instance = GUI->GetKeys();
    }

    bool PropertyColorKeyGradientHandler::ReadValuesIntoGUI(
        [[maybe_unused]] size_t index,
        PropertyColorKeyGradientCtrl* GUI,
        const property_t& instance,
        [[maybe_unused]] AzToolsFramework::InstanceDataNode* node)
    {
        GUI->SetKeys(instance);
        return false;
    }

    void RegisterColorKeyGradientPropertyHandler()
    {
        if (g_colorKeyGradientHandler == nullptr)
        {
            g_colorKeyGradientHandler = aznew PropertyColorKeyGradientHandler();
            AzToolsFramework::PropertyTypeRegistrationMessages::Bus::Broadcast(
                &AzToolsFramework::PropertyTypeRegistrationMessages::Bus::Handler::RegisterPropertyType, g_colorKeyGradientHandler);
        }
    }

    void UnregisterColorKeyGradientPropertyHandler()
    {
        if (g_colorKeyGradientHandler != nullptr)
        {
            AzToolsFramework::PropertyTypeRegistrationMessages::Bus::Broadcast(
                &AzToolsFramework::PropertyTypeRegistrationMessages::Bus::Handler::UnregisterPropertyType, g_colorKeyGradientHandler);
            delete g_colorKeyGradientHandler;
            g_colorKeyGradientHandler = nullptr;
        }
    }
} // namespace OpenParticleSystemEditor
