# Qournal

Handwritten notes and PDF annotation for Linux, Windows, macOS and Android, on Qt 6.
[vereo.ch/software/qournal](https://vereo.ch/software/qournal)

Qournal has the tools and the file format of [Xournal++](https://github.com/xournalpp/xournalpp), written anew on
Qt: documents (`.xopp`) go back and forth between the two. It is a project of its own. It is not made by the
developers of Xournal++ and must not be taken for it.

## What it does

- Pen and highlighter with pressure, line styles and filling; eraser, shapes with a shape recogniser, setsquare and
  compass, text, images, links and LaTeX formulas
- Annotates PDF files and exports them again with the pages of the PDF kept as they are; selects, highlights and
  searches the text of a PDF
- Pages with the papers of Xournal++ (ruled, graph, dotted, staves, isometric), layers, selection with move, scale
  and rotate, undo and redo
- Audio recordings that are linked to what was written meanwhile
- Plugins written in Lua, with the interface of Xournal++; its plugins come along
- One arrangement of the user interface for mouse and keyboard, one for fingers and pen, and one for electronic
  paper, with a fast pen on Onyx Boox tablets (see below)
- Light and dark colours, toolbars that can be arranged, German completely and 30 other languages
  with the texts they share with Xournal++

## What is new in it

Most of Qournal is what Xournal++ has. These are its own:

- **A tool for a region of the screen.** "Insert a region of the screen" hides Qournal and waits, so that the
  window that is to be captured can be brought to the front; then the screen is frozen, a rectangle is dragged
  over it, and what is in it lies on the page as an image, selected and ready to be moved. No other program and no
  file in between. (Xournal++ has a plugin that saves a region to a file, with tools of Linux.)
- **Formulas without a LaTeX installation.** Where LaTeX is not installed, which is always the case on Android,
  Qournal sets the formula itself. The result is stored with its source, so that Xournal++ shows it and can set
  it again with LaTeX.
- **Android, and a user interface for fingers.** Toolbars that keep out of the bars of the system, menus with
  "Back" and "Close", dialogs that become one column on a phone.
- **Electronic paper**, see the next section.

## Electronic paper

Qournal is made to be written with on tablets with an E Ink screen, and was worked out on an Onyx Boox (Note Air
5 C, a colour screen). Such screens show two inks well and everything between them badly, and they are slow; an
application made for a glowing screen looks washed out on them and trails behind the pen. On such a device Qournal
switches to an arrangement of its own (it can also be chosen in the preferences):

- **A fast pen on Onyx Boox.** The firmware of these tablets can draw the stroke itself, at once, while the pen
  writes; Qournal hands it the pen and shows its own drawing a moment after the stroke. Writing feels like on the
  note application of the device. Nothing of Onyx is in the package.
- **Black on white, without tones.** The user interface has no shadows, no coloured areas and no animations; the
  icons are black, a dialog does not dim the window behind it, and the pointer of the pen is not drawn, because it
  would leave traces.
- **Strokes that stay black.** The pen starts black instead of blue, and strokes are drawn without smoothed edges:
  the gray pixels at their edges made them fainter. A setting turns the smoothing on again.
- **Fills and rulings that look even.** A filled shape is drawn as a regular pattern of dots of the full colour
  instead of a tone, which the screen shows with noise; the lines of ruled or squared paper are black and whole
  pixels wide, so that none of them has gaps at any zoom.

These changes are for the screen only: the document and what is exported are the same as everywhere else. Tablets
of other makers get the arrangement for electronic paper, but not the fast pen, which each maker does in its own
way.

## Why Qt

Xournal++ is built on GTK 3. That serves it well on the Linux desktop, and it is the reason it stays there: GTK
has no Android and no iOS, and on Windows a GTK application is a guest. Qournal started from the question what
the same application would be on a toolkit that is at home on all of these.

- **One code base for desktop and tablets.** The same sources build for Linux, Windows, macOS and Android (and are
  kept building for iOS). Tablets are where handwriting belongs, and they are where Xournal++ is not.
- **The pen.** Qt hands on the events of a pen with their pressure as the system delivers them, on every platform
  in the same form. A first trial on Windows wrote smoothly at 266 events a second, with pressure and with the
  palm ignored; that trial is why the work went on.
- **Drawing with the graphics card.** The pages are tiles in the scene graph of Qt Quick: scrolling and zooming
  move what is already drawn, also on the dense screens of tablets, while the strokes themselves are drawn with
  `QPainter` as vectors.
- **A user interface that can be for fingers.** Qt Quick Controls has styles and sizes for touch; the same
  dialogs become one column on a phone, and menus get what a finger needs.
- **PDF everywhere.** Qt PDF shows the pages on every platform, and a reader of its own keeps the pages of a PDF
  untouched when a document is exported, so that text and links survive. Neither Cairo nor Poppler has to be
  carried to Android or Windows for it.
- **Packages.** Qt brings what an application needs to be a package of each system: the installer and the Store
  package on Windows, the package for Android, the AppImage on Linux.

What Qournal does not take from Xournal++ is its core: the document model, the rendering and the user interface
are written anew. What it does take are algorithms that do not depend on GTK, ported piece by piece, and the
icons, plugins, translations and toolbar configurations, with thanks to those who made them.

## Getting it

The sources are here. Packages for Linux (AppImage, `.deb`, `.rpm`) and an installer for Windows are offered with
the [releases](https://github.com/patois87/qournal/releases) of this repository. For Android and for the Microsoft
Store, Qournal is offered in the stores.

### macOS

The releases also have a disk image (`.dmg`) for Macs with Apple silicon and macOS 14.4 or newer. Open it and drag
Qournal into the folder "Applications". It is not signed with a certificate of Apple and not notarized, so macOS
refuses to open it the first time ("Apple could not verify..."). To allow it:

1. Open Qournal once and close the message with "Done" (not "Move to Trash").
2. Open the System Settings, "Privacy & Security", and scroll down to "Security": it says there that Qournal was
   blocked. Click "Open Anyway" and confirm with your password or Touch ID.
3. Open Qournal again and choose "Open Anyway". macOS remembers this; it is asked once for each version.

Or, in the Terminal, take the mark off that macOS puts on what was downloaded:

```sh
xattr -dr com.apple.quarantine /Applications/qournal.app
```

On macOS 14, a click on the application with the Control key held and "Open" does the same as steps 2 and 3.

## Donations

The packages here cost nothing. If Qournal is of use to you, a donation helps the work on it to go on: there is a
button for it at [vereo.ch/software/qournal](https://vereo.ch/software/qournal). The donation goes through the
payment provider of that web site; the application has no part in it and knows nothing of it.

## Building it

Qt 6.5 or newer with Qt Quick, Quick Controls 2, and for all features Qt PDF, SVG, Print Support and Multimedia;
CMake and a C++17 compiler. Lua, zlib, libogg, libvorbis, MicroTeX and tinyxml2 are downloaded and built where
the system does not have them. `-DREQUIRE_ALL_FEATURES=ON` fails if a module of Qt is missing, instead of building
without its feature.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$HOME/Qt/6.12.0/gcc_64
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
build/app/qournal
```

### Packages for Linux

- `.deb` or `.rpm`, with the Qt of the distribution (configure without `CMAKE_PREFIX_PATH`):
  `cd build && cpack -G DEB` (or `RPM`)
- AppImage, with the Qt it was built with inside: `packaging/linux/appimage.sh build ~/Qt/6.12.0/gcc_64`. Build it
  on the oldest distribution it is to run on.

### Windows

Build with the Qt for MSVC or MinGW as above. Put `qournal.exe` into a folder `package` at the root of the
sources, run `windeployqt --release --no-translations --qmldir app\qml package\qournal.exe`, and copy the folders
`plugins` and `licenses`, `LICENSE` and `THIRD-PARTY.md` next to it. From that folder:

- the installer, with Inno Setup: `iscc /DAppVersion=1.0.0 /DArch=x64 packaging\windows\qournal.iss`
- the package for the Microsoft Store, with the Windows SDK: `packaging\windows\msix.ps1 -Version 1.0.0 -Arch x64`

### macOS

Build with the Qt for macOS as above; the application is `build/app/qournal.app`. Add
`-DCMAKE_OSX_DEPLOYMENT_TARGET=14.4` (what Qt 6.12 asks for), or it runs on the macOS of the machine that built it
only. `~/Qt/6.12.0/macos/bin/macdeployqt build/app/qournal.app -qmldir=app/qml -dmg` puts Qt into the application
and makes the disk image.

### Android

Configure with the `qt-cmake` of the Qt for Android and build the target `apk`:

```sh
~/Qt/6.12.0/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=$HOME/Qt/6.12.0/gcc_64 -DANDROID_SDK_ROOT=$HOME/Android/Sdk -DANDROID_NDK_ROOT=<the NDK> \
    -DBUILD_TESTING=OFF
cmake --build build-android --target apk
```

The package is `build-android/app/android-build/qournal.apk`. Android installs a package only if it is signed, with
a key of your own:

```sh
keytool -genkeypair -keystore my.jks -storetype PKCS12 -alias my-key -keyalg RSA -keysize 4096 -validity 10000
apksigner sign --ks my.jks --ks-key-alias my-key build-android/app/android-build/qournal.apk
```

`-DENABLE_EINK_PEN=OFF` leaves out the fast pen for Onyx Boox tablets and the library it needs.

## Structure

- `core/` – the document, reading and writing `.xopp`, rendering, export and the PDF reader; no user interface
- `app/` – the canvas (`PageCanvas`), the tools and the user interface in QML
- `plugins/`, `app/icons/`, `translations/` – from Xournal++
- `packaging/` – what the packages of each system need
- `tests/` – the tests, with files of Xournal++ to check that nothing is lost between the two

## License

GNU General Public License, version 2 or later, like Xournal++: see `LICENSE`. The parts made by others are named
in [THIRD-PARTY.md](THIRD-PARTY.md), with the texts of their licenses in `licenses/`.

Contact: kontakt@vereo.ch
