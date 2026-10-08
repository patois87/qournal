qt_add_executable(qournal main.cpp)

set(QML_SOURCES
    qml/ExportDialog.qml
    qml/InputSettingsDialog.qml
    qml/LatexDialog.qml
    qml/LayerSidebar.qml
    qml/LinkDialog.qml
    qml/Main.qml
    qml/OutlineSidebar.qml
    qml/PageSettingsDialog.qml
    qml/FitComboBox.qml
    qml/ModalDim.qml
    qml/WrapCheckBox.qml
    qml/PageSidebar.qml
    qml/PluginManagerDialog.qml
    qml/PreferencesDialog.qml
    qml/PrintDialog.qml
    qml/RecordingsDialog.qml
    qml/TextEditor.qml
    qml/ToolbarDialog.qml
    qml/ToolbarItem.qml
    qml/ToolbarStrip.qml
)
foreach (file IN LISTS QML_SOURCES)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach ()

qt_add_qml_module(qournal
    URI Qournal
    VERSION 1.0
    DEPENDENCIES QtQuick
    QML_FILES ${QML_SOURCES}
    SOURCES
        AudioController.cpp
        BuiltinLatex.cpp
        BuiltinLatex.h
        AudioController.h
        ColorPalette.cpp
        CrashHandler.cpp
        CrashHandler.h
        ColorPalette.h
        FormulaPreview.cpp
        FormulaPreview.h
        InputSettings.h
        LatexRunner.cpp
        Licenses.cpp
        Licenses.h
        LatexRunner.h
        Localization.cpp
        Localization.h
        PageCanvas.cpp
        PageCanvas.h
        PageCanvasFiles.cpp
        PageCanvasInsert.cpp
        PageCanvasJournal.cpp
        PageCanvasLayers.cpp
        PageCanvasPdf.cpp
        PageCanvasScripting.cpp
        PageCanvasSelection.cpp
        PageCanvasSettings.cpp
        PageCanvasTools.cpp
        PageCanvasView.cpp
        PluginController.cpp
        PluginController.h
        PagePreview.cpp
        PagePreview.h
        Platform.cpp
        Platform.h
        RecentFiles.cpp
        RecentFiles.h
        ScreenCapture.cpp
        ScreenCapture.h
        Theme.cpp
        Theme.h
        ToolbarModel.cpp
        ToolbarModel.h
)

# The icon themes of Xournal++, see icons/README.md
file(GLOB ICON_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS icons/*/*.svg)
qt_add_resources(qournal "icons" PREFIX "/" FILES ${ICON_FILES})
# For electronic paper: the Lucide icons in black instead of their blue gray, which such screens dither. Made here
# from the light ones, so that they need not be kept twice
file(GLOB LUCIDE_ICONS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/icons/lucide-light/*.svg)
set(EINK_ICONS)
foreach (icon ${LUCIDE_ICONS})
    get_filename_component(name ${icon} NAME)
    file(READ ${icon} content)
    string(REGEX REPLACE "#(566d86|556e87|4d6e87)" "#000000" content "${content}")
    set(target ${CMAKE_CURRENT_BINARY_DIR}/icons/lucide-eink/${name})
    file(GENERATE OUTPUT ${target} CONTENT "${content}")
    list(APPEND EINK_ICONS ${target})
endforeach ()
qt_add_resources(qournal "icons-eink" PREFIX "/" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${EINK_ICONS})
# The licenses of the application and of the parts of others in it, to be read in the application
file(GLOB LICENSE_TEXTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/licenses/*)
qt_add_resources(qournal "licenses" PREFIX "/licenses" BASE ${PROJECT_SOURCE_DIR}
    FILES ${PROJECT_SOURCE_DIR}/THIRD-PARTY.md ${PROJECT_SOURCE_DIR}/LICENSE ${LICENSE_TEXTS})
# The toolbar configurations of Xournal++ (its resources-templates/toolbar.ini.in)
qt_add_resources(qournal "toolbars" PREFIX "/" FILES toolbar.ini)

# Translations, built into the application. Without the Qt Linguist tools the user interface is English only.
# "cmake --build <dir> --target update_translations" collects the texts of the sources into the .ts files;
# tools/import_po.py fills them with the translations of Xournal++ where the texts match
if (Qt6LinguistTools_FOUND)
    file(GLOB TS_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/translations/*.ts)
    file(GLOB CORE_SOURCES ${PROJECT_SOURCE_DIR}/core/*.cpp)
    qt_add_translations(qournal
        TS_FILES ${TS_FILES}
        SOURCES ${CORE_SOURCES}
        RESOURCE_PREFIX "/i18n"
        LUPDATE_OPTIONS -no-obsolete -locations none
    )
endif ()
# The translations of Qt itself, for the buttons of dialogs ("Close"): from the Qt the application is built with
file(GLOB QT_TRANSLATIONS ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_*.qm)
if (QT_TRANSLATIONS)
    qt_add_resources(qournal "i18n-qt" PREFIX "/i18n-qt" BASE ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}
        FILES ${QT_TRANSLATIONS})
endif ()

target_include_directories(qournal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(qournal PRIVATE APP_VERSION="${PROJECT_VERSION}")

if (TARGET Qt6::PrintSupport)
    target_link_libraries(qournal PRIVATE Qt6::PrintSupport)
    target_compile_definitions(qournal PRIVATE HAVE_PRINTSUPPORT)
endif ()
# On Wayland only the desktop can capture the screen: it is asked over D-Bus
if (TARGET Qt6::DBus)
    target_link_libraries(qournal PRIVATE Qt6::DBus)
    target_compile_definitions(qournal PRIVATE HAVE_QTDBUS)
endif ()
# Plugins need Lua
if (TARGET xoj-lua)
    target_sources(qournal PRIVATE LuaBridge.cpp LuaBridge.h PluginApi.cpp PluginApi.h)
    xoj_use_lua(qournal)
endif ()
if (TARGET xoj-lua AND (ANDROID OR IOS))
    # There is no folder next to the program there: the plugins are resources of the application
    file(GLOB_RECURSE PLUGIN_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/plugins/*/*)
    qt_add_resources(qournal "plugins" PREFIX "/plugins" BASE ${PROJECT_SOURCE_DIR}/plugins FILES ${PLUGIN_FILES})
elseif (TARGET xoj-lua AND APPLE)
    # In the bundle the plugins are resources: files that are not programs next to the program make its signature
    # invalid ("code object is not signed at all")
    add_custom_command(TARGET qournal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/plugins
                $<TARGET_BUNDLE_CONTENT_DIR:qournal>/Resources/plugins
        VERBATIM
    )
elseif (TARGET xoj-lua)
    # The plugins of Xournal++ come along: next to the program, where it looks for them, and installed
    add_custom_command(TARGET qournal POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/plugins $<TARGET_FILE_DIR:qournal>/plugins
        VERBATIM
    )
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/qournal/plugins)
endif ()
# Audio recording and playback need Qt Multimedia
if (TARGET Qt6::Multimedia)
    target_link_libraries(qournal PRIVATE Qt6::Multimedia)
    target_compile_definitions(qournal PRIVATE HAVE_QTMULTIMEDIA)
endif ()
if (TARGET xoj-microtex)
    target_link_libraries(qournal PRIVATE xoj-microtex)
    target_compile_definitions(qournal PRIVATE HAVE_MICROTEX)
    qt_add_resources(qournal "microtex" PREFIX "/microtex" BASE ${MICROTEX_RESOURCE_BASE}
        FILES ${MICROTEX_RESOURCES})
endif ()
if (TARGET xoj-vorbis)
    target_sources(qournal PRIVATE VorbisWriter.cpp VorbisWriter.h)
    target_link_libraries(qournal PRIVATE xoj-vorbis)
    target_compile_definitions(qournal PRIVATE HAVE_VORBIS)
endif ()

target_link_libraries(qournal PRIVATE
    xoj-core
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

# A window without a console on Windows, an application bundle on macOS and iOS
set_target_properties(qournal PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Qournal"
    MACOSX_BUNDLE_GUI_IDENTIFIER ch.vereo.qournal
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
if (WIN32)
    target_sources(qournal PRIVATE ${PROJECT_SOURCE_DIR}/packaging/windows/qournal.rc)
elseif (APPLE AND NOT IOS)
    set(MACOS_ICON ${PROJECT_SOURCE_DIR}/packaging/macos/qournal.icns)
    target_sources(qournal PRIVATE ${MACOS_ICON})
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(qournal PROPERTIES MACOSX_BUNDLE_ICON_FILE qournal.icns)
endif ()
qt_add_resources(qournal "appicon" PREFIX "/" BASE ${CMAKE_CURRENT_SOURCE_DIR}/icons FILES icons/qournal.png)

if (IOS)
    # The Apple Pencil, through UIKit; the Info.plist with the microphone and the document types
    enable_language(OBJCXX)
    target_sources(qournal PRIVATE Platform_ios.mm)
    target_link_libraries(qournal PRIVATE "-framework UIKit")
    set_target_properties(qournal PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/packaging/ios/Info.plist.in
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    )
endif ()

if (ANDROID)
    # What goes into the package besides the program: the files of android/ (the manifest), and in libs/ a small
    # library that lets the application call functions of the device that Android hides from applications. The pen
    # of tablets with electronic paper needs it (Platform::EinkPen); -DENABLE_EINK_PEN=OFF builds without it.
    # HiddenApiBypass is under the Apache License 2.0, which goes with the GPL from version 3 on
    option(ENABLE_EINK_PEN "Fast pen on tablets with electronic paper (downloads HiddenApiBypass)" ON)
    set(ANDROID_PACKAGE_DIR ${CMAKE_CURRENT_BINARY_DIR}/android-package)
    file(REMOVE_RECURSE ${ANDROID_PACKAGE_DIR})
    file(COPY ${PROJECT_SOURCE_DIR}/android/ DESTINATION ${ANDROID_PACKAGE_DIR})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/android/AndroidManifest.xml)
    if (ENABLE_EINK_PEN)
        set(HIDDEN_API_VERSION 6.1)
        set(HIDDEN_API_FILE ${CMAKE_BINARY_DIR}/downloads/hiddenapibypass-${HIDDEN_API_VERSION}.aar)
        if (NOT EXISTS ${HIDDEN_API_FILE})
            set(HIDDEN_API_URL https://repo1.maven.org/maven2/org/lsposed/hiddenapibypass/hiddenapibypass)
            file(DOWNLOAD
                ${HIDDEN_API_URL}/${HIDDEN_API_VERSION}/hiddenapibypass-${HIDDEN_API_VERSION}.aar
                ${HIDDEN_API_FILE}
                EXPECTED_HASH SHA256=e3161dd21c97a4540b1698a33f7062aeaa1450008e1e2176070e5380f7a6324c
                TLS_VERIFY ON
            )
        endif ()
        file(COPY ${HIDDEN_API_FILE} DESTINATION ${ANDROID_PACKAGE_DIR}/libs)
    endif ()
    # Not the id of the application: Google Play never lets an app that was free once have a price again, and
    # "ch.vereo.qournal" was set to free there by mistake. It cannot change once the app is published
    set_target_properties(qournal PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR ${ANDROID_PACKAGE_DIR}
        QT_ANDROID_PACKAGE_NAME ch.vereo.qournal.android
        QT_ANDROID_VERSION_NAME ${PROJECT_VERSION}
        QT_ANDROID_VERSION_CODE 3
    )
endif ()

if (UNIX AND NOT APPLE AND NOT ANDROID)
    # What a desktop needs to list the application and to open its documents with it
    set(LINUX_FILES ${PROJECT_SOURCE_DIR}/packaging/linux)
    install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD-PARTY.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qournal)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/licenses DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qournal)
    install(FILES ${LINUX_FILES}/ch.vereo.qournal.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${LINUX_FILES}/ch.vereo.qournal.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES ${LINUX_FILES}/ch.vereo.qournal.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES ${LINUX_FILES}/ch.vereo.qournal.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES icons/qournal.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
        RENAME ch.vereo.qournal.png)
    install(FILES ${LINUX_FILES}/application-x-xopp.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/mimetypes)
endif ()

install(TARGETS qournal
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # on Android the program is a library
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
