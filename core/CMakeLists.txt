# Document model, file format and rendering: no dependency on Qt Quick
add_library(xoj-core STATIC
    BackgroundConfig.h
    Document.cpp
    Document.h
    Eraser.cpp
    Eraser.h
    Export.cpp
    Export.h
    GeometryTool.cpp
    GeometryTool.h
    LatexTemplate.cpp
    LatexTemplate.h
    Palette.cpp
    PdfCrypt.cpp
    PdfCrypt.h
    PdfFunction.cpp
    PdfFunction.h
    PdfPage.cpp
    PdfPage.h
    PdfEncodings.h
    PdfFile.cpp
    PdfFile.h
    Palette.h
    PageLayout.cpp
    PageLayout.h
    Renderer.cpp
    Renderer.h
    Selection.cpp
    Selection.h
    ShapeRecognizer.cpp
    ShapeRecognizer.h
    Shapes.cpp
    Shapes.h
    Snapping.cpp
    Snapping.h
    SplineSegment.cpp
    SplineSegment.h
    StrokeBuilder.cpp
    StrokeBuilder.h
    StrokeStabilizer.cpp
    StrokeStabilizer.h
    TextBlock.cpp
    TextBlock.h
    ToolbarIni.cpp
    ToolbarIni.h
    XoppCompat.cpp
    XoppCompat.h
    XoppLoader.cpp
    XoppLoader.h
    XoppWriter.cpp
    XoppWriter.h
    XournalClipboard.cpp
    XournalClipboard.h
)

target_include_directories(xoj-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(xoj-core PUBLIC Qt6::Gui PRIVATE ZLIB::ZLIB)

if (TARGET Qt6::Pdf)
    target_link_libraries(xoj-core PUBLIC Qt6::Pdf)
    target_compile_definitions(xoj-core PUBLIC HAVE_QTPDF)
endif ()

if (TARGET Qt6::Svg)
    target_link_libraries(xoj-core PUBLIC Qt6::Svg)
    target_compile_definitions(xoj-core PUBLIC HAVE_QTSVG)
endif ()
