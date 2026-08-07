// ============================================================================
// GrowthClock - QTimer 包装的播放控制器（realtime / step / pause / reset / speed）
// ============================================================================
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include "Engine/GrowthTimeline.h"

class GrowthClock : public QObject {
    Q_OBJECT
public:
    explicit GrowthClock(QObject* parent = nullptr);

    // 配置
    void setTickInterval(int msec);          // 默认 33ms（≈30 FPS）
    int  tickInterval() const { return tickInterval_; }

    // 状态查询
    bool isRunning() const { return timer_.isActive(); }
    GrowthTimeline&       timeline()       { return timeline_; }
    const GrowthTimeline& timeline() const { return timeline_; }

public slots:
    // 播放控制（Qt slots）
    void start();                              // 进入 Realtime 模式
    void pause();                              // 暂停：mode_ = Paused
    void resume();                             // 恢复：mode_ = Realtime
    void reset(float initialYears = 0.0f);     // 清零 age 并暂停
    void setSpeed(float speed);                // 0.1 .. 8.0
    void stepOnce(float deltaYears);           // 单步推进（Step 模式）

signals:
    void tickProduced(const GrowthSample& sample);
    void modeChanged(int mode);                // 传 int 以避免 moc 引用自定义枚举
    void speedChanged(float speed);
    void logMessage(const QString& message);

private slots:
    void onTimeout();

private:
    void emitMode(int mode);
    void emitSpeed(float speed);
    float wallSecondsToYears(float seconds) const;

    QTimer         timer_;
    QElapsedTimer  wallClock_;
    GrowthTimeline timeline_;
    int   tickInterval_ = 33;     // ms
    float speed_        = 1.0f;   // 镜像 timeline_.speed() 便于本地读取
};