/*
 * Qournal
 *
 * Settings of the input: pressure curve, stroke stabilizer, snapping, shape recognizer, touch and zoom.
 * The defaults are those of Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

#include "Snapping.h"
#include "StrokeStabilizer.h"

class InputSettings: public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by PageCanvas")

    // Pressure: the width of a stroke is the width of the tool times max(minimumPressure, pressure * multiplier)
    Q_PROPERTY(double minimumPressure MEMBER minimumPressure NOTIFY changed)
    Q_PROPERTY(double pressureMultiplier MEMBER pressureMultiplier NOTIFY changed)

    Q_PROPERTY(Averaging stabilizerAveraging MEMBER stabilizerAveraging NOTIFY changed)
    Q_PROPERTY(Preprocessor stabilizerPreprocessor MEMBER stabilizerPreprocessor NOTIFY changed)
    Q_PROPERTY(int stabilizerBufferSize MEMBER stabilizerBufferSize NOTIFY changed)
    Q_PROPERTY(double stabilizerSigma MEMBER stabilizerSigma NOTIFY changed)
    Q_PROPERTY(double stabilizerDeadzoneRadius MEMBER stabilizerDeadzoneRadius NOTIFY changed)
    Q_PROPERTY(bool stabilizerCuspDetection MEMBER stabilizerCuspDetection NOTIFY changed)
    Q_PROPERTY(double stabilizerDrag MEMBER stabilizerDrag NOTIFY changed)
    Q_PROPERTY(double stabilizerMass MEMBER stabilizerMass NOTIFY changed)
    Q_PROPERTY(bool stabilizerFinalizeStroke MEMBER stabilizerFinalizeStroke NOTIFY changed)

    /// Shapes snap to the grid of the page and lines to multiples of 15°; the Alt key toggles both
    Q_PROPERTY(bool snapGrid MEMBER snapGrid NOTIFY changed)
    Q_PROPERTY(bool snapRotation MEMBER snapRotation NOTIFY changed)
    Q_PROPERTY(double snapGridSize MEMBER snapGridSize NOTIFY changed)

    /// Strokes with a smaller diagonal are not turned into shapes
    Q_PROPERTY(double recognizerMinSize MEMBER recognizerMinSize NOTIFY changed)
    /// Milliseconds until the strokes of the laser pointer start to fade
    Q_PROPERTY(int laserFadeOutTime MEMBER laserFadeOutTime NOTIFY changed)

    /// Dragging a rectangle, ellipse or coordinate system to the left acts like Shift, dragging upwards like
    /// Control. The direction counts until the pointer is drawDirModsRadius pixels away from the start
    Q_PROPERTY(bool drawDirModsEnabled MEMBER drawDirModsEnabled NOTIFY changed)
    Q_PROPERTY(int drawDirModsRadius MEMBER drawDirModsRadius NOTIFY changed)
    /// A stroke shorter than strokeFilterLength millimetres and strokeFilterTime milliseconds is a tap, not a
    /// stroke, unless the stroke before it ended less than strokeFilterSuccessive milliseconds ago (dots of an i).
    /// A tap selects what is under it if strokeFilterSelects is set
    Q_PROPERTY(bool strokeFilterEnabled MEMBER strokeFilterEnabled NOTIFY changed)
    Q_PROPERTY(int strokeFilterTime MEMBER strokeFilterTime NOTIFY changed)
    Q_PROPERTY(double strokeFilterLength MEMBER strokeFilterLength NOTIFY changed)
    Q_PROPERTY(int strokeFilterSuccessive MEMBER strokeFilterSuccessive NOTIFY changed)
    Q_PROPERTY(bool strokeFilterSelects MEMBER strokeFilterSelects NOTIFY changed)
    /// For devices without pressure: slow strokes are wider than fast ones
    Q_PROPERTY(bool pressureGuessing MEMBER pressureGuessing NOTIFY changed)
    /// Shapes the recognizer finds are moved and scaled to the grid
    Q_PROPERTY(bool snapRecognizedShapes MEMBER snapRecognizedShapes NOTIFY changed)
    /// When the square of the eraser is shown
    Q_PROPERTY(EraserVisibility eraserVisibility MEMBER eraserVisibility NOTIFY changed)

    /// Two fingers zoom; if not, they only move the view
    Q_PROPERTY(bool zoomGestures MEMBER zoomGestures NOTIFY changed)
    /// Touches are ignored while the pen is in use and for palmRejectionTime milliseconds after it
    Q_PROPERTY(bool palmRejection MEMBER palmRejection NOTIFY changed)
    Q_PROPERTY(int palmRejectionTime MEMBER palmRejectionTime NOTIFY changed)
    /// Percent by which a step of zooming in enlarges the view
    Q_PROPERTY(double zoomStep MEMBER zoomStep NOTIFY changed)
    /// Percent by which a notch of the wheel zooms while Control is held
    Q_PROPERTY(double wheelZoomStep MEMBER wheelZoomStep NOTIFY changed)
    /// The pointer where the stylus is: none, a dot, a large dot or an arrow, as in Xournal++
    Q_PROPERTY(StylusCursor stylusCursor MEMBER stylusCursor NOTIFY changed)
    /// The first events of each stroke of the stylus are left out (for tablets that send wrong ones)
    Q_PROPERTY(int ignoredStylusEvents MEMBER ignoredStylusEvents NOTIFY changed)
    /// Two fingers zoom only once their distance changed by this many percent
    Q_PROPERTY(double touchZoomThreshold MEMBER touchZoomThreshold NOTIFY changed)
    /// How close a shape has to come to the grid, or to a step of the angle, to snap to it (0 to 1)
    Q_PROPERTY(double snapGridTolerance MEMBER snapGridTolerance NOTIFY changed)
    Q_PROPERTY(double snapRotationTolerance MEMBER snapRotationTolerance NOTIFY changed)
    /// Dragging a selection to the edge of the view moves the view: speed in percent of the view per second, and
    /// how many times faster it gets the further the pointer is in the edge
    Q_PROPERTY(double edgePanSpeed MEMBER edgePanSpeed NOTIFY changed)
    Q_PROPERTY(double edgePanMaxMult MEMBER edgePanMaxMult NOTIFY changed)
    /// The Tab key puts this many spaces into a text; 0 puts a tab
    Q_PROPERTY(int tabSpaces MEMBER tabSpaces NOTIFY changed)
    /// On electronic paper strokes are drawn with hard edges, as the device draws them while the pen writes: smooth
    /// edges are gray there, which makes strokes look faint. This draws them smooth as on other screens
    Q_PROPERTY(bool einkSmoothing MEMBER einkSmoothing NOTIFY changed)
    /// On electronic paper: fills of shapes as a pattern of dots instead of a tone, which looks uneven there
    Q_PROPERTY(bool einkPatternFills MEMBER einkPatternFills NOTIFY changed)
    /// What an input device is used as, by its name: values of PageCanvas::DeviceClass. Devices that are not
    /// here are used as what they report themselves to be
    Q_PROPERTY(QVariantMap deviceClasses MEMBER deviceClasses NOTIFY changed)

public:
    enum Averaging { NoAveraging, Arithmetic, VelocityGaussian };
    Q_ENUM(Averaging)
    enum Preprocessor { NoPreprocessor, Deadzone, Inertia };
    Q_ENUM(Preprocessor)

    enum EraserVisibility { EraserNever, EraserAlways, EraserHover, EraserTouch };
    Q_ENUM(EraserVisibility)
    enum StylusCursor { StylusCursorNone, StylusCursorDot, StylusCursorBig, StylusCursorArrow };
    Q_ENUM(StylusCursor)

    using QObject::QObject;

    StabilizerSettings stabilizer() const {
        StabilizerSettings s;
        s.averaging = static_cast<StabilizerSettings::Averaging>(stabilizerAveraging);
        s.preprocessor = static_cast<StabilizerSettings::Preprocessor>(stabilizerPreprocessor);
        s.bufferSize = stabilizerBufferSize;
        s.sigma = stabilizerSigma;
        s.deadzoneRadius = stabilizerDeadzoneRadius;
        s.cuspDetection = stabilizerCuspDetection;
        s.drag = stabilizerDrag;
        s.mass = stabilizerMass;
        s.finalizeStroke = stabilizerFinalizeStroke;
        return s;
    }

    SnapSettings snapping() const {
        SnapSettings s;
        s.grid = snapGrid;
        s.rotation = snapRotation;
        s.gridSize = snapGridSize;
        s.gridTolerance = snapGridTolerance;
        s.rotationTolerance = snapRotationTolerance;
        return s;
    }

    double minimumPressure = 0.05;
    double pressureMultiplier = 1.0;

    Averaging stabilizerAveraging = NoAveraging;
    Preprocessor stabilizerPreprocessor = NoPreprocessor;
    int stabilizerBufferSize = 20;
    double stabilizerSigma = 0.5;
    double stabilizerDeadzoneRadius = 1.3;
    bool stabilizerCuspDetection = true;
    double stabilizerDrag = 0.4;
    double stabilizerMass = 5.0;
    bool stabilizerFinalizeStroke = true;

    bool snapGrid = true;
    bool snapRotation = true;
    double snapGridSize = 14.17;

    double recognizerMinSize = 40.0;
    int laserFadeOutTime = 500;

    bool drawDirModsEnabled = false;
    int drawDirModsRadius = 50;
    bool strokeFilterEnabled = false;
    int strokeFilterTime = 150;
    double strokeFilterLength = 1.0;
    int strokeFilterSuccessive = 500;
    bool strokeFilterSelects = true;
    bool pressureGuessing = false;
    bool snapRecognizedShapes = false;
    QVariantMap deviceClasses;
    StylusCursor stylusCursor = StylusCursorDot;
    int ignoredStylusEvents = 0;
    double touchZoomThreshold = 0;
    double snapGridTolerance = 0.5;
    double snapRotationTolerance = 0.3;
    int tabSpaces = 0;
    bool einkSmoothing = false;
    bool einkPatternFills = true;
    double edgePanSpeed = 20;
    double edgePanMaxMult = 5;
    EraserVisibility eraserVisibility = EraserAlways;

    bool zoomGestures = true;
    bool palmRejection = true;
    int palmRejectionTime = 500;
    double zoomStep = 25.0;
    double wheelZoomStep = 20.0;

signals:
    void changed();
};
