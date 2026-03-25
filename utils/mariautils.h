#ifndef MARIAUTILS_H
#define MARIAUTILS_H
#include "osinfo.h"
#include <QDebug>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVersionNumber>
#include <functional>

class MariaUtils : public QObject {
  Q_OBJECT

public:
  explicit MariaUtils(QObject *parent = nullptr);

  static QString findMariaPackages(
      PkgManagerType type, const QString &logicalName,
      const QVersionNumber &minVer,
      std::function<bool(const QVersionNumber &)> isSupported);

  static QString findMariaPackagesLinux(
      PkgManagerType type, const QString &logicalName,
      const QVersionNumber &minVer,
      std::function<bool(const QVersionNumber &)> isSupported);

  static QString findMariaPackagesHomebrew(
      const QVersionNumber &minVer,
      std::function<bool(const QVersionNumber &)> isSupported);

  static QString findMariaPackagesWinget(
      const QVersionNumber &minVer,
      std::function<bool(const QVersionNumber &)> isSupported);


private:
  static QString cleanVersion(const QString &version);
};

#endif