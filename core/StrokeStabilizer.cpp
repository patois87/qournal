#include "StrokeStabilizer.h"

#include <cmath>
#include <limits>

using Averaging = StabilizerSettings::Averaging;
using Preprocessor = StabilizerSettings::Preprocessor;

StrokeStabilizer::StrokeStabilizer(const StabilizerSettings& settings, StrokeBuilder& builder, double zoom,
                                   const Event& first, quint64 timestamp):
        m_settings(settings), m_builder(builder), m_zoom(zoom) {
    m_lastEvent = first;
    m_lastPaintedEvent = first;
    m_lastLiveEvent = first;
    m_lastEventTimestamp = timestamp;
    if (m_settings.averaging == Averaging::Arithmetic) {
        m_arithmeticBuffer.assign(static_cast<size_t>(std::max(m_settings.bufferSize, 1)), first);
    } else if (m_settings.averaging == Averaging::VelocityGaussian) {
        m_velocityBuffer.push_front(VelocityEvent{first, 0});
    }
}

void StrokeStabilizer::drawEvent(const Event& event) {
    m_builder.paintTo(QPointF(event.x / m_zoom, event.y / m_zoom), event.pressure);
}

void StrokeStabilizer::processEvent(const Event& event, quint64 timestamp) {
    if (!m_settings.active()) {
        drawEvent(event);
        return;
    }
    switch (m_settings.preprocessor) {
        case Preprocessor::Deadzone:
            processDeadzone(event, timestamp);
            break;
        case Preprocessor::Inertia:
            processInertia(event, timestamp);
            break;
        case Preprocessor::None:
            m_lastEvent = event;
            averageAndPaint(event, timestamp);
            break;
    }
}

void StrokeStabilizer::processDeadzone(const Event& event, quint64 timestamp) {
    m_lastEvent = event;  // for the end of the stroke

    const QPointF movement(m_lastEvent.x - m_lastPaintedEvent.x, m_lastEvent.y - m_lastPaintedEvent.y);
    const double ratio = m_settings.deadzoneRadius / std::hypot(movement.x(), movement.y());
    if (ratio >= 1) {
        // Inside the deadzone: ignore the event. The buffer is flushed for sharper changes of direction
        resetBuffer(m_lastPaintedEvent, timestamp);
        return;
    }

    if (m_settings.cuspDetection && QPointF::dotProduct(movement, m_lastLiveDirection) < 0) {
        // The direction changed by more than 90°: a cusp. Draw up to its tip ...
        quadraticSplineTo(m_lastLiveEvent);

        // ... and the way back from it, through an artificial point between the last two events
        const QPointF diff(m_lastEvent.x - m_lastLiveEvent.x, m_lastEvent.y - m_lastLiveEvent.y);
        const double coeff = m_settings.deadzoneRadius / std::hypot(diff.x(), diff.y());
        m_lastLiveDirection = coeff * diff;
        m_lastPaintedEvent.x = m_lastEvent.x - m_lastLiveDirection.x();
        m_lastPaintedEvent.y = m_lastEvent.y - m_lastLiveDirection.y();
        m_lastPaintedEvent.pressure = coeff * m_lastLiveEvent.pressure + (1 - coeff) * m_lastEvent.pressure;
        drawEvent(m_lastPaintedEvent);

        m_lastLiveEvent = m_lastEvent;
        resetBuffer(m_lastPaintedEvent, timestamp);
        return;
    }

    // The drawn point follows the pen at the distance of the deadzone radius
    m_lastLiveEvent = m_lastEvent;
    m_lastLiveDirection = movement;
    averageAndPaint(Event{m_lastEvent.x - ratio * movement.x(), m_lastEvent.y - ratio * movement.y(),
                          m_lastEvent.pressure},
                    timestamp);
}

void StrokeStabilizer::processInertia(const Event& event, quint64 timestamp) {
    m_lastEvent = event;

    // The drawn point is pulled towards the pen by a spring
    const QPointF springAcceleration((m_lastEvent.x - m_lastPaintedEvent.x) / m_settings.mass,
                                     (m_lastEvent.y - m_lastPaintedEvent.y) / m_settings.mass);
    m_speed = (1 - m_settings.drag) * m_speed + springAcceleration;
    averageAndPaint(Event{m_lastPaintedEvent.x + m_speed.x(), m_lastPaintedEvent.y + m_speed.y(),
                          m_lastEvent.pressure},
                    timestamp);
}

void StrokeStabilizer::averageAndPaint(const Event& event, quint64 timestamp) {
    Event result = event;

    if (m_settings.averaging == Averaging::Arithmetic) {
        // The new event replaces the oldest one
        m_arithmeticBuffer.pop_back();
        m_arithmeticBuffer.push_front(event);
        Event sum;
        for (const Event& e: m_arithmeticBuffer) {
            sum.x += e.x;
            sum.y += e.y;
            sum.pressure += e.pressure;
        }
        const double count = static_cast<double>(m_arithmeticBuffer.size());
        result = Event{sum.x / count, sum.y / count, sum.pressure / count};
    } else if (m_settings.averaging == Averaging::VelocityGaussian) {
        if (m_velocityBuffer.empty()) {
            m_velocityBuffer.push_front(VelocityEvent{event, 0});
        } else {
            // Timestamps are in milliseconds: different events can have the same one
            const VelocityEvent& last = m_velocityBuffer.front();
            const quint64 timelaps = timestamp > m_lastEventTimestamp ? timestamp - m_lastEventTimestamp : 1;
            const double velocity = std::hypot(event.x - last.x, event.y - last.y) / static_cast<double>(timelaps);
            m_velocityBuffer.push_front(VelocityEvent{event, velocity});
        }
        m_lastEventTimestamp = timestamp;

        // The weights are those of GIMP's smoothing; the first one is always 1
        const double twoSigmaSquared = 2 * m_settings.sigma * m_settings.sigma;
        Event weightedSum;
        double sumOfWeights = 0;
        double sumOfVelocities = 0;
        auto it = m_velocityBuffer.begin();
        for (; it != m_velocityBuffer.end(); ++it) {
            const double weight = std::exp(-sumOfVelocities * sumOfVelocities / twoSigmaSquared);
            if (weight < 0.01) {
                break;
            }
            sumOfVelocities += it->velocity;
            weightedSum.x += weight * it->x;
            weightedSum.y += weight * it->y;
            weightedSum.pressure += weight * it->pressure;
            sumOfWeights += weight;
        }
        m_velocityBuffer.erase(it, m_velocityBuffer.end());
        result = Event{weightedSum.x / sumOfWeights, weightedSum.y / sumOfWeights,
                       weightedSum.pressure / sumOfWeights};
    }

    m_lastPaintedEvent = result;
    drawEvent(result);
}

void StrokeStabilizer::resetBuffer(const Event& event, quint64 timestamp) {
    if (m_settings.averaging == Averaging::Arithmetic) {
        if (m_arithmeticBuffer.back() != event) {
            m_arithmeticBuffer.assign(m_arithmeticBuffer.size(), event);
        }
    } else if (m_settings.averaging == Averaging::VelocityGaussian) {
        if (m_velocityBuffer.size() != 1 || m_lastEventTimestamp != timestamp || m_velocityBuffer.front() != event) {
            m_velocityBuffer.clear();
            m_lastEventTimestamp = timestamp;
            m_velocityBuffer.push_front(VelocityEvent{event, 0});
        }
    }
}

StrokeStabilizer::Event StrokeStabilizer::lastEvent() const {
    if (m_settings.preprocessor != Preprocessor::None) {
        return m_lastEvent;
    }
    if (m_settings.averaging == Averaging::Arithmetic) {
        return m_arithmeticBuffer.front();
    }
    if (m_settings.averaging == Averaging::VelocityGaussian && !m_velocityBuffer.empty()) {
        return m_velocityBuffer.front();
    }
    return m_lastEvent;
}

void StrokeStabilizer::finalizeStroke() {
    if (!m_settings.active() || !m_settings.finalizeStroke) {
        return;
    }
    if (m_settings.preprocessor != Preprocessor::None) {
        // Smooth the variation of the pressure a little
        const int count = m_builder.pointCount();
        if (count >= 3 && m_builder.hasPressure()) {
            m_builder.setSecondToLastPressure((m_builder.point(count - 2).z + m_builder.point(count - 3).z) / 2);
        }
    }
    quadraticSplineTo(lastEvent());
}

/// Draws a quadratic spline from the last point of the stroke to the event, tangent to the last segment
void StrokeStabilizer::quadraticSplineTo(const Event& event) {
    const int count = m_builder.pointCount();
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        drawEvent(event);
        return;
    }

    PathPoint B = m_builder.point(count - 1);
    const PathPoint A = m_builder.point(count - 2);
    const bool usePressure = m_builder.hasPressure();
    const PathPoint C(event.x / m_zoom, event.y / m_zoom,
                      usePressure ? event.pressure * m_builder.stroke().width : PathPoint::NO_PRESSURE);

    const QPointF vAB = B.pos() - A.pos();
    const QPointF vBC = C.pos() - B.pos();
    const double squaredNormBC = QPointF::dotProduct(vBC, vBC);
    const double normBC = std::sqrt(squaredNormBC);
    const double normAB = std::hypot(vAB.x(), vAB.y());
    if (normBC < std::numeric_limits<double>::epsilon()) {
        return;
    }
    if (normAB < std::numeric_limits<double>::epsilon()) {
        drawEvent(event);
        return;
    }

    // The first argument would give a symmetric segment; the second keeps the spline close to its knots
    const double distance = std::min(std::abs(squaredNormBC * normAB / (2 * QPointF::dotProduct(vAB, vBC))), normBC);

    if (usePressure) {
        const double coeff = normBC / 2 + distance;  // very rough estimate of the length of the spline
        B.z = (coeff * A.z + normAB * C.z) / (normAB + coeff);
        m_builder.setLastPressure(B.z);
    }

    // The quadratic control point, on the line AB beyond B, as two cubic control points
    const QPointF Q = B.pos() + vAB / normAB * distance;
    const QPointF fp = B.pos() + (Q - B.pos()) * (2.0 / 3.0);
    const QPointF sp = C.pos() + (Q - C.pos()) * (2.0 / 3.0);
    const SplineSegment spline{B, PathPoint(fp), PathPoint(sp), C};

    const std::vector<PathPoint> points = spline.toPointSequence(usePressure);
    for (size_t i = 1; i < points.size(); ++i) {  // B is part of the stroke already
        m_builder.drawSegmentTo(points[i]);
    }
    m_builder.drawSegmentTo(C);
}
