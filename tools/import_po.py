#!/usr/bin/env python3
"""Fills the translation files with the translations of Xournal++ where the texts match.

    tools/import_po.py <directory with the .po files of Xournal++> translations/*.ts

Texts match if they are equal apart from the marks of keyboard shortcuts ("_File" in GTK, "&File" in Qt), the
ellipsis ("..." or "…"), the form of the placeholders ("{1}" or "%1"), upper and lower case and a colon or an
ellipsis at the end. Only messages
without a finished translation are touched, so translations made by hand stay.
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape


def read_po(path):
    """The messages of a .po file as (msgid, msgstr) pairs; plural forms and fuzzy entries are left out"""
    entries = []
    fields, current, fuzzy = {}, None, False

    def flush():
        nonlocal fields, fuzzy
        if fields.get("msgid") and fields.get("msgstr") and "msgid_plural" not in fields and not fuzzy:
            entries.append((fields["msgid"], fields["msgstr"]))
        fields, fuzzy = {}, False

    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            flush()
            current = None
        elif line.startswith("#,") and "fuzzy" in line:
            fuzzy = True
        elif line.startswith("#"):
            continue
        elif line.startswith('"') and current:
            fields[current] += unquote(line)
        else:
            match = re.match(r'(msgctxt|msgid|msgid_plural|msgstr(?:\[\d+\])?)\s+(".*")$', line)
            if match:
                current = match.group(1)
                fields[current] = unquote(match.group(2))
    flush()
    return entries


def unquote(text):
    return bytes(text[1:-1], "utf-8").decode("unicode_escape").encode("latin-1", "ignore").decode("utf-8", "ignore") \
        if "\\" in text else text[1:-1]


def normalize(text):
    """What two texts have in common if they only differ in the notation of GTK and Qt"""
    text = re.sub(r"[_&](?=\w)", "", text)
    text = text.replace("...", "…")
    text = re.sub(r"\{(\d)\}", r"%\1", text)
    return text.strip().rstrip(":…").strip().casefold()


def adapt(translation, source, po_source):
    """The translation of Xournal++ in the notation the source of this application uses"""
    mnemonic = re.search(r"[_&](?=\w)", translation)
    if "&" in source and mnemonic:
        translation = translation[:mnemonic.start()] + "&" + translation[mnemonic.start() + 1:]
    elif re.search(r"_(?=\w)", po_source):
        translation = re.sub(r"_(?=\w)", "", translation, count=1)
    translation = re.sub(r"\{(\d)\}", r"%\1", translation)
    if "…" in source:
        translation = translation.replace("...", "…")
    translation = translation.strip()
    # An ellipsis at the end that the two sources do not share
    if source.rstrip().endswith("…") != po_source.rstrip().endswith(("…", "...")):
        translation = translation.rstrip("….").rstrip()
        if source.rstrip().endswith("…"):
            translation += "…"
    # A colon the two sources do not share
    if source.rstrip().endswith(":") != po_source.rstrip().endswith(":"):
        translation = translation.rstrip(":：").rstrip()
        if source.rstrip().endswith(":"):
            translation += ":"
    return translation


def write_ts(tree, path):
    """As lupdate writes the file, so that updates do not change every line"""
    root = tree.getroot()
    lines = ['<?xml version="1.0" encoding="utf-8"?>', "<!DOCTYPE TS>",
             '<TS version="%s" language="%s">' % (root.get("version"), root.get("language"))]

    def text(value):
        return escape(value or "", {'"': "&quot;", "'": "&apos;"})

    for context in root.findall("context"):
        lines.append("<context>")
        lines.append("    <name>%s</name>" % text(context.findtext("name")))
        for message in context.findall("message"):
            numerus = ' numerus="yes"' if message.get("numerus") else ""
            lines.append("    <message%s>" % numerus)
            for child in message:
                if child.tag == "location":
                    attributes = "".join(' %s="%s"' % (k, text(v)) for k, v in child.attrib.items())
                    lines.append("        <location%s/>" % attributes)
                elif child.tag == "translation":
                    attributes = "".join(' %s="%s"' % (k, text(v)) for k, v in child.attrib.items())
                    forms = child.findall("numerusform")
                    if forms:
                        lines.append("        <translation%s>" % attributes)
                        for form in forms:
                            lines.append("            <numerusform>%s</numerusform>" % text(form.text))
                        lines.append("        </translation>")
                    else:
                        lines.append("        <translation%s>%s</translation>" % (attributes, text(child.text)))
                else:
                    lines.append("        <%s>%s</%s>" % (child.tag, text(child.text), child.tag))
            lines.append("    </message>")
        lines.append("</context>")
    lines.append("</TS>")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    po_dir = Path(sys.argv[1])
    for ts_path in sys.argv[2:]:
        language = re.search(r"_([A-Za-z]{2,3}(?:_[A-Z]{2})?)\.ts$", ts_path).group(1)
        po_path = po_dir / (language + ".po")
        if not po_path.exists():
            print("%s: no %s" % (ts_path, po_path))
            continue
        known = {}
        for msgid, msgstr in read_po(po_path):
            known.setdefault(normalize(msgid), (msgid, msgstr))

        tree = ET.parse(ts_path)
        total = finished = imported = 0
        for message in tree.getroot().iter("message"):
            translation = message.find("translation")
            total += 1
            if translation.get("type") is None and (translation.text or translation.findall("numerusform")):
                finished += 1
                continue
            if message.get("numerus"):
                continue
            source = message.findtext("source")
            match = known.get(normalize(source))
            if match:
                translation.text = adapt(match[1], source, match[0])
                del translation.attrib["type"]
                imported += 1
        write_ts(tree, ts_path)
        print("%s: %d of %d translated (%d taken from Xournal++)" % (ts_path, finished + imported, total, imported))


if __name__ == "__main__":
    main()
