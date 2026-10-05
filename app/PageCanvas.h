/*
 * Qournal
 *
 * Qt Quick item showing the document and handling pen, touch and mouse input
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QInputDevice>
#include <QLineF>
#include <QList>
#include <QMarginsF>
#include <QMatrix4x4>
#include <QPointer>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include <QtQml/qqmlregistration.h>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

#include "Document.h"
#include "Export.h"
#include "GeometryTool.h"
#include "InputSettings.h"
#include "PageLayout.h"
#include "Selection.h"
#include "Shapes.h"
#include "StrokeBuilder.h"
#include "StrokeStabilizer.h"

class QKeyEvent;
class QHoverEvent;
class QTabletEvent;

class QQuickPaintedItem;
class QSGNode;

class PageCanvas: public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    // The following properties belong to the selected tool: each tool remembers its own
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY toolChanged)
    Q_PROPERTY(ToolSize toolSize READ toolSize WRITE setToolSize NOTIFY toolChanged)
    Q_PROPERTY(double thickness READ thickness NOTIFY toolChanged)
    Q_PROPERTY(DrawingType drawingType READ drawingType WRITE setDrawingType NOTIFY toolChanged)
    Q_PROPERTY(bool fill READ fill WRITE setFill NOTIFY toolChanged)
    Q_PROPERTY(int fillAlpha READ fillAlpha WRITE setFillAlpha NOTIFY toolChanged)
    /// Opacity of the marker that highlights selected text of the PDF, 1 to 255
    Q_PROPERTY(int pdfMarkerAlpha READ pdfMarkerAlpha WRITE setPdfMarkerAlpha NOTIFY toolChanged)
    Q_PROPERTY(QString lineStyle READ lineStyle WRITE setLineStyle NOTIFY toolChanged)
    Q_PROPERTY(EraserType eraserType READ eraserType WRITE setEraserType NOTIFY toolChanged)
    Q_PROPERTY(GeometryToolType geometryTool READ geometryTool WRITE setGeometryTool NOTIFY geometryToolChanged)
    Q_PROPERTY(InputSettings* input READ input CONSTANT)
    // Texts and links: what the text tool writes with, and what selected or edited texts are given
    Q_PROPERTY(QString textFamily READ textFamily WRITE setTextFamily NOTIFY textStyleChanged)
    Q_PROPERTY(bool textBold READ textBold WRITE setTextBold NOTIFY textStyleChanged)
    Q_PROPERTY(bool textItalic READ textItalic WRITE setTextItalic NOTIFY textStyleChanged)
    Q_PROPERTY(double textSize READ textSize WRITE setTextSize NOTIFY textStyleChanged)
    Q_PROPERTY(QString textAlign READ textAlign WRITE setTextAlign NOTIFY textStyleChanged)
    /// Wrapped lines are stretched to the full width
    Q_PROPERTY(bool textJustify READ textJustify WRITE setTextJustify NOTIFY textStyleChanged)
    // The text that is being edited: an editor on top of the canvas shows it, see TextEditor.qml
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditChanged)
    Q_PROPERTY(QString textEditText READ textEditText WRITE setTextEditText NOTIFY textEditChanged)
    Q_PROPERTY(QFont textEditFont READ textEditFont NOTIFY textEditChanged)
    Q_PROPERTY(QColor textEditColor READ textEditColor NOTIFY textEditChanged)
    Q_PROPERTY(QString textEditAlign READ textEditAlign NOTIFY textEditChanged)
    Q_PROPERTY(bool textEditJustify READ textEditJustify NOTIFY textEditChanged)
    Q_PROPERTY(double textEditWrap READ textEditWrap NOTIFY textEditChanged)
    Q_PROPERTY(double textEditLineHeight READ textEditLineHeight NOTIFY textEditChanged)
    Q_PROPERTY(QMatrix4x4 textEditMatrix READ textEditMatrix NOTIFY textEditMatrixChanged)
    // The layers of the current page. The list has an entry with "name" and "visible" for each layer, from the
    // bottom one up
    Q_PROPERTY(QVariantList layers READ layers NOTIFY layersChanged)
    Q_PROPERTY(int currentLayer READ currentLayer WRITE setCurrentLayer NOTIFY layersChanged)
    Q_PROPERTY(bool backgroundVisible READ backgroundVisible WRITE setBackgroundVisible NOTIFY layersChanged)
    // Text selected in the PDF of the background
    Q_PROPERTY(bool hasPdfSelection READ hasPdfSelection NOTIFY pdfSelectionChanged)
    Q_PROPERTY(QString pdfSelectionText READ pdfSelectionText NOTIFY pdfSelectionChanged)
    Q_PROPERTY(QRectF pdfSelectionRect READ pdfSelectionRect NOTIFY pdfSelectionRectChanged)
    /// The outline of the PDF: entries with "title", "level" and "page" (of the PDF, 0-based)
    Q_PROPERTY(QVariantList pdfOutline READ pdfOutline NOTIFY documentChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    /// Number of results on the page of the current result, and the place of the current one among them (from 1)
    Q_PROPERTY(int searchResultCount READ searchResultCount NOTIFY searchChanged)
    Q_PROPERTY(int searchResultIndex READ searchResultIndex NOTIFY searchChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool selectAllLayers READ selectAllLayers WRITE setSelectAllLayers NOTIFY toolChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(bool usePressure READ usePressure WRITE setUsePressure NOTIFY usePressureChanged)
    Q_PROPERTY(bool fingerDraws READ fingerDraws WRITE setFingerDraws NOTIFY fingerDrawsChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY currentPageChanged)
    Q_PROPERTY(int pdfPageCount READ pdfPageCount NOTIFY documentChanged)
    Q_PROPERTY(bool pairedPages READ pairedPages WRITE setPairedPages NOTIFY layoutChanged)
    /// Pages before the first pair: 1 shows the first page alone, like the cover of a book
    Q_PROPERTY(int pairedPagesOffset READ pairedPagesOffset WRITE setPairedPagesOffset NOTIFY layoutChanged)
    // The places that were left by a jump (go to page, a link, the outline, a search result)
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY navigationChanged)
    Q_PROPERTY(bool canNavigateForward READ canNavigateForward NOTIFY navigationChanged)
    /// A circle shows where the pointer is, for an audience
    Q_PROPERTY(bool highlightPosition READ highlightPosition WRITE setHighlightPosition NOTIFY highlightPositionChanged)
    Q_PROPERTY(int layoutColumns READ layoutColumns WRITE setLayoutColumns NOTIFY layoutChanged)
    Q_PROPERTY(int layoutRows READ layoutRows WRITE setLayoutRows NOTIFY layoutChanged)
    Q_PROPERTY(bool layoutVertical READ layoutVertical WRITE setLayoutVertical NOTIFY layoutChanged)
    Q_PROPERTY(bool layoutRightToLeft READ layoutRightToLeft WRITE setLayoutRightToLeft NOTIFY layoutChanged)
    Q_PROPERTY(bool layoutBottomToTop READ layoutBottomToTop WRITE setLayoutBottomToTop NOTIFY layoutChanged)
    Q_PROPERTY(bool presentationMode READ presentationMode WRITE setPresentationMode NOTIFY presentationModeChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderingChanged)
    Q_PROPERTY(QString title READ title NOTIFY documentChanged)
    Q_PROPERTY(bool hasFile READ hasFile NOTIFY documentChanged)
    /// The .xopp file of the document, empty if it has none yet
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    // Changes are written to an autosave file from time to time, which is removed when the document is saved
    Q_PROPERTY(bool autosaveEnabled READ autosaveEnabled WRITE setAutosaveEnabled NOTIFY autosaveChanged)
    /// Minutes between two autosaves
    Q_PROPERTY(int autosaveInterval READ autosaveInterval WRITE setAutosaveInterval NOTIFY autosaveChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    /// The audio file that is being recorded, as it is noted in the document (see AudioController); empty if none.
    /// Strokes of the pen and texts made meanwhile remember the file and the time, and can be played back
    Q_PROPERTY(QString audioRecording READ audioRecording WRITE setAudioRecording NOTIFY audioRecordingChanged)
    /// The colour around the pages
    Q_PROPERTY(QColor canvasColor READ canvasColor WRITE setCanvasColor NOTIFY canvasColorChanged)
    /// For electronic paper: white around the pages with a thin frame around each, and no pointer of the stylus
    /// (gray areas are dithered there, and what moves leaves traces)
    Q_PROPERTY(bool einkMode READ einkMode WRITE setEinkMode NOTIFY canvasColorChanged)

    // The view, as the settings of Xournal++ have it
    /// The visible part of what can be scrolled through, as fractions of it (x, y, width, height): for scroll bars
    Q_PROPERTY(QRectF scrollView READ scrollView NOTIFY scrollChanged)
    /// Space around the pages, in points: above the first, below the last, left and right of them
    Q_PROPERTY(double spaceAbove READ spaceAbove WRITE setSpaceAbove NOTIFY viewSettingsChanged)
    Q_PROPERTY(double spaceBelow READ spaceBelow WRITE setSpaceBelow NOTIFY viewSettingsChanged)
    Q_PROPERTY(double spaceLeft READ spaceLeft WRITE setSpaceLeft NOTIFY viewSettingsChanged)
    Q_PROPERTY(double spaceRight READ spaceRight WRITE setSpaceRight NOTIFY viewSettingsChanged)
    /// The pages can be scrolled to any place in the view, also beyond their edges
    Q_PROPERTY(bool unlimitedScrolling READ unlimitedScrolling WRITE setUnlimitedScrolling NOTIFY viewSettingsChanged)
    /// Resolution of the screen in pixels per inch: at the zoom of 100 % a page has its real size then. 72 makes a
    /// point a pixel
    Q_PROPERTY(double displayDpi READ displayDpi WRITE setDisplayDpi NOTIFY viewSettingsChanged)
    /// When a page is appended by itself
    Q_PROPERTY(AppendPage appendPage READ appendPage WRITE setAppendPage NOTIFY viewSettingsChanged)
    /// Colour of the selection and its handles
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY viewSettingsChanged)
    /// How the highlighted position of the pointer looks
    Q_PROPERTY(QColor positionColor READ positionColor WRITE setPositionColor NOTIFY viewSettingsChanged)
    Q_PROPERTY(double positionRadius READ positionRadius WRITE setPositionRadius NOTIFY viewSettingsChanged)
    Q_PROPERTY(
            QColor positionBorderColor READ positionBorderColor WRITE setPositionBorderColor NOTIFY viewSettingsChanged)
    Q_PROPERTY(
            double positionBorderWidth READ positionBorderWidth WRITE setPositionBorderWidth NOTIFY viewSettingsChanged)
    /// Size and background of the page of new documents, with the keys of pageProperties(); empty for plain A4
    Q_PROPERTY(QVariantMap pageTemplate READ pageTemplate WRITE setPageTemplate NOTIFY pageTemplateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString inputInfo READ inputInfo NOTIFY inputInfoChanged)

public:
    enum Tool {
        Pen,
        Highlighter,
        Eraser,
        Hand,
        VerticalSpace,
        LaserPen,
        LaserHighlighter,
        SelectRect,
        SelectRegion,
        SelectObject,
        Text,
        Link,
        Latex,
        SelectPdfTextLinear,
        SelectPdfTextRect,
        PlayObject,  ///< plays the recording of the stroke or text that is tapped
        Image        ///< asks for an image where the page is tapped, see imageRequested()
    };
    Q_ENUM(Tool)
    /// What the pen and the highlighter draw
    enum DrawingType {
        Freehand,
        Line,
        Rectangle,
        Ellipse,
        Arrow,
        DoubleArrow,
        CoordinateSystem,
        Spline,
        ShapeRecognizer
    };
    Q_ENUM(DrawingType)
    enum ToolSize { VeryFine, Fine, Medium, Thick, VeryThick };
    Q_ENUM(ToolSize)
    enum EraserType {
        EraseStandard,  ///< removes the part of a stroke under the eraser
        EraseWhiteout,  ///< paints white
        EraseStrokes    ///< removes whole strokes
    };
    Q_ENUM(EraserType)
    enum GeometryToolType { NoGeometryTool, Setsquare, Compass };
    Q_ENUM(GeometryToolType)
    /// Changes of the order of the selected elements within their layer
    enum OrderChange { BringToFront, BringForward, SendBackward, SendToBack };
    Q_ENUM(OrderChange)

    /// What can be given a tool of its own, which is used as long as it is pressed
    enum Button {
        ButtonEraserTip,
        ButtonStylus1,
        ButtonStylus2,
        ButtonMouseMiddle,
        ButtonMouseRight,
        ButtonMouse4,  ///< "back"
        ButtonMouse5   ///< "forward"
    };
    Q_ENUM(Button)

    /// What an input device is used as, as in Xournal++ (the numbers are those of its settings)
    enum DeviceClass {
        DeviceAutomatic = -1,  ///< what the device reports itself to be
        DeviceDisabled = 0,
        DeviceMouse = 1,
        DevicePen = 2,
        DeviceEraser = 3,
        DeviceTouchscreen = 4,
    };
    Q_ENUM(DeviceClass)
    /// What a button does, besides the values of Tool
    enum ButtonAction {
        ButtonNoAction = -1,         ///< the selected tool is used
        ButtonFloatingToolbox = 100  ///< floatingToolboxRequested() is emitted
    };
    Q_ENUM(ButtonAction)
    static constexpr int BUTTON_COUNT = 7;
    static constexpr int TOOL_COUNT = 17;

    explicit PageCanvas(QQuickItem* parent = nullptr);
    ~PageCanvas() override;

    /// What is painted on top of the tiles: the stroke in progress, the selection, the aids of the tools
    void paintOverlays(QPainter* painter);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    QColor color() const { return toolState().color; }
    void setColor(const QColor& color);
    ToolSize toolSize() const { return toolState().size; }
    void setToolSize(ToolSize size);
    /// Width of the strokes of the selected tool in points; for the eraser half the side of its square
    double thickness() const { return thickness(m_tool); }
    DrawingType drawingType() const { return toolState().drawingType; }
    void setDrawingType(DrawingType type);
    bool fill() const { return toolState().fill; }
    void setFill(bool fill);
    /// Opacity of the filling, 1 to 255
    int fillAlpha() const { return toolState().fillAlpha; }
    void setFillAlpha(int alpha);
    int pdfMarkerAlpha() const { return m_pdfMarkerAlpha; }
    void setPdfMarkerAlpha(int alpha);
    /// "plain", "dash", "dashdot" or "dot"
    QString lineStyle() const { return toolState().lineStyle; }
    void setLineStyle(const QString& style);
    EraserType eraserType() const { return m_eraserType; }
    void setEraserType(EraserType type);
    /// The setsquare or compass shown on the current page
    GeometryToolType geometryTool() const { return m_geometryToolType; }
    void setGeometryTool(GeometryToolType type);
    InputSettings* input() { return &m_input; }

    QString textFamily() const;
    void setTextFamily(const QString& family);
    bool textBold() const;
    void setTextBold(bool bold);
    bool textItalic() const;
    void setTextItalic(bool italic);
    double textSize() const { return m_textSize; }
    void setTextSize(double size);
    /// "left", "center" or "right"
    QString textAlign() const { return m_textAlign; }
    bool textJustify() const { return m_textJustify; }
    void setTextJustify(bool justify);
    void setTextAlign(const QString& align);

    bool textEditing() const { return m_textEdit.active; }
    QString textEditText() const { return m_textEdit.element.text; }
    /// The editor reports every change, so that the text can be kept whenever the editing ends
    void setTextEditText(const QString& text);
    /// The font of the edited text at the size the editor lays it out with; the matrix scales it
    QFont textEditFont() const;
    QColor textEditColor() const { return m_textEdit.element.color; }
    bool textEditJustify() const { return m_textEdit.element.justify; }
    QString textEditAlign() const {
        return m_textEdit.element.align.isEmpty() ? QStringLiteral("left") : m_textEdit.element.align;
    }
    /// Width at which the editor wraps the lines, in its own units; negative for no wrapping
    double textEditWrap() const;
    double textEditLineHeight() const;
    /// From the coordinates of the editor to the item: the place, size and rotation of the text in the view
    QMatrix4x4 textEditMatrix() const;
    /// The area of the item the editor of the text covers
    QRectF textEditViewRect() const;
    /// Ends the editing and keeps the text. A text without content is removed
    Q_INVOKABLE void finishTextEdit();
    /// Sets the width at which the edited text wraps, in the units of the editor; negative for no wrapping
    Q_INVOKABLE void setTextEditWrap(double width);
    /// Gives the lines of the editor's document the distance the text will have on the page
    Q_INVOKABLE void prepareTextDocument(QQuickTextDocument* document) const;

    /// Inserts an image file into the current page, in the middle of the view or at a position of the item
    Q_INVOKABLE bool insertImage(const QUrl& url);
    Q_INVOKABLE bool insertImageAt(const QUrl& url, const QPointF& viewPos);
    /// Inserts a picture that is not a file (a region of the screen) in the middle of the view, as a PNG. A
    /// picture of a screen with more pixels than points (its devicePixelRatio()) gets the size it had there
    Q_INVOKABLE bool insertPicture(const QImage& image);
    /// Inserts a text at a position of the item, e.g. one that was dropped there
    Q_INVOKABLE void insertTextAt(const QString& text, const QPointF& viewPos);

    /// Creates or changes the link that linkRequested() asked for. Without text the URL is shown
    Q_INVOKABLE void applyLink(const QString& text, const QString& url);
    Q_INVOKABLE void removeLink();
    /// Creates or changes the formula that latexRequested() asked for, with the PDF LaTeX made of the source
    Q_INVOKABLE void applyLatex(const QString& source, const QByteArray& pdf);

    QVariantList layers() const;
    /// The layer of the current page that is drawn on (0 is the bottom one)
    int currentLayer() const;
    void setCurrentLayer(int layer);
    bool backgroundVisible() const;
    void setBackgroundVisible(bool visible);
    /// Adds an empty layer above the current one and draws on it
    /// Adds a layer above the current one, or below it
    Q_INVOKABLE void addLayer(bool below = false);
    /// Shows or hides all layers of the current page
    Q_INVOKABLE void setAllLayersVisible(bool visible);
    /// Deletes a layer of the current page. The last remaining layer is not deleted
    Q_INVOKABLE void deleteLayer(int layer);
    Q_INVOKABLE void duplicateLayer(int layer);
    Q_INVOKABLE void renameLayer(int layer, const QString& name);
    Q_INVOKABLE void moveLayer(int from, int to);
    /// Puts the elements of a layer on top of the layer below it and removes the layer
    Q_INVOKABLE void mergeLayerDown(int layer);
    Q_INVOKABLE void setLayerVisible(int layer, bool visible);
    /// Moves the selected elements to another layer of their page
    Q_INVOKABLE void moveSelectionToLayer(int layer);

    bool hasPdfSelection() const { return m_pdfSelection.active && !m_pdfSelection.bounds.isEmpty(); }
    QString pdfSelectionText() const { return m_pdfSelection.text; }
    /// The area of the item the selected text covers
    QRectF pdfSelectionRect() const;
    Q_INVOKABLE void clearPdfSelection();
    Q_INVOKABLE void copyPdfSelection();
    /// Marks the selected text with strokes on the current layer
    Q_INVOKABLE void highlightPdfSelection();
    Q_INVOKABLE void underlinePdfSelection();
    Q_INVOKABLE void strikeThroughPdfSelection();

    QVariantList pdfOutline() const;
    /// Shows the page that has a page of the PDF as background
    Q_INVOKABLE bool goToPdfPage(int pdfPage);

    /**
     * Searches the texts and the PDF from the current page on and shows the first result.
     * @return false if the text is nowhere in the document
     */
    Q_INVOKABLE bool search(const QString& text);
    Q_INVOKABLE bool searchNext();
    Q_INVOKABLE bool searchPrevious();
    Q_INVOKABLE void clearSearch();
    bool searching() const { return !m_search.text.isEmpty(); }
    int searchResultCount() const;
    int searchResultIndex() const { return m_search.index + 1; }

    bool hasSelection() const { return m_selection.active; }
    /// The selection tools look for elements on all layers, from the top one down, instead of on the current one
    bool selectAllLayers() const { return m_selectAllLayers; }
    void setSelectAllLayers(bool all);
    /// Whether the clipboard holds something that can be pasted: elements, an image or text
    bool canPaste() const;
    bool usePressure() const { return m_usePressure; }
    void setUsePressure(bool use);
    bool fingerDraws() const { return m_fingerDraws; }
    void setFingerDraws(bool draws);

    /// 1 is 100 %: a point is displayDpi / 72 pixels
    double zoom() const { return m_scale * 72.0 / m_displayDpi; }
    int pageCount() const { return static_cast<int>(m_doc.pages.size()); }
    /// The page most of the view shows, or the page that was selected last (0-based)
    int currentPage() const { return m_currentPage; }
    /// Selects a page and scrolls to it
    void setCurrentPage(int page);
    /// Number of pages of the background PDF, 0 if there is none
    int pdfPageCount() const;

    bool pairedPages() const { return m_layoutSettings.pairedPages; }
    void setPairedPages(bool paired);
    int pairedPagesOffset() const { return m_layoutSettings.pairsOffset; }
    void setPairedPagesOffset(int offset);
    bool canNavigateBack() const { return !m_placesBack.isEmpty(); }
    bool canNavigateForward() const { return !m_placesForward.isEmpty(); }
    /// The next page after the current one that has elements; -1 if there is none
    Q_INVOKABLE int nextAnnotatedPage() const;
    /**
     * The audio recordings the elements of the document refer to, in the order they were first used: maps with
     * file (as stored), elements (how many refer to it) and marks (list of maps with page, 0-based, and timestamp
     * in milliseconds: where on each page the recording was first referred to, in the order of the recording)
     */
    Q_INVOKABLE QVariantList recordings() const;
    /// The previous page before the current one that has elements; -1 if there is none
    Q_INVOKABLE int previousAnnotatedPage() const;
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    bool highlightPosition() const { return m_highlightPosition; }
    void setHighlightPosition(bool highlight);
    /// Adds a page at the end for every page of the PDF that no page has as background. @return how many
    Q_INVOKABLE int appendNewPdfPages();
    /// Makes an image the background of a page, which takes the size of the image
    Q_INVOKABLE bool setPageBackgroundImage(int page, const QUrl& url);
    /// The fixed number of columns, 0 if the number of rows is fixed instead
    int layoutColumns() const { return m_layoutSettings.fixedRows ? 0 : m_layoutSettings.columns; }
    void setLayoutColumns(int columns);
    /// The fixed number of rows, 0 if the number of columns is fixed instead
    int layoutRows() const { return m_layoutSettings.fixedRows ? m_layoutSettings.rows : 0; }
    void setLayoutRows(int rows);
    bool layoutVertical() const { return m_layoutSettings.vertical; }
    void setLayoutVertical(bool vertical);
    bool layoutRightToLeft() const { return m_layoutSettings.rightToLeft; }
    void setLayoutRightToLeft(bool rightToLeft);
    bool layoutBottomToTop() const { return m_layoutSettings.bottomToTop; }
    void setLayoutBottomToTop(bool bottomToTop);
    /// Shows one page at a time, fitted into the view
    bool presentationMode() const { return m_presentationMode; }
    void setPresentationMode(bool presentation);
    /// Whether parts of the view are still being rendered
    bool rendering() const { return m_rendering; }

    QString title() const { return m_title; }
    bool hasFile() const { return !m_filePath.isEmpty(); }
    QString filePath() const { return m_filePath; }

    bool autosaveEnabled() const { return m_autosaveEnabled; }
    void setAutosaveEnabled(bool enabled);
    int autosaveInterval() const { return m_autosaveMinutes; }
    void setAutosaveInterval(int minutes);
    /// Where the autosave of this document goes: next to its file, or to the cache for a document without file
    QString autosavePath() const;
    /// Writes the autosave file whatever the state: for the moment the application crashes
    void emergencySave();
    /// Writes the autosave file if there are changes since the last one
    Q_INVOKABLE void autosaveNow();
    /// @return the autosave file of a document if it is newer than the document, else nothing
    Q_INVOKABLE QString autosaveFor(const QUrl& url) const;
    /**
     * The document next to a PDF file that annotates it, as Xournal++ looks for it: "name.pdf.xopp", "name.xopp"
     * (and .xoj). Empty if there is none
     */
    Q_INVOKABLE QUrl annotationFileFor(const QUrl& pdf) const;
    /**
     * A file name made from a pattern, as Xournal++ makes the names it suggests: the fields of strftime (%Y, %m,
     * %d, %H, %M, %S, %F, %T, ...) and %{name}, the name of the PDF or of the document
     */
    Q_INVOKABLE QString nameFromPattern(const QString& pattern) const;
    /// The autosave files of documents that were never saved, the newest first
    Q_INVOKABLE QStringList orphanAutosaves() const;
    /// Opens an autosave file in place of its document (an empty URL for a document without file)
    Q_INVOKABLE bool recoverAutosave(const QString& autosave, const QUrl& original);
    Q_INVOKABLE void discardAutosave(const QString& autosave);

    /**
     * Exports the document as PDF, PNG or SVG, by the extension of the file. Options: "pages" and "layers"
     * (ranges like "1-3,5", empty for all), "background" ("all", "noRuling" or "none"), "progressive" (one page
     * per layer), "dpi", "width", "height" (of PNG files), "format" ("pdf", "png", "svg") if the file has no
     * extension that tells it.
     */
    Q_INVOKABLE bool exportDocument(const QUrl& url, const QVariantMap& options = {});
    /// Whether SVG files can be written and whether documents can be printed with this build
    Q_INVOKABLE bool canExportSvg() const;
    Q_INVOKABLE bool canPrint() const;
    Q_INVOKABLE QStringList printers() const;
    /**
     * Prints the document, every page on a sheet of its size.
     * @param printer name of the printer; the default printer if empty
     * @param pages range like "1-3,5", empty for all
     * @param outputFile prints into a PDF file instead, if given
     */
    Q_INVOKABLE bool print(const QString& printer, const QString& pages = {}, const QString& outputFile = {});
    /**
     * Prints with more choices: "printer", "pages" and "outputFile" as for print(), "copies", "duplex" ("none",
     * "longSide" or "shortSide"), "grayscale", and "paper": "page" for sheets of the size of the pages, "printer"
     * for the paper of the printer, on which the pages are fitted and centred.
     */
    Q_INVOKABLE bool printWith(const QVariantMap& choices);
    QString audioRecording() const { return m_audioFile; }
    void setAudioRecording(const QString& filename);
    QColor canvasColor() const { return m_canvasColor; }
    bool einkMode() const { return m_einkMode; }
    void setEinkMode(bool eink);
    void setCanvasColor(const QColor& color);

    /// When a page is appended by itself, as in Xournal++ ("emptyLastPageAppend")
    enum AppendPage { AppendNever, AppendWhenWritten, AppendWhenScrolledToEnd };
    Q_ENUM(AppendPage)

    QRectF scrollView() const;
    /// Scrolls so that the view starts at a fraction of what can be scrolled through
    Q_INVOKABLE void scrollToFraction(double x, double y);
    double spaceAbove() const { return m_space.top(); }
    double spaceBelow() const { return m_space.bottom(); }
    double spaceLeft() const { return m_space.left(); }
    double spaceRight() const { return m_space.right(); }
    void setSpaceAbove(double space);
    void setSpaceBelow(double space);
    void setSpaceLeft(double space);
    void setSpaceRight(double space);
    bool unlimitedScrolling() const { return m_unlimitedScrolling; }
    void setUnlimitedScrolling(bool unlimited);
    double displayDpi() const { return m_displayDpi; }
    void setDisplayDpi(double dpi);
    AppendPage appendPage() const { return m_appendPage; }
    void setAppendPage(AppendPage mode);
    QColor selectionColor() const { return m_selectionColor; }
    void setSelectionColor(const QColor& color);
    QColor positionColor() const { return m_positionColor; }
    void setPositionColor(const QColor& color);
    double positionRadius() const { return m_positionRadius; }
    void setPositionRadius(double radius);
    QColor positionBorderColor() const { return m_positionBorderColor; }
    void setPositionBorderColor(const QColor& color);
    double positionBorderWidth() const { return m_positionBorderWidth; }
    void setPositionBorderWidth(double width);
    QVariantMap pageTemplate() const { return m_pageTemplate; }
    void setPageTemplate(const QVariantMap& pageTemplate);

    /// The action of a button: a value of Tool or of ButtonAction
    Q_INVOKABLE int buttonAction(Button button) const { return m_buttonActions[static_cast<size_t>(button)]; }
    Q_INVOKABLE void setButtonAction(Button button, int action);
    /**
     * The input devices: those Qt knows of and those that were used. Maps with name, type (a text), deviceClass
     * (the one chosen, DeviceAutomatic if none) and automaticClass (what it is used as without a choice)
     */
    Q_INVOKABLE QVariantList inputDevices() const;
    Q_INVOKABLE void setDeviceClass(const QString& name, int deviceClass);
    /// The Apple Pencil was tapped twice: what to do, as Platform::PencilTap
    void pencilTapped(int action);
    /**
     * What the tool of a button differs in from the tool as it is set: "drawingType" and "size" (indices, -1 for
     * no difference) and "color" (invalid for no difference). They apply while the button is pressed.
     */
    Q_INVOKABLE QVariantMap buttonOptions(Button button) const;
    Q_INVOKABLE void setButtonOptions(Button button, const QVariantMap& options);

    /// Reads the settings that were stored by saveSettings(): the tools, the input, the layout, the buttons
    Q_INVOKABLE void loadSettings();
    Q_INVOKABLE void saveSettings() const;
    /// Back to the settings the application starts with the first time
    Q_INVOKABLE void resetSettings();

    /**
     * Whether a pen that is used outside of the pages (on a toolbar, a menu, a dialog) is turned into a mouse here.
     * Qt does that itself on the desktop, but not on Android, where a pen could not press a button; so it is on
     * there and off elsewhere
     */
    void setPenClicksAsMouse(bool enabled) { m_penClicksAsMouse = enabled; }

    bool modified() const { return m_modified; }
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString inputInfo() const { return m_inputInfo; }

    /// Opens a .xopp/.xoj file, or a PDF file to annotate
    Q_INVOKABLE void openFile(const QUrl& url);
    /// Saves to the file the document was loaded from. Fails for documents without a file: use saveAs()
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& url);
    /// What saving for Xournal++ 1.3.8 and earlier changes in the document, a line for each kind of change
    Q_INVOKABLE QStringList changesForXournalpp13() const;
    /**
     * Saves a copy in format 4, which Xournal++ 1.3.8 and earlier can read (see XoppCompat). The document stays
     * what it is, with its file.
     */
    Q_INVOKABLE bool saveForXournalpp13(const QUrl& url);
    Q_INVOKABLE void newDocument();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void zoomBy(double factor);
    /// Zoom by the step of the settings
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    /// Sets the zoom: 1.0 is 100 %
    Q_INVOKABLE void zoomTo(double zoom);
    Q_INVOKABLE void fitWidth();
    /// Fits the current page into the view
    Q_INVOKABLE void fitPage();

    /// Inserts an empty page at index (0 to pageCount). It gets the format of the page before it
    Q_INVOKABLE void insertPage(int index);
    /// Deletes a page. The last remaining page is not deleted
    Q_INVOKABLE void deletePage(int page);
    /// Inserts a copy of a page after it
    Q_INVOKABLE void duplicatePage(int page);
    Q_INVOKABLE void movePage(int from, int to);

    Q_INVOKABLE QSizeF pageSize(int page) const;
    /// Position of a page in the item, in the current zoom and scroll position
    Q_INVOKABLE QRectF pageViewRect(int page) const;
    /**
     * Size and background of a page: width, height (points), type ("solid", "pdf" or "pixmap"), and depending on
     * the type color, style, config (ruling as in the .xopp format) or pdfPage (1-based)
     */
    Q_INVOKABLE QVariantMap pageProperties(int page) const;
    /// Changes the properties given in the map, see pageProperties(). The type "pixmap" cannot be set
    Q_INVOKABLE void setPageProperties(int page, const QVariantMap& properties, bool allPages = false);
    /// The predefined rulings: name, style, config
    Q_INVOKABLE QVariantList pageTypes() const;

    const Document& document() const { return m_doc; }

    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void arrangeSelection(OrderChange change);
    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy();
    /// Pastes elements copied here, or an image or text of another application, into the middle of the view
    Q_INVOKABLE void paste();

    // For scripts (plugins): what the user interface does not need
    /// The place of an element in the document
    struct ElementRef {
        int page;
        int layer;
        size_t index;
    };
    /// Adds elements on top of the active layer of the current page. @param grouped undone in one step
    std::vector<ElementRef> addElements(const std::vector<Element>& elements, bool grouped = true);
    /// The selected elements. @return false if nothing is selected
    bool selectedElements(int& page, int& layer, std::vector<size_t>& indices) const;
    /// The frame of the selection on its page, and its rotation in radians
    QRectF selectionRect() const { return m_selection.rect; }
    double selectionRotation() const { return m_selection.rotation; }
    /// Selects elements of the active layer of the current page, in addition to those already selected there
    void addToSelection(const std::vector<size_t>& indices);
    /// Top left corner of the view in pixels, counted from the top left corner of all pages
    QPointF scrollPosition() const;
    void scrollTo(const QPointF& position);
    /// The label a page of the background PDF has in the PDF ("iv", "12"), empty if there is none
    QString pdfPageLabel(int pdfPage) const;
    QString pdfPath() const { return m_doc.pdfPath; }
    void setBackgroundName(const QString& name);
    /// The settings of a tool: color, size (index), thickness, drawingType (index), fill, fillAlpha, lineStyle
    QVariantMap toolInfo(Tool tool) const;
    /// Changes settings of a tool, which need not be the selected one: the keys of toolInfo() except thickness
    void setToolInfo(Tool tool, const QVariantMap& values);
    /// The font of new texts as Pango describes it, without the size: "Sans", "Times New Roman, Bold"
    QString textFontDescription() const { return m_textFont; }
    void setTextFontDescription(const QString& description);
    /// Makes a page the current one without scrolling to it
    void selectPage(int page);
    /// Renders everything anew
    void refresh();

    /// Paints a page scaled into the given size, for previews
    void paintPage(QPainter* painter, int page, const QSizeF& size);

signals:
    void toolChanged();
    void geometryToolChanged();
    void selectionChanged();
    void clipboardChanged();
    void layersChanged();
    void pdfSelectionChanged();
    void pdfSelectionRectChanged();
    void searchChanged();
    void textStyleChanged();
    void textEditChanged();
    void textEditMatrixChanged();
    /// The link tool was used: on an existing link (with its text and URL) or on an empty place
    void linkRequested(const QString& text, const QString& url, bool existing);
    /// A link was tapped with the hand tool
    void openLinkRequested(const QString& url);
    /// The LaTeX tool was used: on an existing formula (with its source) or on an empty place
    void latexRequested(const QString& source, bool existing);
    void usePressureChanged();
    void fingerDrawsChanged();
    void viewChanged();
    void documentChanged();
    void currentPageChanged();
    void layoutChanged();
    void presentationModeChanged();
    void renderingChanged();
    /// The content or the format of a page changed; -1 if all pages may have changed
    void pageChanged(int page);
    void modifiedChanged();
    void undoChanged();
    void inputInfoChanged();
    void loadFailed(const QString& message);
    void autosaveChanged();
    void exportFailed(const QString& message);
    /// What is on the clipboard could not be pasted
    void pasteFailed(const QString& message);
    /// A document was opened from or saved to a file
    void fileOpened(const QString& path);
    void fileSaved(const QString& path);
    void navigationChanged();
    void highlightPositionChanged();
    void audioRecordingChanged();
    /// The image tool was used at this position of the item: insertImageAt() puts an image there
    void imageRequested(const QPointF& pos);
    /// The play tool was used on a stroke or text with a recording
    void audioPlayRequested(const QString& filename, qint64 timestamp);
    void canvasColorChanged();
    void scrollChanged();
    void viewSettingsChanged();
    void pageTemplateChanged();
    void buttonActionsChanged();
    /// A button that shows the floating toolbox was pressed at this position of the item
    void floatingToolboxRequested(const QPointF& pos);
    void saveFailed(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// The pages are tiles: textures the scene graph moves and scales, without painting anything again
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void touchEvent(QTouchEvent* event) override;

private:
    static constexpr int TILE_SIZE = 512;  ///< device pixels

    /// Reversible changes. An element was added to or removed from a layer
    struct ElementEdit {
        bool added;
        int page;
        int layer;
        size_t index;
        Element element;
    };
    /// A page was inserted or deleted
    struct PageEdit {
        bool added;
        int index;
        Page page;
    };
    struct PageMoveEdit {
        int from;
        int to;
    };
    struct PageFormat {
        double width;
        double height;
        Background background;
    };
    struct PageFormatEdit {
        int page;
        PageFormat before;
        PageFormat after;
    };
    /// A layer was added to or removed from a page
    struct LayerEdit {
        bool added;
        int page;
        int index;
        Layer layer;
    };
    struct LayerMoveEdit {
        int page;
        int from;
        int to;
    };
    struct LayerRenameEdit {
        int page;
        int index;
        QString before;
        QString after;
    };
    /// Elements were moved on their page
    struct ElementMoveEdit {
        int page;
        std::vector<std::pair<int, size_t>> elements;  ///< layer and index
        QPointF delta;
    };
    /// Elements were changed in place, e.g. transformed or recoloured
    struct ElementReplaceEdit {
        struct Item {
            int layer;
            size_t index;
            Element before;
            Element after;
        };
        int page;
        std::vector<Item> items;
    };
    using Edit = std::variant<ElementEdit, PageEdit, PageMoveEdit, PageFormatEdit, ElementMoveEdit, ElementReplaceEdit,
                              LayerEdit, LayerMoveEdit, LayerRenameEdit>;
    using EditGroup = std::vector<Edit>;

    /// What has to be updated after an edit was applied
    struct EditEffect {
        bool structural = false;  ///< pages were added, removed, moved or resized
        bool layers = false;      ///< layers of the page were added, removed, moved or renamed
        int page = -1;            ///< the changed page, or the page to show after a structural change
        QRectF rect;              ///< changed part of the page, invalid for the whole page
    };

    /// A square part of the view, rendered by a worker thread. The key is its position in the grid of tiles
    struct Tile {
        QImage image;
        bool dirty = false;   ///< shown until its replacement is rendered
        quint64 version = 0;  ///< changes with the image, so that its texture is made anew
    };
    struct PendingTile {
        quint64 id = 0;
        std::shared_ptr<std::atomic_bool> cancelled;
        bool outdated = false;  ///< the document changed after the job took its snapshot
    };
    struct TileRequest;

    /// A position of the pen, the mouse or a finger
    struct PointerInput {
        QPointF pos;           ///< in the item
        double pressure = -1;  ///< 0 to 1, negative if the device has none
        quint64 timestamp = 0;
        Qt::KeyboardModifiers modifiers;
    };
    /// What the pointer is doing between press and release
    enum class Action {
        None,
        Draw,
        Laser,
        Shape,
        Spline,
        Erase,
        Hand,
        VerticalSpace,
        GeometryStroke,
        GeometryMove,
        PdfSelect,        ///< selecting text of the PDF
        Select,           ///< drawing the rectangle or lasso of a selection tool
        SelectionGesture  ///< moving, scaling or rotating the selection
    };

    struct ToolState {
        QColor color = Qt::black;
        ToolSize size = Medium;
        DrawingType drawingType = Freehand;
        bool fill = false;
        int fillAlpha = 128;
        QString lineStyle = QStringLiteral("plain");
    };

    /// A spline that is being built: clicks set its knots, dragging sets the tangent of the last one
    struct SplineInput {
        bool active = false;
        int page = -1;
        Stroke style;
        QList<QPointF> knots;
        QList<QPointF> tangents;
        QPointF current;  ///< where the next knot would be
        bool pressed = false;
        bool inFirstKnotZone = false;  ///< the pointer is on the first knot: releasing closes the spline
        QPointF lastPressPos;
        QElapsedTimer sincePress;
    };

    /// The elements the vertical space tool has taken out of the page while it moves them
    struct VerticalSpaceInput {
        struct Item {
            int layer;
            size_t index;
            Element element;
        };
        int page = -1;
        double startY = 0;
        double endY = 0;
        std::vector<Item> items;  ///< in the order of the page
        QImage image;             ///< the elements, rendered once
        QRectF imageRect;         ///< the part of the page the image shows
    };

    /// The parts of the frame around the selection that can be grabbed
    enum class Handle {
        None,
        Move,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Left,
        Right,
        Top,
        Bottom,
        Rotate,
        Delete
    };

    /**
     * The selected elements. They stay in the document; the tiles are rendered without them and they are painted
     * on top, so that they can follow the pointer.
     */
    struct SelectionState {
        bool active = false;
        int page = -1;
        int layer = -1;
        std::vector<size_t> indices;  ///< ascending
        // The frame: a rectangle turned by the rotation around its center
        QRectF rect;
        double rotation = 0;

        // The gesture in progress: what it does to the elements and to the frame
        Handle handle = Handle::None;
        QPointF start;
        QTransform pending;
        double pendingWidthFactor = 1.0;
        QRectF gestureRect;
        double gestureRotation = 0;

        QImage image;           ///< the elements, rendered once
        QRectF imageRect;       ///< the part of the page the image shows
        double imageScale = 0;  ///< the zoom the image was rendered for
    };

    /// The text that is being edited. An existing one stays in the document, hidden until the editing ends
    struct TextEditState {
        bool active = false;
        int page = -1;
        int layer = -1;
        std::optional<size_t> index;  ///< of the existing text; none for a new one
        TextElement element;          ///< as it is edited
    };

    /// What the link or LaTeX dialog is open for
    struct InsertTarget {
        int page = -1;
        int layer = -1;
        std::optional<size_t> index;  ///< of the existing element; none for a new one
        QPointF pos;                  ///< where a new one goes
    };

    /// Text selected in the PDF of the background of a page
    struct PdfSelectionState {
        bool active = false;
        int page = -1;
        QPointF start;         ///< on the page
        QList<QRectF> bounds;  ///< one for each line, on the page
        QString text;
    };

    /// Characters of a page of the PDF with their places, for selecting text by a rectangle
    struct PdfCharacters {
        QString text;
        QList<QRectF> boxes;  ///< in the coordinates of the PDF page
    };

    struct SearchState {
        QString text;
        int page = -1;                      ///< of the current result
        int index = -1;                     ///< of the current result on its page
        QHash<int, QList<QRectF>> results;  ///< per page, found when the page is needed
    };

    struct LaserStroke {
        int page;
        Stroke stroke;
    };

    void setDocument(Document doc, const QString& title, const QString& filePath = {});
    bool saveTo(const QString& path);
    void setModified(bool modified);
    void watchWindow(QQuickWindow* window);
    void relayout();
    /// Updates everything after pages were added, removed, moved or resized
    void structureChanged(int focusPage);
    void setLayoutSettings(const LayoutSettings& settings);
    void setCurrentPageInternal(int page);
    void updateCurrentPage();
    void scrollToPage(int page);

    // View transformation: view = (world - origin) * scale
    QPointF viewToWorld(const QPointF& p) const { return p / m_scale + m_origin; }
    QPointF worldToView(const QPointF& p) const { return (p - m_origin) * m_scale; }
    int pageAt(const QPointF& world) const;
    void applyPageTransform(QPainter& p, int page) const;
    void panBy(const QPointF& viewDelta);
    void zoomAt(const QPointF& viewPos, double factor);
    void setScale(double scale);
    QRectF scrollBounds() const;
    void clampView();
    void viewMoved();
    /// Repaints the item, or a part of it. To be used instead of update()
    void updateView();
    void updateView(const QRect& rect);

    // Tiles
    double tileScale() const;  ///< device pixels per point
    QRect tileRange(const QRectF& world) const;
    QRectF tileViewRect(const QPoint& tile) const;
    std::shared_ptr<const Page> snapshot(int page);
    void scheduleTiles();
    void startTile(const QPoint& tile, const QList<int>& candidatePages, bool visible);
    void tileReady(const QPoint& tile, quint64 id, const QImage& image, double ms);
    void cancelPending();
    void resetTiles();
    /// Marks a part of a page (page coordinates; invalid: the whole page) as changed
    void markDirty(int page, const QRectF& rect);
    static QImage renderTile(const TileRequest& request);

    // Input: all devices end up in these
    void handleTablet(QTabletEvent* event, const QPointF& pos);
    /// @param buttonTool the tool of the button that is pressed, if it has one: it replaces the selected tool
    void pointerPress(const PointerInput& input, std::optional<Tool> buttonTool = std::nullopt);
    /// What a press with a button does. @return false if the press is used up (floating toolbox)
    bool resolveButton(Button button, const QPointF& pos, std::optional<Tool>& tool);
    void applyPageTemplate(Page& page) const;
    /// A place of the view that can be returned to
    struct Place {
        int page;
        QPointF origin;
    };
    /// Remembers where the view is, before it jumps elsewhere
    void notePlace();
    void goToPlace(const Place& place);
    /// The input with a pressure made up from the speed, if the device has none and the settings ask for it
    PointerInput withGuessedPressure(const PointerInput& input, bool press);
    /// The modifiers of the keys, changed by the direction the shape is dragged in
    Shapes::Modifiers shapeModifiers();
    /// Whether the press that ends now was a tap that the filter for short strokes drops
    bool isFilteredTap(const PointerInput& input) const;
    /// Where the pointer is, for highlightPosition
    void notePointer(const QPointF& pos);
    /// What a new stroke or text is given while audio is recorded
    AudioRef currentAudio() const;
    void playObjectPress(const PointerInput& input);
    void pointerMove(const PointerInput& input);
    void pointerRelease(const PointerInput& input);
    /// The pointer moves without being pressed
    void pointerHover(const PointerInput& input);
    bool inputActive() const { return m_action != Action::None; }
    /// Ends what the pointer is doing and keeps the result, e.g. before the document is saved
    void finishInput();
    /// Ends what the pointer is doing and drops the result
    void cancelInput();
    void endAction(double pressure);

    // Tools
    const ToolState& toolState() const { return m_toolStates[static_cast<size_t>(m_tool)]; }
    ToolState& toolState() { return m_toolStates[static_cast<size_t>(m_tool)]; }
    double thickness(Tool tool) const;
    Stroke strokeStyle(Tool tool) const;
    Snapper snapper(int page) const;
    QPointF viewToPage(int page, const QPointF& viewPos) const;
    QRect pageToViewRect(int page, const QRectF& rect) const;
    void updatePageRect(int page, const QRectF& rect);

    void startDrawing(const PointerInput& input, const Stroke& style);
    double filteredPressure(double pressure) const;
    StrokeStabilizer::Event stabilizerEvent(const PointerInput& input) const;
    /// Adds a stroke to the top layer of a page, undoable
    void addStroke(int page, const Stroke& newStroke);
    void updateShape();
    void eraseAt(const QPointF& pagePos);

    void splinePress(const PointerInput& input);
    void splineMove(const PointerInput& input);
    void splineRelease(const PointerInput& input);
    void splineHover(const PointerInput& input);
    bool splineKey(QKeyEvent* event);
    void finishSpline();
    void clearSpline();

    void startVerticalSpace(const PointerInput& input);
    void finishVerticalSpace(bool cancel);

    void fadeLaser();

    std::vector<QLineF> geometryLines() const;
    bool geometryPress(const PointerInput& input, Tool tool);
    void geometryMove(const PointerInput& input);
    void geometryRelease();
    bool geometryKey(QKeyEvent* event);
    bool geometryTouch(QTouchEvent* event);

    /// The layer of a page that is drawn on. A page without layers gets one
    int activeLayer(int page);

    // Selection
    static double thicknessOf(Tool tool, ToolSize size);
    void selectPress(const PointerInput& input);
    void selectRelease();
    void setSelection(int page, int layer, std::vector<size_t> indices);
    bool selectObjectAt(int page, const QPointF& pagePos);
    void renderSelectionImage();
    Handle selectionHandleAt(const QPointF& viewPos) const;
    void selectionGestureMove(const PointerInput& input);
    void selectionGestureEnd(const PointerInput* input);
    bool selectionKey(QKeyEvent* event);
    /// Changes the selected elements in place, undoable
    void modifySelection(const std::function<void(Element&)>& modify);
    void transformSelection(const QTransform& transformation, double widthFactor, const QRectF& rect, double rotation);
    /// @param pagePos where the middle of the elements goes; in the middle of the view if not given
    void pasteElements(std::vector<Element> elements, int page = -1, std::optional<QPointF> pagePos = std::nullopt);

    // PDF: text selection, links, search
    bool pageHasPdf(int page) const;
    /// Factors from the coordinates of the PDF page to those of the page (which may have been resized)
    QSizeF pdfScale(int page) const;
    void pdfSelectPress(const PointerInput& input);
    void pdfSelectMove(const PointerInput& input);
    const PdfCharacters& pdfCharacters(int pdfPage);
    void markPdfSelection(int kind);
    bool followPdfLink(int page, const QPointF& pagePos);
    const QList<QRectF>& searchResults(int page);
    bool searchStep(bool forward, bool includeCurrent);
    void showSearchResult();
    void paintPdfOverlays(QPainter* painter);

    // Text, images, links, formulas
    void textPress(const PointerInput& input);
    void startTextEdit(int page, int layer, std::optional<size_t> index, const TextElement& element);
    void setTextEditElement(const std::function<void(TextElement&)>& modify);
    void applyTextStyle(const std::function<void(QString& font, double& size, QString& align)>& modify);
    void linkPress(const PointerInput& input);
    void latexPress(const PointerInput& input);
    std::optional<std::pair<int, size_t>> elementOfKindAt(int page, const QPointF& pagePos, int kind) const;
    /// @param data the file the image comes from; it is stored as PNG if this is empty or of another format
    static ImageElement imageElement(const QImage& image, const QByteArray& data, const Page& page);
    void paintSelection(QPainter* painter);

    /// Whether paintOverlays() has anything to paint
    bool hasOverlays() const;

    EditEffect applyEdit(const Edit& edit, bool reverse);
    void applyEffects(const std::vector<EditEffect>& effects);
    /// Applies the edits and makes them undoable
    void perform(EditGroup group);
    void pushUndo(EditGroup group);
    PageFormat pageFormat(int page) const;
    void noteInput(const QString& device, double pressure, bool hasPressure);
    bool exportOptions(const QVariantMap& map, ExportOptions& options);
    void removeAutosave();

private:
    Document m_doc;
    QString m_title;
    QString m_filePath;  ///< the .xopp file, empty for new documents and freshly opened PDFs
    bool m_autosaveEnabled = true;
    int m_autosaveMinutes = 3;
    QTimer m_autosaveTimer;
    QString m_autosaveId;          ///< names the autosave file of a document without file
    QString m_autosaveFile;        ///< the autosave file that was written for this document, if any
    bool m_autosaveDirty = false;  ///< changes since the last autosave
    bool m_modified = false;
    LayoutSettings m_layoutSettings;
    bool m_presentationMode = false;
    PageLayout m_layout;
    QList<QRectF> m_pageRects;  ///< page positions in world coordinates
    QSizeF m_worldSize;
    int m_currentPage = 0;
    int m_pdfMarkerAlpha = 0x7f;
    double m_wheelPages = 0;  ///< wheel rotation not yet turned into a page change (presentation mode)

    double m_scale = 1.0;  ///< logical pixels per point
    QPointF m_origin;
    bool m_fitPending = true;

    // The static content is rendered in tiles by worker threads; only the stroke in progress is painted on top
    QHash<QPoint, Tile> m_tiles;
    QHash<QPoint, PendingTile> m_pending;
    QHash<QPoint, QImage> m_oldTiles;  ///< tiles of the previous zoom level, shown scaled until the new ones are ready
    double m_oldTileScale = 1.0;
    std::vector<std::shared_ptr<const Page>> m_snapshots;  ///< copies of the pages for the worker threads
    quint64 m_nextTileId = 1;
    bool m_rendering = false;
    bool m_fullUpdate = false;               ///< the whole overlay is repainted with the next frame
    QQuickPaintedItem* m_overlay = nullptr;  ///< paints paintOverlays() on top of the tiles
    bool m_overlayShown = false;             ///< the overlay had something to paint the last time
    quint64 m_tileVersion = 0;
    quint64 m_oldTileGeneration = 0;  ///< changes when m_oldTiles is filled anew
    double m_tileMs = 0;
    QThreadPool m_pool;

    Tool m_tool = Pen;

    Tool m_previousTool = Pen;  ///< the tool before the current one, for the Apple Pencil
    Tool m_pressTool = Pen;     ///< the tool of the press in progress: the selected one or the one of a button
    struct ButtonOptions {
        int drawingType = -1;
        int size = -1;
        QColor color;
    };
    std::array<int, BUTTON_COUNT> m_buttonActions{Eraser, Eraser, Eraser, Hand, Hand, ButtonNoAction, ButtonNoAction};
    std::array<ButtonOptions, BUTTON_COUNT> m_buttonOptions;
    /// The settings of the tool a button has changed for the time it is pressed
    std::optional<std::pair<Tool, ToolState>> m_savedToolState;
    void restoreButtonState();
    QList<Place> m_placesBack;
    QList<Place> m_placesForward;
    // Modifiers by the direction a shape is dragged in
    bool m_dirModsFixed = false;
    bool m_dirShift = false;
    bool m_dirControl = false;
    // The press in progress and the end of the stroke before it, for the filter of taps
    QPointF m_pressPos;
    quint64 m_pressTime = 0;
    quint64 m_lastStrokeEnd = 0;
    bool m_hasLastStrokeEnd = false;
    // Guessed pressure for devices without it
    double m_guessedPressure = 0;
    QPointF m_guessPos;
    quint64 m_guessTime = 0;
    bool m_highlightPosition = false;
    QPointF m_pointerPos{-1, -1};
    QString m_audioFile;
    QElapsedTimer m_audioClock;  ///< runs since the recording started
    AudioRef m_pressAudio;       ///< the recording and its time when the pointer was pressed
    QColor m_canvasColor{0x5a, 0x5a, 0x60};
    bool m_einkMode = false;
    /// Whether strokes and shapes are drawn with smooth edges
    bool smoothEdges() const { return !m_einkMode || m_input.einkSmoothing; }
    /// Whether fills are drawn as a pattern of dots (electronic paper), see Renderer::PatternFills
    bool patternFills() const { return m_einkMode && m_input.einkPatternFills; }
    /// How strokes and fills are drawn, to see when the tiles have to be rendered again
    int drawingStyle() const { return (smoothEdges() ? 1 : 0) | (patternFills() ? 2 : 0); }
    int m_drawingStyleDrawn = 1;  ///< as the tiles were rendered
    /// What electronic paper has other defaults for: the pen is black
    void applyEinkDefaults();
    /// The colour around the pages that is shown
    QColor surroundColor() const { return m_einkMode ? QColor(Qt::white) : m_canvasColor; }
    QMarginsF m_space;
    bool m_unlimitedScrolling = false;
    double m_displayDpi = 72;
    AppendPage m_appendPage = AppendNever;
    bool m_appending = false;  ///< a page is being appended by itself
    QColor m_selectionColor{0xff, 0x00, 0x00};
    QColor m_positionColor{0xff, 0xff, 0x00, 0x80};
    double m_positionRadius = 30;
    QColor m_positionBorderColor{0x00, 0x00, 0xff, 0x80};
    double m_positionBorderWidth = 0;
    /// A new page as one after the page at an index would be: the same paper, no PDF or image
    Page newPageAfter(int index) const;
    /// Appends a page if the last one has something on it and the setting asks for it
    void appendPageIfWritten(EditGroup& group);
    void appendPageIfScrolledToEnd();
    QVariantMap m_pageTemplate;
    std::array<ToolState, TOOL_COUNT> m_toolStates;
    PdfSelectionState m_pdfSelection;
    QHash<int, PdfCharacters> m_pdfCharacters;
    SearchState m_search;
    QString m_textFont = QStringLiteral("Sans");  ///< description of the font of new texts, without the size
    double m_textSize = 12.0;
    QString m_textAlign = QStringLiteral("left");
    bool m_textJustify = false;
    TextEditState m_textEdit;
    InsertTarget m_insertTarget;
    QPointF m_handPressPos;  ///< where the hand tool was pressed: a tap on a link opens it
    EraserType m_eraserType = EraseStandard;
    InputSettings m_input;
    bool m_usePressure = true;
    bool m_fingerDraws = false;

    // What the pointer is doing
    Action m_action = Action::None;
    int m_curPage = -1;
    StrokeBuilder m_builder;  ///< the stroke of the pen, highlighter, whiteout eraser or laser pointer
    std::unique_ptr<StrokeStabilizer> m_stabilizer;
    DrawingType m_drawingType = Freehand;  ///< of the stroke in progress
    Stroke m_shape;                        ///< the shape in progress
    QPointF m_shapeStart;
    QPointF m_shapeCurrent;
    Shapes::Modifiers m_shapeModifiers;
    EditGroup m_group;    ///< what the eraser has removed so far
    QPointF m_eraserPos;  ///< on the page
    SplineInput m_spline;
    VerticalSpaceInput m_vertical;

    SelectionState m_selection;
    bool m_selectAllLayers = false;
    QList<QPointF> m_selectPoints;  ///< the rectangle (two points) or lasso that is being drawn

    // Laser pointer: strokes that fade away
    std::vector<LaserStroke> m_laserStrokes;
    int m_laserAlpha = 255;
    QTimer m_laserFadeDelay;
    QTimer m_laserFade;

    // Setsquare and compass
    GeometryToolType m_geometryToolType = NoGeometryTool;
    GeometryTool m_geometry;
    int m_geometryPage = -1;
    QPointF m_geometryLast;  ///< last position of the pointer that moves the tool
    double m_geometryLastAngle = 0;
    double m_geometryLastDistance = 0;
    bool m_geometryTouch = false;
    std::vector<QLineF> m_geometryLines;  ///< straight strokes the tool aligns with while it is moved
    struct GeometryCacheKey {
        int type = -1;
        double height = 0;
        double rotation = 0;
        QPointF origin;
        double scale = 0;

        bool operator==(const GeometryCacheKey& o) const {
            return type == o.type && height == o.height && rotation == o.rotation && origin == o.origin &&
                   scale == o.scale;
        }
    };
    GeometryCacheKey m_geometryCacheKey;
    QImage m_geometryCache;  ///< the tool as it is shown, rendered once

    bool m_penDown = false;
#ifdef Q_OS_ANDROID
    bool m_penClicksAsMouse = true;
#else
    bool m_penClicksAsMouse = false;
#endif
    bool m_penMouseDown = false;    ///< the pen is pressed as a mouse: what follows is for where it was pressed
    QPointF m_penMousePress;        ///< where it was pressed
    bool m_penMouseSteady = false;  ///< small movements are left out, see sendPenAsMouse()
    bool sendPenAsMouse(QTabletEvent* tablet);
    /// Whether a menu or a dialog is open in the window
    bool popupOpen() const;
    bool m_mouseDrawing = false;
    Qt::MouseButton m_mouseButton = Qt::NoButton;  ///< the button that draws
    bool m_touchDrawing = false;
    bool m_panning = false;
    /// A pen or a mouse that is used as a touchscreen moves the view
    bool m_devicePanning = false;
    /// Devices that sent events, by name, with their type: some are only known to Qt once they are used
    QHash<QString, QInputDevice::DeviceType> m_seenDevices;
    int m_stylusEventsToIgnore = 0;
    bool m_stylusCursorShown = false;
    QString m_stylusCursorKey;  ///< what the cursor of the pen looks like, see showStylusCursor()
    /// Shows the pointer of the stylus where it is, as the settings want it
    void showStylusCursor();
    /// Moving the view while a selection is dragged to its edge
    QTimer m_edgePan;
    PointerInput m_edgePanInput;
    QElapsedTimer m_edgePanClock;
    void edgePanStep();
    double m_pinchStart = 0;
    bool m_pinchZooming = true;
    int deviceClassOf(const QInputDevice* device);
    QPointF m_lastPan;
    QPointF m_lastTouch;
    QList<QPointF> m_touchPositions;  ///< of the fingers, when the last touch event was handled
    QElapsedTimer m_inputClock;       ///< timestamps of the input, in milliseconds
    QElapsedTimer m_penSeen;          ///< for palm rejection

    std::vector<EditGroup> m_undo;
    std::vector<EditGroup> m_redo;

    QPointer<QQuickWindow> m_window;

    // Input diagnostics
    QString m_inputInfo;
    QElapsedTimer m_rateTimer;
    int m_eventCount = 0;
    double m_eventRate = 0;

    // Tiles are rendered at the final resolution once zooming has settled
    QTimer m_settle;

    // The pen of electronic paper (Platform::EinkPen): the device draws the stroke of the pen itself, at once.
    // While it does, nothing the application draws on the pages is shown, so it is on only while the pen writes
    QTimer m_einkPenTick;  ///< looks whether it is to be on
    bool m_einkPenOn = false;
    QElapsedTimer m_einkPenClock;
    qint64 m_einkPenHoldUntil = 0;  ///< off until then: something else than the pen changes the view
    qint64 m_einkPenSyncAt = 0;     ///< when what was written is shown as the application draws it; 0 for never
    bool einkPenWanted() const;
    /// Stops the drawing of the device for a while, so that what the application draws is seen
    void einkPenHold(int ms = 600);
    void einkPenUpdate();
    bool m_zooming = false;
#ifdef HAVE_QTPDF
    QPdfDocument m_pdf;
#endif
};
