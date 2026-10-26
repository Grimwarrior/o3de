/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <ViewInspector.h>

#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QSlider>
#include <QDoubleValidator>
#include <AzCore/std/algorithm.h>
#include <OpenParticleSystem/ParticleRequestBus.h>
#include <QAbstractItemView>
#include <Viewport/OpenParticleViewportRequestBus.h>
#include <Viewport/OpenParticleViewportSettings.h>


namespace OpenParticleSystemEditor
{
    static const char* ICON_CIRCULATE = ":/Gallery/Rotate.svg";
    static const char* ICON_PALY = ":/stylesheet/img/UI20/toolbar/Play.svg";
    static const char* ICON_STOP = ":/stylesheet/img/UI20/toolbar/Deploy.svg";
    static const char* ICON_NEXT = ":/Breadcrumb/img/UI20/Breadcrumb/arrow_right-default_hover.svg";
    static const char* ICON_RESET = ":/stylesheet/img/logging/reset.svg";
    static constexpr float MaxPreviewSpeed = 10.0f;

    ViewInspector::ViewInspector(QWidget* parent)
        : QWidget(parent)
    {
        m_verticalLayout = new QVBoxLayout(this);
        m_verticalLayout->setContentsMargins(0, 0, 0, 0);
        m_verticalLayout->setSpacing(0);

        CreatTopBar();

        m_viewPort = new OpenParticleViewportWidget(this);
        m_verticalLayout->addWidget(m_viewPort);

#if defined(O3DE_REV_BUILD_RELEASE) && (O3DE_REV_BUILD_RELEASE)
        CreatePlayerProgressBar();
        CreatePlayerAction();
#endif

        setLayout(m_verticalLayout);

        OpenParticleViewportNotificationBus::Handler::BusConnect();
    }

    ViewInspector::~ViewInspector()
    {
        OpenParticleViewportNotificationBus::Handler::BusDisconnect();
    }

    void ViewInspector::CreatTopBar()
    {
        QHBoxLayout* horizontalTopLayout = new QHBoxLayout(this);
        QSpacerItem* spacerTopFront = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        AZStd::intrusive_ptr<OpenParticleViewportSettings> viewportSettings =
            AZ::UserSettings::CreateFind<OpenParticleViewportSettings>(AZ::Crc32("OpenParticleViewportSettings"), AZ::UserSettings::CT_GLOBAL);
        QToolBar* topBar = new QToolBar(this);
        // Add toggle grid button
        m_toggleGrid = topBar->addAction(QIcon(":/Gallery/Grid-small.svg"), tr("Use Grid"));
        m_toggleGrid->setCheckable(true);
        connect(
            m_toggleGrid, &QAction::triggered,
            [this]()
            {
                OpenParticleViewportRequestBus::Broadcast(&OpenParticleViewportRequestBus::Events::SetGridEnabled, m_toggleGrid->isChecked());
            });
        m_toggleGrid->setChecked(viewportSettings->m_enableGrid);

        // Add toggle alternate skybox button
        m_toggleAlternateSkybox = topBar->addAction(QIcon(":/stylesheet/img/UI20/toolbar/Environment.svg"), tr("Use Default Skybox"));
        m_toggleAlternateSkybox->setCheckable(true);
        connect(
            m_toggleAlternateSkybox, &QAction::triggered,
            [this]()
            {
                OpenParticleViewportRequestBus::Broadcast(
                    &OpenParticleViewportRequestBus::Events::SetAlternateSkyboxEnabled, m_toggleAlternateSkybox->isChecked());
            });
        m_toggleAlternateSkybox->setChecked(viewportSettings->m_enableAlternateSkybox);

        m_skyBtn = new QToolButton(this);
        m_skyBtn->setObjectName(QString::fromUtf8("m_skyBtn"));
        m_skyBtn->setIcon(QIcon(":/Cards/img/UI20/Cards/menu_ico.svg"));
        LightingPresetMenu* menu = new LightingPresetMenu(tr("Select Skybox"), this);
        m_skyBtn->setMenu(menu);
        m_skyBtn->setPopupMode(QToolButton::InstantPopup);

        horizontalTopLayout->addItem(spacerTopFront);
        AddSpeedControl(horizontalTopLayout);
        horizontalTopLayout->addWidget(topBar);
        horizontalTopLayout->addWidget(m_skyBtn);
        m_verticalLayout->addLayout(horizontalTopLayout);
    }

    // The player bar below is only built in release-revision builds, so the speed control lives in the top bar.
    void ViewInspector::AddSpeedControl(QHBoxLayout* layout)
    {
        QLabel* labelSpeed = new QLabel(tr("Speed"), this);
        QLineEdit* lineEditSpeedVal = new QLineEdit("1.0", this);
        lineEditSpeedVal->setFixedWidth(40);
        lineEditSpeedVal->setToolTip(tr("Preview playback speed: 1 is normal, 0.5 is slow motion. Not saved with the particle."));
        // A dot decimal whatever the system locale, matching QString::toFloat below.
        QDoubleValidator* speedValidator = new QDoubleValidator(0.0, MaxPreviewSpeed, 2, lineEditSpeedVal);
        speedValidator->setLocale(QLocale::c());
        lineEditSpeedVal->setValidator(speedValidator);
        connect(lineEditSpeedVal, &QLineEdit::editingFinished, this, [lineEditSpeedVal]()
            {
                bool valid = false;
                float speed = lineEditSpeedVal->text().toFloat(&valid);
                speed = valid ? AZStd::clamp(speed, 0.0f, MaxPreviewSpeed) : 1.0f;
                lineEditSpeedVal->setText(QString::number(speed, 'g', 3));
                OpenParticleViewportNotificationBus::Broadcast(&OpenParticleViewportNotificationBus::Events::OnPlaybackSpeedChanged, speed);
            });
        layout->addWidget(labelSpeed);
        layout->addSpacing(5);
        layout->addWidget(lineEditSpeedVal);
        layout->addSpacing(10);
    }

    void ViewInspector::CreatePlayerProgressBar()
    {
        QHBoxLayout* horizontalLayout = new QHBoxLayout(this);
        QSlider* sliderPlay = new QSlider(this);
        sliderPlay->setOrientation(Qt::Horizontal);
        QLabel* timePlay = new QLabel("00:00:00", this);
        timePlay->setFixedHeight(18);
        timePlay->setFixedWidth(55);
        horizontalLayout->addSpacing(30);
        horizontalLayout->addWidget(sliderPlay);
        horizontalLayout->addSpacing(5);
        horizontalLayout->addWidget(timePlay);
        horizontalLayout->addSpacing(30);
        horizontalLayout->setContentsMargins(0, 10, 0, 5);
        m_verticalLayout->addLayout(horizontalLayout);
    }

    void ViewInspector::CreatePlayerAction()
    {
        QHBoxLayout* horizontalBottomLayout = new QHBoxLayout(this);
        QSpacerItem* spacerFront = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        QSpacerItem* spacerBack = new QSpacerItem(20, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
        QToolBar* toolbar = new QToolBar(this);
        toolbar->addAction(QIcon(ICON_CIRCULATE), "", [this]()
        {
            AZ_UNUSED(this);
        });
        toolbar->addAction(QIcon(ICON_PALY), "", []()
        {
            EBUS_EVENT(OpenParticle::ParticleRequestBus, Play);
        });
        toolbar->addAction(QIcon(ICON_STOP), "", []()
        {
            EBUS_EVENT(OpenParticle::ParticleRequestBus, Stop);
        });
        toolbar->addAction(QIcon(ICON_NEXT), "", [this]()
        {
            AZ_UNUSED(this);
        });
        toolbar->addAction(QIcon(ICON_RESET), "", [this]()
        {
            AZ_UNUSED(this);
        });
        horizontalBottomLayout->addItem(spacerFront);
        horizontalBottomLayout->addWidget(toolbar);
        horizontalBottomLayout->addItem(spacerBack);
        m_verticalLayout->addLayout(horizontalBottomLayout);
    }

    void ViewInspector::OnGridEnabledChanged(bool enable)
    {
        m_toggleGrid->setChecked(enable);
    }

    void ViewInspector::OnAlternateSkyboxEnabledChanged(bool enable)
    {
        m_toggleAlternateSkybox->setChecked(enable);
    }

    void ViewInspector::OnDisplayMapperOperationTypeChanged(AZ::Render::DisplayMapperOperationType operationType)
    {
        for (auto operationActionPair : m_operationActions)
        {
            operationActionPair.second->setChecked(operationActionPair.first == operationType);
        }
    }
}
