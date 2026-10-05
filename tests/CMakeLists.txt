set(TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
if (WIN32)
    # The offscreen platform does not know the fonts of Windows: it takes those of a folder, and the first of them
    # where a test asks for "Sans". Arial alone, so that texts are as wide as the tests expect
    file(GLOB TEST_FONTS "$ENV{WINDIR}/Fonts/arial*.ttf")
    file(COPY ${TEST_FONTS} DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/fonts)
    list(APPEND TEST_ENVIRONMENT "QT_QPA_FONTDIR=${CMAKE_CURRENT_BINARY_DIR}/fonts")
endif ()

function(add_core_test name)
    qt_add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE xoj-core Qt6::Test)
    target_compile_definitions(${name} PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${TEST_ENVIRONMENT}")
endfunction()

add_core_test(tst_xopp)
# To read the files of Xournal++ as they are
target_link_libraries(tst_xopp PRIVATE ZLIB::ZLIB)
add_core_test(tst_layout)
add_core_test(tst_render)
add_core_test(tst_tools)
add_core_test(tst_text)
add_core_test(tst_export)
add_core_test(tst_clipboard)
add_core_test(tst_pdfpage)
if (TARGET Qt6::Pdf)
    # Against Xournal++ itself; skips unless XOURNALPP names its program
    add_core_test(tst_xournalpp)
    # Not a test of its own: checks one PDF file, see tools/check_pdf_corpus.sh
    qt_add_executable(pdfcorpus pdfcorpus.cpp)
    target_link_libraries(pdfcorpus PRIVATE xoj-core)
endif ()
add_core_test(tst_toolbars ../app/ToolbarModel.cpp ../app/ToolbarModel.h)
target_include_directories(tst_toolbars PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../app)
target_link_libraries(tst_toolbars PRIVATE Qt6::Qml)
target_compile_definitions(tst_toolbars PRIVATE TOOLBAR_INI="${CMAKE_CURRENT_SOURCE_DIR}/../app/toolbar.ini")

# The canvas is tested without the QML user interface, in a bare window
set(CANVAS_SOURCES
    CanvasFixture.h
    ../app/AudioController.cpp
    ../app/BuiltinLatex.cpp
    ../app/BuiltinLatex.h
    ../app/AudioController.h
    ../app/ColorPalette.cpp
    ../app/CrashHandler.cpp
    ../app/CrashHandler.h
    ../app/ColorPalette.h
    ../app/InputSettings.h
    ../app/LatexRunner.cpp
    ../app/LatexRunner.h
    ../app/Localization.cpp
    ../app/Localization.h
    ../app/PageCanvas.cpp
    ../app/PageCanvas.h
    ../app/PageCanvasFiles.cpp
    ../app/PageCanvasInsert.cpp
    ../app/PageCanvasJournal.cpp
    ../app/PageCanvasLayers.cpp
    ../app/PageCanvasPdf.cpp
    ../app/PageCanvasScripting.cpp
    ../app/PageCanvasSelection.cpp
    ../app/PageCanvasSettings.cpp
    ../app/PageCanvasTools.cpp
    ../app/PageCanvasView.cpp
    ../app/PluginController.cpp
    ../app/PluginController.h
    ../app/PagePreview.cpp
    ../app/PagePreview.h
    ../app/Platform.cpp
    ../app/Platform.h
    ../app/RecentFiles.cpp
    ../app/RecentFiles.h
    ../app/ScreenCapture.cpp
    ../app/ScreenCapture.h
    ../app/Theme.cpp
    ../app/Theme.h
)
foreach (name tst_canvas tst_drawing tst_selection tst_insert tst_layers tst_files tst_settings tst_audio tst_plugins tst_journal)
    add_core_test(${name} ${CANVAS_SOURCES})
    target_include_directories(${name} PRIVATE ../app)
    target_link_libraries(${name} PRIVATE Qt6::Quick)
    if (TARGET Qt6::PrintSupport)
        target_link_libraries(${name} PRIVATE Qt6::PrintSupport)
        target_compile_definitions(${name} PRIVATE HAVE_PRINTSUPPORT)
    endif ()
    if (TARGET xoj-lua)
        target_sources(${name} PRIVATE ../app/LuaBridge.cpp ../app/LuaBridge.h ../app/PluginApi.cpp ../app/PluginApi.h)
        xoj_use_lua(${name})
    endif ()
    if (TARGET Qt6::Multimedia)
        target_link_libraries(${name} PRIVATE Qt6::Multimedia)
        target_compile_definitions(${name} PRIVATE HAVE_QTMULTIMEDIA)
    endif ()
    if (TARGET xoj-microtex)
        target_link_libraries(${name} PRIVATE xoj-microtex)
        target_compile_definitions(${name} PRIVATE HAVE_MICROTEX)
    endif ()
    if (TARGET xoj-vorbis)
        target_sources(${name} PRIVATE ../app/VorbisWriter.cpp ../app/VorbisWriter.h)
        target_link_libraries(${name} PRIVATE xoj-vorbis)
        target_compile_definitions(${name} PRIVATE HAVE_VORBIS)
    endif ()
endforeach ()

# The icon themes, for the test of the lookup
get_filename_component(APP_DIR ../app ABSOLUTE)
file(GLOB ICON_FILES CONFIGURE_DEPENDS ${APP_DIR}/icons/*/*.svg)
qt_add_resources(tst_settings "icons" PREFIX "/" BASE ${APP_DIR} FILES ${ICON_FILES})

# The fonts and tables of MicroTeX, as in the application
if (TARGET xoj-microtex)
    qt_add_resources(tst_insert "microtex" PREFIX "/microtex" BASE ${MICROTEX_RESOURCE_BASE}
        FILES ${MICROTEX_RESOURCES})
endif ()

# The plugins of Xournal++, to see that they run
get_filename_component(PLUGINS_DIR ../plugins ABSOLUTE)
target_compile_definitions(tst_plugins PRIVATE PLUGINS_DIR="${PLUGINS_DIR}")
# One of them as a resource, as on Android and iOS
qt_add_resources(tst_plugins "plugins" PREFIX "/plugins" BASE ${PLUGINS_DIR}
    FILES ${PLUGINS_DIR}/Example/plugin.ini ${PLUGINS_DIR}/Example/main.lua ${PLUGINS_DIR}/Example/var_dump.lua
)

# The application starts, and its user interface loads without warnings of QML
if (TARGET qournal)
    add_test(NAME app_start COMMAND qournal ${CMAKE_CURRENT_SOURCE_DIR}/data/strokes.xopp)
    set_tests_properties(app_start PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QOURNAL_START_TEST=1")
endif ()

# A Lua module written in C, loaded by the plugin tests: such modules work where the program exports Lua, or where
# Lua is a DLL they link against
if (TARGET xoj-lua AND NOT ANDROID AND NOT IOS)
    add_library(xoj_test_module MODULE luamodule/xoj_test_module.c)
    set_target_properties(xoj_test_module PROPERTIES PREFIX "" AUTOMOC OFF)
    target_include_directories(xoj_test_module PRIVATE $<TARGET_PROPERTY:xoj-lua,INTERFACE_INCLUDE_DIRECTORIES>)
    if (WIN32 OR APPLE)
        # Windows resolves the functions of Lua through the DLL; macOS looks them up in the program
        if (WIN32)
            target_link_libraries(xoj_test_module PRIVATE xoj-lua)
        else ()
            target_link_options(xoj_test_module PRIVATE -undefined dynamic_lookup)
        endif ()
    endif ()
    add_dependencies(tst_plugins xoj_test_module)
    target_compile_definitions(tst_plugins PRIVATE
        LUA_TEST_MODULE="$<TARGET_FILE_DIR:xoj_test_module>/?$<IF:$<BOOL:${WIN32}>,.dll,.so>")
endif ()
