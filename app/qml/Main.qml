pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material as MaterialStyle
import QtQuick.Dialogs
import QtQuick.Layouts
import Qournal

ApplicationWindow {
    id: root

    // The style of Android has a light and a dark look of its own, which does not follow the palette: it gets
    // the one of the theme, also for the menus and dialogs. Without this they stayed light in the dark mode
    MaterialStyle.Material.theme: theme.dark ? MaterialStyle.Material.Dark : MaterialStyle.Material.Light

    property url initialFile
    property string trackedFile: ""  // the document whose current page is remembered
    /// Logs of crashes since the last start, see CrashHandler
    property var crashLogs: []
    property int initialPage: 0  // from the command line, counted from 1; 0: where the document was left

    // A document is a file, or a location of another kind (content:// on Android), which is a URL already
    function locationUrl(location) {
        return recentFiles.locationUrl(location)
    }

    // The system asks for a document to be opened, see main.cpp
    function openRequested(url) {
        guarded(() => openDocument(url))
    }

    // Opens a document. If an autosave of it is newer than the file, the user chooses which one to take
    function openDocument(url) {
        // A PDF opens the document that annotates it, if there is one next to it (Xournal++: autoloadPdfXoj)
        if (openAnnotationOfPdf && url.toString().toLowerCase().endsWith(".pdf")) {
            const annotation = canvas.annotationFileFor(url)
            if (annotation.toString() !== "") {
                url = annotation
            }
        }
        lastFolder = folderOf(url)
        const autosave = canvas.autosaveFor(url)
        if (autosave !== "") {
            recoverDialog.openFor(autosave, url)
        } else {
            canvas.openFile(url)
        }
    }
    property bool menuBarVisible: true
    property bool toolbarVisible: true
    property bool closeConfirmed: false
    property bool sidebarVisible: true
    /// A line at the bottom with what the last input device reported
    property bool inputInfoVisible: false

    // The view, as in the settings of Xournal++
    /// "auto" (right, but none in the touch layout, where they only take room), "right", "left" or "hidden"
    property string scrollbars: "auto"
    readonly property string scrollbarSide: scrollbars === "auto" ? (touchUi ? "hidden" : "right") : scrollbars

    /// The room at an edge of the window that is not for the application: the status bar and the navigation bar of a
    /// phone, a camera in the screen (Qt 6.9 and newer; 0 before). Menus keep out of it
    function safeMargin(side) {
        try {
            return Overlay.overlay.SafeArea.margins[side]
        } catch (e) {
            return 0
        }
    }
    property bool sidebarRight: false
    property string previewNumbers: "below"  ///< where the previews have their numbers, see PageSidebar
    property bool titleShowsPath: false
    property bool titleShowsPage: false
    /// What full screen and presentation mode show
    property bool fullScreenMenubar: true
    property bool fullScreenToolbars: true
    property bool fullScreenSidebar: true
    property bool presentationMenubar: false
    property bool presentationToolbars: false
    property bool presentationSidebar: false
    // Files
    property string saveNamePattern: qsTr("%F-Note-%H-%M")
    property string exportNamePattern: qsTr("%{name}_annotated")
    property bool openAnnotationOfPdf: true
    property bool openLastAtStart: false
    property string paperUnit: "cm"  ///< "cm", "mm", "in" or "pt"
    property url lastFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
    property url lastImageFolder: StandardPaths.writableLocation(StandardPaths.PicturesLocation)

    /// Whether the mode of the view shows a part of the window: "menubar", "toolbars" or "sidebar"
    function modeShows(part) {
        if (canvas.presentationMode) {
            return part === "menubar" ? presentationMenubar : part === "toolbars" ? presentationToolbars
                                                                                  : presentationSidebar
        }
        if (fullScreen) {
            return part === "menubar" ? fullScreenMenubar : part === "toolbars" ? fullScreenToolbars
                                                                                : fullScreenSidebar
        }
        return true
    }

    /// Saves a copy that Xournal++ 1.3.8 and earlier can open, after telling what changes in it
    function saveForXournalpp13() {
        const changes = canvas.changesForXournalpp13()
        if (changes.length > 0) {
            oldFormatDialog.changes = changes
            oldFormatDialog.open()
        } else {
            oldFormatFileDialog.open()
        }
    }

    /// The folder of a file, for the next file dialog
    function folderOf(file) {
        const text = file.toString()
        return text.substring(0, text.lastIndexOf("/"))
    }

    // The arrangement for fingers and a pen, as on tablets: the menus behind one button, larger buttons, the
    // sidebar on top of the page. "auto" chooses it on Android and iOS
    property string layoutMode: "auto"
    readonly property bool touchUi: layoutMode === "touch"
                                    || (layoutMode === "auto" && (Qt.platform.os === "android"
                                                                  || Qt.platform.os === "ios"))
    readonly property int buttonSize: touchUi ? Math.max(iconSize, 32) : iconSize

    // The menus are in the menu bar, or in the menu of the button of the touch layout
    function placeMenus() {
        if (touchUi) {
            while (mainMenuBar.count > 0) {
                mainMenu.addMenu(mainMenuBar.takeMenu(0))
            }
        } else {
            while (mainMenu.count > 0) {
                mainMenuBar.addMenu(mainMenu.takeMenu(0))
            }
        }
    }

    onTouchUiChanged: {
        placeMenus()
        if (touchUi) {
            sidebarVisible = false  // it would cover the page
        }
    }

    // The look of the user interface; kept between sessions, see uiSettings
    property string themeMode: "system"
    property string iconTheme: "lucide"
    property int iconSize: 24
    property string paletteFile: ""
    property string latexCommand: ""
    property string latexTemplate: ""
    // The items of the toolbars, by "top1", "left1", ...: those of the chosen configuration, see ToolbarModel.
    // They are ids of actions and of the controls of ToolbarItem
    readonly property var bars: toolbars.bars
    // The floating toolbox shows its four rows as one
    readonly property var toolboxItems: ["float1", "float2", "float3", "float4"].reduce(
        (all, bar) => all.concat(bars[bar] ?? []), [])
    readonly property var allBarItems: Object.values(bars).reduce((all, items) => all.concat(items), [])
    // The controls of ToolbarItem that are not buttons
    readonly property var controlNames: ({
        "separator": qsTr("Separator"),
        "spacer": qsTr("Space"),
        "eraserType": qsTr("Eraser type"),
        "colors": qsTr("Colours"),
        "size": qsTr("Size of the tool"),
        "lineStyle": qsTr("Line style"),
        "font": qsTr("Font"),
        "zoomLabel": qsTr("Zoom level"),
        "zoomSlider": qsTr("Zoom slider"),
        "layer": qsTr("Layer"),
        "page": qsTr("Page number")
    })
    readonly property var placeholders: plugins.placeholders
    // Single colours of the palette, by their position in it
    readonly property var colorItems: colorPalette.colors.map((color, index) => "color:" + index)
    readonly property var allItems: Object.keys(controlNames).concat(Object.keys(actions), colorItems,
                                                                     Object.keys(plugins.placeholders))

    function itemName(id) {
        if (id.startsWith("color:")) {
            const color = colorPalette.colors[Number(id.substring(6))]
            return qsTr("Colour %1").arg(color && color.name !== "" ? color.name : Number(id.substring(6)) + 1)
        }
        if (id.startsWith("xournalpp:")) {
            // An item of a toolbar of Xournal++ that does not exist here: it is kept, but not shown
            return qsTr("%1 (not available)").arg(id.substring(10))
        }
        return controlNames[id] ?? actions[id]?.text ?? plugins.placeholders[id]?.description ?? id
    }

    // What the plugins offer for the toolbars, by their ids ("Plugin::...")
    readonly property var pluginActions: {
        const result = {}
        for (const entry of plugins.toolbarEntries) {
            result[entry.toolbarId] = {
                text: entry.text, iconSource: entry.icon.toString(),
                trigger: () => plugins.trigger(entry.id)
            }
        }
        return result
    }
    readonly property var actions: Object.assign({}, baseActions, pluginActions)

    // Actions of the window that plugins trigger, by the names they have in Xournal++
    readonly property var xournalppActions: ({
        "new-file": "new", "open": "open", "annotate-pdf": "open", "save": "save", "save-as": "saveAs",
        "export-as-pdf": "export", "export-as": "export", "print": "print", "search": "find",
        "preferences": "preferences", "presentation-mode": "present", "fullscreen": "fullScreen",
        "show-sidebar": "sidebar", "customize-toolbar": "customizeToolbars", "manage-toolbar": "customizeToolbars",
        "goto-page": "goToPage", "audio-record": "record", "audio-pause-playback": "audioPause",
        "audio-stop-playback": "audioStop", "audio-seek-forwards": "audioSeekForwards",
        "audio-seek-backwards": "audioSeekBackwards", "tool-image": "image"
    })

    // @param state the state the action is to have, undefined to trigger or toggle it
    function pluginAction(name, state) {
        if (name === "quit") {
            close()
        } else if (name === "plugin-manager") {
            pluginManagerDialog.open()
        } else if (name === "paper-format" || name === "paper-background-color") {
            pageSettingsDialog.openFor(canvas.currentPage)
        } else if (name === "open-file") {
            // A plugin opens a document while there are unsaved changes
            guarded(() => {
                canvas.openFile(state.url)
                canvas.currentPage = state.page - 1
            })
        } else if (name === "position-highlighting") {
            canvas.highlightPosition = typeof state === "boolean" ? state : !canvas.highlightPosition
        } else if (name === "navigate-back") {
            canvas.navigateBack()
        } else if (name === "navigate-forward") {
            canvas.navigateForward()
        } else if (name === "append-new-pdf-pages") {
            canvas.appendNewPdfPages()
        } else if (name === "show-menubar") {
            menuBarVisible = typeof state === "boolean" ? state : !menuBarVisible
        } else if (name === "show-toolbar") {
            toolbarVisible = typeof state === "boolean" ? state : !toolbarVisible
        } else if (name === "about") {
            aboutDialog.open()
        } else if (name === "sidebar-page") {
            sidebarTabs.currentIndex = Math.max(0, Math.min(state - 1, sidebarTabs.count - 1))
        } else if (xournalppActions[name] !== undefined) {
            const action = actions[xournalppActions[name]]
            const isSet = typeof state === "boolean" && action.checked !== undefined && action.checked() === state
            if (!isSet && (action.enabled === undefined || action.enabled())) {
                action.trigger()
            }
        }
        // What has no counterpart here is left out
    }

    function toolAction(tool, icon) {
        return {
            text: toolNames[tool], icon: icon,
            checked: () => canvas.tool === tool,
            trigger: () => canvas.tool = tool
        }
    }

    function drawingAction(type, icon) {
        return {
            text: drawingTypes[type], icon: icon,
            checked: () => root.drawingTool && canvas.drawingType === type,
            enabled: () => root.drawingTool,
            // Pressed again, the tool draws freehand again
            trigger: () => canvas.drawingType = canvas.drawingType === type ? PageCanvas.Freehand : type
        }
    }

    function eraserTypeAction(type) {
        return {
            text: eraserTypes[type],
            checked: () => canvas.eraserType === type,
            trigger: () => canvas.eraserType = type
        }
    }

    function sizeAction(size, icon) {
        return {
            text: toolSizes[size], icon: icon,
            checked: () => root.sizeTool && canvas.toolSize === size,
            enabled: () => root.sizeTool,
            trigger: () => canvas.toolSize = size
        }
    }

    function lineStyleAction(index, icon) {
        return {
            text: lineStyles[index].name, icon: icon,
            checked: () => canvas.lineStyle === lineStyles[index].style
                           || (index === 0 && canvas.lineStyle === ""),
            enabled: () => canvas.tool === PageCanvas.Pen || canvas.hasSelection,
            trigger: () => canvas.lineStyle = lineStyles[index].style
        }
    }

    // What the buttons of the toolbars do. checked and enabled are functions, so that the buttons follow the state
    readonly property var baseActions: ({
        "new": { text: qsTr("New"), icon: "document-new", trigger: () => root.guarded(() => canvas.newDocument()) },
        "open": { text: qsTr("Open…"), icon: "document-open", trigger: () => root.guarded(() => fileDialog.open()) },
        "save": {
            text: qsTr("Save"), icon: "document-save",
            enabled: () => canvas.modified || !canvas.hasFile,
            trigger: () => root.save()
        },
        "saveAs": { text: qsTr("Save as…"), trigger: () => saveDialog.open() },
        "saveForXournalpp13": { text: qsTr("Save for Xournal++ 1.3…"), trigger: () => root.saveForXournalpp13() },
        "export": { text: qsTr("Export…"), icon: "document-export-pdf", trigger: () => exportDialog.open() },
        "print": { text: qsTr("Print…"), icon: "document-print", trigger: () => printDialog.open() },
        "undo": { text: qsTr("Undo"), icon: "edit-undo", enabled: () => canvas.canUndo, trigger: () => canvas.undo() },
        "redo": { text: qsTr("Redo"), icon: "edit-redo", enabled: () => canvas.canRedo, trigger: () => canvas.redo() },
        "cut": { text: qsTr("Cut"), icon: "edit-cut", enabled: () => canvas.hasSelection, trigger: () => canvas.cut() },
        "copy": {
            text: qsTr("Copy"), icon: "edit-copy",
            enabled: () => canvas.hasSelection,
            trigger: () => canvas.copy()
        },
        "paste": {
            text: qsTr("Paste"), icon: "edit-paste",
            enabled: () => canvas.canPaste,
            trigger: () => canvas.paste()
        },
        "find": { text: qsTr("Find…"), icon: "edit-find", trigger: () => searchBar.show() },
        "pen": Object.assign(toolAction(PageCanvas.Pen, "tool-pencil"), {
            options: ["linePlain", "lineDashed", "lineDashDotted", "lineDotted"]
        }),
        "drawingType": {
            text: qsTr("Drawing type"), icon: "combo-drawing-type",
            menu: ["drawRectangle", "drawEllipse", "drawArrow", "drawDoubleArrow", "drawLine",
                   "drawCoordinateSystem", "drawSpline", "shapeRecognizer"]
        },
        "highlighter": toolAction(PageCanvas.Highlighter, "tool-highlighter"),
        "eraser": Object.assign(toolAction(PageCanvas.Eraser, "tool-eraser"), {
            options: ["eraserStandard", "eraserWhiteout", "eraserDeleteStroke"]
        }),
        "eraserStandard": eraserTypeAction(0),
        "eraserWhiteout": eraserTypeAction(1),
        "eraserDeleteStroke": eraserTypeAction(2),
        "hand": toolAction(PageCanvas.Hand, "hand"),
        "verticalSpace": toolAction(PageCanvas.VerticalSpace, "vertical-space"),
        "laserPen": toolAction(PageCanvas.LaserPen, "laser-pointer"),
        "laserHighlighter": toolAction(PageCanvas.LaserHighlighter, ""),
        "selectRect": toolAction(PageCanvas.SelectRect, "select-rect"),
        "selectRegion": toolAction(PageCanvas.SelectRegion, "select-lasso"),
        "selectObject": toolAction(PageCanvas.SelectObject, "object-select"),
        "text": toolAction(PageCanvas.Text, "tool-text"),
        "link": toolAction(PageCanvas.Link, "tool-link"),
        "latex": toolAction(PageCanvas.Latex, "tool-math-tex"),
        "pdfText": toolAction(PageCanvas.SelectPdfTextLinear, "select-pdf-text-ht"),
        "pdfTextRect": toolAction(PageCanvas.SelectPdfTextRect, "select-pdf-text-area"),
        "playObject": toolAction(PageCanvas.PlayObject, "object-play"),
        "imageTool": toolAction(PageCanvas.Image, "tool-image"),
        "record": {
            text: qsTr("Record / Stop"), icon: "audio-record",
            checked: () => audio.recording,
            enabled: () => audio.available,
            trigger: () => audio.recording ? audio.stopRecording() : audio.startRecording()
        },
        "audioPause": {
            text: qsTr("Pause / Play"), icon: "audio-playback-pause",
            checked: () => audio.paused,
            enabled: () => audio.playing,
            trigger: () => audio.paused ? audio.resume() : audio.pause()
        },
        "audioStop": {
            text: qsTr("Stop playback"), icon: "audio-playback-stop",
            enabled: () => audio.playing,
            trigger: () => audio.stop()
        },
        "audioSeekBackwards": {
            text: qsTr("Seek backwards"), icon: "audio-seek-backwards",
            enabled: () => audio.playing,
            trigger: () => audio.seekBackwards()
        },
        "audioSeekForwards": {
            text: qsTr("Seek forwards"), icon: "audio-seek-forwards",
            enabled: () => audio.playing,
            trigger: () => audio.seekForwards()
        },
        "image": { text: qsTr("Insert image…"), icon: "tool-image", trigger: () => imageDialog.open() },
        "drawLine": drawingAction(PageCanvas.Line, "draw-line"),
        "drawRectangle": drawingAction(PageCanvas.Rectangle, "draw-rect"),
        "drawEllipse": drawingAction(PageCanvas.Ellipse, "draw-ellipse"),
        "drawArrow": drawingAction(PageCanvas.Arrow, "draw-arrow"),
        "drawDoubleArrow": drawingAction(PageCanvas.DoubleArrow, "draw-double-arrow"),
        "drawCoordinateSystem": drawingAction(PageCanvas.CoordinateSystem, "draw-coordinate-system"),
        "drawSpline": drawingAction(PageCanvas.Spline, "draw-spline"),
        "shapeRecognizer": drawingAction(PageCanvas.ShapeRecognizer, "shape-recognizer"),
        "fill": {
            text: qsTr("Fill"), icon: "fill",
            checked: () => canvas.fill,
            enabled: () => root.drawingTool || canvas.hasSelection,
            trigger: () => canvas.fill = !canvas.fill
        },
        "setsquare": {
            text: qsTr("Setsquare"), icon: "setsquare",
            checked: () => canvas.geometryTool === PageCanvas.Setsquare,
            trigger: () => canvas.geometryTool = canvas.geometryTool === PageCanvas.Setsquare
                           ? PageCanvas.NoGeometryTool : PageCanvas.Setsquare
        },
        "compass": {
            text: qsTr("Compass"), icon: "compass",
            checked: () => canvas.geometryTool === PageCanvas.Compass,
            trigger: () => canvas.geometryTool = canvas.geometryTool === PageCanvas.Compass
                           ? PageCanvas.NoGeometryTool : PageCanvas.Compass
        },
        "snapGrid": {
            text: qsTr("Grid Snapping"), icon: "snapping-grid",
            checked: () => canvas.input.snapGrid,
            trigger: () => canvas.input.snapGrid = !canvas.input.snapGrid
        },
        "snapRotation": {
            text: qsTr("Rotation Snapping"), icon: "snapping-rotation",
            checked: () => canvas.input.snapRotation,
            trigger: () => canvas.input.snapRotation = !canvas.input.snapRotation
        },
        "fingerDraws": {
            text: qsTr("Touch Drawing"), icon: "touch-drawing",
            checked: () => canvas.fingerDraws,
            trigger: () => canvas.fingerDraws = !canvas.fingerDraws
        },
        "zoomOut": { text: qsTr("Zoom out"), icon: "zoom-out", shortText: "−", trigger: () => canvas.zoomOut() },
        "zoomIn": { text: qsTr("Zoom in"), icon: "zoom-in", shortText: "+", trigger: () => canvas.zoomIn() },
        "zoom100": { text: qsTr("100 %"), icon: "zoom-original", trigger: () => canvas.zoomTo(1.0) },
        "fitWidth": { text: qsTr("Fit width"), icon: "zoom-fit-best", trigger: () => canvas.fitWidth() },
        "fitPage": { text: qsTr("Fit page"), trigger: () => canvas.fitPage() },
        "present": {
            text: qsTr("Presentation mode"), icon: "presentation-mode",
            checked: () => canvas.presentationMode,
            trigger: () => root.setPresentation(!canvas.presentationMode)
        },
        "fullScreen": {
            text: qsTr("Fullscreen"), icon: "fullscreen",
            checked: () => root.fullScreen,
            trigger: () => root.setFullScreen(!root.fullScreen)
        },
        "sidebar": {
            text: qsTr("Sidebar"), icon: "sidebar-show",
            checked: () => root.sidebarVisible,
            trigger: () => root.sidebarVisible = !root.sidebarVisible
        },
        "pairedPages": {
            text: qsTr("Paired pages"), icon: "show-paired-pages",
            checked: () => canvas.pairedPages,
            trigger: () => canvas.pairedPages = !canvas.pairedPages
        },
        "previousPage": {
            text: qsTr("Previous page"), icon: "go-previous",
            enabled: () => canvas.currentPage > 0,
            trigger: () => canvas.currentPage = canvas.currentPage - 1
        },
        "nextPage": {
            text: qsTr("Next page"), icon: "go-next",
            enabled: () => canvas.currentPage < canvas.pageCount - 1,
            trigger: () => canvas.currentPage = canvas.currentPage + 1
        },
        "goToPage": { text: qsTr("Go to page…"), icon: "go-to", trigger: () => goToPageDialog.open() },
        "insertPage": {
            text: qsTr("New Page After"), icon: "page-add",
            trigger: () => canvas.insertPage(canvas.currentPage + 1)
        },
        "deletePage": {
            text: qsTr("Delete page"), icon: "page-delete",
            enabled: () => canvas.pageCount > 1,
            trigger: () => canvas.deletePage(canvas.currentPage)
        },
        "floatingToolbox": {
            text: qsTr("Floating toolbox"), icon: "floating-toolbox",
            trigger: () => floatingToolbox.openAt(Qt.point(canvas.width / 2, canvas.height / 3))
        },
        "customizeToolbars": {
            text: qsTr("Customize toolbars…"), icon: "toolbars-customize",
            trigger: () => toolbarDialog.open()
        },
        "manageToolbars": {
            text: qsTr("Manage toolbars…"), icon: "toolbars-manage",
            trigger: () => toolbarDialog.open()
        },
        // The items below exist for the toolbar configurations of Xournal++
        "delete": {
            text: qsTr("Delete"), icon: "edit-delete",
            enabled: () => canvas.hasSelection,
            trigger: () => canvas.deleteSelection()
        },
        "firstPage": {
            text: qsTr("First page"), icon: "go-first", shortText: "|<",
            enabled: () => canvas.currentPage > 0,
            trigger: () => canvas.currentPage = 0
        },
        "lastPage": {
            text: qsTr("Last page"), icon: "go-last", shortText: ">|",
            enabled: () => canvas.currentPage < canvas.pageCount - 1,
            trigger: () => canvas.currentPage = canvas.pageCount - 1
        },
        "nextAnnotatedPage": {
            text: qsTr("Next annotated page"), icon: "page-annotated-next",
            enabled: () => canvas.currentPage < canvas.pageCount - 1,
            trigger: () => {
                const page = canvas.nextAnnotatedPage()
                if (page >= 0) {
                    canvas.currentPage = page
                }
            }
        },
        "previousAnnotatedPage": {
            text: qsTr("Previous annotated page"),
            enabled: () => canvas.currentPage > 0,
            trigger: () => {
                const page = canvas.previousAnnotatedPage()
                if (page >= 0) {
                    canvas.currentPage = page
                }
            }
        },
        "fillOpacity": {
            text: qsTr("Fill opacity…"), icon: "fill-opacity",
            enabled: () => root.drawingTool,
            trigger: () => opacityDialog.openFor(qsTr("Opacity of the filling"), canvas.fillAlpha,
                                                 alpha => canvas.fillAlpha = alpha)
        },
        "pdfMarkerOpacity": {
            text: qsTr("Opacity of the PDF text marker…"),
            trigger: () => opacityDialog.openFor(qsTr("Opacity of the marker of PDF text"), canvas.pdfMarkerAlpha,
                                                 alpha => canvas.pdfMarkerAlpha = alpha)
        },
        "layerShowAll": { text: qsTr("Show all layers"), trigger: () => canvas.setAllLayersVisible(true) },
        "layerHideAll": { text: qsTr("Hide all layers"), trigger: () => canvas.setAllLayersVisible(false) },
        "newLayerAbove": { text: qsTr("New layer above the current one"), trigger: () => canvas.addLayer(false) },
        "newLayerBelow": { text: qsTr("New layer below the current one"), trigger: () => canvas.addLayer(true) },
        "previousLayer": {
            text: qsTr("Layer below"),
            enabled: () => canvas.currentLayer > 0,
            trigger: () => canvas.currentLayer = canvas.currentLayer - 1
        },
        "nextLayer": {
            text: qsTr("Layer above"),
            enabled: () => canvas.currentLayer < canvas.layers.length - 1,
            trigger: () => canvas.currentLayer = canvas.currentLayer + 1
        },
        "topLayer": {
            text: qsTr("Top layer"),
            enabled: () => canvas.currentLayer < canvas.layers.length - 1,
            trigger: () => canvas.currentLayer = canvas.layers.length - 1
        },
        "defaultTool": { text: qsTr("Default tool"), icon: "default", trigger: () => canvas.tool = PageCanvas.Pen },
        // Buttons with a menu of tools: they show the tool of theirs that is in use
        "selectTool": {
            text: qsTr("Selection tools"), icon: "combo-selection",
            menu: ["selectRect", "selectRegion", "selectObject"]
        },
        "pdfTool": {
            text: qsTr("Select text of the PDF"), icon: "select-pdf-text-ht",
            menu: ["pdfText", "pdfTextRect"]
        },
        "selectRectAllLayers": {
            text: qsTr("Select rectangle on all layers"), icon: "select-rect",
            checked: () => canvas.tool === PageCanvas.SelectRect && canvas.selectAllLayers,
            trigger: () => {
                canvas.selectAllLayers = true
                canvas.tool = PageCanvas.SelectRect
            }
        },
        "selectRegionAllLayers": {
            text: qsTr("Select region on all layers"), icon: "select-lasso",
            checked: () => canvas.tool === PageCanvas.SelectRegion && canvas.selectAllLayers,
            trigger: () => {
                canvas.selectAllLayers = true
                canvas.tool = PageCanvas.SelectRegion
            }
        },
        "colorSelect": {
            text: qsTr("Choose a colour…"),
            enabled: () => root.colorTool,
            trigger: () => {
                toolColorDialog.selectedColor = canvas.color
                toolColorDialog.open()
            }
        },
        "sizeVeryFine": sizeAction(0, "thickness-finer"),
        "sizeFine": sizeAction(1, "thickness-fine"),
        "sizeMedium": sizeAction(2, "thickness-medium"),
        "sizeThick": sizeAction(3, "thickness-thick"),
        "sizeVeryThick": sizeAction(4, "thickness-thicker"),
        "linePlain": lineStyleAction(0, "line-style-plain"),
        "lineDashed": lineStyleAction(1, "line-style-dash"),
        "lineDashDotted": lineStyleAction(2, "line-style-dash-dot"),
        "lineDotted": lineStyleAction(3, "line-style-dot"),
        "preferences": { text: qsTr("Preferences…"), trigger: () => preferencesDialog.open() },
        "pluginManager": { text: qsTr("Plugin manager…"), trigger: () => pluginManagerDialog.open() }
    })

    // Lists of item ids are stored as text. What is not known any more (an older version) is left out
    function parseItems(text, fallback) {
        try {
            const items = JSON.parse(text)
            if (Array.isArray(items)) {
                return items.filter(id => allItems.includes(id))
            }
        } catch (error) {
        }
        return fallback
    }

    function resetInterfaceSettings() {
        themeMode = "system"
        layoutMode = "auto"
        iconTheme = "lucide"
        iconSize = 24
        inputInfoVisible = false
        scrollbars = "auto"
        sidebarRight = false
        previewNumbers = "below"
        titleShowsPath = false
        titleShowsPage = false
        fullScreenMenubar = true
        fullScreenToolbars = true
        fullScreenSidebar = true
        presentationMenubar = false
        presentationToolbars = false
        presentationSidebar = false
        saveNamePattern = qsTr("%F-Note-%H-%M")
        exportNamePattern = qsTr("%{name}_annotated")
        openAnnotationOfPdf = true
        openLastAtStart = false
        paperUnit = "cm"
        toolbars.current = ""
        latexCommand = ""
        latexTemplate = ""
        audio.folder = ""
        audio.seekTime = 5
        audio.inputDevice = ""
        audio.outputDevice = ""
        audio.gain = 1
        audio.compact = false
        paletteFile = ""
        colorPalette.reset()
    }

    onLatexCommandChanged: latexDialog.command = latexCommand
    onLatexTemplateChanged: latexDialog.templateFile = latexTemplate
    readonly property bool fullScreen: visibility === Window.FullScreen
    property int visibilityBeforeFullScreen: Window.Windowed

    // What the selected tool can be given
    readonly property bool drawingTool: canvas.tool === PageCanvas.Pen || canvas.tool === PageCanvas.Highlighter
    readonly property bool selectTool: canvas.tool === PageCanvas.SelectRect
                                       || canvas.tool === PageCanvas.SelectRegion
                                       || canvas.tool === PageCanvas.SelectObject
    readonly property bool textTool: canvas.tool === PageCanvas.Text || canvas.tool === PageCanvas.Link
                                     || canvas.textEditing
    readonly property bool insertTool: textTool || canvas.tool === PageCanvas.Latex
    readonly property var textSizes: [8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48, 72]
    // Colour and size also apply to the selected elements
    readonly property bool pdfTool: canvas.tool === PageCanvas.SelectPdfTextLinear
                                    || canvas.tool === PageCanvas.SelectPdfTextRect
    readonly property bool sizeTool: canvas.hasSelection || (canvas.tool !== PageCanvas.Hand
                                                             && canvas.tool !== PageCanvas.VerticalSpace
                                                             && !selectTool && !insertTool && !pdfTool)
    readonly property bool colorTool: canvas.hasSelection || insertTool
                                      || (sizeTool && canvas.tool !== PageCanvas.Eraser)

    // In the order of the enumerations of PageCanvas
    readonly property var toolNames: [qsTr("Pen"), qsTr("Highlighter"), qsTr("Eraser"), qsTr("Hand"),
                                      qsTr("Vertical space"), qsTr("Laser Pointer - Pen"),
                                      qsTr("Laser Pointer - Highlighter"), qsTr("Select rectangle"),
                                      qsTr("Select region"), qsTr("Select object"), qsTr("Text"), qsTr("Link"),
                                      qsTr("LaTeX formula"), qsTr("Select Linear PDF Text"),
                                      qsTr("Select PDF Text In Rectangle"), qsTr("Play object"), qsTr("Image")]
    readonly property var drawingTypes: [qsTr("Freehand"), qsTr("Draw Line"), qsTr("Draw Rectangle"),
                                         qsTr("Draw Ellipse"), qsTr("Draw Arrow"), qsTr("Draw Double Arrow"),
                                         qsTr("Draw coordinate system"), qsTr("Draw Spline"),
                                         qsTr("Shape recognizer")]
    readonly property var eraserTypes: [qsTr("Standard"), qsTr("Whiteout"), qsTr("Delete strokes")]
    readonly property var toolSizes: [qsTr("Very fine"), qsTr("Fine"), qsTr("Medium"), qsTr("Thick"),
                                      qsTr("Very thick")]
    readonly property var lineStyles: [
        { style: "plain", name: qsTr("Solid") },
        { style: "dash", name: qsTr("Dashed") },
        { style: "dashdot", name: qsTr("Dash-dotted") },
        { style: "dot", name: qsTr("Dotted") }
    ]

    function setFullScreen(on) {
        if (on === fullScreen) {
            return
        }
        if (on) {
            visibilityBeforeFullScreen = visibility
            showFullScreen()
        } else if (visibilityBeforeFullScreen === Window.Maximized) {
            showMaximized()
        } else {
            showNormal()
        }
    }

    // One page at a time on the whole screen, as for a talk
    function setPresentation(on) {
        canvas.presentationMode = on
        setFullScreen(on)
    }

    function save() {
        if (canvas.hasFile) {
            canvas.save()
        } else {
            saveDialog.open()
        }
    }

    // Runs action, after asking if it would lose unsaved changes
    function guarded(action) {
        if (canvas.modified) {
            discardDialog.action = action
            discardDialog.open()
        } else {
            action()
        }
    }

    function showError(title, message) {
        errorDialog.title = title
        errorLabel.text = message
        errorDialog.open()
    }

    width: 1100
    height: 800
    visible: true
    title: (root.titleShowsPath && canvas.filePath !== "" ? canvas.filePath : canvas.title)
           + (canvas.modified ? "*" : "")
           + (root.titleShowsPage && canvas.pageCount > 0
              ? " – " + qsTr("Page %1 of %2").arg(canvas.currentPage + 1).arg(canvas.pageCount) : "")
           + " – Qournal"

    onClosing: close => {
        if (canvas.modified && !closeConfirmed) {
            close.accepted = false
            guarded(() => {
                closeConfirmed = true
                root.close()
            })
        } else {
            canvas.saveSettings()
        }
    }

    // On mobile platforms the application may not come back once it is in the background
    Connections {
        target: Application

        function onStateChanged() {
            if (Application.state !== Qt.ApplicationActive) {
                canvas.saveSettings()
            }
        }
    }

    Settings {
        id: uiSettings

        category: "ui"

        property alias theme: root.themeMode
        property alias iconTheme: root.iconTheme
        property alias iconSize: root.iconSize
        property alias layoutMode: root.layoutMode
        property alias sidebarVisible: root.sidebarVisible
        property alias inputInfoVisible: root.inputInfoVisible
        // Not "scrollbars": earlier versions stored their default "right" under that name
        property alias scrollbarPlace: root.scrollbars
        property alias sidebarRight: root.sidebarRight
        property alias previewNumbers: root.previewNumbers
        property alias titleShowsPath: root.titleShowsPath
        property alias titleShowsPage: root.titleShowsPage
        property alias fullScreenMenubar: root.fullScreenMenubar
        property alias fullScreenToolbars: root.fullScreenToolbars
        property alias fullScreenSidebar: root.fullScreenSidebar
        property alias presentationMenubar: root.presentationMenubar
        property alias presentationToolbars: root.presentationToolbars
        property alias presentationSidebar: root.presentationSidebar
        property alias saveNamePattern: root.saveNamePattern
        property alias exportNamePattern: root.exportNamePattern
        property alias openAnnotationOfPdf: root.openAnnotationOfPdf
        property alias openLastAtStart: root.openLastAtStart
        property alias paperUnit: root.paperUnit
        property alias lastFolder: root.lastFolder
        property alias lastImageFolder: root.lastImageFolder
        property alias menuBarVisible: root.menuBarVisible
        property alias toolbarVisible: root.toolbarVisible
        property alias paletteFile: root.paletteFile
        property alias latexCommand: root.latexCommand
        property alias latexTemplate: root.latexTemplate
        property alias windowWidth: root.width
        property alias windowHeight: root.height
        /// The id of the toolbar configuration, empty for the one of the layout
        property string toolbarConfig: ""
        // Earlier versions had one toolbar and kept its items here; see Component.onCompleted
        property string toolbar: ""
        property string toolbox: ""
    }

    ToolbarModel {
        id: toolbars

        // As in Xournal++, whose default is "Portrait"
        fallback: root.touchUi ? "Qournal Tablet" : "Portrait"
    }

    Theme {
        id: theme

        mode: root.themeMode
        iconTheme: root.iconTheme
    }

    Localization { id: localization }

    // Plugins written in Lua, as in Xournal++
    PluginController {
        id: plugins

        canvas: canvas
        palette: colorPalette.colors

        onPluginFailed: (plugin, message) => root.showError(qsTr("Plugin \"%1\"").arg(plugin), message)
        onPluginPrinted: (plugin, text) => pluginOutput.add(plugin, text)
        onDialogRequested: (request, plugin, message, buttons, error) =>
                           pluginDialog.show({ request: request, plugin: plugin, message: message, buttons: buttons,
                                               error: error })
        onFileDialogRequested: (request, save, suggestion, filters) =>
                               pluginFileDialog.openFor(request, save, suggestion, filters)
        onActionRequested: (action, state) => root.pluginAction(action, state)
        onFloatingToolboxRequested: pos => floatingToolbox.openAt(
                                        pos.x < 0 ? Qt.point(canvas.width / 2, canvas.height / 3)
                                                  : canvas.mapFromItem(root.contentItem, pos))
    }

    // The shortcuts of the menu entries of the plugins
    Instantiator {
        model: plugins.menuEntries.filter(entry => entry.shortcut !== "")

        delegate: Shortcut {
            required property var modelData

            sequence: modelData.shortcut
            onActivated: plugins.trigger(modelData.id)
        }
    }

    // Records while the user writes; the strokes and texts made meanwhile play the recording from their time on
    AudioController {
        id: audio

        onFailed: message => root.showError(qsTr("Audio"), message)
    }

    Settings {
        category: "audio"

        property alias folder: audio.folder
        property alias seekTime: audio.seekTime
        property alias inputDevice: audio.inputDevice
        property alias outputDevice: audio.outputDevice
        property alias gain: audio.gain
        property alias compact: audio.compact
    }

    // The colours come from the palette of the application, which the theme sets: menus and dialogs have them too,
    // with those of disabled controls. Colours set here would not reach them for the disabled state

    Component.onCompleted: {
        placeMenus()
        canvas.loadSettings()
        plugins.load()  // before the toolbars: plugins have items for them
        toolbars.load()
        if (uiSettings.toolbar !== "" || uiSettings.toolbox !== "") {
            // The one toolbar of earlier versions becomes a configuration of the user
            uiSettings.toolbarConfig = toolbars.add(qsTr("My toolbar"), {
                "top1": parseItems(uiSettings.toolbar, bars.top1),
                "float1": parseItems(uiSettings.toolbox, bars.float1)
            })
            uiSettings.toolbar = ""
            uiSettings.toolbox = ""
        }
        toolbars.current = uiSettings.toolbarConfig
        toolbars.currentChanged.connect(() => uiSettings.toolbarConfig = toolbars.current)
        if (paletteFile !== "" && !colorPalette.load(paletteFile)) {
            paletteFile = ""
        }
        if (initialFile.toString() !== "") {
            openDocument(initialFile)
            if (initialPage > 0) {
                canvas.currentPage = initialPage - 1
            }
        } else {
            // A document that was never saved, of a session that did not end regularly
            const orphans = canvas.orphanAutosaves()
            if (orphans.length > 0) {
                recoverDialog.openFor(orphans[0], "")
            } else if (openLastAtStart && recentFiles.files.length > 0) {
                // The document of the last session, as Xournal++ can (autoloadMostRecent)
                openDocument(locationUrl(recentFiles.files[0]))
            }
        }
        if (crashLogs.length > 0) {
            crashDialog.open()
        }
    }

    Shortcut { sequences: [StandardKey.Open]; onActivated: root.guarded(() => fileDialog.open()) }
    Shortcut { sequences: [StandardKey.Print]; onActivated: printDialog.open() }
    Shortcut { sequence: "Ctrl+E"; onActivated: exportDialog.open() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: root.save() }
    Shortcut { sequences: [StandardKey.SaveAs]; onActivated: saveDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; onActivated: canvas.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Y"]; onActivated: canvas.redo() }
    Shortcut { sequences: [StandardKey.ZoomIn]; onActivated: canvas.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: canvas.zoomOut() }
    Shortcut { sequences: [StandardKey.Preferences]; onActivated: preferencesDialog.open() }
    Shortcut { sequence: "Ctrl+0"; onActivated: canvas.zoomTo(1.0) }
    Shortcut { sequence: "F5"; onActivated: root.setPresentation(!canvas.presentationMode) }
    Shortcut { sequences: [StandardKey.FullScreen, "F11"]; onActivated: root.setFullScreen(!root.fullScreen) }
    Shortcut { sequence: "F12"; onActivated: root.sidebarVisible = !root.sidebarVisible }
    Shortcut { sequence: "F10"; onActivated: root.menuBarVisible = !root.menuBarVisible }
    Shortcut { sequence: "F9"; onActivated: root.toolbarVisible = !root.toolbarVisible }
    Shortcut { sequence: "Alt+Left"; onActivated: canvas.navigateBack() }
    Shortcut { sequence: "Ctrl+Alt+R"; enabled: screenCapture.available; onActivated: screenCapture.start(root) }
    Shortcut { sequence: "Alt+Right"; onActivated: canvas.navigateForward() }
    Shortcut {
        sequence: "Escape"
        enabled: canvas.presentationMode || root.fullScreen
        onActivated: root.setPresentation(false)
    }
    Shortcut { sequence: "Ctrl+G"; onActivated: goToPageDialog.open() }
    Shortcut { sequences: ["PgUp"]; onActivated: canvas.currentPage = canvas.currentPage - 1 }
    Shortcut { sequences: ["PgDown"]; onActivated: canvas.currentPage = canvas.currentPage + 1 }
    Shortcut { sequence: "Ctrl+Home"; onActivated: canvas.currentPage = 0 }
    Shortcut { sequence: "Ctrl+End"; onActivated: canvas.currentPage = canvas.pageCount - 1 }
    Shortcut {
        sequences: ["Right", "Space"]
        enabled: canvas.presentationMode
        onActivated: canvas.currentPage = canvas.currentPage + 1
    }
    Shortcut {
        sequences: ["Left", "Backspace"]
        enabled: canvas.presentationMode
        onActivated: canvas.currentPage = canvas.currentPage - 1
    }
    Shortcut { sequences: [StandardKey.Find]; onActivated: searchBar.show() }
    Shortcut { sequences: [StandardKey.FindNext]; onActivated: canvas.searchNext() }
    Shortcut { sequences: [StandardKey.FindPrevious]; onActivated: canvas.searchPrevious() }
    Shortcut { sequences: [StandardKey.Cut]; onActivated: canvas.cut() }
    Shortcut { sequences: [StandardKey.Copy]; onActivated: canvas.copy() }
    Shortcut { sequences: [StandardKey.Paste]; onActivated: canvas.paste() }
    Shortcut { sequences: [StandardKey.SelectAll]; onActivated: canvas.selectAll() }
    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: canvas.tool = PageCanvas.Pen }
    Shortcut { sequence: "Ctrl+Shift+H"; onActivated: canvas.tool = PageCanvas.Highlighter }
    Shortcut { sequence: "Ctrl+Shift+E"; onActivated: canvas.tool = PageCanvas.Eraser }
    Shortcut { sequence: "Ctrl+D"; onActivated: canvas.insertPage(canvas.currentPage + 1) }
    Shortcut { sequence: "Ctrl+Delete"; onActivated: canvas.deletePage(canvas.currentPage) }

    menuBar: MenuBar {
        id: mainMenuBar

        visible: root.modeShows("menubar") && !root.touchUi && root.menuBarVisible

        FitMenu {
            title: qsTr("&File")

            MenuItem { text: qsTr("New"); onTriggered: root.guarded(() => canvas.newDocument()) }
            MenuItem { text: qsTr("Open…"); onTriggered: root.guarded(() => fileDialog.open()) }
            FitMenu {
                title: qsTr("Open recent")
                enabled: recentFiles.files.length > 0

                Repeater {
                    model: recentFiles.files

                    MenuItem {
                        required property string modelData

                        text: recentFiles.fileName(modelData)
                        onTriggered: root.guarded(() => root.openDocument(root.locationUrl(modelData)))

                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                    }
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Clear list"); onTriggered: recentFiles.clear() }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Save")
                enabled: canvas.modified || !canvas.hasFile
                onTriggered: root.save()
            }
            MenuItem { text: qsTr("Save as…"); onTriggered: saveDialog.open() }
            MenuItem { text: qsTr("Save for Xournal++ 1.3…"); onTriggered: root.saveForXournalpp13() }
            MenuSeparator {}
            MenuItem { text: qsTr("Export…"); onTriggered: exportDialog.open() }
            MenuItem { text: qsTr("Print…"); onTriggered: printDialog.open() }
            MenuSeparator {}
            MenuItem { text: qsTr("Quit"); onTriggered: root.close() }
        }

        FitMenu {
            title: qsTr("&Edit")

            MenuItem { text: qsTr("Undo"); enabled: canvas.canUndo; onTriggered: canvas.undo() }
            MenuItem { text: qsTr("Redo"); enabled: canvas.canRedo; onTriggered: canvas.redo() }
            MenuSeparator {}
            MenuItem { text: qsTr("Cut"); enabled: canvas.hasSelection; onTriggered: canvas.cut() }
            MenuItem { text: qsTr("Copy"); enabled: canvas.hasSelection; onTriggered: canvas.copy() }
            MenuItem { text: qsTr("Paste"); enabled: canvas.canPaste; onTriggered: canvas.paste() }
            MenuItem { text: qsTr("Delete"); enabled: canvas.hasSelection; onTriggered: canvas.deleteSelection() }
            MenuSeparator {}
            MenuItem { text: qsTr("Find…"); onTriggered: searchBar.show() }
            MenuSeparator {}
            MenuItem { text: qsTr("Preferences…"); onTriggered: preferencesDialog.open() }
            MenuSeparator {}
            MenuItem { text: qsTr("Select all"); onTriggered: canvas.selectAll() }
            MenuItem {
                text: qsTr("Select on all layers")
                checkable: true
                checked: canvas.selectAllLayers
                onTriggered: canvas.selectAllLayers = checked
            }
            FitMenu {
                title: qsTr("Move selection to layer")
                enabled: canvas.hasSelection

                Repeater {
                    model: canvas.layers

                    MenuItem {
                        required property int index
                        required property var modelData

                        text: modelData.name
                        enabled: index !== canvas.currentLayer
                        onTriggered: canvas.moveSelectionToLayer(index)
                    }
                }
            }
            MenuSeparator {}
            FitMenu {
                title: qsTr("Arrange")
                enabled: canvas.hasSelection

                MenuItem {
                    text: qsTr("Bring to front")
                    onTriggered: canvas.arrangeSelection(PageCanvas.BringToFront)
                }
                MenuItem {
                    text: qsTr("Bring forward")
                    onTriggered: canvas.arrangeSelection(PageCanvas.BringForward)
                }
                MenuItem {
                    text: qsTr("Send backward")
                    onTriggered: canvas.arrangeSelection(PageCanvas.SendBackward)
                }
                MenuItem {
                    text: qsTr("Send to back")
                    onTriggered: canvas.arrangeSelection(PageCanvas.SendToBack)
                }
            }
        }

        FitMenu {
            title: qsTr("&View")

            MenuItem {
                text: qsTr("Paired pages")
                checkable: true
                checked: canvas.pairedPages
                onTriggered: canvas.pairedPages = checked
            }
            MenuItem {
                text: qsTr("First page alone, like a cover")
                enabled: canvas.pairedPages
                checkable: true
                checked: canvas.pairedPagesOffset === 1
                onTriggered: canvas.pairedPagesOffset = checked ? 1 : 0
            }
            MenuItem {
                text: qsTr("Highlight the position of the pointer")
                checkable: true
                checked: canvas.highlightPosition
                onTriggered: canvas.highlightPosition = checked
            }
            MenuItem {
                text: qsTr("Show Menubar") + "\tF10"
                visible: !root.touchUi
                height: visible ? implicitHeight : 0
                checkable: true
                checked: root.menuBarVisible
                onTriggered: root.menuBarVisible = checked
            }
            MenuItem {
                text: qsTr("Show Toolbars") + "\tF9"
                checkable: true
                checked: root.toolbarVisible
                onTriggered: root.toolbarVisible = checked
            }
            MenuItem {
                text: qsTr("Presentation mode")
                checkable: true
                checked: canvas.presentationMode
                onTriggered: root.setPresentation(checked)
            }
            MenuItem {
                text: qsTr("Fullscreen")
                checkable: true
                checked: root.fullScreen
                onTriggered: root.setFullScreen(checked)
            }
            MenuItem {
                text: qsTr("Sidebar")
                checkable: true
                checked: root.sidebarVisible
                onTriggered: root.sidebarVisible = checked
            }
            MenuItem {
                text: qsTr("Input details")
                visible: !root.touchUi
                height: visible ? implicitHeight : 0
                checkable: true
                checked: root.inputInfoVisible
                onTriggered: root.inputInfoVisible = checked
            }
            // The toolbar configurations, as in Xournal++
            FitMenu {
                id: toolbarsMenu

                title: qsTr("Toolbars")

                Instantiator {
                    model: toolbars.configs
                    onObjectAdded: (index, object) => toolbarsMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => toolbarsMenu.removeItem(object)

                    MenuItem {
                        required property var modelData

                        text: modelData.name
                        checkable: true
                        checked: toolbars.shown === modelData.id
                        onTriggered: toolbars.current = modelData.id
                    }
                }

                MenuSeparator {}
                MenuItem { text: qsTr("Customize toolbars…"); onTriggered: toolbarDialog.open() }
            }
            MenuItem { text: qsTr("Floating toolbox"); onTriggered: root.actions.floatingToolbox.trigger() }
            MenuSeparator {}
            FitMenu {
                title: qsTr("Layout")

                FitMenu {
                    id: columnsMenu

                    title: qsTr("Columns")

                    Repeater {
                        model: 8

                        MenuItem {
                            required property int index

                            text: index + 1
                            checkable: true
                            checked: canvas.layoutColumns === index + 1
                            onTriggered: canvas.layoutColumns = index + 1
                        }
                    }
                }
                FitMenu {
                    title: qsTr("Rows")

                    Repeater {
                        model: 8

                        MenuItem {
                            required property int index

                            text: index + 1
                            checkable: true
                            checked: canvas.layoutRows === index + 1
                            onTriggered: canvas.layoutRows = index + 1
                        }
                    }
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Fill columns first")
                    checkable: true
                    checked: canvas.layoutVertical
                    onTriggered: canvas.layoutVertical = checked
                }
                MenuItem {
                    text: qsTr("Right to left")
                    checkable: true
                    checked: canvas.layoutRightToLeft
                    onTriggered: canvas.layoutRightToLeft = checked
                }
                MenuItem {
                    text: qsTr("Bottom to top")
                    checkable: true
                    checked: canvas.layoutBottomToTop
                    onTriggered: canvas.layoutBottomToTop = checked
                }
            }
            MenuSeparator {}
            MenuItem { text: qsTr("Zoom in"); onTriggered: canvas.zoomIn() }
            MenuItem { text: qsTr("Zoom out"); onTriggered: canvas.zoomOut() }
            MenuItem { text: qsTr("Zoom to 100%"); onTriggered: canvas.zoomTo(1.0) }
            MenuItem { text: qsTr("Fit width"); onTriggered: canvas.fitWidth() }
            MenuItem { text: qsTr("Fit page"); onTriggered: canvas.fitPage() }
        }

        FitMenu {
            title: qsTr("&Navigation")

            MenuItem { text: qsTr("Go to page…"); onTriggered: goToPageDialog.open() }
            MenuItem { text: qsTr("Jump Back"); enabled: canvas.canNavigateBack; onTriggered: canvas.navigateBack() }
            MenuItem {
                text: qsTr("Jump Forward")
                enabled: canvas.canNavigateForward
                onTriggered: canvas.navigateForward()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("First page")
                enabled: canvas.currentPage > 0
                onTriggered: canvas.currentPage = 0
            }
            MenuItem {
                text: qsTr("Previous page")
                enabled: canvas.currentPage > 0
                onTriggered: canvas.currentPage = canvas.currentPage - 1
            }
            MenuItem {
                text: qsTr("Next page")
                enabled: canvas.currentPage < canvas.pageCount - 1
                onTriggered: canvas.currentPage = canvas.currentPage + 1
            }
            MenuItem {
                text: qsTr("Last page")
                enabled: canvas.currentPage < canvas.pageCount - 1
                onTriggered: canvas.currentPage = canvas.pageCount - 1
            }
            MenuSeparator {}
            MenuItem {
                text: root.actions.previousAnnotatedPage.text
                enabled: root.actions.previousAnnotatedPage.enabled()
                onTriggered: root.actions.previousAnnotatedPage.trigger()
            }
            MenuItem {
                text: root.actions.nextAnnotatedPage.text
                enabled: root.actions.nextAnnotatedPage.enabled()
                onTriggered: root.actions.nextAnnotatedPage.trigger()
            }
        }

        FitMenu {
            title: qsTr("&Journal")

            MenuItem { text: qsTr("New page before"); onTriggered: canvas.insertPage(canvas.currentPage) }
            MenuItem { text: qsTr("New page after"); onTriggered: canvas.insertPage(canvas.currentPage + 1) }
            MenuItem { text: qsTr("New Page at End"); onTriggered: canvas.insertPage(canvas.pageCount) }
            MenuItem { text: qsTr("Duplicate page"); onTriggered: canvas.duplicatePage(canvas.currentPage) }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Move page towards the beginning")
                enabled: canvas.currentPage > 0
                onTriggered: canvas.movePage(canvas.currentPage, canvas.currentPage - 1)
            }
            MenuItem {
                text: qsTr("Move page towards the end")
                enabled: canvas.currentPage < canvas.pageCount - 1
                onTriggered: canvas.movePage(canvas.currentPage, canvas.currentPage + 1)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Delete page")
                enabled: canvas.pageCount > 1
                onTriggered: canvas.deletePage(canvas.currentPage)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Append New PDF Pages")
                enabled: canvas.pdfPageCount > 0
                onTriggered: {
                    if (canvas.appendNewPdfPages() === 0) {
                        root.showError(qsTr("Append New PDF Pages"), qsTr("All pages of the PDF are in the document."))
                    }
                }
            }
            MenuItem { text: qsTr("Image as background of the page…"); onTriggered: backgroundImageDialog.open() }
            MenuSeparator {}
            MenuItem { text: qsTr("Insert image…"); onTriggered: imageDialog.open() }
            MenuItem {
                text: qsTr("Insert a region of the screen…")
                visible: screenCapture.available
                height: visible ? implicitHeight : 0
                onTriggered: screenCapture.start(root)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Paper format and background…")
                onTriggered: pageSettingsDialog.openFor(canvas.currentPage)
            }
        }

        FitMenu {
            title: qsTr("&Plugins")

            MenuItem { text: qsTr("Plugin manager…"); onTriggered: pluginManagerDialog.open() }
            MenuSeparator {}
            Repeater {
                model: plugins.menuEntries

                MenuItem {
                    required property var modelData

                    // Submenus of a plugin are shown as a path
                    text: (modelData.path !== "" ? modelData.path.split("/").join(" › ") + " › " : "")
                          + modelData.text
                          + (modelData.shortcut !== "" && !root.touchUi ? "\t" + modelData.shortcut : "")
                    onTriggered: plugins.trigger(modelData.id)
                }
            }
        }

        FitMenu {
            title: qsTr("&Help")

            MenuItem {
                text: qsTr("Help")
                onTriggered: Qt.openUrlExternally("https://xournalpp.github.io/guide/overview/")
            }
            MenuItem { text: qsTr("About"); onTriggered: aboutDialog.open() }
        }

        FitMenu {
            title: qsTr("&Tools")

            Repeater {
                model: root.toolNames

                MenuItem {
                    required property int index
                    required property string modelData

                    text: modelData
                    checkable: true
                    checked: canvas.tool === index
                    onTriggered: canvas.tool = index
                }
            }
            MenuSeparator {}
            FitMenu {
                title: qsTr("Draw")
                enabled: root.drawingTool

                Repeater {
                    model: root.drawingTypes

                    MenuItem {
                        required property int index
                        required property string modelData

                        text: modelData
                        checkable: true
                        checked: canvas.drawingType === index
                        onTriggered: canvas.drawingType = index
                    }
                }
            }
            FitMenu {
                title: qsTr("Eraser type")

                Repeater {
                    model: root.eraserTypes

                    MenuItem {
                        required property int index
                        required property string modelData

                        text: modelData
                        checkable: true
                        checked: canvas.eraserType === index
                        onTriggered: canvas.eraserType = index
                    }
                }
            }
            MenuItem {
                text: qsTr("Fill")
                enabled: root.drawingTool
                checkable: true
                checked: canvas.fill
                onTriggered: canvas.fill = checked
            }
            MenuItem {
                text: root.actions.fillOpacity.text
                enabled: root.drawingTool
                onTriggered: root.actions.fillOpacity.trigger()
            }
            MenuItem {
                text: root.actions.pdfMarkerOpacity.text
                onTriggered: root.actions.pdfMarkerOpacity.trigger()
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Setsquare")
                checkable: true
                checked: canvas.geometryTool === PageCanvas.Setsquare
                onTriggered: canvas.geometryTool = checked ? PageCanvas.Setsquare : PageCanvas.NoGeometryTool
            }
            MenuItem {
                text: qsTr("Compass")
                checkable: true
                checked: canvas.geometryTool === PageCanvas.Compass
                onTriggered: canvas.geometryTool = checked ? PageCanvas.Compass : PageCanvas.NoGeometryTool
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Grid Snapping")
                checkable: true
                checked: canvas.input.snapGrid
                onTriggered: canvas.input.snapGrid = checked
            }
            MenuItem {
                text: qsTr("Rotation Snapping")
                checkable: true
                checked: canvas.input.snapRotation
                onTriggered: canvas.input.snapRotation = checked
            }
            MenuItem {
                text: qsTr("Touch Drawing")
                checkable: true
                checked: canvas.fingerDraws
                onTriggered: canvas.fingerDraws = checked
            }
            FitMenu {
                title: qsTr("Audio")
                enabled: audio.available

                Repeater {
                    model: ["record", "audioPause", "audioStop", "audioSeekBackwards", "audioSeekForwards"]

                    MenuItem {
                        required property string modelData
                        readonly property var entry: root.actions[modelData]

                        text: entry.text
                        checkable: entry.checked !== undefined
                        checked: entry.checked ? entry.checked() : false
                        enabled: entry.enabled()
                        onTriggered: entry.trigger()
                    }
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Recordings of the document…"); onTriggered: recordingsDialog.open() }
            }
            MenuItem { text: qsTr("Pen input…"); onTriggered: inputSettingsDialog.open() }
            MenuSeparator {}
            MenuItem { text: qsTr("Load colour palette…"); onTriggered: paletteDialog.open() }
            MenuItem {
                text: qsTr("Default colour palette")
                onTriggered: {
                    colorPalette.reset()
                    root.paletteFile = ""
                }
            }
        }
    }

    // A menu that is as wide as its widest entry: the styles give menus a fixed width, which cuts long texts
    component FitMenu: Menu {
        id: fitMenu

        // Not below the status bar or the navigation bar of a phone
        topMargin: root.safeMargin("top")
        bottomMargin: root.safeMargin("bottom")
        leftMargin: root.safeMargin("left")
        rightMargin: root.safeMargin("right")


        // A submenu that is disabled is still an enabled entry of the menu above it: Qt only disables what opens.
        // Its entry there is its parent
        Binding {
            target: fitMenu.parent instanceof MenuItem ? fitMenu.parent : null
            property: "enabled"
            value: fitMenu.enabled
            when: fitMenu.parent instanceof MenuItem
        }

        onAboutToShow: root.fitMenuWidth(fitMenu)
        // The bar at the top only exists once the menu is laid out: a menu of short entries may be too narrow
        // for it
        onOpened: {
            const bar = contentItem.headerItem
            if (hasBar && bar && bar.wanted > width) {
                root.fitMenuWidth(fitMenu)
            }
        }

        // In the arrangement for fingers a submenu covers the menu it belongs to, and there was no way back but
        // to tap next to the menus, which closes all of them: a bar at its top leads back to the menu above.
        // The menu this one is a submenu of. That menu says so when it opens (fitMenuWidth): where submenus
        // open over their menu (Android), a submenu cannot tell by itself
        property Menu menuAbove: null
        readonly property bool leadsBack: root.touchUi && menuAbove !== null
        /// Closes this submenu and shows the menu it belongs to: where submenus open over their menu, that
        /// one was closed
        function goBack() {
            const above = menuAbove
            close()
            Qt.callLater(() => {
                if (above && !above.visible) {
                    above.open()
                }
            })
        }
        /// The menu behind the menu button of the arrangement for fingers
        property bool isMainMenu: false
        readonly property bool hasBar: root.touchUi && (leadsBack || isMainMenu)
        // A bar at the top that stays there when the entries are scrolled: "Back" in a submenu, the title, and
        // "Close", which closes all menus
        Component {
            id: menuBarRow

            Rectangle {
                id: bar

                // Counted by hand: the layout does not count the whole title, which may be shortened
                readonly property real wanted: (backButton.visible ? backButton.implicitWidth + barRow.spacing : 0)
                                               + titleMetrics.advanceWidth + barRow.spacing
                                               + closeButton.implicitWidth + 16

                TextMetrics {
                    id: titleMetrics

                    font: titleLabel.font
                    text: titleLabel.text
                }

                width: ListView.view ? ListView.view.width : wanted
                height: fitMenu.hasBar ? Math.max(44, barRow.implicitHeight + 8) : 0
                visible: fitMenu.hasBar
                z: 3  // above the entries, which scroll below it
                // The colour of the menu, which the style of Android does not take from the palette
                color: fitMenu.background && fitMenu.background.color !== undefined ? fitMenu.background.color
                                                                                    : palette.window

                RowLayout {
                    id: barRow

                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 8

                    ToolButton {
                        id: backButton

                        visible: fitMenu.leadsBack
                        text: "‹  " + qsTr("Back")
                        onClicked: fitMenu.goBack()
                    }
                    Label {
                        id: titleLabel

                        Layout.fillWidth: true
                        text: fitMenu.title.replace("&", "")
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    ToolButton {
                        id: closeButton

                        text: "×  " + qsTr("Close")
                        onClicked: fitMenu.dismiss()
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: palette.mid
                }
            }
        }
        Component.onCompleted: {
            if (contentItem instanceof ListView) {
                contentItem.header = menuBarRow
                contentItem.headerPositioning = ListView.OverlayHeader
            }
        }
    }

    /// Makes a menu as wide as its widest entry: the styles give it a width of their own, and longer entries were
    /// cut off on the right. Called when the menu is about to show
    function fitMenuWidth(menu) {
        // A long menu starts at its first entry, as every menu does, with its entries where they are shown: a
        // list that was scrolled once jumped to another place under the finger, and the entry that came to be
        // there was chosen
        if (!menu.visible && menu.contentItem instanceof ListView) {
            menu.contentItem.forceLayout()
            menu.contentItem.positionViewAtBeginning()
        }
        let widest = 200
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item) {
                // The entry of a submenu also has its arrow, which its width does not count
                const arrow = item.subMenu && item.arrow ? item.arrow.width + item.spacing : 0
                widest = Math.max(widest, item.implicitWidth + arrow)
                if (item.subMenu && item.subMenu.menuAbove !== undefined) {
                    item.subMenu.menuAbove = menu
                }
            }
        }
        // The bar at the top of a menu in the arrangement for fingers
        const bar = menu.contentItem.headerItem
        if (bar && menu.hasBar) {
            widest = Math.max(widest, bar.wanted)
        }
        menu.width = Math.min(Math.ceil(widest) + 12, root.width - 16)
    }

    // The menus of the menu bar, in the touch layout
    FitMenu {
        id: mainMenu

        isMainMenu: true
    }

    component Strip: ToolbarStrip {
        app: root
        canvas: canvas
        colorPalette: colorPalette
        theme: theme
        iconSize: root.buttonSize
    }

    header: Column {
        // In the touch layout the first toolbar has the button for the menu: it stays
        visible: root.toolbarVisible && root.modeShows("toolbars") || root.touchUi && !canvas.presentationMode

        Strip {
            id: topStrip

            width: parent.width
            edge: "top"
            items: root.bars.top1
            visible: items.length > 0 || root.touchUi
            leading: root.touchUi ? menuButtonComponent : null
        }
        Strip {
            width: parent.width
            edge: topStrip.visible ? "" : "top"
            items: root.bars.top2
            visible: items.length > 0 && root.toolbarVisible
        }
    }

    Component {
        id: menuButtonComponent

        Row {
            spacing: 4

            ToolButton {
                id: menuButton

                implicitWidth: root.buttonSize + 16
                implicitHeight: root.buttonSize + 12
                onClicked: mainMenu.popup(menuButton, 0, menuButton.height)

                Accessible.name: qsTr("Menu")

                // Three bars: fonts of some platforms lack the sign for it
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: root.buttonSize * 0.16

                        Repeater {
                            model: 3

                            Rectangle {
                                width: root.buttonSize * 0.7
                                height: Math.max(2, root.buttonSize * 0.08)
                                radius: height / 2
                                color: theme.colors.buttonText
                            }
                        }
                    }
                }
            }
            ToolbarItem {
                itemId: "sidebar"
                app: root
                canvas: canvas
                colorPalette: colorPalette
                theme: theme
                iconSize: root.buttonSize
            }
        }
    }

    footer: Column {
        visible: root.modeShows("toolbars")

        Strip {
            width: parent.width
            edge: bottomStrip.visible || infoBar.visible ? "" : "bottom"
            items: root.bars.bottom1
            visible: items.length > 0 && root.toolbarVisible
        }
        Strip {
            id: bottomStrip

            width: parent.width
            edge: infoBar.visible ? "" : "bottom"
            items: root.bars.bottom2
            visible: items.length > 0 && root.toolbarVisible
        }
        ToolBar {
            id: infoBar

            width: parent.width
            height: 32
            visible: !root.touchUi && root.inputInfoVisible

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: canvas.inputInfo !== "" ? canvas.inputInfo
                                                  : qsTr("Draw with a pen, the mouse or a finger to see input details")
                }
                // Not twice: a toolbar may have the page number
                Label {
                    visible: pageSpin.visible
                    text: qsTr("Page")
                }
                SpinBox {
                    id: pageSpin

                    Layout.preferredHeight: 26
                    visible: !root.allBarItems.includes("page")
                    from: 1
                    to: Math.max(canvas.pageCount, 1)
                    editable: true
                    value: canvas.currentPage + 1
                    onValueModified: canvas.currentPage = value - 1
                }
                Label {
                    visible: pageSpin.visible
                    text: qsTr("of %1").arg(canvas.pageCount)
                }
            }
        }
    }

    // The toolbars at the sides
    Row {
        id: leftBars

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        visible: root.toolbarVisible && root.modeShows("toolbars")

        Strip {
            id: leftStrip

            height: parent.height
            vertical: true
            edge: "left"
            items: root.bars.left1
            visible: items.length > 0
        }
        Strip {
            height: parent.height
            vertical: true
            edge: leftStrip.visible ? "" : "left"
            items: root.bars.left2
            visible: items.length > 0
        }
    }
    Row {
        id: rightBars

        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        visible: root.toolbarVisible && root.modeShows("toolbars")

        Strip {
            height: parent.height
            vertical: true
            edge: rightStrip.visible ? "" : "right"
            items: root.bars.right1
            visible: items.length > 0
        }
        Strip {
            id: rightStrip

            height: parent.height
            vertical: true
            edge: "right"
            items: root.bars.right2
            visible: items.length > 0
        }
    }

    // Pages, layers and the outline of the PDF
    Pane {
        id: sidebar

        anchors.left: root.sidebarRight ? undefined : leftBars.visible ? leftBars.right : parent.left
        anchors.right: root.sidebarRight ? (rightBars.visible ? rightBars.left : parent.right) : undefined
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.touchUi ? Math.min(260, parent.width * 0.8) : 190
        padding: 0
        visible: root.sidebarVisible && root.modeShows("sidebar")
        z: root.touchUi ? 2 : 0  // on top of the page there

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TabBar {
                id: sidebarTabs

                Layout.fillWidth: true

                TabButton { text: qsTr("Pages") }
                TabButton { text: qsTr("Layers") }
                TabButton { text: qsTr("Outline") }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: sidebarTabs.currentIndex

                PageSidebar { canvas: canvas; numbers: root.previewNumbers }
                LayerSidebar { canvas: canvas }
                OutlineSidebar { canvas: canvas }
            }
        }
    }

    // The pages, with scroll bars next to them
    Item {
        id: canvasArea

        // Next to the toolbars at the sides and to the sidebar, where that is not on top of the pages. As margins:
        // anchors that change what they are anchored to made a loop when the toolbars were filled
        readonly property real sidebarRoom: !root.touchUi && sidebar.visible ? sidebar.width : 0

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: (leftBars.visible ? leftBars.width : 0) + (root.sidebarRight ? 0 : sidebarRoom)
        anchors.rightMargin: (rightBars.visible ? rightBars.width : 0) + (root.sidebarRight ? sidebarRoom : 0)
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        // Behind the scroll bars it looks as around the pages, not as the window
        Rectangle {
            anchors.fill: parent
            color: canvas.einkMode ? "white" : canvas.canvasColor
        }

        PageCanvas {
            id: canvas

            einkMode: theme.eink

            // Room for the scroll bars, which are next to it: pen input on them is not for the page
            anchors.fill: parent
            anchors.leftMargin: vScroll.visible && root.scrollbarSide === "left" ? vScroll.width : 0
            anchors.rightMargin: vScroll.visible && root.scrollbarSide === "right" ? vScroll.width : 0
            anchors.bottomMargin: hScroll.visible ? hScroll.height : 0
            // A page that is wider than the view must not be drawn over the sidebar and the toolbars next to it
            clip: true
            // Keeps pen input away from the page while a dialog is shown on top of it
            enabled: !errorDialog.visible && !discardDialog.visible && !pageSettingsDialog.visible
                     && !goToPageDialog.visible && !inputSettingsDialog.visible && !linkDialog.visible
                     && !latexDialog.visible && !exportDialog.visible && !printDialog.visible
                     && !recoverDialog.visible && !preferencesDialog.visible && !toolbarDialog.visible
                     && !recordingsDialog.visible
                     && !pluginManagerDialog.visible && !pluginDialog.visible && !aboutDialog.visible

            onLoadFailed: message => root.showError(qsTr("The file could not be opened"), message)
            onExportFailed: message => root.showError(qsTr("The document could not be exported"), message)
            onPasteFailed: message => root.showError(qsTr("The clipboard could not be pasted"), message)
            // The list of recent files, and the page each of them was left at
            onFileOpened: path => {
                recentFiles.add(path)
                root.trackedFile = ""
                canvas.currentPage = recentFiles.pageOf(path)
                root.trackedFile = path
            }
            onFileSaved: path => {
                recentFiles.add(path)
                root.trackedFile = path
                recentFiles.setPage(path, canvas.currentPage)
            }
            onCurrentPageChanged: {
                if (canvas.hasFile && canvas.filePath === root.trackedFile) {
                    recentFiles.setPage(canvas.filePath, canvas.currentPage)
                }
            }
            audioRecording: audio.recordingFile
            onAudioPlayRequested: (filename, timestamp) => audio.play(filename, timestamp, canvas.filePath)
            onImageRequested: pos => {
                imageDialog.position = pos
                imageDialog.open()
            }
            onFloatingToolboxRequested: pos => floatingToolbox.openAt(pos)
            onLinkRequested: (text, url, existing) => linkDialog.openFor(text, url, existing)
            onOpenLinkRequested: url => Qt.openUrlExternally(url)
            onLatexRequested: (source, existing) => latexDialog.openFor(source, existing)

            TextEditor {
                anchors.fill: parent
                canvas: canvas
            }

            // What can be done with text selected in the PDF, next to it
            Pane {
                visible: canvas.hasPdfSelection
                x: Math.max(4, Math.min(canvas.pdfSelectionRect.x, canvas.width - width - 4))
                y: canvas.pdfSelectionRect.y + canvas.pdfSelectionRect.height + 6 + height < canvas.height
                   ? canvas.pdfSelectionRect.y + canvas.pdfSelectionRect.height + 6
                   : Math.max(4, canvas.pdfSelectionRect.y - height - 6)
                padding: 2

                RowLayout {
                    spacing: 0

                    ToolButton {
                        text: qsTr("Copy")
                        onClicked: {
                            canvas.copyPdfSelection()
                            canvas.clearPdfSelection()
                        }
                    }
                    ToolButton { text: qsTr("Highlight"); onClicked: canvas.highlightPdfSelection() }
                    ToolButton { text: qsTr("Underline"); onClicked: canvas.underlinePdfSelection() }
                    ToolButton { text: qsTr("Strike through"); onClicked: canvas.strikeThroughPdfSelection() }
                }
            }

            // The page that is shown, in the touch layout, which has no status bar. A tap goes to another page
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                width: pageIndicator.implicitWidth + 24
                height: pageIndicator.implicitHeight + 16
                radius: height / 2
                visible: root.touchUi && !canvas.presentationMode
                color: Qt.rgba(0, 0, 0, 0.55)

                Label {
                    id: pageIndicator

                    anchors.centerIn: parent
                    color: "white"
                    text: qsTr("%1 / %2").arg(canvas.currentPage + 1).arg(canvas.pageCount)
                }
                TapHandler { onTapped: goToPageDialog.open() }
            }

            // Search in the texts and in the PDF
            Pane {
                id: searchBar

                function show() {
                    visible = true
                    searchField.forceActiveFocus()
                    searchField.selectAll()
                }

                function hide() {
                    visible = false
                    canvas.clearSearch()
                    canvas.forceActiveFocus()
                }

                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                visible: false
                padding: 4

                RowLayout {
                    spacing: 4

                    TextField {
                        id: searchField

                        Layout.preferredWidth: 200
                        placeholderText: qsTr("Find")
                        onTextEdited: canvas.search(text)
                        onAccepted: canvas.searchNext()
                        Keys.onEscapePressed: searchBar.hide()
                    }
                    Label {
                        visible: searchField.text !== ""
                        text: canvas.searchResultCount > 0
                              ? qsTr("%1 of %2 on page %3").arg(canvas.searchResultIndex).arg(canvas.searchResultCount)
                                                           .arg(canvas.currentPage + 1)
                              : qsTr("Not found")
                    }
                    ToolButton { text: "↑"; enabled: canvas.searchResultCount > 0; onClicked: canvas.searchPrevious() }
                    ToolButton { text: "↓"; enabled: canvas.searchResultCount > 0; onClicked: canvas.searchNext() }
                    ToolButton { text: "×"; onClicked: searchBar.hide() }
                }
            }

            // Images and texts dragged from other applications
            DropArea {
                anchors.fill: parent
                onDropped: drop => {
                    if (drop.hasUrls) {
                        for (const url of drop.urls) {
                            canvas.insertImageAt(url, Qt.point(drop.x, drop.y))
                        }
                        drop.accept()
                    } else if (drop.hasText) {
                        canvas.insertTextAt(drop.text, Qt.point(drop.x, drop.y))
                        drop.accept()
                    }
                }
            }
            onSaveFailed: message => root.showError(qsTr("The document could not be saved"), message)
        }

        // As Xournal++ has them: they follow the view, and moving them moves it
        ScrollBar {
            id: vScroll

            anchors.top: parent.top
            anchors.bottom: hScroll.visible ? hScroll.top : parent.bottom
            x: root.scrollbarSide === "left" ? 0 : parent.width - width
            orientation: Qt.Vertical
            // Keeps its place when there is nothing to scroll: else the view would change its size by it
            visible: root.scrollbarSide !== "hidden" && !canvas.presentationMode
            policy: canvas.scrollView.height < 0.999 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            size: canvas.scrollView.height
            position: canvas.scrollView.y
            onPositionChanged: {
                if (pressed) {
                    canvas.scrollToFraction(canvas.scrollView.x, position)
                }
            }
        }
        ScrollBar {
            id: hScroll

            anchors.left: root.scrollbarSide === "left" && vScroll.visible ? vScroll.right : parent.left
            anchors.right: root.scrollbarSide === "right" && vScroll.visible ? vScroll.left : parent.right
            anchors.bottom: parent.bottom
            orientation: Qt.Horizontal
            // Keeps its place when there is nothing to scroll: else the view would change its size by it
            visible: root.scrollbarSide !== "hidden" && !canvas.presentationMode
            policy: canvas.scrollView.width < 0.999 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            size: canvas.scrollView.width
            position: canvas.scrollView.x
            onPositionChanged: {
                if (pressed) {
                    canvas.scrollToFraction(position, canvas.scrollView.y)
                }
            }
        }
    }

    FileDialog {
        id: fileDialog

        title: qsTr("Open file")
        nameFilters: [qsTr("Xournal++ and PDF files (*.xopp *.xoj *.pdf)"), qsTr("All files (*)")]
        currentFolder: root.lastFolder
        onAccepted: root.openDocument(selectedFile)
    }

    RecentFiles { id: recentFiles }

    // A region of the screen as an image on the page: the window hides while the region is selected
    ScreenCapture {
        id: screenCapture

        onCaptured: image => canvas.insertPicture(image)
        onFailed: message => root.showError(qsTr("Insert a region of the screen"), message)
    }

    ExportDialog {
        id: exportDialog

        canvas: canvas
        namePattern: root.exportNamePattern
        folder: root.lastFolder
    }

    PrintDialog {
        id: printDialog

        canvas: canvas
    }

    Dialog {
        id: recoverDialog

        property string autosave: ""
        property url original

        function openFor(autosaveFile, originalUrl) {
            autosave = autosaveFile
            original = originalUrl
            open()
        }

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        modal: true
        Overlay.modal: ModalDim {}
        closePolicy: Popup.NoAutoClose
        title: qsTr("Recover unsaved changes?")

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: recoverDialog.original.toString() !== ""
                      ? qsTr("The last session did not end regularly. An automatically saved version of this document "
                             + "is newer than the file.")
                      : qsTr("The last session did not end regularly. A document that was never saved is still there.")
            }
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Recover")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: canvas.recoverAutosave(recoverDialog.autosave, recoverDialog.original)
            }
            Button {
                text: qsTr("Discard")
                DialogButtonBox.buttonRole: DialogButtonBox.DestructiveRole
                onClicked: {
                    canvas.discardAutosave(recoverDialog.autosave)
                    if (recoverDialog.original.toString() !== "") {
                        canvas.openFile(recoverDialog.original)
                    }
                    recoverDialog.close()
                }
            }
        }
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Save file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xopp"
        nameFilters: [qsTr("Xournal++ files (*.xopp)")]
        currentFolder: root.lastFolder
        // The name it suggests, as in Xournal++: from a pattern with the date
        onVisibleChanged: {
            if (visible && !canvas.hasFile) {
                selectedFile = currentFolder + "/" + canvas.nameFromPattern(root.saveNamePattern) + ".xopp"
            }
        }
        onAccepted: {
            root.lastFolder = root.folderOf(selectedFile)
            canvas.saveAs(selectedFile)
        }
    }

    // Xournal++ 1.3.8 and earlier cannot read what came later: what the copy for them loses
    Dialog {
        id: oldFormatDialog

        property var changes: []

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Save for Xournal++ 1.3")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: oldFormatFileDialog.open()

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Xournal++ 1.3.8 and earlier cannot open files with links or turned texts and images. In "
                           + "the copy for them:") + "\n\n• " + oldFormatDialog.changes.join("\n• ") + "\n\n"
                      + qsTr("This document stays as it is.")
            }
        }
    }

    FileDialog {
        id: oldFormatFileDialog

        title: qsTr("Save for Xournal++ 1.3")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "xopp"
        nameFilters: [qsTr("Xournal++ files (*.xopp)")]
        currentFolder: root.lastFolder
        onVisibleChanged: {
            if (visible) {
                selectedFile = currentFolder + "/" + canvas.nameFromPattern("%{name}-1.3") + ".xopp"
            }
        }
        onAccepted: {
            root.lastFolder = root.folderOf(selectedFile)
            canvas.saveForXournalpp13(selectedFile)
        }
    }

    ColorPalette {
        id: colorPalette

        onLoadFailed: message => root.showError(qsTr("The palette could not be loaded"), message)
    }

    FileDialog {
        id: paletteDialog

        title: qsTr("Load colour palette")
        nameFilters: [qsTr("GIMP palettes (*.gpl)"), qsTr("All files (*)")]
        onAccepted: {
            if (colorPalette.load(selectedFile)) {
                root.paletteFile = selectedFile
            }
        }
    }

    FileDialog {
        id: imageDialog

        // Where the image tool was used; else the image goes to the middle of the view
        property var position: null

        title: qsTr("Insert image")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.svg *.webp)"), qsTr("All files (*)")]
        currentFolder: root.lastImageFolder
        onAccepted: {
            root.lastImageFolder = root.folderOf(selectedFile)
            if (position !== null) {
                canvas.insertImageAt(selectedFile, position)
            } else {
                canvas.insertImage(selectedFile)
            }
            position = null
        }
        onRejected: position = null
    }

    FileDialog {
        id: backgroundImageDialog

        title: qsTr("Image as background of the page")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.svg *.webp)"), qsTr("All files (*)")]
        onAccepted: canvas.setPageBackgroundImage(canvas.currentPage, selectedFile)
    }

    Dialog {
        id: aboutDialog

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 460)
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("About Qournal")

        footer: DialogButtonBox {
            Button {
                text: qsTr("Licenses…")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: licensesDialog.open()
            }
            Button {
                text: qsTr("Close")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                onLinkActivated: link => Qt.openUrlExternally(link)
                text: qsTr("Version %1<br><br>A project of its own, not made by the developers of "
                           + "<a href=\"https://xournalpp.github.io\">Xournal++</a>. It reads and writes the "
                           + "files of Xournal++ and has its tools, written anew on Qt, with parts of its source "
                           + "code, its icons, plugins and translations.<br><br>"
                           + "Free software under the GNU General Public License, version 2 or later. It comes without "
                           + "any warranty.<br><br>"
                           + "<a href=\"https://vereo.ch/software/qournal\">vereo.ch/software/qournal</a><br>"
                           + "The source code is at "
                           + "<a href=\"https://github.com/patois87/qournal\">github.com/patois87/qournal</a>.")
                      .arg(Qt.application.version)
            }
        }
    }

    // The license of the application and those of the parts of others in it
    Dialog {
        id: licensesDialog

        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 640 * Math.max(1, font.pixelSize / 13))
        height: Math.min(parent.height - 32, 600 * Math.max(1, font.pixelSize / 13))
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Licenses")
        standardButtons: Dialog.Close

        Licenses { id: licenses }

        ColumnLayout {
            anchors.fill: parent

            FitComboBox {
                id: licenseBox

                Layout.fillWidth: true
                model: licenses.documents
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextArea {
                    readonly property bool overview: licenseBox.currentText.endsWith(".md")

                    readOnly: true
                    wrapMode: TextArea.Wrap
                    // The overview is written with marks for a table and links; the licenses are plain texts
                    textFormat: overview ? TextEdit.MarkdownText : TextEdit.PlainText
                    font.family: overview ? licensesDialog.font.family : "monospace"
                    font.pixelSize: Math.round(licensesDialog.font.pixelSize * 0.9)
                    text: licensesDialog.visible ? licenses.text(licenseBox.currentText) : ""
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
        }
    }

    LinkDialog {
        id: linkDialog

        canvas: canvas
    }

    LatexDialog {
        id: latexDialog

        canvas: canvas
    }

    InputSettingsDialog {
        id: inputSettingsDialog

        canvas: canvas
        onClosed: canvas.saveSettings()
    }

    PreferencesDialog {
        id: preferencesDialog

        app: root
        canvas: canvas
        theme: theme
        localization: localization
        audio: audio
        onPenInputRequested: inputSettingsDialog.open()
    }

    // What plugins print: Xournal++ shows it only in the terminal, but plugins tell there why they did nothing
    Rectangle {
        id: pluginOutput

        property string plugin: ""
        property var lines: []

        function add(name, text) {
            lines = (name === plugin && visible ? lines : []).concat(text.split("\n")).slice(-12)
            plugin = name
            visible = true
            hideTimer.restart()
        }

        parent: Overlay.overlay
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: Math.min(parent.width - 32, outputColumn.implicitWidth + 24)
        height: outputColumn.implicitHeight + 16
        z: 10
        visible: false
        radius: 6
        color: theme.colors.window
        border.color: theme.colors.disabledText

        Timer {
            id: hideTimer

            interval: 10000
            onTriggered: pluginOutput.visible = false
        }
        TapHandler { onTapped: pluginOutput.visible = false }

        Column {
            id: outputColumn

            x: 12
            y: 8
            width: Math.min(implicitWidth, pluginOutput.parent.width - 56)
            spacing: 2

            Label {
                font.bold: true
                text: qsTr("Plugin \"%1\"").arg(pluginOutput.plugin)
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: pluginOutput.lines.join("\n")
            }
        }
    }

    // The last session crashed: what was saved and where the log is
    Dialog {
        id: crashDialog

        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 520 * Math.max(1, font.pixelSize / 13))
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("The application crashed")
        standardButtons: Dialog.Ok

        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The last session ended with a crash. A document with unsaved changes was saved and is "
                           + "offered for recovery when it is opened. What happened is written in:")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                font.family: "monospace"
                text: root.crashLogs.join("\n")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Please report the crash, with this log, to the developers.")
            }
            Button {
                // The folder is private to the application there
                visible: Qt.platform.os !== "android" && Qt.platform.os !== "ios"
                text: qsTr("Open the folder of the log")
                onClicked: {
                    const folder = root.crashLogs[0].replace(/\/[^\/]*$/, "")
                    Qt.openUrlExternally((folder.startsWith("/") ? "file://" : "file:///") + folder)
                }
            }
        }
    }

    RecordingsDialog {
        id: recordingsDialog

        canvas: canvas
        audio: audio
    }

    ToolbarDialog {
        id: toolbarDialog

        app: root
        toolbars: toolbars
    }

    // Chooses an opacity, as the dialog of Xournal++ for the filling and the marker of PDF text
    Dialog {
        id: opacityDialog

        property var apply: null

        /// @param alpha 1 to 255
        function openFor(title, alpha, apply) {
            opacityDialog.title = title
            opacityDialog.apply = apply
            opacitySlider.value = Math.round(alpha * 100 / 255)
            open()
        }

        anchors.centerIn: parent
        modal: true
        Overlay.modal: ModalDim {}
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: apply(Math.max(1, Math.round(opacitySlider.value * 255 / 100)))

        RowLayout {
            Slider {
                id: opacitySlider

                Layout.preferredWidth: 240
                from: 1
                to: 100
                stepSize: 1
            }
            Label {
                Layout.preferredWidth: 48
                text: Math.round(opacitySlider.value) + " %"
            }
        }
    }

    ColorDialog {
        id: toolColorDialog

        onAccepted: canvas.color = selectedColor
    }

    PluginManagerDialog {
        id: pluginManagerDialog

        plugins: plugins
    }

    // A message of a plugin, with the buttons it asks for
    Dialog {
        id: pluginDialog

        property var current: null
        property var waiting: []
        property int answer: 0

        function show(message) {
            if (current !== null) {
                waiting.push(message)  // one after the other
                return
            }
            current = message
            answer = 0
            open()
        }

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        modal: true
        Overlay.modal: ModalDim {}
        title: current === null ? "" : current.error ? qsTr("Error in the plugin \"%1\"").arg(current.plugin)
                                                     : current.plugin

        onClosed: {
            const finished = current
            current = null
            plugins.dialogFinished(finished.request, answer)
            if (current === null && waiting.length > 0) {
                show(waiting.shift())
            }
        }

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: pluginDialog.current?.message ?? ""
            }
        }

        footer: DialogButtonBox {
            Repeater {
                model: pluginDialog.current?.buttons ?? []

                Button {
                    required property var modelData

                    text: modelData.text
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: {
                        pluginDialog.answer = modelData.id
                        pluginDialog.close()
                    }
                }
            }
        }
    }

    FileDialog {
        id: pluginFileDialog

        property int request: -1

        function openFor(newRequest, save, suggestion, filters) {
            if (visible) {
                plugins.fileDialogFinished(newRequest, "")  // one at a time
                return
            }
            request = newRequest
            fileMode = save ? FileDialog.SaveFile : FileDialog.OpenFile
            nameFilters = filters.length > 0 ? [filters.join(" "), qsTr("All files (*)")] : [qsTr("All files (*)")]
            if (save && suggestion !== "") {
                selectedFile = suggestion.startsWith("/") || suggestion.includes(":")
                               ? recentFiles.locationUrl(suggestion) : currentFolder + "/" + suggestion
            }
            open()
        }

        title: fileMode === FileDialog.SaveFile ? qsTr("Save file") : qsTr("Open file")
        onAccepted: plugins.fileDialogFinished(request, selectedFile)
        onRejected: plugins.fileDialogFinished(request, "")
    }

    // Tools at the pointer, shown by a button of the pen or of the mouse
    Popup {
        id: floatingToolbox

        // Not below the status bar or the navigation bar of a phone
        topMargin: root.safeMargin("top")
        bottomMargin: root.safeMargin("bottom")
        leftMargin: root.safeMargin("left")
        rightMargin: root.safeMargin("right")

        /// @param pos in the canvas
        function openAt(pos) {
            const p = canvas.mapToItem(Overlay.overlay, pos)
            x = Math.max(0, Math.min(p.x - width / 2, root.width - width))
            y = Math.max(0, Math.min(p.y - height / 2, root.contentItem.height - height))
            open()
        }

        parent: Overlay.overlay
        width: 6 * (root.iconSize + 22) + leftPadding + rightPadding
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        Flow {
            id: toolboxFlow

            width: parent.width
            spacing: 2

            Repeater {
                model: root.toolboxItems.filter(id => id !== "separator" && id !== "spacer")

                ToolbarItem {
                    required property string modelData

                    itemId: modelData
                    app: root
                    canvas: canvas
                    colorPalette: colorPalette
                    theme: theme
                    iconSize: root.buttonSize
                    maxWidth: toolboxFlow.width
                    onTriggered: floatingToolbox.close()
                }
            }
        }
    }

    PageSettingsDialog {
        id: pageSettingsDialog

        canvas: canvas
        unit: root.paperUnit
    }

    Dialog {
        id: goToPageDialog

        anchors.centerIn: parent
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Go to page")
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAboutToShow: {
            goToPageBox.value = canvas.currentPage + 1
            goToPageBox.forceActiveFocus()
        }
        onAccepted: canvas.currentPage = goToPageBox.value - 1

        RowLayout {
            SpinBox {
                id: goToPageBox

                from: 1
                to: Math.max(canvas.pageCount, 1)
                editable: true
                Keys.onReturnPressed: goToPageDialog.accept()
                Keys.onEnterPressed: goToPageDialog.accept()
            }
            Label { text: qsTr("of %1").arg(canvas.pageCount) }
        }
    }

    Dialog {
        id: discardDialog

        property var action: null

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        modal: true
        Overlay.modal: ModalDim {}
        title: qsTr("Discard unsaved changes?")
        standardButtons: Dialog.Discard | Dialog.Cancel

        onDiscarded: {
            close()
            if (action) {
                action()
            }
        }

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("\"%1\" has changes that are not saved. They are lost if you continue.").arg(canvas.title)
            }
        }
    }

    Dialog {
        id: errorDialog

        anchors.centerIn: parent
        width: Math.min(root.width - 32, 480)
        modal: true
        Overlay.modal: ModalDim {}
        standardButtons: Dialog.Ok

        // A layout gives the text its width: with a width of its own, its height and that of the dialog
        // would depend on each other
        ColumnLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                id: errorLabel

                wrapMode: Text.Wrap
            }
        }
    }
}
