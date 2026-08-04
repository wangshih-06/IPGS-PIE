#include "Engine/EngineWindow.h"

#include "Engine/SimulationEngine.h"
#include "Rendering/Renderer.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QVBoxLayout>

#include <QtMath>

namespace {
QLabel* createLabel(const QString& text, const QString& objectName, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    return label;
}

QLabel* createSectionTitle(const QString& text, QWidget* parent) {
    return createLabel(text, QStringLiteral("SectionTitle"), parent);
}

void addMetric(QVBoxLayout* layout, const QString& name, const QString& value, QWidget* parent) {
    auto* row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    auto* nameLabel = createLabel(name, QStringLiteral("MetricName"), parent);
    auto* valueLabel = createLabel(value, QStringLiteral("MetricValue"), parent);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(nameLabel);
    row->addWidget(valueLabel);
    layout->addLayout(row);
}

QFrame* createDivider(QWidget* parent) {
    auto* divider = new QFrame(parent);
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    return divider;
}
}

EngineWindow::EngineWindow(SimulationEngine* simulationEngine, QWidget* parent)
    : QMainWindow(parent), simulationEngine_(simulationEngine) {
    setObjectName(QStringLiteral("EngineWindow"));
    setWindowTitle(QStringLiteral("PlantSim | Growth Lab"));
    setMinimumSize(1080, 680);
    resize(1320, 820);
    applyTheme();

    auto* root = new QWidget(this);
    root->setObjectName(QStringLiteral("WindowRoot"));
    setCentralWidget(root);

    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* header = new QFrame(root);
    header->setObjectName(QStringLiteral("HeaderBar"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 18, 24, 18);

    auto* brandLayout = new QVBoxLayout();
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(3);
    brandLayout->addWidget(createLabel(QStringLiteral("PLANTSIM"), QStringLiteral("Brand"), header));
    brandLayout->addWidget(createLabel(QStringLiteral("GROWTH LAB / NATIVE ENGINE"), QStringLiteral("Subtitle"), header));
    headerLayout->addLayout(brandLayout);
    headerLayout->addStretch();

    engineStatusLabel_ = createLabel(QStringLiteral("●  ENGINE READY"), QStringLiteral("EngineStatus"), header);
    headerLayout->addWidget(engineStatusLabel_);
    headerLayout->addSpacing(18);
    headerLayout->addWidget(createLabel(QStringLiteral("OPENGL 4.3 CORE"), QStringLiteral("TransportLabel"), header));
    headerLayout->addSpacing(18);
    headerLayout->addWidget(createLabel(QStringLiteral("WS 4317"), QStringLiteral("TransportLabel"), header));
    rootLayout->addWidget(header);

    auto* content = new QWidget(root);
    auto* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 14);
    contentLayout->setSpacing(16);

    renderer_ = new Renderer(content);
    renderer_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout->addWidget(renderer_, 1);

    auto* inspector = new QFrame(content);
    inspector->setObjectName(QStringLiteral("InspectorPanel"));
    inspector->setMinimumWidth(268);
    inspector->setMaximumWidth(300);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(19, 19, 19, 18);
    inspectorLayout->setSpacing(0);

    inspectorLayout->addWidget(createSectionTitle(QStringLiteral("ENVIRONMENT"), inspector));
    inspectorLayout->addSpacing(14);

    auto* lightTitleRow = new QHBoxLayout();
    lightTitleRow->setContentsMargins(0, 0, 0, 0);
    lightTitleRow->addWidget(createLabel(QStringLiteral("Light intensity"), QStringLiteral("ControlName"), inspector));
    lightValueLabel_ = createLabel(QStringLiteral("80%"), QStringLiteral("ControlValue"), inspector);
    lightValueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lightTitleRow->addWidget(lightValueLabel_);
    inspectorLayout->addLayout(lightTitleRow);
    inspectorLayout->addSpacing(8);

    lightSlider_ = new QSlider(Qt::Horizontal, inspector);
    lightSlider_->setRange(0, 100);
    lightSlider_->setSingleStep(1);
    lightSlider_->setPageStep(10);
    lightSlider_->setToolTip(QStringLiteral("Adjust the renderer light intensity"));
    inspectorLayout->addWidget(lightSlider_);

    auto* rangeLabels = new QHBoxLayout();
    rangeLabels->setContentsMargins(0, 5, 0, 0);
    rangeLabels->addWidget(createLabel(QStringLiteral("0"), QStringLiteral("RangeLabel"), inspector));
    rangeLabels->addStretch();
    rangeLabels->addWidget(createLabel(QStringLiteral("100"), QStringLiteral("RangeLabel"), inspector));
    inspectorLayout->addLayout(rangeLabels);
    inspectorLayout->addSpacing(18);

    auto* resetButton = new QPushButton(QStringLiteral("Reset camera"), inspector);
    resetButton->setObjectName(QStringLiteral("SecondaryButton"));
    resetButton->setToolTip(QStringLiteral("Reset the viewport camera (R)"));
    resetButton->setShortcut(Qt::Key_R);
    inspectorLayout->addWidget(resetButton);
    inspectorLayout->addSpacing(12);

    auto* rotateCheck = new QCheckBox(QStringLiteral("Auto-rotate viewport"), inspector);
    rotateCheck->setChecked(true);
    rotateCheck->setToolTip(QStringLiteral("Continuously rotate the reference scene"));
    inspectorLayout->addWidget(rotateCheck);
    inspectorLayout->addSpacing(20);
    inspectorLayout->addWidget(createDivider(inspector));
    inspectorLayout->addSpacing(20);

    inspectorLayout->addWidget(createSectionTitle(QStringLiteral("PLANT STRUCTURE"), inspector));
    inspectorLayout->addSpacing(14);
    const auto nodeCount = static_cast<qulonglong>(simulationEngine_->plantNodeCount());
    const auto nodeSourceCount = static_cast<qulonglong>(simulationEngine_->metaballField().nodeSources().size());
    const auto segmentSourceCount = static_cast<qulonglong>(simulationEngine_->metaballField().segmentSources().size());
    addMetric(inspectorLayout, QStringLiteral("Skeleton nodes"), QString::number(nodeCount), inspector);
    inspectorLayout->addSpacing(10);
    addMetric(inspectorLayout, QStringLiteral("Metaball nodes"), QString::number(nodeSourceCount), inspector);
    inspectorLayout->addSpacing(10);
    addMetric(inspectorLayout, QStringLiteral("Segment sources"), QString::number(segmentSourceCount), inspector);
    inspectorLayout->addSpacing(20);
    inspectorLayout->addWidget(createDivider(inspector));
    inspectorLayout->addSpacing(20);

    inspectorLayout->addWidget(createSectionTitle(QStringLiteral("VIEWPORT"), inspector));
    inspectorLayout->addSpacing(14);
    auto* help = createLabel(QStringLiteral("LMB   Orbit\nMMB   Pan\nWheel  Zoom"), QStringLiteral("HelpText"), inspector);
    help->setTextFormat(Qt::PlainText);
    inspectorLayout->addWidget(help);
    inspectorLayout->addStretch();
    inspectorLayout->addWidget(createLabel(QStringLiteral("REFERENCE MESH / 180 VERTICES"), QStringLiteral("Footnote"), inspector));
    contentLayout->addWidget(inspector);
    rootLayout->addWidget(content, 1);

    statusBar()->showMessage(QStringLiteral("Local engine ready  ·  WebSocket ws://127.0.0.1:4317"));
    statusBar()->setSizeGripEnabled(false);

    connect(resetButton, &QPushButton::clicked, renderer_, &Renderer::resetCamera);
    connect(rotateCheck, &QCheckBox::toggled, renderer_, &Renderer::setAutoRotate);
    connect(lightSlider_, &QSlider::valueChanged, this, &EngineWindow::onLightSliderChanged);
    connect(simulationEngine_, &SimulationEngine::environmentUpdated,
            this, &EngineWindow::onEnvironmentUpdated);
    connect(simulationEngine_, &SimulationEngine::environmentUpdated,
            renderer_, &Renderer::setLightIntensity);
    onEnvironmentUpdated(simulationEngine_->environment().lightIntensity);
}

void EngineWindow::applyTheme() {
    setStyleSheet(QStringLiteral(R"qss(
        QMainWindow, QWidget#WindowRoot { background: #091114; color: #dfece5; }
        QFrame#HeaderBar { background: #101c20; border-bottom: 1px solid #263c3c; }
        QFrame#InspectorPanel { background: #101b1f; border: 1px solid #263c3c; }
        QLabel#Brand { color: #eef7f0; font-size: 18px; font-weight: 600; }
        QLabel#Subtitle, QLabel#TransportLabel, QLabel#SectionTitle, QLabel#RangeLabel,
        QLabel#Footnote, QLabel#HelpText { color: #769088; }
        QLabel#Subtitle, QLabel#TransportLabel, QLabel#SectionTitle, QLabel#RangeLabel,
        QLabel#Footnote { font-size: 10px; }
        QLabel#EngineStatus { color: #a9e5a7; font-size: 11px; font-weight: 600; }
        QLabel#ControlName, QLabel#MetricName { color: #b5c8bf; font-size: 12px; }
        QLabel#ControlValue, QLabel#MetricValue { color: #b9e6a9; font-size: 16px; font-weight: 600; }
        QLabel#HelpText { font-size: 11px; line-height: 1.6em; }
        QFrame#Divider { color: #263c3c; max-height: 1px; }
        QSlider::groove:horizontal { height: 4px; background: #2a403d; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #88d6a5; border-radius: 2px; }
        QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: #f0bf70; border: 2px solid #101b1f; border-radius: 7px; }
        QPushButton#SecondaryButton { min-height: 34px; color: #c1d2c9; background: #16282b; border: 1px solid #31504c; border-radius: 2px; }
        QPushButton#SecondaryButton:hover { color: #eff8f0; background: #1c3535; border-color: #78c79a; }
        QCheckBox { color: #9fb5aa; font-size: 11px; spacing: 8px; }
        QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #41605a; background: #0b1518; border-radius: 2px; }
        QCheckBox::indicator:checked { background: #8ed8a5; border-color: #8ed8a5; }
        QStatusBar { min-height: 27px; color: #6f8980; background: #0b1518; border-top: 1px solid #203634; font-size: 10px; }
    )qss"));
}

void EngineWindow::showEngineMessage(const QString& message) {
    if (engineStatusLabel_) {
        engineStatusLabel_->setText(QStringLiteral("●  ENGINE ONLINE"));
    }
    statusBar()->showMessage(message, 4500);
}

void EngineWindow::onLightSliderChanged(int value) {
    if (simulationEngine_) {
        simulationEngine_->setLightIntensity(static_cast<float>(value) / 100.0f);
    }
}

void EngineWindow::onEnvironmentUpdated(float intensity) {
    const int value = qBound(0, qRound(intensity * 100.0f), 100);
    if (lightSlider_) {
        const QSignalBlocker blocker(lightSlider_);
        lightSlider_->setValue(value);
    }
    if (lightValueLabel_) {
        lightValueLabel_->setText(QStringLiteral("%1%").arg(value));
    }
}
