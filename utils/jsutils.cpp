#include "jsutils.h"
#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSysInfo>

const int JSUtils::AUTO_SEED_HEX_LENGTH = 128;
const int JSUtils::CUSTOM_APPENDIX_LENGTH = 32;
const int JSUtils::MIN_CUSTOM_SEED_LENGTH = 96;

QVariantMap JSUtils::generateANNEKeys(const QString &seedInput) {
  qDebug() << "=== generateANNEKeys DEBUG START ===";
  qDebug() << "seedInput length:" << seedInput.length();
  qDebug() << "seedInput first 20:" << seedInput;

  QVariantMap result;
  QString finalSeed;
  QString originalInput = seedInput.trimmed();
  bool isCustom = !originalInput.isEmpty();

  if (isCustom) {

    if (originalInput.length() < MIN_CUSTOM_SEED_LENGTH) {
      result["error"] = QString("Custom seed must be at least %1 characters (%2 bits). Provided: %3 chars")
                            .arg(MIN_CUSTOM_SEED_LENGTH)
                            .arg(MIN_CUSTOM_SEED_LENGTH * 4)
                            .arg(originalInput.length());
      return result;
    }

    qDebug() << "Calling processCustomSeed...";
    finalSeed = processCustomSeed(originalInput);
    qDebug() << "After processCustomSeed, finalSeed length:" << finalSeed.length();
    qDebug() << "finalSeed first 20:" << finalSeed;
    qDebug() << "Processed custom seed. Original:" << originalInput.length() << "chars, Final:" << finalSeed.length() << "chars";
  } else {

    finalSeed = generateSecureHex(AUTO_SEED_HEX_LENGTH);
    qDebug() << "Auto-generated secure seed. Length:" << finalSeed.length() << "chars (" << (finalSeed.length() * 4) << "bits)";
  }

  QVariantMap jsResult = runJSWithSeed(finalSeed);

  QString finalSeedCopy = finalSeed;
  secureClear(finalSeed);

  if (jsResult.contains("error")) {
    secureClear(finalSeedCopy);
    return jsResult;
  }

  QString minerAccountId = jsResult["nid"].toString();
  QString minerPublicKey = jsResult["publicKey"].toString();
  QString minerSeed = jsResult["seed"].toString();

  if (minerAccountId.isEmpty() || minerSeed.isEmpty() || minerPublicKey.isEmpty()) {
    result["error"] = "Error: Incomplete keys generated from JS library.";
    secureClear(finalSeedCopy);
    return result;
  }

  result["nid"] = minerAccountId;
  result["publicKey"] = minerPublicKey;
  result["seed"] = minerSeed;
  result["source"] = isCustom ? "custom" : "auto";
  result["original_length"] = isCustom ? originalInput.length() : finalSeedCopy.length();

  secureClear(finalSeedCopy);

  qDebug() << "Successfully generated miner keys from" << (isCustom ? "custom" : "auto-generated") << "seed";

  return result;
}

QByteArray JSUtils::generateSecureBytes(int byteLength) {
  QByteArray bytes(byteLength, '\0');

  QRandomGenerator secureRng = QRandomGenerator::securelySeeded();

  for (int i = 0; i < byteLength; ++i) {
    bytes[i] = static_cast<char>(secureRng.generate() & 0xFF);
  }

  QRandomGenerator *systemRng = QRandomGenerator::system();
  for (int i = 0; i < qMin(byteLength, 32); ++i) {
    bytes[i] ^= static_cast<char>(systemRng->generate() & 0xFF);
  }

  return bytes;
}

QString JSUtils::generateSecureHex(int length) {
  int byteLength = (length + 1) / 2;
  QByteArray bytes = generateSecureBytes(byteLength);
  QString hex = bytes.toHex().left(length).toLower();
  secureClear(bytes);
  return hex;
}

QByteArray JSUtils::getAppSalt() {

  QString appIdentifier = QStringLiteral("AnneAcc-Seed");
  QString appVersion = QStringLiteral("1.0.0");

  QString buildInfo = QStringLiteral("Qt%1-Build%2").arg(QT_VERSION_STR).arg(__DATE__);

  QString systemContext = QStringLiteral("%1-%2").arg(QSysInfo::productType()).arg(QSysInfo::buildCpuArchitecture());

  QString compileTimestamp = QStringLiteral("2026-31-01");

  QString saltBase = QStringLiteral("%1|%2|%3|%4|%5|%6")
                         .arg(appIdentifier)
                         .arg(appVersion)
                         .arg(buildInfo)
                         .arg(systemContext)
                         .arg(compileTimestamp)
                         .arg("FixedPublicComponent:d7f2a9c4b6e8f1a3c5d9b2e7f0a8d6c4b");

  qDebug() << "Salt derivation string:" << saltBase;

  QByteArray salt = QCryptographicHash::hash(saltBase.toUtf8(), QCryptographicHash::Sha3_512);

  salt = QCryptographicHash::hash(salt + QByteArrayLiteral("FinalRound"), QCryptographicHash::Sha3_512);

  return salt;
}

QString JSUtils::hashWithSaltAndIterations(const QByteArray &data, const QByteArray &salt, int iterations) {
  QByteArray hash = data + salt;

  for (int i = 0; i < iterations; ++i) {
    if (i % 2 == 0) {
      hash = QCryptographicHash::hash(hash, QCryptographicHash::Sha3_512);
    } else {
      hash = QCryptographicHash::hash(hash + QByteArray::number(i), QCryptographicHash::Sha512);
    }
  }

  QString result = hash.toHex();
  secureClear(hash);
  return result;
}

QString JSUtils::processCustomSeed(const QString &userInput) {
  QString processed = userInput.trimmed();

  if (processed.length() < MIN_CUSTOM_SEED_LENGTH) {
    return processed;
  }

  QString appendix = generateSecureHex(CUSTOM_APPENDIX_LENGTH);

  QString finalSeed = processed + appendix;

  qDebug() << "Simple append: Original:" << processed.length() << " + Appendix:" << appendix.length() << " = Total:" << finalSeed.length();

  if (finalSeed.length() < AUTO_SEED_HEX_LENGTH) {

    finalSeed += generateSecureHex(AUTO_SEED_HEX_LENGTH - finalSeed.length());
  }

  secureClear(appendix);
  return finalSeed;
}

QVariantMap JSUtils::runJSWithSeed(const QString &seed) {
  QVariantMap result;
  qDebug() << "Running JS with" << seed.length() << "char seed";

  QJSEngine engine;
  QFile jsFile(":/assets/anneacc.js");

  if (!jsFile.open(QIODevice::ReadOnly)) {
    result["error"] = "Cannot open JS file";
    return result;
  }

  QByteArray jsData = jsFile.readAll();
  jsFile.close();

  QJSValue loadResult = engine.evaluate(jsData, "anneacc.js");
  if (loadResult.isError()) {
    result["error"] = QString("JS Load: %1").arg(loadResult.toString());
    secureClear(jsData);
    return result;
  }

  QJSValue cryptoObj = engine.globalObject().property("ANNE");
  if (!cryptoObj.isObject()) {
    result["error"] = "ANNE object not found";
    secureClear(jsData);
    return result;
  }

  QJSValue generateFunc = cryptoObj.property("getNewAnneAccount");
  if (!generateFunc.isCallable()) {
    result["error"] = "getNewAnneAccount not callable";
    secureClear(jsData);
    return result;
  }

  QJSValueList args;
  args << QJSValue(seed);
  QJSValue jsResult = generateFunc.callWithInstance(cryptoObj, args);

  secureClear(jsData);

  if (jsResult.isError()) {
    result["error"] = QString("JS Runtime: %1").arg(jsResult.toString());
    return result;
  }

  if (jsResult.isUndefined() || jsResult.isNull()) {
    result["error"] = "JS returned no value";
    return result;
  }

  QVariantMap resultMap = jsResult.toVariant().toMap();
  if (resultMap.isEmpty()) {
    result["error"] = "JS returned empty map";
    return result;
  }

  return resultMap;
}

void JSUtils::secureClear(QString &str) {
  if (!str.isEmpty()) {

    for (QChar &ch : str)
      ch = QChar(0xFF);
    for (QChar &ch : str)
      ch = QChar(0x00);
    for (QChar &ch : str)
      ch = QChar(0xAA);
    for (QChar &ch : str)
      ch = QChar(0x55);

    asm volatile("" ::: "memory");
  }
  str.clear();
  str.squeeze();
}

void JSUtils::secureClear(QByteArray &arr) {
  if (!arr.isEmpty()) {

    memset(arr.data(), 0xFF, arr.size());
    memset(arr.data(), 0x00, arr.size());
    memset(arr.data(), 0xAA, arr.size());
    memset(arr.data(), 0x55, arr.size());
    memset(arr.data(), rand() & 0xFF, arr.size());

    asm volatile("" ::: "memory");
  }
  arr.clear();
  arr.squeeze();
}
