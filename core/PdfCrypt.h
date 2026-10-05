/*
 * Qournal
 *
 * The standard security handler of PDF files (section 7.6 of the specification): files whose content is encrypted
 * although they open without a password, as many are to keep them from being changed or printed. Understood are
 * RC4 with 40 to 128 bits and AES with 128 and 256 bits (revisions 2 to 6), with an empty user or owner password.
 * Files that need a password to be opened are not.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QString>

namespace Pdf {

/// AES, as PDF files use it: in CBC mode, with PKCS#7 padding
namespace Aes {
/// The 16 bytes of an initialisation vector are in front of the data
QByteArray decrypt(const QByteArray& key, const QByteArray& data, bool padded = true);
QByteArray encrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data, bool padded = true);
}  // namespace Aes

QByteArray rc4(const QByteArray& key, const QByteArray& data);

class Crypt {
public:
    /// The parts of the encryption dictionary that are needed
    struct Parameters {
        int v = 0;  ///< the algorithm: 1, 2, 4 or 5
        int r = 0;  ///< the revision: 2 to 6
        int length = 40;
        QByteArray o;
        QByteArray u;
        QByteArray oe;
        QByteArray ue;
        qint32 p = 0;
        bool encryptMetadata = true;
        /// For V 4 and 5: the methods of the crypt filters for streams and strings: "V2", "AESV2", "AESV3" or
        /// "None" (also for the filter Identity)
        QByteArray streamMethod;
        QByteArray stringMethod;
        QByteArray id;  ///< the first part of the ID of the file
    };

    /**
     * Finds the key of the file with an empty password.
     * @return false if the file needs a password, or uses something that is not understood; error() tells which
     */
    bool init(const Parameters& parameters);
    QString error() const { return m_error; }

    QByteArray decryptString(const QByteArray& data, int number, int generation) const;
    QByteArray decryptStream(const QByteArray& data, int number, int generation) const;
    QByteArray encryptString(const QByteArray& data, int number, int generation) const;
    QByteArray encryptStream(const QByteArray& data, int number, int generation) const;
    /// Whether metadata streams are encrypted
    bool encryptsMetadata() const { return m_parameters.encryptMetadata; }

private:
    QByteArray objectKey(int number, int generation, bool aes) const;
    QByteArray crypt(const QByteArray& method, const QByteArray& data, int number, int generation,
                     bool encrypting) const;
    bool userKey(const QByteArray& password);
    QByteArray hash2B(const QByteArray& password, const QByteArray& salt, const QByteArray& userData) const;

    Parameters m_parameters;
    QByteArray m_key;
    QString m_error;
};

}  // namespace Pdf
