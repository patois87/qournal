#!/usr/bin/env python3
"""Writes the encrypted PDF files of tests/data/encrypted with qpdf (through pikepdf), an implementation of the
encryption that is not ours: every method of the standard security handler, with an empty user password and an
owner password, and one file that needs a password to be opened.

    pip install pikepdf
    python3 tools/make_encrypted_pdfs.py
"""
import os
import pikepdf
from pikepdf import Dictionary, Name, Array, String

folder = os.path.join(os.path.dirname(__file__), "..", "tests", "data", "encrypted")
os.makedirs(folder, exist_ok=True)


def document():
    pdf = pikepdf.new()
    font = pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica))
    content = b"0.1 0.3 0.8 rg 20 20 80 60 re f BT /F1 18 Tf 20 150 Td (Encrypted text) Tj ET"
    page = pdf.make_indirect(Dictionary(
        Type=Name.Page, MediaBox=Array([0, 0, 200, 200]),
        Resources=Dictionary(Font=Dictionary(F1=font)),
        Contents=pdf.make_stream(content),
        # A link: its address is a string, which is encrypted too
        Annots=Array([pdf.make_indirect(Dictionary(
            Type=Name.Annot, Subtype=Name.Link, Rect=Array([20, 140, 180, 175]), Border=Array([0, 0, 0]),
            A=Dictionary(S=Name.URI, URI=String("https://xournalpp.github.io/"))))])))
    pdf.pages.append(pikepdf.Page(page))
    pdf.docinfo["/Title"] = "Encrypted test file"
    return pdf


methods = {
    "rc4-40": dict(R=2, metadata=False, aes=False),
    "rc4-128": dict(R=3, metadata=False, aes=False),
    "rc4-128-r4": dict(R=4, aes=False, metadata=False),
    "aes-128": dict(R=4, aes=True),
    "aes-128-plain-metadata": dict(R=4, aes=True, metadata=False),
    "aes-256": dict(R=6),
}
for name, options in methods.items():
    document().save(os.path.join(folder, name + ".pdf"),
                    encryption=pikepdf.Encryption(owner="owner", user="", **options),
                    object_stream_mode=pikepdf.ObjectStreamMode.generate)
document().save(os.path.join(folder, "needs-password.pdf"),
                encryption=pikepdf.Encryption(owner="owner", user="secret", R=6))
