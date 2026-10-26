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
#include <QTimer>
#include <OpenParticleSystem/ParticleEditorRequestBus.h>
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
    static const char* ICON_PAUSE = ":/Trackview/Pause.svg";
    static constexpr int SliderSteps = 1000;
    static constexpr float FrameStep = 1.0f / 30.0f;
    // A non-looping particle restarts this long after its last emitter stops, so the last particles can finish.
    static constexpr float LoopRestartDelay = 1.0f;
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

        CreatePlayerProgressBar();
        CreatePlayerAction();

        // Polls the preview, which simulates on the render thread, about thirty times a second.
        m_playerTimer = new QTimer(this);
        connect(m_playerTimer, &QTimer::timeout, this, &ViewInspector::UpdatePlayerBar);
        m_playerTimer->start(33);

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
        m_timeSlider = new QSlider(this);
        m_timeSlider->setOrientation(Qt::Horizontal);
        m_timeSlider->setRange(0, SliderSteps);
        m_timeSlider->setToolTip(tr("Drag to scrub the preview through its duration"));
        // Scrubbing pauses the preview, seeks it on every move and resumes it on release if it was playing.
        connect(m_timeSlider, &QSlider::sliderPressed, this, [this]()
            {
                const AZ::EntityId preview = GetPreviewEntityId();
                bool playing = false;
                OpenParticle::ParticleRequestBus::EventResult(playing, preview, &OpenParticle::ParticleRequest::IsPlaying);
                m_resumeAfterScrub = playing;
                OpenParticle::ParticleRequestBus::Event(preview, &OpenParticle::ParticleRequest::Pause);
            });
        connect(m_timeSlider, &QSlider::sliderMoved, this, [this](int value)
            {
                const float seconds = m_duration * static_cast<float>(value) / static_cast<float>(SliderSteps);
                OpenParticle::ParticleRequestBus::Event(GetPreviewEntityId(), &OpenParticle::ParticleRequest::SetPlaybackTime, seconds);
            });
        connect(m_timeSlider, &QSlider::sliderReleased, this, [this]()
            {
                if (m_resumeAfterScrub)
                {
                    OpenParticle::ParticleRequestBus::Event(GetPreviewEntityId(), &OpenParticle::ParticleRequest::Play);
                }
            });

        m_timeLabel = new QLabel("00:00:00", this);
        m_timeLabel->setFixedHeight(18);
        m_timeLabel->setMinimumWidth(110);
        m_timeLabel->setToolTip(tr("Preview time / duration (minutes:seconds:hundredths)"));
        horizontalLayout->addSpacing(30);
        horizontalLayout->addWidget(m_timeSlider);
        horizontalLayout->addSpacing(5);
        horizontalLayout->addWidget(m_timeLabel);
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
        m_loopAction = toolbar->addAction(QIcon(ICON_CIRCULATE), "");
        m_loopAction->setCheckable(true);
        m_loopAction->setToolTip(tr("Loop preview: restart a particle that does not loop shortly after it ends"));

        m_playAction = toolbar->addAction(QIcon(ICON_PALY), "", [this]()
        {
            const AZ::EntityId preview = GetPreviewEntityId();
            bool playing = false;
            OpenParticle::ParticleRequestBus::EventResult(playing, preview, &OpenParticle::ParticleRequest::IsPlaying);
            OpenParticle::ParticleRequestBus::Event(preview, playing ? &OpenParticle::ParticleRequest::Pause : &OpenParticle::ParticleRequest::Play);
            UpdatePlayerBar();
        });
        m_playAction->setToolTip(tr("Play / Pause"));

        toolbar->addAction(QIcon(ICON_STOP), "", [this]()
        {
            OpenParticle::ParticleRequestBus::Event(GetPreviewEntityId(), &OpenParticle::ParticleRequest::Stop);
            UpdatePlayerBar();
        })->setToolTip(tr("Stop and clear the preview"));

        toolbar->addAction(QIcon(ICON_NEXT), "", [this]()
        {
            OpenParticle::ParticleRequestBus::Event(GetPreviewEntityId(), &OpenParticle::ParticleRequest::Step, FrameStep);
            UpdatePlayerBar();
        })->setToolTip(tr("Pause and step one frame"));

        toolbar->addAction(QIcon(ICON_RESET), "", [this]()
        {
            OpenParticle::ParticleRequestBus::Event(GetPreviewEntityId(), &OpenParticle::ParticleRequest::SetPlaybackTime, 0.0f);
            UpdatePlayerBar();
        })->setToolTip(tr("Restart from the beginning"));
        horizontalBottomLayout->addItem(spacerFront);
        horizontalBottomLayout->addWidget(toolbar);
        horizontalBottomLayout->addItem(spacerBack);
        m_verticalLayout->addLayout(horizontalBottomLayout);
    }

    AZ::EntityId ViewInspector::GetPreviewEntityId() const
    {
        AZ::EntityId preview;
        OpenParticleSystem::ParticleEditorRequestBus::BroadcastResult(
            preview, &OpenParticleSystem::ParticleEditorRequestBus::Events::GetPreviewParticleEntityId);
        return preview;
    }

    void ViewInspector::UpdatePlayerBar()
    {
        const AZ::EntityId preview = GetPreviewEntityId();
        if (!preview.IsValid() || m_timeSlider == nullptr)
        {
            return;
        }

        bool playing = false;
        bool looping = false;
        float time = 0.0f;
        float duration = 0.0f;
        OpenParticle::ParticleRequestBus::EventResult(playing, preview, &OpenParticle::ParticleRequest::IsPlaying);
        OpenParticle::ParticleRequestBus::EventResult(looping, preview, &OpenParticle::ParticleRequest::IsLooping);
        OpenParticle::ParticleRequestBus::EventResult(time, preview, &OpenParticle::ParticleRequest::GetPlaybackTime);
        OpenParticle::ParticleRequestBus::EventResult(duration, preview, &OpenParticle::ParticleRequest::GetPlaybackDuration);
        m_duration = duration;

        if (m_loopAction->isChecked() && playing && !looping && duration > 0.0f && time >= duration + LoopRestartDelay)
        {
            OpenParticle::ParticleRequestBus::Event(preview, &OpenParticle::ParticleRequest::SetPlaybackTime, 0.0f);
            time = 0.0f;
        }

        // A looping particle shows where it is in its current loop; one that does not loop stops at its end.
        const float shown = duration <= 0.0f ? 0.0f : (looping ? fmodf(time, duration) : AZStd::min(time, duration));
        if (!m_timeSlider->isSliderDown())
        {
            const QSignalBlocker blocker(m_timeSlider);
            m_timeSlider->setValue(duration > 0.0f ? static_cast<int>(shown / duration * SliderSteps) : 0);
        }

        const auto format = [](float seconds)
        {
            const int hundredths = static_cast<int>(seconds * 100.0f + 0.5f);
            return QString("%1:%2:%3")
                .arg(hundredths / 6000, 2, 10, QChar('0'))
                .arg((hundredths / 100) % 60, 2, 10, QChar('0'))
                .arg(hundredths % 100, 2, 10, QChar('0'));
        };
        m_timeLabel->setText(QString("%1 / %2").arg(format(shown), format(duration)));
        m_playAction->setIcon(QIcon(playing ? ICON_PAUSE : ICON_PALY));
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
