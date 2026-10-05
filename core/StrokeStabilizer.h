/*
 * Qournal
 *
 * Smooths the input of a pen while a stroke is drawn, ported from Xournal++ (StrokeStabilizer).
 * A preprocessor (deadzone or inertia) changes the position of each event, an averaging method (arithmetic
 * mean or a velocity based gaussian weight) then averages the last events.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <deque>

#include <QPointF>

#include "StrokeBuilder.h"

struct StabilizerSettings {
    enum class Averaging { None, Arithmetic, VelocityGaussian };
    enum class Preprocessor { None, Deadzone, Inertia };

    Averaging averaging = Averaging::None;
    Preprocessor preprocessor = Preprocessor::None;

    int bufferSize = 20;           ///< number of events of the arithmetic mean
    double sigma = 0.5;            ///< width of the gaussian weight
    double deadzoneRadius = 1.3;   ///< in pixels: movements within it are ignored
    bool cuspDetection = true;     ///< keep sharp turns sharp with the deadzone
    double drag = 0.4;             ///< inertia: 0 to 1
    double mass = 5.0;             ///< inertia: larger is slower
    bool finalizeStroke = true;    ///< draw up to the last position of the pen when the stroke ends

    bool active() const { return averaging != Averaging::None || preprocessor != Preprocessor::None; }
};

class StrokeStabilizer {
public:
    /// An input event: the position in pixels (page coordinates times zoom) and the pressure
    struct Event {
        double x = 0;
        double y = 0;
        double pressure = 0;

        bool operator!=(const Event& o) const { return x != o.x || y != o.y || pressure != o.pressure; }
    };

    /**
     * @param builder receives the stabilized points; the stroke has to be started already
     * @param zoom pixels per page unit
     */
    StrokeStabilizer(const StabilizerSettings& settings, StrokeBuilder& builder, double zoom, const Event& first,
                     quint64 timestamp);

    /// @param timestamp in milliseconds
    void processEvent(const Event& event, quint64 timestamp);
    void finalizeStroke();

private:
    struct VelocityEvent: Event {
        double velocity = 0;
    };

    void drawEvent(const Event& event);
    void averageAndPaint(const Event& event, quint64 timestamp);
    void resetBuffer(const Event& event, quint64 timestamp);
    void quadraticSplineTo(const Event& event);
    void processDeadzone(const Event& event, quint64 timestamp);
    void processInertia(const Event& event, quint64 timestamp);
    Event lastEvent() const;

    StabilizerSettings m_settings;
    StrokeBuilder& m_builder;
    double m_zoom;

    Event m_lastEvent;
    Event m_lastPaintedEvent;

    // Deadzone
    Event m_lastLiveEvent;
    QPointF m_lastLiveDirection;

    // Inertia
    QPointF m_speed;

    // Averaging: the newest event is at the front
    std::deque<Event> m_arithmeticBuffer;
    std::deque<VelocityEvent> m_velocityBuffer;
    quint64 m_lastEventTimestamp = 0;
};
