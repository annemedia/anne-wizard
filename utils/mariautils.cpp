#include "mariautils.h"
#include "utils.h"
#include <QDebug>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QStringList>
#include <QSysInfo>
#include <algorithm>

MariaUtils::MariaUtils(QObject *parent) : QObject(parent) {}

QString MariaUtils::findMariaPackages(PkgManagerType type, const QString &logicalName, const QVersionNumber &minVer, std::function<bool(const QVersionNumber &)> isSupported) {

  QString osName = QSysInfo::productType().toLower();

  if (osName.contains("macos") || osName == "osx") {

    return findMariaPackagesHomebrew(minVer, isSupported);
  } else if (osName.contains("windows")) {

    return findMariaPackagesWinget(minVer, isSupported);
  } else {

    return findMariaPackagesLinux(type, logicalName, minVer, isSupported);
  }
}

QString MariaUtils::findMariaPackagesLinux(PkgManagerType type, const QString &logicalName, const QVersionNumber &minVer, std::function<bool(const QVersionNumber &)> isSupported) {

  if (type == PkgManagerType::Unknown || logicalName.isEmpty())
    return "";

  auto cleanVersion = [](const QString &version) -> QString {
    QString v = version;
    int p = v.indexOf(':');
    if (p != -1)
      v = v.mid(p + 1);
    v = v.section('-', 0, 0).section('+', 0, 0).section('~', 0, 0);
    if (v.startsWith("1."))
      v = v.mid(2);
    return v.replace('_', '.').trimmed();
  };

  auto getCandidatePackages = [&]() -> QStringList {
    if (logicalName == "mariadb" || logicalName == "mariadb-server") {
      switch (type) {
      case PkgManagerType::Apt:
      case PkgManagerType::Dnf:
        return {"mariadb-server", "mariadb"};
      case PkgManagerType::Pacman:
        return {"mariadb"};
      default:
        return {"mariadb-server", "mariadb"};
      }
    }
    return {logicalName};
  };

  if (type == PkgManagerType::Dnf) {
    QStringList candidates = getCandidatePackages();

    for (const QString &pkg : candidates) {
      QString verStr = Utils::getPackageVersion(type, "", {}, pkg);
      if (verStr.isEmpty())
        continue;

      if (verStr == "999.999") {
        qDebug() << "DNF: Package" << pkg << "exists (version unknown)";
        return pkg;
      }

      QVersionNumber ver = QVersionNumber::fromString(cleanVersion(verStr));
      if (ver.isNull() || ver < minVer || !isSupported(ver))
        continue;

      qDebug() << "SELECTED:" << pkg << ver.toString() << "for" << logicalName;
      return pkg;
    }

    if (!candidates.isEmpty()) {
      qDebug() << "DNF: Fallback - using" << logicalName;
      return logicalName;
    }

    qWarning() << "No MariaDB package found for" << logicalName;
    return "";
  }

  QString bestPkg;
  QVersionNumber bestVer;
  QStringList candidates = getCandidatePackages();

  qDebug() << "Looking for MariaDB, logicalName:" << logicalName;
  qDebug() << "Candidates:" << candidates;

  for (const QString &pkg : candidates) {
    qDebug() << "Trying package:" << pkg;
    QString verStr = Utils::getPackageVersion(type, "", {}, pkg);
    qDebug() << "Result for" << pkg << ":" << verStr;
    if (verStr.isEmpty())
      continue;

    QVersionNumber ver = QVersionNumber::fromString(cleanVersion(verStr));
    if (ver.isNull() || ver < minVer || !isSupported(ver))
      continue;

    if (bestVer.isNull() || ver > bestVer) {
      bestVer = ver;
      bestPkg = pkg;
    }
  }

  if (bestPkg.isEmpty()) {
    qWarning() << "No MariaDB package found for" << logicalName;
    return "";
  }

  qDebug() << "SELECTED:" << bestPkg << bestVer.toString() << "for" << logicalName;
  return bestPkg;
}

QString MariaUtils::findMariaPackagesHomebrew(const QVersionNumber &minVer, std::function<bool(const QVersionNumber &)> isSupported) {

  QStringList brewPackages = {"mariadb", "mariadb@latest"};

  for (int major = 20; major >= 10; major--) {
    for (int minor = 11; minor >= 0; minor--) {
      brewPackages << QString("mariadb@%1.%2").arg(major).arg(minor);
    }
  }

  for (const QString &pkg : brewPackages) {
    QProcessResult result = Utils::executeProcess("brew", {"info", pkg});

    if (result.QPSuccess && result.exitCode == 0) {
      QString output = result.stdOut;

      QRegularExpression re("([0-9]+\\.[0-9]+\\.[0-9]+)");
      QRegularExpressionMatch match = re.match(output);

      if (match.hasMatch()) {
        QString versionStr = match.captured(1);
        QVersionNumber ver = QVersionNumber::fromString(versionStr);

        if (ver >= minVer && isSupported(ver)) {
          qDebug() << "SELECTED (Homebrew):" << pkg << ver.toString();
          return pkg;
        }
      }
    }
  }

  qWarning() << "No MariaDB package found in Homebrew";
  return "";
}

QString MariaUtils::findMariaPackagesWinget(const QVersionNumber &minVer, std::function<bool(const QVersionNumber &)> isSupported) {

  QProcessResult result = Utils::executeProcess("winget", {"search", "MariaDB.Server", "--source", "winget"});

  if (!result.QPSuccess) {
    qWarning() << "Failed to execute winget command";
    return "";
  }

  QString output = result.stdOut;
  QStringList lines = output.split('\n', Qt::SkipEmptyParts);

  QString bestPkg;
  QVersionNumber bestVer;

  for (int i = 1; i < lines.size(); i++) {
    QString line = lines[i];
    QStringList parts = line.split('\t', Qt::SkipEmptyParts);

    if (parts.size() >= 3) {
      QString id = parts[0].trimmed();
      QString version = parts[2].trimmed();

      if (id.startsWith("MariaDB.Server")) {
        QVersionNumber ver = QVersionNumber::fromString(version);
        if (ver >= minVer && isSupported(ver)) {
          if (bestVer.isNull() || ver > bestVer) {
            bestVer = ver;
            bestPkg = id;
          }
        }
      }
    }
  }

  if (!bestPkg.isEmpty()) {
    qDebug() << "SELECTED (winget):" << bestPkg << bestVer.toString();
    return bestPkg;
  }

  qWarning() << "No MariaDB package found in winget";
  return "";
}