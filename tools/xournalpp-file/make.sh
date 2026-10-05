#!/bin/sh
# Writes tests/data/written-by-xournalpp-1.3.8.xopp with Xournal++ 1.3.8 itself, so that the tests have a file of
# that version with the features its plugin interface can make (pressure, highlighter, filling, line styles,
# splines, texts, images, layers, all backgrounds, a PDF and an image as backgrounds, page sizes).
#
#   tools/xournalpp-file/make.sh /path/to/xournalpp-1.3.8/AppRun
#
# Without a screen, run it with GDK_BACKEND=broadway BROADWAY_DISPLAY=:7 and "broadwayd :7" running.
set -e
XOURNALPP=${1:?"the program of Xournal++ 1.3.8"}
HERE=$(cd "$(dirname "$0")" && pwd)
DATA=$HERE/../../tests/data
NAME=written-by-xournalpp-1.3.8.xopp
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cc -shared -fPIC -O2 -o "$WORK/timer.so" "$HERE/timer.c"

# A home of its own: the plugin is enabled there, and the settings of the user are left alone
mkdir -p "$WORK/home/.config/xournalpp/plugins"
cp -r "$HERE/plugin" "$WORK/home/.config/xournalpp/plugins/WriteTestFile"

# The document it starts with: a plain page, a page on a PDF and a page on an image, both attached to the file
python3 - "$WORK" "$NAME" <<'EOF'
import gzip, struct, sys, zlib
work, name = sys.argv[1], sys.argv[2]

def png(path, w, h, colour):
    raw = b"".join(b"\0" + bytes(colour) * w for _ in range(h))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

png(f"{work}/image.png", 60, 40, (0x30, 0x90, 0xd0))
png(f"{work}/{name}.bg_1.png", 120, 80, (0xff, 0xf0, 0xc0))

content = b"0.2 0.5 0.8 rg 50 600 300 150 re f BT /F1 24 Tf 60 520 Td (A page of a PDF) Tj ET"
objects = [b"<< /Type /Catalog /Pages 2 0 R >>",
           b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
           b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R "
           b"/Resources << /Font << /F1 5 0 R >> >> >>",
           b"<< /Length %d >>\nstream\n" % len(content) + content + b"\nendstream",
           b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
pdf = b"%PDF-1.4\n"
offsets = []
for i, body in enumerate(objects):
    offsets.append(len(pdf))
    pdf += b"%d 0 obj\n" % (i + 1) + body + b"\nendobj\n"
xref = len(pdf)
pdf += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
pdf += b"".join(b"%010d 00000 n \n" % o for o in offsets)
pdf += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
with open(f"{work}/{name}.bg.pdf", "wb") as f:
    f.write(pdf)

xml = ('<?xml version="1.0" standalone="no"?>\n<xournal creator="Xournal++ 1.3.8" fileversion="4">'
       '<title>Xournal++ document - see https://xournalpp.github.io/</title>'
       '<page width="595.27559" height="841.88976"><background type="solid" color="#ffffffff" style="plain"/>'
       '<layer/></page>'
       # The page has the size of the PDF page, as when Xournal++ annotates a PDF
       '<page width="595" height="842"><background type="pdf" domain="attach" filename="bg.pdf" '
       'pageno="1"/><layer/></page>'
       '<page width="595.27559" height="841.88976"><background type="pixmap" domain="attach" filename="bg_1.png"/>'
       '<layer/></page></xournal>\n')
with gzip.open(f"{work}/{name}", "wb") as f:
    f.write(xml.encode())
EOF

HOME=$WORK/home XDG_CONFIG_HOME=$WORK/home/.config XOJ_TIMER=$WORK/timer.so XOJ_IMAGE=$WORK/image.png \
    timeout 120 "$XOURNALPP" "$WORK/$NAME" || true

if ! zcat "$WORK/$NAME" | grep -q "Written by Xournal++ 1.3.8"; then
    echo "Xournal++ did not write the file" >&2
    exit 1
fi
for file in "$WORK/$NAME"*; do
    cp "$file" "$DATA/"
done
ls -l "$DATA/$NAME"*
