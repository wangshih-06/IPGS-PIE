// ============================================================================
// GrowthClock - 实现
// ============================================================================
#include "Engine/GrowthClock.h"

#include <algorithm>

#include <QDebug>

GrowthClock::GrowthClock(QObject* parent) : QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(tickInterval_);
    connect(&timer_, &QTimer::timeout, this, &GrowthClock::onTimeout);
}

void GrowthClock::setTickInterval(int msec) {
    tickInterval_ = std::max(8, msec);   // 不低于 8ms（约 120 FPS）
    timer_.setInterval(tickInterval_);
}

void GrowthClock::emitMode(int mode) {
    emit modeChanged(mode);
    emit tickProduced(timeline_.sample(timeline_.currentAge()));
}

void GrowthClock::emitSpeed(float speed) {
    emit speedChanged(speed);
}

// ============================================================================
// Slots
// ============================================================================
void GrowthClock::start() {
    if (timeline_.mode() == GrowthPlaybackMode::Completed) {
        // 已经完成，先 reset 回 0
        timeline_.reset(0.0f);
        emit logMessage(QStringLiteral("Growth restart from seed"));
    }
    timeline_.setMode(GrowthPlaybackMode::Realtime);
    wallClock_.restart();
    if (!timer_.isActive()) timer_.start();
    emitMode(static_cast<int>(timeline_.mode()));
    emit logMessage(QStringLiteral("Growth started @ speed %1x").arg(speed_, 0, 'f', 1));
}

void GrowthClock::pause() {
    if (!timer_.isActive() && timeline_.mode() == GrowthPlaybackMode::Paused) return;
    timeline_.setMode(GrowthPlaybackMode::Paused);
    timer_.stop();
    emitMode(static_cast<int>(timeline_.mode()));
    emit logMessage(QStringLiteral("Growth paused"));
}

void GrowthClock::resume() {
    if (timeline_.mode() == GrowthPlaybackMode::Completed) {
        timeline_.reset(0.0f);
    }
    timeline_.setMode(GrowthPlaybackMode::Realtime);
    wallClock_.restart();
    if (!timer_.isActive()) timer_.start();
    emitMode(static_cast<int>(timeline_.mode()));
    emit logMessage(QStringLiteral("Growth resumed @ speed %1x").arg(speed_, 0, 'f', 1));
}

void GrowthClock::reset(float initialYears) {
    timer_.stop();
    timeline_.reset(initialYears);
    speed_ = timeline_.speed();
    emitMode(static_cast<int>(timeline_.mode()));
    emit logMessage(QStringLiteral("Growth reset to %1y").arg(initialYears, 0, 'f', 2));
}

void GrowthClock::setSpeed(float speed) {
    timeline_.setSpeed(speed);
    speed_ = timeline_.speed();
    emitSpeed(speed_);
    emit logMessage(QStringLiteral("Speed -> %1x").arg(speed_, 0, 'f', 1));
}

void GrowthClock::stepOnce(float deltaYears) {
    if (deltaYears <= 0.0f) return;
    timeline_.setMode(GrowthPlaybackMode::Step);
    const GrowthSample sample = timeline_.advance(deltaYears);
    emitMode(static_cast<int>(timeline_.mode()));
    emit tickProduced(sample);
}

// ============================================================================
// Internal
// ============================================================================
void GrowthClock::onTimeout() {
    const qint64 elapsedMs = wallClock_.isValid() ? wallClock_.elapsed() : tickInterval_;
    wallClock_.restart();
    const float deltaYears = wallSecondsToYears(static_cast<float>(elapsedMs) / 1000.0f);
    if (deltaYears <= 0.0f) return;
    const GrowthSample sample = timeline_.advance(deltaYears);
    emit tickProduced(sample);
    if (timeline_.mode() == GrowthPlaybackMode::Completed) {
        timer_.stop();
        emit logMessage(QStringLiteral("Growth completed at %1y").arg(timeline_.currentAge(), 0, 'f', 2));
    }
}

float GrowthClock::wallSecondsToYears(float seconds) const {
    // 1 秒墙钟 = (1 / secondsPerYear) * speed 年
    return seconds * speed_ / timeline_.secondsPerYear();
}