/*
 * Qournal
 *
 * Reads the structure of a PDF file and appends to it: enough to put annotations on its pages while the pages
 * themselves stay as they are (an "incremental update" in the terms of the PDF specification). Not a general PDF
 * library: only what is needed to find and to rewrite pages is understood. Encrypted files are read and updated if
 * they open without a password (see PdfCrypt.h).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Pdf {

class Crypt;

struct Ref {
    int number = 0;
    int generation = 0;

    bool operator==(const Ref& o) const { return number == o.number && generation == o.generation; }
};

class Value;
using Array = std::vector<Value>;

/// A dictionary; the order of the entries is kept
class Dict {
public:
    const Value* find(const QByteArray& key) const;
    Value* find(const QByteArray& key);
    void set(const QByteArray& key, Value value);
    void remove(const QByteArray& key);
    bool isEmpty() const { return m_entries.empty(); }
    const std::vector<std::pair<QByteArray, Value>>& entries() const { return m_entries; }

private:
    std::vector<std::pair<QByteArray, Value>> m_entries;
};

class Value {
public:
    enum class Kind { Null, Bool, Number, Name, String, Array, Dict, Ref };

    Value() = default;
    static Value boolean(bool value);
    static Value number(double value);
    static Value name(const QByteArray& name);  ///< without the slash
    static Value array(Array array);
    static Value dict(Dict dict);
    static Value ref(Ref ref);
    /// A string of these bytes (written in hexadecimal)
    static Value string(const QByteArray& bytes);

    Kind kind() const { return m_kind; }
    bool isNull() const { return m_kind == Kind::Null; }
    double toNumber() const { return m_number; }
    int toInt() const { return static_cast<int>(m_number); }
    /// The name without the slash, or the string as it is written in the file (with its delimiters)
    const QByteArray& text() const { return m_text; }
    const Array& toArray() const { return m_array; }
    Array& toArray() { return m_array; }
    const Dict& toDict() const { return m_dict; }
    Dict& toDict() { return m_dict; }
    Ref toRef() const { return m_ref; }
    /// The bytes of a string, with its escapes undone
    QByteArray stringBytes() const;

    /// As it is written in a PDF file
    QByteArray serialize() const;

private:
    friend class Parser;

    Kind m_kind = Kind::Null;
    double m_number = 0;
    QByteArray m_text;  ///< name, string, or the number as it was written
    Array m_array;
    Dict m_dict;
    Ref m_ref;
};

/// A page of a file, with what it inherits from the nodes of the page tree above it
struct PageInfo {
    Ref ref;
    Dict dict;  ///< Resources, MediaBox, CropBox and Rotate are in it even if they were inherited
};

class Reader {
    friend class Update;

public:
    Reader();
    ~Reader();

    /// @return false if the data is not a PDF file that can be read, or if it needs a password; error() tells why
    bool load(const QByteArray& data);
    QString error() const { return m_error; }

    const QByteArray& data() const { return m_data; }
    const Dict& trailer() const { return m_trailer; }
    /// The offset of the newest cross-reference section, and whether it is a stream
    qint64 xrefOffset() const { return m_xrefOffset; }
    bool usesXrefStream() const { return m_xrefStream; }
    /// One more than the highest object number
    int size() const { return m_size; }

    /// The object a reference stands for; null if there is none. For a stream, its dictionary
    Value object(Ref ref) const;
    /// The value itself, or the object it refers to
    Value resolve(const Value& value) const;
    /// The pages in the order of the document
    std::vector<PageInfo> pages() const;
    /// The object that is the root of the page tree
    std::optional<Ref> pagesRoot() const;
    /**
     * The data of a stream object, with its dictionary. The filters that compress (Flate with its predictors,
     * ASCIIHex, ASCII85) are undone. An image format at the end of the filters (DCTDecode, JPXDecode, ...) is left
     * as it is if imageFilter is given, which gets its name; otherwise such a stream cannot be read
     */
    bool stream(Ref ref, Dict& dict, QByteArray& data, QByteArray* imageFilter = nullptr) const;
    /// The data of a stream object as it is stored, with its filters
    bool rawStream(Ref ref, Dict& dict, QByteArray& raw) const;
    /// Undoes the filters a dictionary names, as stream() does: also for data that is not a stream of the file
    bool decodeStream(const QByteArray& raw, const Dict& dict, QByteArray& decoded, QByteArray* imageFilter) const;

    /// The encryption of the file, null if it has none. What the reader gives is decrypted
    const Crypt* crypt() const { return m_crypt.get(); }
    /// Whether the table of the objects was damaged and was rebuilt by looking for the objects in the file
    bool repaired() const { return m_repaired; }

private:
    struct Entry {
        int type = 0;       ///< 1: at an offset, 2: in an object stream
        qint64 offset = 0;  ///< or the number of the object stream
        int index = 0;      ///< generation, or the place in the object stream
    };

    bool fail(const QString& message);
    bool readXref(qint64 offset, int depth);
    bool readXrefTable(qint64 offset, int depth);
    bool readXrefStream(qint64 offset, int depth);
    /// The decoded data of the stream object at an offset
    bool streamAt(qint64 offset, Dict& dict, QByteArray& decoded) const;
    void collectPages(const Value& node, Dict inherited, std::vector<PageInfo>& pages, int depth) const;
    bool setUpEncryption();
    /// Reads the file as it is, with its table of objects. A table that leads to no pages counts as wrong, unless
    /// pages are not required
    bool loadWithTable(bool requirePages = true);
    /// Reads a damaged file: its objects are looked for, as viewers do
    bool loadRepaired();
    void indexObjectStreams();
    /// The length of the data of a stream that starts at an offset: the one its dictionary gives, if the stream
    /// ends there, else up to the next "endstream"
    qint64 streamLength(qint64 start, const Dict& dict) const;
    /// Decrypts the strings of an object, or the data of a stream (which is not decrypted if it is not encrypted)
    void decryptStrings(Value& value, int number, int generation) const;
    QByteArray decryptStreamData(const QByteArray& raw, const Dict& dict, int number, int generation) const;

    QByteArray m_data;
    QString m_error;
    Dict m_trailer;
    qint64 m_xrefOffset = 0;
    bool m_xrefStream = false;
    int m_size = 0;
    QHash<int, Entry> m_entries;
    mutable QHash<int, Value> m_cache;
    mutable QHash<int, std::vector<std::pair<int, Value>>> m_objectStreams;
    std::unique_ptr<Crypt> m_crypt;
    bool m_repaired = false;
    int m_encryptObject = -1;  ///< the encryption dictionary, whose strings are not encrypted
};

/// Objects that are appended to a file, with the cross-reference section and trailer that make them part of it
class Update {
public:
    explicit Update(const Reader& reader);

    /// A number for a new object
    Ref newRef();
    /// Writes an object: a new one, or a new version of one of the file
    void add(Ref ref, const Value& value);
    /// Writes a stream. The data is compressed unless it is given as it is to be stored (with its filter in dict)
    void addStream(Ref ref, Dict dict, const QByteArray& data, bool compress = true);

    /// The file with the update
    QByteArray finish();

private:
    const Reader& m_reader;
    QByteArray m_out;
    struct Written {
        int number;
        int generation;  ///< or the place in an object stream (type 2)
        qint64 offset;   ///< in the whole file, or the number of the object stream (type 2)
        int type = 1;
    };
    std::vector<Written> m_offsets;
    int m_next;
};

}  // namespace Pdf
