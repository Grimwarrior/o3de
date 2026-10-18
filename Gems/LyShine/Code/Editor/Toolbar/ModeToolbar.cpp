/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "ModeToolbar.h"

#include "AlignToolbarSection.h"
#include "Helpers/ViewportHelpers.h"
#include "Widgets/ViewportWidget/Interaction/ViewportInteraction.h"
#include "Widgets/ViewportWidget/ViewportWidget.h"
#include "Windows/EditorWindow/EditorWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QIcon>
#include <QString>
#include <QToolBar>

ModeToolbar::ModeToolbar(EditorWindow* parent)
    : QToolBar(tr("Mode Toolbar"), parent)
    , m_group(nullptr)
    , m_previousAction(nullptr)
    , m_scalePivotCombobox(new QComboBox(this))
    , m_scalePivotAction(nullptr)
    , m_alignToolbarSection(new AlignToolbarSection)
{
    setObjectName("ModeToolbar"); // needed to save state
    setFloatable(false);

    AddModes(parent);

    m_alignToolbarSection->AddButtons(this);

    // Scale mode: what the selected elements scale around
    {
        m_scalePivotCombobox->setToolTip(tr("What the selected elements scale around"));
        m_scalePivotCombobox->setMinimumContentsLength(14);
        m_scalePivotCombobox->addItem(tr("Own Pivot"), static_cast<int>(ViewportInteraction::ScalePivotMode::OWN));
        m_scalePivotCombobox->setItemData(0, tr("Every selected element scales around its own pivot"), Qt::ToolTipRole);
        m_scalePivotCombobox->addItem(tr("Selection Pivot"), static_cast<int>(ViewportInteraction::ScalePivotMode::SELECTION));
        m_scalePivotCombobox->setItemData(
            1, tr("The selected elements scale together around the pivot of the first one, their children follow"), Qt::ToolTipRole);
        m_scalePivotCombobox->addItem(tr("Parent Pivot"), static_cast<int>(ViewportInteraction::ScalePivotMode::PARENT));
        m_scalePivotCombobox->setItemData(2, tr("Every selected element scales around the pivot of its parent"), Qt::ToolTipRole);

        QObject::connect(
            m_scalePivotCombobox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [parent](int index)
            {
                parent->GetViewport()->GetViewportInteraction()->SetScalePivotMode(static_cast<ViewportInteraction::ScalePivotMode>(index));
            });

        m_scalePivotAction = addWidget(m_scalePivotCombobox);
        m_scalePivotAction->setVisible(false);
    }

    parent->addToolBar(this);
}

void ModeToolbar::SetScalePivotIsVisible(bool visible)
{
    m_scalePivotAction->setVisible(visible);
}

void ModeToolbar::SetScalePivotIndex(int index)
{
    m_scalePivotCombobox->setCurrentIndex(index);
}

void ModeToolbar::SetCheckedItem(int index)
{
    if (m_group)
    {
        for (auto action : m_group->actions())
        {
            if (action->data().toInt() == index)
            {
                m_previousAction->setChecked(false);
                m_previousAction = action;
                m_previousAction->setChecked(true);
                return;
            }
        }
    }
}

void ModeToolbar::AddModes(EditorWindow* parent)
{
    m_group = new QActionGroup(this);

    int i = 0;
    for (const auto m : ViewportInteraction::InteractionMode())
    {
        int key = (Qt::Key_1 + i++);

        int mode = static_cast<int>(m);

        QString nodeName = ViewportHelpers::InteractionModeToString(mode);

        QString iconImageDefault = QString(":/Icons/Mode%1Default.png").arg(nodeName);

        QIcon icon(iconImageDefault);

        QAction* action = new QAction(
            icon, (QString("%1 (%2)").arg(tr(ViewportHelpers::InteractionModeToString(mode)), QString(static_cast<char>(key)))), this);

        action->setData(mode);
        action->setShortcut(QKeySequence(key));
        action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        action->setCheckable(true); // Give it the behavior of a toggle button.
        QObject::connect(
            action,
            &QAction::triggered,
            this,
            [this, parent, action]([[maybe_unused]] bool checked)
            {
                if (m_previousAction == action)
                {
                    // Nothing to do.
                    return;
                }

                parent->GetViewport()->GetViewportInteraction()->SetMode((ViewportInteraction::InteractionMode)action->data().toInt());

                m_previousAction = action;
            });
        m_group->addAction(action);
    }

    // Give it the behavior of radio buttons.
    m_group->setExclusive(true);

    // Set the first action as the default.
    m_previousAction = m_group->actions().constFirst();
    m_previousAction->setChecked(true);

    addActions(m_group->actions());
}

