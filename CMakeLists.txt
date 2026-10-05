cmake_minimum_required(VERSION 3.21)

# Downloaded sources get the time they were unpacked at
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif ()

project(qournal VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (MSVC)
    # M_PI and the like, which <cmath> of MSVC only has on request; no macros min and max from <windows.h>
    add_compile_definitions(_USE_MATH_DEFINES NOMINMAX)
endif ()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
# Qt PDF is optional: without it, PDF backgrounds and LaTeX formulas are shown as placeholders
find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS Pdf Test)
# Optional as well: SVG export, printing
find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS Svg PrintSupport LinguistTools Multimedia)
# On Linux: capturing a region of the screen on Wayland, through the portal of the desktop
if (UNIX AND NOT APPLE AND NOT ANDROID)
    find_package(Qt6 6.5 QUIET OPTIONAL_COMPONENTS DBus)
endif ()

# Packages are built with everything: a module missing on the build machine must not go unnoticed
option(REQUIRE_ALL_FEATURES "Fail if an optional Qt module is missing" OFF)
if (REQUIRE_ALL_FEATURES)
    foreach (module Pdf Svg PrintSupport Multimedia)
        if (NOT TARGET Qt6::${module})
            message(FATAL_ERROR "REQUIRE_ALL_FEATURES: Qt ${module} was not found")
        endif ()
    endforeach ()
    if (NOT Qt6LinguistTools_FOUND)
        message(FATAL_ERROR "REQUIRE_ALL_FEATURES: the Qt Linguist tools were not found")
    endif ()
endif ()

# zlib is not available with the Qt kits on Windows: build it from source there
find_package(ZLIB QUIET)
if (NOT ZLIB_FOUND)
    message(STATUS "zlib not found: downloading and building it")
    include(FetchContent)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    )
    FetchContent_GetProperties(zlib)
    if (NOT zlib_POPULATED)
        FetchContent_Populate(zlib)
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory(${zlib_SOURCE_DIR} ${zlib_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif ()
    target_include_directories(zlibstatic INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif ()

# Lua runs the plugins. It is built from source where it is not installed
option(ENABLE_PLUGINS "Build with support for Lua plugins" ON)
if (ENABLE_PLUGINS)
    find_package(Lua 5.3 QUIET)
    if (LUA_FOUND)
        add_library(xoj-lua INTERFACE)
        target_include_directories(xoj-lua INTERFACE ${LUA_INCLUDE_DIR})
        target_link_libraries(xoj-lua INTERFACE ${LUA_LIBRARIES})
    else ()
        message(STATUS "Lua not found: downloading and building it (-DENABLE_PLUGINS=OFF builds without plugins)")
        include(FetchContent)
        FetchContent_Declare(lua
            URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
            URL_HASH SHA256=9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
        )
        # The archive has no CMakeLists.txt: this only downloads and unpacks it
        FetchContent_MakeAvailable(lua)
        enable_language(C)
        file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/src/*.c)
        # Not the interpreter and the compiler, which are programs of their own
        list(REMOVE_ITEM LUA_SOURCES ${lua_SOURCE_DIR}/src/lua.c ${lua_SOURCE_DIR}/src/luac.c)
        # Plugins may load Lua modules written in C (luarocks). Those link against the Lua library of the system
        # or expect its functions in the program: on Windows Lua is a DLL of the name they look for, elsewhere
        # the program exports the functions of Lua (see xoj_use_lua). Android and iOS load no such modules
        if (WIN32)
            add_library(xoj-lua SHARED ${LUA_SOURCES})
            set_target_properties(xoj-lua PROPERTIES OUTPUT_NAME lua54 PREFIX "")
            target_compile_definitions(xoj-lua PUBLIC LUA_BUILD_AS_DLL)
            install(TARGETS xoj-lua RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        else ()
            add_library(xoj-lua STATIC ${LUA_SOURCES})
        endif ()
        target_include_directories(xoj-lua PUBLIC ${lua_SOURCE_DIR}/src)
        set_target_properties(xoj-lua PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
        if (IOS)
            target_compile_definitions(xoj-lua PRIVATE LUA_USE_IOS)
        elseif (ANDROID)
            target_compile_definitions(xoj-lua PRIVATE LUA_USE_POSIX)
            target_link_libraries(xoj-lua PRIVATE m)
        elseif (APPLE)
            target_compile_definitions(xoj-lua PRIVATE LUA_USE_MACOSX)
        elseif (UNIX)
            target_compile_definitions(xoj-lua PRIVATE LUA_USE_LINUX)
            target_link_libraries(xoj-lua PRIVATE m ${CMAKE_DL_LIBS})
        endif ()
    endif ()
endif ()

# A program that runs plugins: Lua, and what C modules of Lua need of the program
function(xoj_use_lua target)
    target_link_libraries(${target} PRIVATE xoj-lua)
    target_compile_definitions(${target} PRIVATE HAVE_LUA)
    get_target_property(lua_type xoj-lua TYPE)
    if (lua_type STREQUAL "SHARED_LIBRARY")
        # The DLL next to the program, where Windows looks for it
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:xoj-lua> $<TARGET_FILE_DIR:${target}>
            VERBATIM
        )
    elseif (lua_type STREQUAL "STATIC_LIBRARY" AND NOT ANDROID AND NOT IOS)
        set_target_properties(${target} PROPERTIES ENABLE_EXPORTS ON)
    endif ()
endfunction()

# Audio is recorded as Ogg Vorbis, as Xournal++ does. The multimedia backend of Qt has no encoder for it, so the
# reference encoder is built in, from source
option(ENABLE_VORBIS "Record audio as Ogg Vorbis" ON)
if (ENABLE_VORBIS AND TARGET Qt6::Multimedia)
    include(FetchContent)
    # Only downloaded and unpacked: the few files that are needed are compiled here
    FetchContent_Declare(ogg
        URL https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.gz
        URL_HASH SHA256=0eb4b4b9420a0f51db142ba3f9c64b333f826532dc0f48c6410ae51f4799b664
        SOURCE_SUBDIR not-built-with-its-own-cmake
    )
    FetchContent_Declare(vorbis
        URL https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz
        URL_HASH SHA256=0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab
        SOURCE_SUBDIR not-built-with-its-own-cmake
    )
    FetchContent_MakeAvailable(ogg vorbis)
    enable_language(C)
    # The integer types of libogg, which its own build looks for
    set(INCLUDE_INTTYPES_H 1)
    set(INCLUDE_STDINT_H 1)
    set(INCLUDE_SYS_TYPES_H 0)
    set(SIZE16 int16_t)
    set(USIZE16 uint16_t)
    set(SIZE32 int32_t)
    set(USIZE32 uint32_t)
    set(SIZE64 int64_t)
    set(USIZE64 uint64_t)
    configure_file(${ogg_SOURCE_DIR}/include/ogg/config_types.h.in ${CMAKE_BINARY_DIR}/ogg-include/ogg/config_types.h
        @ONLY)
    set(VORBIS_SOURCES)
    foreach (name analysis bitrate block codebook envelope floor0 floor1 info lookup lpc lsp mapping0 mdct psy
             registry res0 sharedbook smallft synthesis vorbisenc window)
        list(APPEND VORBIS_SOURCES ${vorbis_SOURCE_DIR}/lib/${name}.c)
    endforeach ()
    add_library(xoj-vorbis STATIC ${ogg_SOURCE_DIR}/src/bitwise.c ${ogg_SOURCE_DIR}/src/framing.c ${VORBIS_SOURCES})
    target_include_directories(xoj-vorbis
        PUBLIC ${ogg_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/ogg-include ${vorbis_SOURCE_DIR}/include
        PRIVATE ${vorbis_SOURCE_DIR}/lib
    )
    set_target_properties(xoj-vorbis PROPERTIES AUTOMOC OFF C_VISIBILITY_PRESET hidden)
    if (NOT MSVC)
        target_compile_options(xoj-vorbis PRIVATE -w)  # not our code
    endif ()
    if (UNIX)
        target_link_libraries(xoj-vorbis PRIVATE m)
    endif ()
endif ()

# Formulas without a LaTeX installation (mobile platforms, desktops without LaTeX): MicroTeX sets them. It is built
# in, from source, with tinyxml2, which it reads its tables with
option(ENABLE_BUILTIN_LATEX "Set formulas with MicroTeX where LaTeX is not installed" ON)
if (ENABLE_BUILTIN_LATEX)
    include(FetchContent)
    # Only downloaded and unpacked (there is no such folder with a CMakeLists.txt): the files that are needed are
    # compiled here. Its own build wants tinyxml2 of the system, and Qt Widgets for its Qt platform
    FetchContent_Declare(microtex
        URL https://github.com/NanoMichael/MicroTeX/archive/0e3707f6dafebb121d98b53c64364d16fefe481d.tar.gz
        URL_HASH SHA256=47476269d29c41df322bce6bdd2daa7017cc50b9eda841b2bec1767dba28daa6
        SOURCE_SUBDIR not-built-with-its-cmake
    )
    FetchContent_Declare(tinyxml2
        URL https://github.com/leethomason/tinyxml2/archive/refs/tags/10.0.0.tar.gz
        URL_HASH SHA256=3bdf15128ba16686e69bce256cc468e76c7b94ff2c7f391cc5ec09e40bff3839
        SOURCE_SUBDIR not-built-with-its-cmake
    )
    FetchContent_MakeAvailable(microtex tinyxml2)
    set(MICROTEX_SOURCES ${microtex_SOURCE_DIR}/src/latex.cpp ${microtex_SOURCE_DIR}/src/render.cpp)
    foreach (folder atom box core fonts utils res)
        file(GLOB_RECURSE sources ${microtex_SOURCE_DIR}/src/${folder}/*.cpp)
        list(APPEND MICROTEX_SOURCES ${sources})
    endforeach ()
    add_library(xoj-microtex STATIC ${MICROTEX_SOURCES} ${tinyxml2_SOURCE_DIR}/tinyxml2.cpp)
    target_include_directories(xoj-microtex PUBLIC ${microtex_SOURCE_DIR}/src PRIVATE ${tinyxml2_SOURCE_DIR})
    target_compile_features(xoj-microtex PUBLIC cxx_std_17)
    set_target_properties(xoj-microtex PROPERTIES AUTOMOC OFF POSITION_INDEPENDENT_CODE ON)
    if (MSVC)
        target_compile_options(xoj-microtex PRIVATE /w /utf-8)
    else ()
        target_compile_options(xoj-microtex PRIVATE -w)  # not our code
    endif ()
    # Its fonts and tables, which go into the resources of the application. Not the alphabets for Greek and
    # Cyrillic text: their fonts are under the GPL 3 only, this application is under the GPL 2 or later. The Greek
    # letters of formulas are in the fonts for mathematics
    file(GLOB_RECURSE MICROTEX_RESOURCES ${microtex_SOURCE_DIR}/res/fonts/*)
    list(APPEND MICROTEX_RESOURCES ${microtex_SOURCE_DIR}/res/.clatexmath-res_root)
    set(MICROTEX_RESOURCE_BASE ${microtex_SOURCE_DIR}/res)
endif ()

qt_standard_project_setup(REQUIRES 6.5)
if (QT_KNOWN_POLICY_QTP0002)
    qt_policy(SET QTP0002 NEW)  # paths of the Android package may be generator expressions
endif ()

if (TARGET Qt6::Pdf)
    message(STATUS "Qt PDF found: PDF backgrounds and LaTeX formulas enabled")
else ()
    message(STATUS "Qt PDF not found: PDF backgrounds and LaTeX formulas are shown as placeholders")
endif ()

add_subdirectory(core)
add_subdirectory(app)

include(CTest)
if (BUILD_TESTING)
    if (TARGET Qt6::Test)
        add_subdirectory(tests)
    else ()
        message(STATUS "Qt Test not found: tests are not built")
    endif ()
endif ()

# Packages: "cpack -G DEB" and "cpack -G RPM" in the build folder. They use the Qt of the distribution they are
# built on, so each is for that distribution only.
if (UNIX AND NOT APPLE AND NOT ANDROID)
    set(CPACK_PACKAGE_NAME qournal)
    set(CPACK_PACKAGE_VENDOR "vereo")
    set(CPACK_PACKAGE_CONTACT "vereo <kontakt@vereo.ch>")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://vereo.ch/software/qournal")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Take handwritten notes and annotate PDF files")
    set(CPACK_PACKAGE_DESCRIPTION
        "Handwritten notes and PDF annotation. Reads and writes the files of Xournal++; a project of its own.")
    set(CPACK_RESOURCE_FILE_LICENSE ${PROJECT_SOURCE_DIR}/LICENSE)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_STRIP_FILES ON)
    # Only the application: the libraries built along (zlib, Lua, Vorbis) are linked into it
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};ALL;/")

    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # The libraries are found by themselves; QML modules and plugins of Qt are loaded at run time and are not
    string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS
        qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts
        qml6-module-qtquick-templates qml6-module-qtquick-window qml6-module-qtquick-dialogs
        qml6-module-qt-labs-folderlistmodel qml6-module-qtcore qml6-module-qtqml-workerscript
        qt6-svg-plugins qt6-qpa-plugins)
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "texlive-latex-extra")

    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtsvg")
    # Folders that belong to the system, not to this package
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications /usr/share/metainfo /usr/share/mime /usr/share/mime/packages /usr/share/icons
        /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable /usr/share/icons/hicolor/scalable/apps
        /usr/share/icons/hicolor/scalable/mimetypes /usr/share/icons/hicolor/256x256
        /usr/share/icons/hicolor/256x256/apps)
    include(CPack)
endif ()
