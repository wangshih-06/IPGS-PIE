#include <QApplication>
#include <QComboBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "Algorithm/PlantSkeletonPresets.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"

namespace {
struct DemoData {
    PlantSkeletonPreset preset;
    int iterations = 4;
    quint32 seed = 20260804u;
    PlantModel model;
};

QJsonArray vectorToJson(const Vec3& value) {
    return QJsonArray{value.x(), value.y(), value.z()};
}

QString derivedPath(const QString& imagePath, const QString& suffix) {
    const QFileInfo info(imagePath);
    return QDir(info.absolutePath()).filePath(info.completeBaseName() + suffix);
}

bool prepareDemoData(const QString& presetName,
                     int iterationsOverride,
                     quint32 seed,
                     DemoData* output,
                     QString* error) {
    if (!output || !PlantSkeletonPresets::fromName(presetName, &output->preset)) {
        if (error) {
            *error = QStringLiteral("Unknown preset: %1").arg(presetName);
        }
        return false;
    }

    output->iterations = iterationsOverride >= 0
                             ? iterationsOverride
                             : std::min(4, output->preset.iterations);
    output->seed = seed;
    output->preset.turtleRule.randomSeed = seed;
    const LSystemGenerationResult generated = output->preset.system.generateDetailed(
        output->preset.system.axiom, output->iterations, seed);
    if (generated.truncated) {
        if (error) {
            *error = QStringLiteral("L-System output was truncated before all iterations completed.");
        }
        return false;
    }

    TurtleInterpretationResult interpreted = TurtleInterpreter().interpretDetailed(
        generated.sequence, output->preset.turtleRule);
    if (!interpreted.root || interpreted.stats.unmatchedClosingBrackets != 0 ||
        interpreted.stats.unclosedBranches != 0) {
        if (error) {
            *error = QStringLiteral("Turtle interpretation failed or produced an unbalanced stack.");
        }
        return false;
    }

    output->model.id = 600;
    output->model.name = QStringLiteral("Metaball %1 Debug Plant")
                             .arg(output->preset.displayName);
    output->model.species = output->preset.key;
    output->model.age = 1.0f;
    output->model.lifeStage = PlantLifeStage::Vegetative;
    output->model.growthState = PlantGrowthState::Active;
    output->model.setRootNode(std::move(interpreted.root));

    QString validationError;
    if (!output->model.validate(&validationError)) {
        if (error) {
            *error = QStringLiteral("Plant skeleton validation failed: %1")
                         .arg(validationError);
        }
        return false;
    }
    return true;
}

int sliceIndex(const ScalarFieldGrid& grid, int axis, float normalizedSlice) {
    const int dimension = grid.dimensions[axis];
    return std::max(0, std::min(dimension - 1,
        static_cast<int>(std::round(std::max(0.0f, std::min(1.0f, normalizedSlice)) *
                                    static_cast<float>(dimension - 1)))));
}

float sliceValue(const ScalarFieldGrid& grid,
                 int axis,
                 int fixedIndex,
                 int imageX,
                 int imageY) {
    if (axis == 0) {
        return grid.value(fixedIndex,
                          grid.dimensions.y() - 1 - imageY,
                          imageX);
    }
    if (axis == 1) {
        return grid.value(imageX,
                          fixedIndex,
                          grid.dimensions.z() - 1 - imageY);
    }
    return grid.value(imageX,
                      grid.dimensions.y() - 1 - imageY,
                      fixedIndex);
}

QImage renderSlice(const ScalarFieldGrid& grid,
                   int axis,
                   float normalizedSlice,
                   float threshold) {
    if (!grid.isValid()) {
        return {};
    }

    const int width = axis == 0 ? grid.dimensions.z() : grid.dimensions.x();
    const int height = axis == 1 ? grid.dimensions.z() : grid.dimensions.y();
    const int fixedIndex = sliceIndex(grid, axis, normalizedSlice);
    QImage image(width, height, QImage::Format_RGB32);
    const float safeThreshold = std::max(0.000001f, threshold);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float value = sliceValue(grid, axis, fixedIndex, x, y);
            const float normalized = std::max(0.0f, std::min(2.0f, value / safeThreshold));
            QColor color;
            if (value >= safeThreshold) {
                const float inside = std::min(1.0f, normalized - 1.0f);
                color = QColor::fromRgbF(0.95f,
                                         0.38f + 0.48f * inside,
                                         0.08f + 0.18f * inside);
            } else {
                const float outside = std::min(1.0f, normalized);
                color = QColor::fromRgbF(0.015f + 0.05f * outside,
                                         0.04f + 0.42f * outside,
                                         0.10f + 0.68f * outside);
            }

            bool contour = false;
            if (x + 1 < width) {
                contour = (value - safeThreshold) *
                           (sliceValue(grid, axis, fixedIndex, x + 1, y) - safeThreshold) <= 0.0f;
            }
            if (!contour && y + 1 < height) {
                contour = (value - safeThreshold) *
                           (sliceValue(grid, axis, fixedIndex, x, y + 1) - safeThreshold) <= 0.0f;
            }
            image.setPixelColor(x, y, contour ? QColor(255, 255, 255) : color);
        }
    }

    const int scale = std::max(1, std::min(8, 700 / std::max(width, height)));
    return scale > 1
               ? image.scaled(width * scale,
                              height * scale,
                              Qt::KeepAspectRatio,
                              Qt::FastTransformation)
               : image;
}

bool saveSummary(const QString& path,
                 const DemoData& demo,
                 const MetaballField& field,
                 const ScalarFieldGrid& grid,
                 QString* error) {
    const QJsonObject json{
        {QStringLiteral("format"), QStringLiteral("PlantSim Metaball Field Summary")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("preset"), demo.preset.key},
        {QStringLiteral("iterations"), demo.iterations},
        {QStringLiteral("seed"), static_cast<qint64>(demo.seed)},
        {QStringLiteral("plantNodeCount"), static_cast<qint64>(demo.model.nodeCount())},
        {QStringLiteral("nodeSourceCount"), static_cast<qint64>(field.nodeSources().size())},
        {QStringLiteral("segmentSourceCount"), static_cast<qint64>(field.segmentSources().size())},
        {QStringLiteral("isoThreshold"), field.isoThreshold()},
        {QStringLiteral("jointSmoothness"), field.settings().jointSmoothness},
        {QStringLiteral("influenceScale"), field.settings().influenceScale},
        {QStringLiteral("bounds"), QJsonObject{
             {QStringLiteral("minimum"), vectorToJson(grid.bounds.minimum)},
             {QStringLiteral("maximum"), vectorToJson(grid.bounds.maximum)}}},
        {QStringLiteral("grid"), QJsonObject{
             {QStringLiteral("dimensions"), QJsonArray{grid.dimensions.x(),
                                                        grid.dimensions.y(),
                                                        grid.dimensions.z()}},
             {QStringLiteral("spacing"), vectorToJson(grid.spacing)},
             {QStringLiteral("requestedSpacing"), grid.requestedSpacing},
             {QStringLiteral("spacingAdjusted"), grid.spacingAdjusted},
             {QStringLiteral("sampleCount"), static_cast<qint64>(grid.sampleCount())},
             {QStringLiteral("minimumValue"), grid.minimumValue},
             {QStringLiteral("maximumValue"), grid.maximumValue},
             {QStringLiteral("meanValue"), grid.meanValue},
             {QStringLiteral("samplesAtOrAboveThreshold"),
              static_cast<qint64>(grid.samplesAtOrAboveThreshold)}}}
    };

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot write summary: %1").arg(path);
        }
        return false;
    }
    file.write(QJsonDocument(json).toJson(QJsonDocument::Indented));
    return true;
}

bool saveSliceCsv(const QString& path,
                  const ScalarFieldGrid& grid,
                  int axis,
                  float normalizedSlice,
                  QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot write slice CSV: %1").arg(path);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);
    const int width = axis == 0 ? grid.dimensions.z() : grid.dimensions.x();
    const int height = axis == 1 ? grid.dimensions.z() : grid.dimensions.y();
    const int fixedIndex = sliceIndex(grid, axis, normalizedSlice);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x > 0) {
                stream << ',';
            }
            stream << sliceValue(grid, axis, fixedIndex, x, y);
        }
        stream << '\n';
    }
    return true;
}

void recomputeThresholdStats(ScalarFieldGrid* grid, float threshold) {
    if (!grid) {
        return;
    }
    grid->thresholdUsed = threshold;
    grid->samplesAtOrAboveThreshold = static_cast<std::size_t>(
        std::count_if(grid->values.begin(), grid->values.end(),
                      [threshold](float value) { return value >= threshold; }));
}

class FieldSliceWidget final : public QWidget {
public:
    explicit FieldSliceWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 520);
    }

    void setImage(const QImage& image) {
        image_ = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(8, 12, 20));
        if (image_.isNull()) {
            painter.setPen(Qt::white);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No scalar field samples"));
            return;
        }
        const QSize target = image_.size().scaled(size() - QSize(24, 24),
                                                  Qt::KeepAspectRatio);
        const QRect area(QPoint((width() - target.width()) / 2,
                                (height() - target.height()) / 2),
                         target);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(area, image_);
        painter.setPen(QPen(QColor(170, 190, 210), 1));
        painter.drawRect(area.adjusted(0, 0, -1, -1));
    }

private:
    QImage image_;
};

class MetaballDebugWindow final : public QWidget {
public:
    MetaballDebugWindow(DemoData demo,
                        MetaballFieldSettings settings,
                        float requestedSpacing,
                        int initialAxis,
                        float initialSlice,
                        QString exportPath)
        : demo_(std::move(demo)),
          settings_(settings),
          requestedSpacing_(requestedSpacing),
          exportPath_(std::move(exportPath)) {
        setWindowTitle(QStringLiteral("Metaball 隐式场调试 - %1").arg(demo_.preset.displayName));
        resize(1050, 720);

        auto* mainLayout = new QHBoxLayout(this);
        sliceWidget_ = new FieldSliceWidget(this);
        mainLayout->addWidget(sliceWidget_, 1);

        auto* controls = new QWidget(this);
        controls->setMinimumWidth(285);
        auto* controlsLayout = new QVBoxLayout(controls);
        auto* form = new QFormLayout();

        thresholdSpin_ = new QDoubleSpinBox(controls);
        thresholdSpin_->setRange(0.01, 3.0);
        thresholdSpin_->setSingleStep(0.05);
        thresholdSpin_->setDecimals(3);
        thresholdSpin_->setValue(settings_.isoThreshold);
        form->addRow(QStringLiteral("等值面阈值"), thresholdSpin_);

        smoothnessSlider_ = new QSlider(Qt::Horizontal, controls);
        smoothnessSlider_->setRange(0, 100);
        smoothnessSlider_->setValue(static_cast<int>(settings_.jointSmoothness * 100.0f));
        smoothnessLabel_ = new QLabel(controls);
        auto* smoothnessRow = new QWidget(controls);
        auto* smoothnessLayout = new QHBoxLayout(smoothnessRow);
        smoothnessLayout->setContentsMargins(0, 0, 0, 0);
        smoothnessLayout->addWidget(smoothnessSlider_, 1);
        smoothnessLayout->addWidget(smoothnessLabel_);
        form->addRow(QStringLiteral("连接平滑度"), smoothnessRow);

        spacingSpin_ = new QDoubleSpinBox(controls);
        spacingSpin_->setRange(0.02, 0.5);
        spacingSpin_->setSingleStep(0.01);
        spacingSpin_->setDecimals(3);
        spacingSpin_->setValue(requestedSpacing_);
        form->addRow(QStringLiteral("采样间距"), spacingSpin_);

        axisCombo_ = new QComboBox(controls);
        axisCombo_->addItems({QStringLiteral("X 截面"),
                              QStringLiteral("Y 截面"),
                              QStringLiteral("Z 截面")});
        axisCombo_->setCurrentIndex(initialAxis);
        form->addRow(QStringLiteral("截面方向"), axisCombo_);

        sliceSlider_ = new QSlider(Qt::Horizontal, controls);
        sliceSlider_->setRange(0, 1000);
        sliceSlider_->setValue(static_cast<int>(initialSlice * 1000.0f));
        form->addRow(QStringLiteral("截面位置"), sliceSlider_);
        controlsLayout->addLayout(form);

        legendLabel_ = new QLabel(
            QStringLiteral("蓝色：场外\n橙色：场内\n白色：当前等值线"), controls);
        controlsLayout->addWidget(legendLabel_);

        statsLabel_ = new QLabel(controls);
        statsLabel_->setWordWrap(true);
        statsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        controlsLayout->addWidget(statsLabel_);
        controlsLayout->addStretch(1);

        auto* exportButton = new QPushButton(QStringLiteral("导出当前切片"), controls);
        controlsLayout->addWidget(exportButton);
        mainLayout->addWidget(controls);

        connect(thresholdSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
                this, [this](double value) {
                    field_.setIsoThreshold(static_cast<float>(value));
                    recomputeThresholdStats(&grid_, field_.isoThreshold());
                    refreshImage();
                });
        connect(smoothnessSlider_, &QSlider::valueChanged, this, [this](int value) {
            smoothnessLabel_->setText(QString::number(value / 100.0, 'f', 2));
        });
        connect(smoothnessSlider_, &QSlider::sliderReleased, this, [this]() {
            settings_.jointSmoothness = smoothnessSlider_->value() / 100.0f;
            rebuildGrid();
        });
        connect(spacingSpin_, &QDoubleSpinBox::editingFinished, this, [this]() {
            requestedSpacing_ = static_cast<float>(spacingSpin_->value());
            rebuildGrid();
        });
        connect(axisCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshImage(); });
        connect(sliceSlider_, &QSlider::valueChanged,
                this, [this](int) { refreshImage(); });
        connect(exportButton, &QPushButton::clicked, this, [this]() {
            const QString selected = QFileDialog::getSaveFileName(
                this,
                QStringLiteral("导出隐式场切片"),
                exportPath_,
                QStringLiteral("PNG Image (*.png);;Portable Pixmap (*.ppm)"));
            if (!selected.isEmpty()) {
                currentImage_.save(selected);
                exportPath_ = selected;
            }
        });

        smoothnessLabel_->setText(QString::number(settings_.jointSmoothness, 'f', 2));
        rebuildGrid();
    }

private:
    void rebuildGrid() {
        settings_.isoThreshold = static_cast<float>(thresholdSpin_->value());
        field_.rebuildFromPlant(demo_.model, settings_);
        grid_ = field_.sampleGrid(requestedSpacing_);
        refreshImage();
    }

    void refreshImage() {
        if (!grid_.isValid()) {
            return;
        }
        currentImage_ = renderSlice(grid_,
                                    axisCombo_->currentIndex(),
                                    sliceSlider_->value() / 1000.0f,
                                    field_.isoThreshold());
        sliceWidget_->setImage(currentImage_);
        const double insidePercent = grid_.sampleCount() == 0
            ? 0.0
            : 100.0 * static_cast<double>(grid_.samplesAtOrAboveThreshold) /
                  static_cast<double>(grid_.sampleCount());
        statsLabel_->setText(
            QStringLiteral("植物节点：%1\n节点场源：%2\n线段场源：%3\n"
                           "网格：%4 × %5 × %6\n采样点：%7\n"
                           "场值范围：%8 ～ %9\n阈值内部采样：%10%")
                .arg(static_cast<qulonglong>(demo_.model.nodeCount()))
                .arg(static_cast<qulonglong>(field_.nodeSources().size()))
                .arg(static_cast<qulonglong>(field_.segmentSources().size()))
                .arg(grid_.dimensions.x())
                .arg(grid_.dimensions.y())
                .arg(grid_.dimensions.z())
                .arg(static_cast<qulonglong>(grid_.sampleCount()))
                .arg(grid_.minimumValue, 0, 'f', 3)
                .arg(grid_.maximumValue, 0, 'f', 3)
                .arg(insidePercent, 0, 'f', 2));
    }

    DemoData demo_;
    MetaballFieldSettings settings_;
    float requestedSpacing_ = 0.1f;
    QString exportPath_;
    MetaballField field_;
    ScalarFieldGrid grid_;
    QImage currentImage_;
    FieldSliceWidget* sliceWidget_ = nullptr;
    QDoubleSpinBox* thresholdSpin_ = nullptr;
    QSlider* smoothnessSlider_ = nullptr;
    QLabel* smoothnessLabel_ = nullptr;
    QDoubleSpinBox* spacingSpin_ = nullptr;
    QComboBox* axisCombo_ = nullptr;
    QSlider* sliceSlider_ = nullptr;
    QLabel* legendLabel_ = nullptr;
    QLabel* statsLabel_ = nullptr;
};

int axisFromName(const QString& name) {
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("x")) {
        return 0;
    }
    if (normalized == QStringLiteral("y")) {
        return 1;
    }
    return 2;
}
}

int main(int argc, char* argv[]) {
    bool noWindow = false;
    for (int index = 1; index < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("--no-window")) {
            noWindow = true;
            break;
        }
    }
    if (noWindow) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MetaballFieldDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Interactive/debug visualization for the plant Metaball scalar field."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption presetOption({QStringLiteral("p"), QStringLiteral("preset")},
                                    QStringLiteral("Plant preset: pine, willow, cherry, shrub."),
                                    QStringLiteral("name"), QStringLiteral("cherry"));
    QCommandLineOption iterationsOption({QStringLiteral("i"), QStringLiteral("iterations")},
                                        QStringLiteral("L-System iterations (default: at most 4)."),
                                        QStringLiteral("count"));
    QCommandLineOption seedOption({QStringLiteral("s"), QStringLiteral("seed")},
                                  QStringLiteral("Random seed."),
                                  QStringLiteral("number"), QStringLiteral("20260804"));
    QCommandLineOption thresholdOption({QStringLiteral("t"), QStringLiteral("threshold")},
                                       QStringLiteral("Iso-surface threshold."),
                                       QStringLiteral("value"), QStringLiteral("0.5"));
    QCommandLineOption smoothnessOption(QStringLiteral("smoothness"),
                                        QStringLiteral("Joint smoothness in [0, 1]."),
                                        QStringLiteral("value"), QStringLiteral("0.65"));
    QCommandLineOption spacingOption(QStringLiteral("spacing"),
                                     QStringLiteral("Requested sampling spacing."),
                                     QStringLiteral("value"), QStringLiteral("0.10"));
    QCommandLineOption axisOption(QStringLiteral("axis"),
                                  QStringLiteral("Slice axis: x, y, or z."),
                                  QStringLiteral("axis"), QStringLiteral("z"));
    QCommandLineOption sliceOption(QStringLiteral("slice"),
                                   QStringLiteral("Normalized slice position in [0, 1]."),
                                   QStringLiteral("value"), QStringLiteral("0.5"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Debug slice image output."),
                                    QStringLiteral("file"),
                                    QStringLiteral("examples/metaball_cherry_slice.png"));
    QCommandLineOption summaryOption(QStringLiteral("summary"),
                                     QStringLiteral("Scalar field summary JSON output."),
                                     QStringLiteral("file"));
    QCommandLineOption csvOption(QStringLiteral("csv"),
                                 QStringLiteral("Scalar values for the selected slice."),
                                 QStringLiteral("file"));
    QCommandLineOption noWindowOption(QStringLiteral("no-window"),
                                      QStringLiteral("Export files and exit without showing a window."));

    parser.addOption(presetOption);
    parser.addOption(iterationsOption);
    parser.addOption(seedOption);
    parser.addOption(thresholdOption);
    parser.addOption(smoothnessOption);
    parser.addOption(spacingOption);
    parser.addOption(axisOption);
    parser.addOption(sliceOption);
    parser.addOption(outputOption);
    parser.addOption(summaryOption);
    parser.addOption(csvOption);
    parser.addOption(noWindowOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");
#endif

    bool valid = false;
    const quint32 seed = parser.value(seedOption).toUInt(&valid);
    if (!valid) {
        err << "Invalid seed." << Qt::endl;
        return 2;
    }

    int iterations = -1;
    if (parser.isSet(iterationsOption)) {
        iterations = parser.value(iterationsOption).toInt(&valid);
        if (!valid || iterations < 0) {
            err << "Iterations must be a non-negative integer." << Qt::endl;
            return 3;
        }
    }

    const float threshold = parser.value(thresholdOption).toFloat(&valid);
    if (!valid || threshold <= 0.0f) {
        err << "Threshold must be positive." << Qt::endl;
        return 4;
    }
    const float smoothness = parser.value(smoothnessOption).toFloat(&valid);
    if (!valid || smoothness < 0.0f || smoothness > 1.0f) {
        err << "Smoothness must be in [0, 1]." << Qt::endl;
        return 5;
    }
    const float spacing = parser.value(spacingOption).toFloat(&valid);
    if (!valid || spacing <= 0.0f) {
        err << "Spacing must be positive." << Qt::endl;
        return 6;
    }
    const float normalizedSlice = parser.value(sliceOption).toFloat(&valid);
    if (!valid || normalizedSlice < 0.0f || normalizedSlice > 1.0f) {
        err << "Slice must be in [0, 1]." << Qt::endl;
        return 7;
    }

    DemoData demo;
    QString error;
    if (!prepareDemoData(parser.value(presetOption), iterations, seed, &demo, &error)) {
        err << error << Qt::endl;
        return 8;
    }

    MetaballFieldSettings settings;
    settings.isoThreshold = threshold;
    settings.jointSmoothness = smoothness;
    settings.influenceScale = 2.0f;
    settings.segmentWeight = 1.0f;
    settings.boundsPadding = spacing;

    MetaballField field;
    field.rebuildFromPlant(demo.model, settings);
    ScalarFieldGrid grid = field.sampleGrid(spacing);
    if (!grid.isValid()) {
        err << "Scalar field grid generation failed." << Qt::endl;
        return 9;
    }

    const int axis = axisFromName(parser.value(axisOption));
    const QImage image = renderSlice(grid, axis, normalizedSlice, field.isoThreshold());
    const QFileInfo imageInfo(parser.value(outputOption));
    QDir().mkpath(imageInfo.absolutePath());
    if (!image.save(imageInfo.absoluteFilePath())) {
        err << "Cannot save debug image: " << imageInfo.absoluteFilePath() << Qt::endl;
        return 10;
    }

    const QString summaryPath = parser.isSet(summaryOption)
        ? QFileInfo(parser.value(summaryOption)).absoluteFilePath()
        : derivedPath(imageInfo.absoluteFilePath(), QStringLiteral("_field.json"));
    const QString csvPath = parser.isSet(csvOption)
        ? QFileInfo(parser.value(csvOption)).absoluteFilePath()
        : derivedPath(imageInfo.absoluteFilePath(), QStringLiteral("_slice.csv"));
    QDir().mkpath(QFileInfo(summaryPath).absolutePath());
    QDir().mkpath(QFileInfo(csvPath).absolutePath());

    if (!saveSummary(summaryPath, demo, field, grid, &error) ||
        !saveSliceCsv(csvPath, grid, axis, normalizedSlice, &error)) {
        err << error << Qt::endl;
        return 11;
    }

    out << "Metaball field generated" << Qt::endl
        << "  preset=" << demo.preset.key
        << ", iterations=" << demo.iterations
        << ", seed=" << demo.seed << Qt::endl
        << "  plantNodes=" << static_cast<qulonglong>(demo.model.nodeCount())
        << ", nodeSources=" << static_cast<qulonglong>(field.nodeSources().size())
        << ", segmentSources=" << static_cast<qulonglong>(field.segmentSources().size())
        << Qt::endl
        << "  threshold=" << field.isoThreshold()
        << ", smoothness=" << field.settings().jointSmoothness
        << ", requestedSpacing=" << spacing << Qt::endl
        << "  grid=" << grid.dimensions.x() << "x"
        << grid.dimensions.y() << "x" << grid.dimensions.z()
        << ", samples=" << static_cast<qulonglong>(grid.sampleCount())
        << ", fieldRange=[" << grid.minimumValue << ", " << grid.maximumValue << "]"
        << Qt::endl
        << "  insideSamples=" << static_cast<qulonglong>(grid.samplesAtOrAboveThreshold)
        << Qt::endl
        << "  image=" << imageInfo.absoluteFilePath() << Qt::endl
        << "  summary=" << summaryPath << Qt::endl
        << "  sliceCsv=" << csvPath << Qt::endl;

    if (parser.isSet(noWindowOption)) {
        return 0;
    }

    auto* window = new MetaballDebugWindow(std::move(demo),
                                            settings,
                                            spacing,
                                            axis,
                                            normalizedSlice,
                                            imageInfo.absoluteFilePath());
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    return app.exec();
}
