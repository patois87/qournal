#!/bin/sh
# Builds the AppImage: one file with the application and the Qt it was built with, for any distribution.
#
#   packaging/linux/appimage.sh <build folder> <folder of the Qt, e.g. ~/Qt/6.12.0/gcc_64>
#
# linuxdeploy and its Qt plugin are downloaded into the build folder. Build on the oldest distribution the AppImage
# is to run on: it needs at least the C library of the machine it was built on.
set -e
BUILD=$(realpath "${1:?build folder}")
QT=$(realpath "${2:?folder of the Qt}")
SOURCE=$(realpath "$(dirname "$0")/../..")
ARCH=$(uname -m)
TOOLS=$BUILD/linuxdeploy
APPDIR=$BUILD/AppDir

mkdir -p "$TOOLS"
for tool in linuxdeploy/linuxdeploy linuxdeploy/linuxdeploy-plugin-qt; do
    file=$TOOLS/$(basename $tool)-$ARCH.AppImage
    if [ ! -x "$file" ]; then
        url=https://github.com/$tool/releases/download/continuous/$(basename $tool)-$ARCH.AppImage
        curl -fL -o "$file" "$url"
        chmod +x "$file"
    fi
done

rm -rf "$APPDIR"
cmake --install "$BUILD" --prefix "$APPDIR/usr"

VERSION=$(sed -n 's/^project(qournal VERSION \([0-9.]*\).*/\1/p' "$SOURCE/CMakeLists.txt")
export LINUXDEPLOY_OUTPUT_VERSION=$VERSION
export LDAI_OUTPUT=qournal-$VERSION-$ARCH.AppImage
export OUTPUT=$LDAI_OUTPUT
export QMAKE=$QT/bin/qmake
export QML_SOURCES_PATHS=$SOURCE/app/qml
export LD_LIBRARY_PATH=$QT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
# Loaded at run time, so not found by looking at the program: icons are SVG files, and Wayland. The Wayland plugins
# have other names from one version of Qt to the next: those that are there are taken
EXTRA_QT_MODULES=svg
for module in WaylandCompositor WaylandClient; do
    if [ -e "$QT/lib/libQt6$module.so" ]; then
        EXTRA_QT_MODULES="$EXTRA_QT_MODULES;$(echo $module | tr 'A-Z' 'a-z')"
        break
    fi
done
EXTRA_PLATFORM_PLUGINS=
for plugin in libqwayland-generic.so libqwayland-egl.so libqwayland.so libqoffscreen.so; do
    if [ -e "$QT/plugins/platforms/$plugin" ]; then
        EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:+$EXTRA_PLATFORM_PLUGINS;}$plugin"
    fi
done
export EXTRA_QT_MODULES EXTRA_PLATFORM_PLUGINS
# Runs the tools where AppImages cannot be mounted (containers of the CI)
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH=$TOOLS:$PATH

cd "$BUILD"
"$TOOLS/linuxdeploy-$ARCH.AppImage" --appdir "$APPDIR" --plugin qt --output appimage \
    --desktop-file "$APPDIR/usr/share/applications/ch.vereo.qournal.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/ch.vereo.qournal.png"
ls -l "$BUILD"/*.AppImage
