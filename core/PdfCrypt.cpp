/*
 * Qournal
 *
 * @license GNU GPLv2 or later
 */

#include "PdfCrypt.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace Pdf {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("Pdf", text); }

// AES (FIPS 197)

constexpr std::array<quint8, 256> SBOX = {
        0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82,
        0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
        0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15, 0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96,
        0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
        0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb,
        0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf, 0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
        0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff,
        0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
        0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32,
        0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
        0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08, 0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6,
        0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
        0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e,
        0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf, 0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
        0xb0, 0x54, 0xbb, 0x16};

constexpr std::array<quint8, 256> inverted(const std::array<quint8, 256>& box) {
    std::array<quint8, 256> result{};
    for (int i = 0; i < 256; ++i) {
        result[box[static_cast<size_t>(i)]] = static_cast<quint8>(i);
    }
    return result;
}

constexpr std::array<quint8, 256> INV_SBOX = inverted(SBOX);

quint8 xtime(quint8 x) { return static_cast<quint8>((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

quint8 multiply(quint8 a, quint8 b) {
    quint8 result = 0;
    while (b) {
        if (b & 1) {
            result ^= a;
        }
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

/// The round keys of a key of 16, 24 or 32 bytes
class AesKey {
public:
    explicit AesKey(const QByteArray& key) {
        const int nk = static_cast<int>(key.size() / 4);
        m_rounds = nk + 6;
        const int words = 4 * (m_rounds + 1);
        m_words.resize(static_cast<size_t>(words) * 4);
        std::memcpy(m_words.data(), key.constData(), static_cast<size_t>(key.size()));
        quint8 rcon = 1;
        for (int i = nk; i < words; ++i) {
            quint8 t[4];
            std::memcpy(t, &m_words[static_cast<size_t>(i - 1) * 4], 4);
            if (i % nk == 0) {
                const quint8 first = t[0];
                t[0] = static_cast<quint8>(SBOX[t[1]] ^ rcon);
                t[1] = SBOX[t[2]];
                t[2] = SBOX[t[3]];
                t[3] = SBOX[first];
                rcon = xtime(rcon);
            } else if (nk > 6 && i % nk == 4) {
                for (quint8& b: t) {
                    b = SBOX[b];
                }
            }
            for (int k = 0; k < 4; ++k) {
                m_words[static_cast<size_t>(i) * 4 + static_cast<size_t>(k)] =
                        m_words[static_cast<size_t>(i - nk) * 4 + static_cast<size_t>(k)] ^ t[k];
            }
        }
    }

    void encrypt(quint8* s) const {
        addRoundKey(s, 0);
        for (int round = 1; round <= m_rounds; ++round) {
            for (int i = 0; i < 16; ++i) {
                s[i] = SBOX[s[i]];
            }
            shiftRows(s);
            if (round < m_rounds) {
                mixColumns(s);
            }
            addRoundKey(s, round);
        }
    }

    void decrypt(quint8* s) const {
        addRoundKey(s, m_rounds);
        for (int round = m_rounds - 1; round >= 0; --round) {
            invShiftRows(s);
            for (int i = 0; i < 16; ++i) {
                s[i] = INV_SBOX[s[i]];
            }
            addRoundKey(s, round);
            if (round > 0) {
                invMixColumns(s);
            }
        }
    }

private:
    void addRoundKey(quint8* s, int round) const {
        for (int i = 0; i < 16; ++i) {
            s[i] ^= m_words[static_cast<size_t>(round) * 16 + static_cast<size_t>(i)];
        }
    }
    // The state is column by column: s[4 * column + row]
    static void shiftRows(quint8* s) {
        quint8 t[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                t[4 * c + r] = s[4 * ((c + r) % 4) + r];
            }
        }
        std::memcpy(s, t, 16);
    }
    static void invShiftRows(quint8* s) {
        quint8 t[16];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                t[4 * ((c + r) % 4) + r] = s[4 * c + r];
            }
        }
        std::memcpy(s, t, 16);
    }
    static void mixColumns(quint8* s) {
        for (int c = 0; c < 4; ++c) {
            quint8* col = s + 4 * c;
            const quint8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            col[0] = static_cast<quint8>(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
            col[1] = static_cast<quint8>(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
            col[2] = static_cast<quint8>(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
            col[3] = static_cast<quint8>((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
        }
    }
    static void invMixColumns(quint8* s) {
        for (int c = 0; c < 4; ++c) {
            quint8* col = s + 4 * c;
            const quint8 a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            col[0] = multiply(a0, 14) ^ multiply(a1, 11) ^ multiply(a2, 13) ^ multiply(a3, 9);
            col[1] = multiply(a0, 9) ^ multiply(a1, 14) ^ multiply(a2, 11) ^ multiply(a3, 13);
            col[2] = multiply(a0, 13) ^ multiply(a1, 9) ^ multiply(a2, 14) ^ multiply(a3, 11);
            col[3] = multiply(a0, 11) ^ multiply(a1, 13) ^ multiply(a2, 9) ^ multiply(a3, 14);
        }
    }

    int m_rounds = 10;
    std::vector<quint8> m_words;
};

/// The padding of passwords (algorithm 2)
const QByteArray PASSWORD_PADDING =
        QByteArray::fromHex("28BF4E5E4E758A4164004E56FFFA01082E2E00B6D0683E802F0CA9FE6453697A");

QByteArray md5(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Md5); }

QByteArray padded(const QByteArray& password) { return (password + PASSWORD_PADDING).left(32); }

/// An initialisation vector for AES
QByteArray randomBytes(int count) {
    QByteArray bytes(count, Qt::Uninitialized);
    for (char& byte: bytes) {
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

}  // namespace

QByteArray Aes::decrypt(const QByteArray& key, const QByteArray& data, bool padded) {
    if (data.size() < 32 || data.size() % 16 != 0 || (key.size() != 16 && key.size() != 24 && key.size() != 32)) {
        // Files have strings shorter than a block with an initialisation vector: they are empty
        return {};
    }
    const AesKey aes(key);
    QByteArray out(data.size() - 16, Qt::Uninitialized);
    const auto* in = reinterpret_cast<const quint8*>(data.constData());
    auto* o = reinterpret_cast<quint8*>(out.data());
    for (qsizetype block = 16; block < data.size(); block += 16) {
        quint8 s[16];
        std::memcpy(s, in + block, 16);
        aes.decrypt(s);
        for (int i = 0; i < 16; ++i) {
            o[block - 16 + i] = s[i] ^ in[block - 16 + i];
        }
    }
    if (padded && !out.isEmpty()) {
        const int pad = static_cast<quint8>(out.back());
        if (pad >= 1 && pad <= 16 && pad <= out.size()) {
            out.chop(pad);
        }
    }
    return out;
}

QByteArray Aes::encrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data, bool padded) {
    QByteArray plain = data;
    if (padded) {
        const int pad = 16 - static_cast<int>(plain.size() % 16);
        plain.append(QByteArray(pad, static_cast<char>(pad)));
    }
    if (plain.size() % 16 != 0 || iv.size() != 16) {
        return {};
    }
    const AesKey aes(key);
    QByteArray out = iv + plain;
    auto* o = reinterpret_cast<quint8*>(out.data());
    for (qsizetype block = 16; block < out.size(); block += 16) {
        for (int i = 0; i < 16; ++i) {
            o[block + i] ^= o[block - 16 + i];
        }
        aes.encrypt(o + block);
    }
    return out;
}

QByteArray rc4(const QByteArray& key, const QByteArray& data) {
    if (key.isEmpty()) {
        return data;
    }
    quint8 s[256];
    for (int i = 0; i < 256; ++i) {
        s[i] = static_cast<quint8>(i);
    }
    for (int i = 0, j = 0; i < 256; ++i) {
        j = (j + s[i] + static_cast<quint8>(key[i % key.size()])) & 0xff;
        std::swap(s[i], s[j]);
    }
    QByteArray out = data;
    for (qsizetype k = 0, i = 0, j = 0; k < out.size(); ++k) {
        i = (i + 1) & 0xff;
        j = (j + s[i]) & 0xff;
        std::swap(s[i], s[j]);
        out[k] = static_cast<char>(out[k] ^ s[(s[i] + s[j]) & 0xff]);
    }
    return out;
}

bool Crypt::init(const Parameters& parameters) {
    m_parameters = parameters;
    const Parameters& p = m_parameters;
    if (p.streamMethod == "None" && p.stringMethod == "None") {
        return true;  // only embedded files are encrypted: the content needs no key
    }
    if (p.v == 5 || p.r >= 5) {
        if (p.u.size() < 48 || p.o.size() < 48 || p.ue.size() < 32 || p.oe.size() < 32) {
            m_error = tr("The encryption of the PDF file is damaged");
            return false;
        }
        // An empty password, as the user's and then as the owner's
        const QByteArray password;
        const auto hash = [&](const QByteArray& salt, const QByteArray& userData) {
            return p.r >= 6 ? hash2B(password, salt, userData) :
                              QCryptographicHash::hash(password + salt + userData, QCryptographicHash::Sha256);
        };
        const QByteArray u = p.u.left(48);
        if (hash(p.u.mid(32, 8), {}) == p.u.left(32)) {
            m_key = Aes::decrypt(hash(p.u.mid(40, 8), {}), QByteArray(16, '\0') + p.ue.left(32), false);
        } else if (hash(p.o.mid(32, 8), u) == p.o.left(32)) {
            m_key = Aes::decrypt(hash(p.o.mid(40, 8), u), QByteArray(16, '\0') + p.oe.left(32), false);
        }
        if (m_key.size() != 32) {
            m_error = tr("The PDF file is protected by a password");
            return false;
        }
        return true;
    }
    if (p.r < 2 || p.r > 4) {
        m_error = tr("The PDF file is encrypted in a way that is not understood");
        return false;
    }
    if (userKey({})) {
        return true;
    }
    // The owner's password is empty: it gives the user's (algorithm 7)
    const int n = p.r == 2 ? 5 : std::clamp(p.length / 8, 5, 16);
    QByteArray ownerKey = md5(padded({}));
    if (p.r >= 3) {
        for (int i = 0; i < 50; ++i) {
            ownerKey = md5(ownerKey.left(n));
        }
    }
    ownerKey = ownerKey.left(n);
    QByteArray user = p.o.left(32);
    if (p.r == 2) {
        user = rc4(ownerKey, user);
    } else {
        for (int i = 19; i >= 0; --i) {
            QByteArray key = ownerKey;
            for (char& c: key) {
                c = static_cast<char>(c ^ i);
            }
            user = rc4(key, user);
        }
    }
    if (userKey(user)) {
        return true;
    }
    m_error = tr("The PDF file is protected by a password");
    return false;
}

bool Crypt::userKey(const QByteArray& password) {
    const Parameters& p = m_parameters;
    const int n = p.r == 2 ? 5 : std::clamp(p.length / 8, 5, 16);
    // Algorithm 2
    QByteArray input = padded(password) + p.o.left(32);
    for (int i = 0; i < 4; ++i) {
        input.append(static_cast<char>((static_cast<quint32>(p.p) >> (8 * i)) & 0xff));
    }
    input += p.id;
    if (p.r >= 4 && !p.encryptMetadata) {
        input += QByteArray(4, '\xff');
    }
    QByteArray key = md5(input);
    if (p.r >= 3) {
        for (int i = 0; i < 50; ++i) {
            key = md5(key.left(n));
        }
    }
    key = key.left(n);
    // Algorithms 4 and 5: the key is right if it makes the U entry of the file
    QByteArray u;
    if (p.r == 2) {
        u = rc4(key, PASSWORD_PADDING);
        if (u != p.u.left(32)) {
            return false;
        }
    } else {
        u = md5(PASSWORD_PADDING + p.id);
        for (int i = 0; i < 20; ++i) {
            QByteArray k = key;
            for (char& c: k) {
                c = static_cast<char>(c ^ i);
            }
            u = rc4(k, u);
        }
        if (u.left(16) != p.u.left(16)) {
            return false;
        }
    }
    m_key = key;
    return true;
}

QByteArray Crypt::hash2B(const QByteArray& password, const QByteArray& salt, const QByteArray& userData) const {
    QByteArray k = QCryptographicHash::hash(password + salt + userData, QCryptographicHash::Sha256);
    for (int round = 0;; ++round) {
        const QByteArray k1 = (password + k + userData).repeated(64);
        const QByteArray e = Aes::encrypt(k.left(16), k.mid(16, 16), k1, false).mid(16);
        int sum = 0;
        for (int i = 0; i < 16; ++i) {
            sum += static_cast<quint8>(e[i]);
        }
        const QCryptographicHash::Algorithm algorithm = sum % 3 == 0 ? QCryptographicHash::Sha256 :
                                                        sum % 3 == 1 ? QCryptographicHash::Sha384 :
                                                                       QCryptographicHash::Sha512;
        k = QCryptographicHash::hash(e, algorithm);
        if (round >= 63 && static_cast<quint8>(e.back()) <= round - 31) {
            break;
        }
    }
    return k.left(32);
}

QByteArray Crypt::objectKey(int number, int generation, bool aes) const {
    // Algorithm 1
    QByteArray input = m_key;
    input.append(static_cast<char>(number & 0xff));
    input.append(static_cast<char>((number >> 8) & 0xff));
    input.append(static_cast<char>((number >> 16) & 0xff));
    input.append(static_cast<char>(generation & 0xff));
    input.append(static_cast<char>((generation >> 8) & 0xff));
    if (aes) {
        input += "sAlT";
    }
    return md5(input).left(std::min<qsizetype>(m_key.size() + 5, 16));
}

QByteArray Crypt::crypt(const QByteArray& method, const QByteArray& data, int number, int generation,
                        bool encrypting) const {
    if (method == "None") {
        return data;
    }
    if (method == "AESV3") {
        return encrypting ? Aes::encrypt(m_key, randomBytes(16), data) : Aes::decrypt(m_key, data);
    }
    if (method == "AESV2") {
        const QByteArray key = objectKey(number, generation, true);
        return encrypting ? Aes::encrypt(key, randomBytes(16), data) : Aes::decrypt(key, data);
    }
    return rc4(objectKey(number, generation, false), data);
}

QByteArray Crypt::decryptString(const QByteArray& data, int number, int generation) const {
    return crypt(m_parameters.stringMethod, data, number, generation, false);
}

QByteArray Crypt::decryptStream(const QByteArray& data, int number, int generation) const {
    return crypt(m_parameters.streamMethod, data, number, generation, false);
}

QByteArray Crypt::encryptString(const QByteArray& data, int number, int generation) const {
    return crypt(m_parameters.stringMethod, data, number, generation, true);
}

QByteArray Crypt::encryptStream(const QByteArray& data, int number, int generation) const {
    return crypt(m_parameters.streamMethod, data, number, generation, true);
}

}  // namespace Pdf
