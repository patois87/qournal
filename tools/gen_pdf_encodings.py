#!/usr/bin/env python3
"""Writes core/PdfEncodings.h: the encodings of PDF fonts and the glyph names they use.

The WinAnsi and MacRoman encodings are taken from the code pages of Python (cp1252, mac_roman), the Standard
encoding of Adobe is written out here. Glyph names follow the Adobe Glyph List: names of letters with accents are
made from their Unicode names, the others are listed here. Names of the form uniXXXX and uXXXX are read at run time.

    tools/gen_pdf_encodings.py > core/PdfEncodings.h
"""
import unicodedata

SYMBOLS = {
    0x20: 'space', 0x21: 'exclam', 0x22: 'quotedbl', 0x23: 'numbersign', 0x24: 'dollar', 0x25: 'percent',
    0x26: 'ampersand', 0x27: 'quotesingle', 0x28: 'parenleft', 0x29: 'parenright', 0x2A: 'asterisk', 0x2B: 'plus',
    0x2C: 'comma', 0x2D: 'hyphen', 0x2E: 'period', 0x2F: 'slash', 0x30: 'zero', 0x31: 'one', 0x32: 'two',
    0x33: 'three', 0x34: 'four', 0x35: 'five', 0x36: 'six', 0x37: 'seven', 0x38: 'eight', 0x39: 'nine',
    0x3A: 'colon', 0x3B: 'semicolon', 0x3C: 'less', 0x3D: 'equal', 0x3E: 'greater', 0x3F: 'question', 0x40: 'at',
    0x5B: 'bracketleft', 0x5C: 'backslash', 0x5D: 'bracketright', 0x5E: 'asciicircum', 0x5F: 'underscore',
    0x60: 'grave', 0x7B: 'braceleft', 0x7C: 'bar', 0x7D: 'braceright', 0x7E: 'asciitilde',
    0xA0: 'space', 0xA1: 'exclamdown', 0xA2: 'cent', 0xA3: 'sterling', 0xA4: 'currency', 0xA5: 'yen',
    0xA6: 'brokenbar', 0xA7: 'section', 0xA8: 'dieresis', 0xA9: 'copyright', 0xAA: 'ordfeminine',
    0xAB: 'guillemotleft', 0xAC: 'logicalnot', 0xAD: 'hyphen', 0xAE: 'registered', 0xAF: 'macron', 0xB0: 'degree',
    0xB1: 'plusminus', 0xB2: 'twosuperior', 0xB3: 'threesuperior', 0xB4: 'acute', 0xB5: 'mu', 0xB6: 'paragraph',
    0xB7: 'periodcentered', 0xB8: 'cedilla', 0xB9: 'onesuperior', 0xBA: 'ordmasculine', 0xBB: 'guillemotright',
    0xBC: 'onequarter', 0xBD: 'onehalf', 0xBE: 'threequarters', 0xBF: 'questiondown', 0xC6: 'AE', 0xD0: 'Eth',
    0xD7: 'multiply', 0xD8: 'Oslash', 0xDE: 'Thorn', 0xDF: 'germandbls', 0xE6: 'ae', 0xF0: 'eth', 0xF7: 'divide',
    0xF8: 'oslash', 0xFE: 'thorn', 0x131: 'dotlessi', 0x141: 'Lslash', 0x142: 'lslash', 0x152: 'OE', 0x153: 'oe',
    0x192: 'florin', 0x2C6: 'circumflex', 0x2C7: 'caron', 0x2D8: 'breve', 0x2D9: 'dotaccent', 0x2DA: 'ring',
    0x2DB: 'ogonek', 0x2DC: 'tilde', 0x2DD: 'hungarumlaut', 0x2013: 'endash', 0x2014: 'emdash',
    0x2018: 'quoteleft', 0x2019: 'quoteright', 0x201A: 'quotesinglbase', 0x201C: 'quotedblleft',
    0x201D: 'quotedblright', 0x201E: 'quotedblbase', 0x2020: 'dagger', 0x2021: 'daggerdbl', 0x2022: 'bullet',
    0x2026: 'ellipsis', 0x2030: 'perthousand', 0x2039: 'guilsinglleft', 0x203A: 'guilsinglright',
    0x2044: 'fraction', 0x20AC: 'Euro', 0x2122: 'trademark', 0x2212: 'minus', 0xFB01: 'fi', 0xFB02: 'fl',
    0xFB00: 'ff', 0xFB03: 'ffi', 0xFB04: 'ffl', 0x2219: 'periodcentered', 0x2215: 'fraction', 0x221A: 'radical',
    0x221E: 'infinity', 0x2260: 'notequal', 0x2264: 'lessequal', 0x2265: 'greaterequal', 0x2202: 'partialdiff',
    0x2211: 'summation', 0x220F: 'product', 0x222B: 'integral', 0x3C0: 'pi', 0x2126: 'Omega', 0x394: 'Delta',
    0x25CA: 'lozenge', 0xF8FF: 'apple', 0x2248: 'approxequal', 0x161: 'scaron', 0x160: 'Scaron',
    0x17D: 'Zcaron', 0x17E: 'zcaron', 0x178: 'Ydieresis',
}
ACCENTS = {'ACUTE': 'acute', 'GRAVE': 'grave', 'CIRCUMFLEX': 'circumflex', 'TILDE': 'tilde', 'DIAERESIS': 'dieresis',
           'RING ABOVE': 'ring', 'CEDILLA': 'cedilla', 'CARON': 'caron', 'STROKE': 'slash', 'MACRON': 'macron',
           'BREVE': 'breve', 'OGONEK': 'ogonek', 'DOT ABOVE': 'dotaccent', 'DOUBLE ACUTE': 'hungarumlaut'}


def glyph_name(code_point):
    if code_point in SYMBOLS:
        return SYMBOLS[code_point]
    char = chr(code_point)
    if char.isascii() and char.isalpha():
        return char
    name = unicodedata.name(char, '')
    for case, prefix in (('CAPITAL', 'LATIN CAPITAL LETTER '), ('SMALL', 'LATIN SMALL LETTER ')):
        if name.startswith(prefix) and ' WITH ' in name:
            letter, accent = name[len(prefix):].split(' WITH ', 1)
            if len(letter) == 1 and accent in ACCENTS:
                return (letter if case == 'CAPITAL' else letter.lower()) + ACCENTS[accent]
    return None


STANDARD_UPPER = {
    0xA1: 'exclamdown', 0xA2: 'cent', 0xA3: 'sterling', 0xA4: 'fraction', 0xA5: 'yen', 0xA6: 'florin',
    0xA7: 'section', 0xA8: 'currency', 0xA9: 'quotesingle', 0xAA: 'quotedblleft', 0xAB: 'guillemotleft',
    0xAC: 'guilsinglleft', 0xAD: 'guilsinglright', 0xAE: 'fi', 0xAF: 'fl', 0xB1: 'endash', 0xB2: 'dagger',
    0xB3: 'daggerdbl', 0xB4: 'periodcentered', 0xB6: 'paragraph', 0xB7: 'bullet', 0xB8: 'quotesinglbase',
    0xB9: 'quotedblbase', 0xBA: 'quotedblright', 0xBB: 'guillemotright', 0xBC: 'ellipsis', 0xBD: 'perthousand',
    0xBF: 'questiondown', 0xC1: 'grave', 0xC2: 'acute', 0xC3: 'circumflex', 0xC4: 'tilde', 0xC5: 'macron',
    0xC6: 'breve', 0xC7: 'dotaccent', 0xC8: 'dieresis', 0xCA: 'ring', 0xCB: 'cedilla', 0xCD: 'hungarumlaut',
    0xCE: 'ogonek', 0xCF: 'caron', 0xD0: 'emdash', 0xE1: 'AE', 0xE3: 'ordfeminine', 0xE8: 'Lslash', 0xE9: 'Oslash',
    0xEA: 'OE', 0xEB: 'ordmasculine', 0xF1: 'ae', 0xF5: 'dotlessi', 0xF8: 'lslash', 0xF9: 'oslash', 0xFA: 'oe',
    0xFB: 'germandbls',
}


def code_page(codec):
    names = []
    for code in range(256):
        try:
            char = bytes([code]).decode(codec)
        except UnicodeDecodeError:
            names.append(None)
            continue
        names.append(glyph_name(ord(char)) if code >= 0x20 else None)
    return names


def standard():
    names = [None] * 256
    for code in range(0x20, 0x7F):
        names[code] = glyph_name(code)
    names[0x27] = 'quoteright'
    names[0x60] = 'quoteleft'
    for code, name in STANDARD_UPPER.items():
        names[code] = name
    return names


def table(name, names):
    entries = ['"%s"' % n if n else 'nullptr' for n in names]
    lines = []
    line = '   '
    for entry in entries:
        if len(line) + len(entry) + 2 > 118:
            lines.append(line.rstrip())
            line = '   '
        line += ' ' + entry + ','
    lines.append(line.rstrip())
    return 'const char* const %s[256] = {\n%s\n};\n' % (name, '\n'.join(lines))


unicode_by_name = {}
for code_point in list(range(0x20, 0x250)) + list(SYMBOLS):
    name = glyph_name(code_point)
    if name and name not in unicode_by_name:
        unicode_by_name[name] = code_point
# Names that two characters share go to the first; these are the ones fonts mean
unicode_by_name['space'] = 0x20
unicode_by_name['hyphen'] = 0x2D
unicode_by_name['periodcentered'] = 0xB7
unicode_by_name['fraction'] = 0x2044

print('// Generated by tools/gen_pdf_encodings.py; do not edit\n')
print('#pragma once\n')
print('#include <utility>\n')
print('namespace Pdf::Encodings {\n')
print(table('STANDARD', standard()))
print(table('WIN_ANSI', code_page('cp1252')))
print(table('MAC_ROMAN', code_page('mac_roman')))
entries = sorted(unicode_by_name.items())
print('/// Glyph names and their characters, sorted by name')
print('const std::pair<const char*, char16_t> UNICODE_BY_NAME[] = {')
for name, code_point in entries:
    print('    {"%s", 0x%04X},' % (name, code_point))
print('};\n')
print('}  // namespace Pdf::Encodings')
