#ifndef JSUTILS_H
#define JSUTILS_H

#include <QRandomGenerator>
#include <QString>
#include <QVariantMap>

class JSUtils {
public:
  static const int AUTO_SEED_HEX_LENGTH;
  static const int CUSTOM_APPENDIX_LENGTH;
  static const int MIN_CUSTOM_SEED_LENGTH;

  static QVariantMap generateANNEKeys(const QString &seedInput = QString());
  
  static QVariantMap runJSWithSeed(const QString &seed);
  static QString processCustomSeed(const QString &userInput);
  static void secureClear(QString &str);
  static void secureClear(QByteArray &arr);
private:
  JSUtils() = delete;
  static QString generateSecureHex(int length);
  static QByteArray generateSecureBytes(int byteLength);
  static QByteArray getAppSalt();
  static QString hashWithSaltAndIterations(const QByteArray &data, const QByteArray &salt, int iterations = 150000);
};

#endif
