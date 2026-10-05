# Parts of Qournal that are made by others

Qournal is free software under the GNU General Public License, version 2 or later (see `LICENSE`). It is built on
the work of others, which keeps its own licenses. This file names these parts; the texts of their licenses are in
the folder `licenses/`, and in the application under Help, About, "Licenses".

Which parts are in a package depends on the platform and on how it was built: the table says where each one is.

| Part | Used for | In | License | Text |
|---|---|---|---|---|
| [Xournal++](https://github.com/xournalpp/xournalpp) | parts of the source code, the icon themes, the plugins, the translations, the toolbar configurations, files for the tests | all | GPL 2 or later | `LICENSE` |
| [Qt](https://www.qt.io) 6 (Core, GUI, QML, Quick, Quick Controls, PDF, SVG, Print Support, Multimedia, D-Bus) | the user interface and the platform | the AppImage, the Windows packages and the Android package have it inside; the Linux packages use the one of the system | LGPL 3 (also GPL 2 or 3) | `LGPL-3.0.txt`, `GPL-3.0.txt` |
| [Lucide](https://lucide.dev) icons | the Lucide icon themes of Xournal++ are based on them, and ten icons are taken from them directly | all | ISC | `Lucide.txt` |
| [Lua](https://www.lua.org) 5.4 | runs the plugins | all, unless the Lua of the system is used | MIT | `Lua.txt` |
| [MicroTeX](https://github.com/NanoMichael/MicroTeX) | sets formulas where LaTeX is not installed | all | MIT | `MicroTeX.txt` |
| Fonts of MicroTeX (Computer Modern, AMS, Euler, RSFS, St Mary Road, dsrom) | the glyphs of these formulas | all | SIL Open Font License 1.1; the license of Knuth for Computer Modern; a license of its own for dsrom | `fonts-OFL.txt`, `fonts-Knuth.txt`, `fonts-dsrom.txt` |
| [tinyxml2](https://github.com/leethomason/tinyxml2) | MicroTeX reads its tables with it | all | zlib | `tinyxml2.txt` |
| [libogg](https://xiph.org/ogg/) and [libvorbis](https://xiph.org/vorbis/) | audio recordings as Ogg Vorbis | all with audio | BSD 3-Clause | `libogg.txt`, `libvorbis.txt` |
| [zlib](https://zlib.net) | reads and writes `.xopp` files and PDF streams | Windows packages (elsewhere the one of the system or of Qt) | zlib | `zlib.txt` |
| [AndroidHiddenApiBypass](https://github.com/LSPosed/AndroidHiddenApiBypass) | the fast pen on Onyx Boox tablets | Android package | Apache 2.0 | `HiddenApiBypass-NOTICE.txt`, `Apache-2.0.txt` |

## Notes

- The files of Xournal++ in this repository are `plugins/`, `app/icons/`, `app/toolbar.ini`, `tests/data/` and the
  translations in `translations/` that were taken from its `.po` files; its algorithms were ported where the
  sources say so. Its authors are listed in the file `AUTHORS` of its repository.
- Qt has parts by others inside, with licenses of their own: among them PDFium (BSD 3-Clause) in Qt PDF and FFmpeg
  (LGPL 2.1 or later) in Qt Multimedia. Qt documents them under
  [Licenses used in Qt](https://doc.qt.io/qt-6/licenses-used-in-qt.html). The sources of Qt are at
  [download.qt.io](https://download.qt.io/official_releases/qt/). Qt is used unchanged; the packages can be built
  again with another Qt from the sources of Qournal (https://github.com/patois87/qournal), as its readme describes.
- The Apache License 2.0 goes together with version 3 of the GPL, not with version 2: a package that has
  AndroidHiddenApiBypass inside (the Android package, unless it is built with `-DENABLE_EINK_PEN=OFF`) is
  distributed under version 3 of the GPL, which "version 2 or later" allows. The same holds where Qt is taken
  under the LGPL 3. The text of version 3 is in `licenses/GPL-3.0.txt`.
- The fonts of MicroTeX for Greek and Cyrillic text are under the GPL 3 only and are not in the packages.
- Lua, MicroTeX, tinyxml2, libogg, libvorbis, zlib and AndroidHiddenApiBypass are not in this repository: they
  are downloaded when the application is built (see `CMakeLists.txt` and `app/CMakeLists.txt` for the versions).
- The icon of Qournal (`packaging/linux/ch.vereo.qournal.svg`) and the sources that are not named above are made
  for Qournal, under the GPL 2 or later.
