#include "javautils.h"
#include "systemutils.h"
#include "utils.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QTextStream>
#include <algorithm>

JavaUtils::JavaUtils(QObject *parent) : QObject(parent) {}

QList<JavaPackageInfo> JavaUtils::getAvailableJavaPackages(PkgManagerType type) {
  QString osName = QSysInfo::productType().toLower();

  if (osName.contains("macos") || osName == "osx") {
    return getAvailableJavaPackagesMacOS();
  } else if (osName.contains("windows")) {
    return getAvailableJavaPackagesWindows();
  } else {
    return getAvailableJavaPackagesLinux(type);
  }
}

QList<JavaPackageInfo> JavaUtils::getAvailableJavaPackagesLinux(PkgManagerType type) {
  QList<JavaPackageInfo> packages;
  QMap<int, JavaPackageInfo> versionMap;
  QStringList packagePatterns;

  switch (type) {
  case PkgManagerType::Apt: {
    QProcessResult aptResult = Utils::executeProcess("apt-cache", {"search", "--names-only", "^openjdk-.*-jdk$"}, 90000);
    QString output = aptResult.stdOut;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
      QString pkgName = line.split(' ').first().trimmed();

      if (!pkgName.contains("-headless") && !pkgName.contains("-jvmci") && !pkgName.contains("-dbg") && pkgName.contains("-jdk")) {
        QRegularExpression re("openjdk-(\\d+)-jdk");
        QRegularExpressionMatch match = re.match(pkgName);
        if (match.hasMatch()) {
          int version = match.captured(1).toInt();
          if (version >= 11 && version <= 40) {
            packagePatterns << pkgName;
          }
        }
      }
    }

    std::sort(packagePatterns.begin(), packagePatterns.end(), [](const QString &a, const QString &b) {
      QRegularExpression reA("openjdk-(\\d+)-jdk");
      QRegularExpression reB("openjdk-(\\d+)-jdk");
      int verA = reA.match(a).captured(1).toInt();
      int verB = reB.match(b).captured(1).toInt();
      return verA > verB;
    });
    break;
  }

  case PkgManagerType::Dnf: {
    QProcessResult dnfResult = Utils::executeProcess("dnf", {"list", "available", "*openjdk*devel*"}, 90000);
    QString output = dnfResult.stdOut;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QMap<QString, QString> packageVersions;

    for (const QString &line : lines) {
      if (line.contains("Available packages") || line.contains("Updating and loading")) {
        continue;
      }

      QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
      if (parts.size() >= 3) {
        QString rawPkgName = parts[0].trimmed();
        QString version = parts[1].trimmed();
        QString pkgName = rawPkgName;

        if (pkgName.contains('.')) {
          pkgName = pkgName.split('.').first();
        }

        QRegularExpression re("java-(\\d+)-openjdk-devel");
        QRegularExpressionMatch match = re.match(pkgName);

        if (match.hasMatch()) {
          int majorVersion = match.captured(1).toInt();
          if (majorVersion >= 11) {
            packagePatterns << pkgName;
            packageVersions[pkgName] = version;
          }
        }
      }
    }

    qDebug() << "Found Java packages with patterns:" << packagePatterns;

    for (const QString &pattern : packagePatterns) {
      QString versionStr = packageVersions.value(pattern, "");
      qDebug() << "Checking package:" << pattern << "-> version:" << versionStr;

      if (!versionStr.isEmpty() && versionStr != "999.999") {
        JavaPackageInfo info;
        info.packageName = pattern;

        QRegularExpression re;
        if (pattern == "java-latest-openjdk-devel") {
          info.majorVersion = 99;
          info.displayName = "Java (Latest)";
        } else {
          re.setPattern("java-(\\d+)-openjdk-devel");
          QRegularExpressionMatch match = re.match(pattern);
          if (match.hasMatch()) {
            info.majorVersion = match.captured(1).toInt();
            info.displayName = QString("Java %1").arg(info.majorVersion);
          } else {
            qDebug() << "Could not extract version from:" << pattern;
            continue;
          }
        }

        QProcessResult checkResult = Utils::executeProcess("rpm", {"-q", pattern}, 2000);
        info.isInstalled = (checkResult.exitCode == 0);

        if (info.isInstalled && info.majorVersion >= 11) {
          if (!verifyJDKInstallation(info.packageName, type)) {
            qDebug() << "Filtering incomplete JDK installation:" << info.packageName;
            info.isInstalled = false;
            info.installedVersion = "";
          } else {
            info.installedVersion = versionStr;
          }
        } else if (info.isInstalled) {
          info.installedVersion = versionStr;
        } else {
          info.installedVersion = "";
        }

        if (!versionMap.contains(info.majorVersion)) {
          versionMap.insert(info.majorVersion, info);
          qDebug() << "Adding Java package:" << info.displayName << "(" << info.packageName << ")" << "Installed:" << info.isInstalled;
        } else {
          JavaPackageInfo existing = versionMap.value(info.majorVersion);
          if (!existing.isInstalled && info.isInstalled) {
            versionMap.insert(info.majorVersion, info);
            qDebug() << "Replacing with installed version:" << pattern;
          } else {
            qDebug() << "Duplicate version" << info.majorVersion << "- skipping" << pattern;
          }
        }
      }
    }

    for (JavaPackageInfo info : versionMap.values()) {
      packages.append(info);
    }

    std::sort(packages.begin(), packages.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

    qDebug() << "Total unique Java packages found:" << packages.size();
    return packages;
    break;
  }

  case PkgManagerType::Pacman: {
    QProcessResult pacmanResult = Utils::executeProcess("pacman", {"-Ss", "openjdk"}, 90000);
    QString output = pacmanResult.stdOut;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QSet<QString> uniquePackages;

    for (int i = 0; i < lines.size(); i++) {
      if (lines[i].startsWith("extra/") || lines[i].startsWith("community/")) {
        QString pkgName = lines[i].split(' ').first().trimmed();
        if (pkgName.contains('/')) {
          pkgName = pkgName.split('/').last();
        }

        if ((pkgName.startsWith("jdk") || pkgName.startsWith("java-")) && pkgName.contains("-openjdk") && !pkgName.contains("-jre") && !pkgName.contains("-headless") &&
            !pkgName.contains("-doc") && !pkgName.contains("-src")) {
          uniquePackages.insert(pkgName);
        }
      }
    }

    packagePatterns = QStringList(uniquePackages.begin(), uniquePackages.end());
    std::sort(packagePatterns.begin(), packagePatterns.end());
    qDebug() << "Arch Java packages found:" << packagePatterns;
    break;
  }

  default:
    qWarning() << "Unsupported package manager type for Linux";
    return packages;
  }

  qDebug() << "Found Java packages with patterns:" << packagePatterns;

  for (const QString &pattern : packagePatterns) {
    QString versionStr = Utils::getPackageVersion(type, "", {}, pattern);
    qDebug() << "Checking package:" << pattern << "-> version:" << versionStr;

    if (!versionStr.isEmpty() && versionStr != "999.999") {
      JavaPackageInfo info;
      info.packageName = pattern;

      QRegularExpression re;
      switch (type) {
      case PkgManagerType::Apt:
        re.setPattern("openjdk-(\\d+)-jdk");
        break;
      case PkgManagerType::Dnf:
        if (pattern == "java-latest-openjdk") {
          info.majorVersion = 99;
          info.displayName = "Java (Latest)";
        } else {
          re.setPattern("java-(\\d+)-openjdk");
        }
        break;
      case PkgManagerType::Pacman:
        if (pattern == "jdk-openjdk") {
          info.majorVersion = 99;
          info.displayName = "Java (Latest)";
        } else {
          re.setPattern("jdk(\\d+)-openjdk");
        }
        break;
      default:
        continue;
      }

      if (info.majorVersion == 0) {
        QRegularExpressionMatch match = re.match(pattern);
        if (match.hasMatch()) {
          info.majorVersion = match.captured(1).toInt();
          info.displayName = QString("Java %1").arg(info.majorVersion);
        } else {
          qDebug() << "Could not extract version from:" << pattern;
          continue;
        }
      }

      bool isInstalled = false;
      switch (type) {
      case PkgManagerType::Apt: {
        QProcessResult dpkgResult = Utils::executeProcess("dpkg-query", {"-W", "-f=${Status}", pattern}, 5000);
        isInstalled = (dpkgResult.exitCode == 0) && dpkgResult.stdOut.contains("install ok installed");
        break;
      }
      case PkgManagerType::Dnf: {
        QProcessResult rpmResult = Utils::executeProcess("rpm", {"-q", pattern}, 5000);
        isInstalled = rpmResult.exitCode == 0;
        break;
      }
      case PkgManagerType::Pacman: {
        QProcessResult pacmanResult = Utils::executeProcess("pacman", {"-Q", pattern}, 5000);
        isInstalled = pacmanResult.exitCode == 0;
        break;
      }
      default:
        isInstalled = false;
      }

      info.isInstalled = isInstalled;

      if (info.isInstalled && info.majorVersion >= 11) {
        if (!verifyJDKInstallation(info.packageName, type)) {
          qDebug() << "Filtering incomplete JDK installation:" << info.packageName;
          info.isInstalled = false;
          info.installedVersion = "";
        } else {
          info.installedVersion = versionStr;
        }
      } else if (info.isInstalled) {
        info.installedVersion = versionStr;
      } else {
        info.installedVersion = "";
      }

      if (!versionMap.contains(info.majorVersion)) {
        versionMap.insert(info.majorVersion, info);
        qDebug() << "Adding Java package:" << info.displayName << "(" << info.packageName << ")" << "Installed:" << info.isInstalled;
      } else {
        JavaPackageInfo existing = versionMap.value(info.majorVersion);
        if (!existing.isInstalled && info.isInstalled) {
          versionMap.insert(info.majorVersion, info);
          qDebug() << "Replacing with installed version:" << pattern;
        } else {
          qDebug() << "Duplicate version" << info.majorVersion << "- skipping" << pattern;
        }
      }
    }
  }

  for (JavaPackageInfo info : versionMap.values()) {
    packages.append(info);
  }

  std::sort(packages.begin(), packages.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

  qDebug() << "Total unique Java packages found:" << packages.size();
  return packages;
}

QList<JavaPackageInfo> JavaUtils::getAvailableJavaPackagesMacOS() {
  QList<JavaPackageInfo> packages;
  QMap<int, JavaPackageInfo> versionMap;

  qDebug() << "=== Scanning for Java installations on Mac ===";

  QProcess javaHomeProcess;
  javaHomeProcess.start("/usr/libexec/java_home", {"-V"});
  javaHomeProcess.waitForFinished(5000);

  QString javaHomeOutput = javaHomeProcess.readAllStandardError();

  if (javaHomeProcess.exitCode() == 0 && !javaHomeOutput.isEmpty()) {
    QStringList lines = javaHomeOutput.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
      if (line.contains("/Library/Java/JavaVirtualMachines/") && line.contains(".jdk/Contents/Home")) {
        QStringList parts = line.split(' ');
        QString path = parts.last().trimmed();

        QRegularExpression versionRe(R"((\d+)(?:\.\d+)*(?:_\d+)?)");
        QRegularExpressionMatch versionMatch = versionRe.match(line);

        if (versionMatch.hasMatch()) {
          QString fullVersion = versionMatch.captured(0);
          QStringList versionParts = fullVersion.split(QRegularExpression("[._]"));
          if (!versionParts.isEmpty()) {
            int majorVersion = versionParts[0].toInt();

            if (majorVersion >= 11) {
              JavaPackageInfo info;
              info.majorVersion = majorVersion;
              info.installedVersion = fullVersion;

              if (path.contains("temurin", Qt::CaseInsensitive)) {
                info.packageName = QString("temurin@%1").arg(majorVersion);
              } else {
                info.packageName = QString("java-%1").arg(majorVersion);
              }

              info.displayName = QString("Java %1").arg(majorVersion);
              info.isInstalled = true;

              if (!versionMap.contains(majorVersion)) {
                versionMap.insert(majorVersion, info);
                qDebug() << "[java_home] Java" << majorVersion << "->" << info.packageName;
              }
            }
          }
        }
      }
    }
  }

  QString brewPath = SystemUtils::findBrew();

  if (!brewPath.isEmpty()) {
    QProcessResult brewResult = Utils::executeProcess(brewPath, {"search", "--cask", "temurin"}, 10000);

    if (brewResult.QPSuccess) {
      QString output = brewResult.stdOut;
      QStringList lines = output.split('\n', Qt::SkipEmptyParts);
      QSet<QString> brewPackagesSet;

      for (const QString &line : lines) {
        QString trimmedLine = line.trimmed().remove('"');
        if (!trimmedLine.isEmpty() && trimmedLine != "temurin") {
          brewPackagesSet.insert(trimmedLine);
        }
      }

      QStringList brewPackages = QStringList(brewPackagesSet.begin(), brewPackagesSet.end());

      if (!brewPackages.isEmpty()) {
        QProcessResult listResult = Utils::executeProcess(brewPath, {"list", "--cask"}, 10000);
        QStringList installedBrewPackages;

        if (listResult.QPSuccess) {
          installedBrewPackages = listResult.stdOut.split('\n', Qt::SkipEmptyParts);
        }

        for (const QString &packageName : brewPackages) {

          int majorVersion = 0;
          QRegularExpression re1("temurin@(\\d+)");
          QRegularExpression re2("temurin(\\d+)");
          QRegularExpressionMatch match1 = re1.match(packageName);
          QRegularExpressionMatch match2 = re2.match(packageName);

          if (match1.hasMatch()) {
            majorVersion = match1.captured(1).toInt();
          } else if (match2.hasMatch()) {
            majorVersion = match2.captured(1).toInt();
          }

          if (majorVersion >= 11) {
            JavaPackageInfo info;
            info.majorVersion = majorVersion;
            info.packageName = packageName;
            info.isInstalled = installedBrewPackages.contains(packageName);

            info.displayName = QString("Java %1").arg(majorVersion);

            if (!versionMap.contains(majorVersion)) {
              versionMap.insert(majorVersion, info);
              qDebug() << "[brew] Java" << majorVersion << "->" << packageName;
            } else if (info.isInstalled) {

              JavaPackageInfo existing = versionMap.value(majorVersion);
              if (!existing.isInstalled) {
                versionMap.insert(majorVersion, info);
              }
            }
          }
        }
      }
    }
  }

  JavaCheckResult systemJava = checkSystemJava();

  if (systemJava.isJavaAvailable && systemJava.majorVersion >= 11) {
    int majorVersion = systemJava.majorVersion;

    if (!versionMap.contains(majorVersion)) {
      JavaPackageInfo info;
      info.packageName = "java-system";
      info.majorVersion = majorVersion;
      info.displayName = QString("Java %1").arg(majorVersion);
      info.isInstalled = true;
      info.installedVersion = systemJava.version;

      versionMap.insert(majorVersion, info);
      qDebug() << "[PATH] Java" << majorVersion << "in PATH";
    }
  }

  for (auto it = versionMap.begin(); it != versionMap.end(); ++it) {
    packages.append(it.value());
  }

  std::sort(packages.begin(), packages.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

  qDebug() << "Found" << packages.size() << "unique Java versions";
  return packages;
}

QList<JavaPackageInfo> JavaUtils::getAvailableJavaPackagesWindows() {
  QList<JavaPackageInfo> packages;
  QSet<int> foundVersions;
  QMap<int, JavaPackageInfo> versionMap;

  qDebug() << "=== Scanning for Java installations on Windows ===";

  JavaCheckResult systemJava = checkSystemJava();
  if (systemJava.isJavaAvailable && systemJava.majorVersion >= 11) {
    JavaPackageInfo info;
    info.packageName = QString("EclipseAdoptium.Temurin.%1.JDK").arg(systemJava.majorVersion);
    info.majorVersion = systemJava.majorVersion;
    info.displayName = QString("Java %1 (Already in PATH)").arg(systemJava.majorVersion);
    info.isInstalled = true;
    info.installedVersion = systemJava.version;

    versionMap.insert(systemJava.majorVersion, info);
    foundVersions.insert(systemJava.majorVersion);
    qDebug() << "Found Java in PATH:" << info.displayName << "version:" << systemJava.version;
  }

  QStringList possibleJavaPaths = {qEnvironmentVariable("ProgramFiles") + "\\Eclipse Adoptium",
                                   qEnvironmentVariable("ProgramFiles") + "\\AdoptOpenJDK",
                                   qEnvironmentVariable("ProgramFiles") + "\\Java",
                                   qEnvironmentVariable("ProgramFiles") + "\\Microsoft\\jdk",
                                   qEnvironmentVariable("ProgramW6432") + "\\Eclipse Adoptium",
                                   qEnvironmentVariable("ProgramW6432") + "\\AdoptOpenJDK",
                                   qEnvironmentVariable("ProgramW6432") + "\\Java",
                                   "C:\\Program Files\\Eclipse Adoptium",
                                   "C:\\Program Files\\AdoptOpenJDK",
                                   "C:\\Program Files\\Java",
                                   "C:\\Program Files\\Microsoft\\jdk"};

  for (const QString &basePath : possibleJavaPaths) {
    QDir dir(basePath);
    if (dir.exists()) {
      QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

      for (const QString &entry : entries) {
        QString javaExePath = basePath + "\\" + entry + "\\bin\\java.exe";

        if (QFile::exists(javaExePath)) {
          qDebug() << "Found java.exe in:" << basePath + "\\" + entry;

          QProcessResult javaResult = Utils::executeProcess(javaExePath, {"-version"}, 3000);
          QString output = javaResult.stdErr;
          QRegularExpression versionRe("version \"([^\"]+)\"");
          QRegularExpressionMatch versionMatch = versionRe.match(output);

          if (versionMatch.hasMatch()) {
            QString version = versionMatch.captured(1);
            QStringList parts = version.split('.');
            int majorVersion = (parts[0] == "1" && parts.size() > 1) ? parts[1].toInt() : parts[0].toInt();

            if (majorVersion >= 11 && !foundVersions.contains(majorVersion)) {
              JavaPackageInfo info;
              info.packageName = QString("EclipseAdoptium.Temurin.%1.JDK").arg(majorVersion);
              info.majorVersion = majorVersion;

              QString javacExePath = basePath + "\\" + entry + "\\bin\\javac.exe";
              bool hasJavac = QFile::exists(javacExePath);

              info.displayName = QString("Java %1 %2 (Found)").arg(majorVersion).arg(hasJavac ? "JDK" : "JRE");
              info.isInstalled = true;
              info.installedVersion = version;

              versionMap.insert(majorVersion, info);
              foundVersions.insert(majorVersion);

              qDebug() << "Added Java" << majorVersion << "from:" << basePath + "\\" << entry;
            }
          }
        }
      }
    }
  }

  if (versionMap.size() < 3) {
    qDebug() << "Checking Windows Registry for additional Java installations...";

    QStringList registryPaths = {"HKEY_LOCAL_MACHINE\\SOFTWARE\\JavaSoft\\Java Development Kit", "HKEY_LOCAL_MACHINE\\SOFTWARE\\Eclipse Adoptium\\JDK",
                                 "HKEY_LOCAL_MACHINE\\SOFTWARE\\AdoptOpenJDK\\JDK", "HKEY_CURRENT_USER\\SOFTWARE\\JavaSoft\\Java Development Kit"};

    for (const QString &regPath : registryPaths) {
      QProcessResult regResult = Utils::executeProcess("reg", {"query", regPath, "/s"}, 5000);

      if (regResult.exitCode == 0) {
        QString output = regResult.stdOut;
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);

        QString currentVersion;
        QString javaHome;

        for (const QString &line : lines) {
          if (line.contains("CurrentVersion")) {
            QRegularExpression re("CurrentVersion\\s+REG_SZ\\s+(.+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
              currentVersion = match.captured(1).trimmed();
            }
          } else if (line.contains("JavaHome") && !currentVersion.isEmpty()) {
            QRegularExpression re("JavaHome\\s+REG_SZ\\s+(.+)");
            QRegularExpressionMatch match = re.match(line);
            if (match.hasMatch()) {
              javaHome = match.captured(1).trimmed();

              QStringList versionParts = currentVersion.split('.');
              if (!versionParts.isEmpty()) {
                int majorVersion = versionParts[0].toInt();

                if (majorVersion >= 11 && !foundVersions.contains(majorVersion)) {
                  JavaPackageInfo info;
                  info.packageName = QString("Java-%1-Registry").arg(majorVersion);
                  info.majorVersion = majorVersion;
                  info.displayName = QString("Java %1 (Registry)").arg(majorVersion);
                  info.isInstalled = true;
                  info.installedVersion = currentVersion;

                  versionMap.insert(majorVersion, info);
                  foundVersions.insert(majorVersion);
                  qDebug() << "Found Java in registry:" << info.displayName;
                }
              }
            }
          }
        }
      }
    }
  }

  qDebug() << "Querying winget for available Java packages...";

  QProcessResult wingetResult = Utils::executeProcess("winget", {"search", "Temurin", "--source", "winget", "--accept-source-agreements"}, 15000);

  if (wingetResult.exitCode == 0) {
    QString output = wingetResult.stdOut;
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);

    QRegularExpression packageRe("EclipseAdoptium\\.Temurin\\.(\\d+)\\.JDK");

    for (const QString &line : lines) {
      QRegularExpressionMatch match = packageRe.match(line);
      if (match.hasMatch()) {
        int version = match.captured(1).toInt();

        if (!foundVersions.contains(version) && version >= 11) {
          JavaPackageInfo info;
          info.packageName = QString("EclipseAdoptium.Temurin.%1.JDK").arg(version);
          info.majorVersion = version;
          info.displayName = QString("Java %1").arg(version);
          info.isInstalled = false;

          versionMap.insert(version, info);
          foundVersions.insert(version);
          qDebug() << "Found available package in winget:" << info.packageName;
        }
      }
    }
  } else {
    qWarning() << "Failed to query winget, using fallback versions";
    QSet<int> fallbackVersions = {25, 21, 17, 11};
    for (int version : fallbackVersions) {
      if (!foundVersions.contains(version)) {
        JavaPackageInfo info;
        info.packageName = QString("EclipseAdoptium.Temurin.%1.JDK").arg(version);
        info.majorVersion = version;
        info.displayName = QString("Java %1").arg(version);
        info.isInstalled = false;

        versionMap.insert(version, info);
      }
    }
  }

  for (auto it = versionMap.begin(); it != versionMap.end(); ++it) {
    packages.append(it.value());
  }

  std::sort(packages.begin(), packages.end(), [](const JavaPackageInfo &a, const JavaPackageInfo &b) { return a.majorVersion > b.majorVersion; });

  qDebug() << "Total Java packages found:" << packages.size();

  for (const JavaPackageInfo &info : packages) {
    qDebug() << "  -" << info.displayName << "v" << info.majorVersion << "Installed:" << info.isInstalled << "Version:" << info.installedVersion;
  }

  return packages;
}

QString JavaUtils::getCurrentDefaultJava() {
  QProcessResult javaResult = Utils::executeProcess("java", {"-version"}, 3000);
  QString output = javaResult.stdOut + javaResult.stdErr;

  QRegularExpression re("(?:java|openjdk)\\s+version\\s+[\"']?([^\"'\\s]+)");
  QRegularExpressionMatch match = re.match(output);

  if (match.hasMatch()) {
    QString version = match.captured(1);
    QStringList parts = version.split('.');
    if (parts[0] == "1" && parts.size() > 1) {
      return parts[1];
    } else {
      return parts[0];
    }
  }

  return "";
}

bool JavaUtils::setJavaDefault(const QString &packageName, PkgManagerType type) {
  QString osName = QSysInfo::productType().toLower();
  qDebug() << "setJavaDefault called for package:" << packageName << "on OS:" << osName;

#ifdef Q_OS_LINUX
  QString targetJavaPath;
  QString version;

  if (type == PkgManagerType::Dnf) {
    QRegularExpression re("java-(\\d+)-openjdk-devel");
    QRegularExpressionMatch match = re.match(packageName);
    if (match.hasMatch()) {
      version = match.captured(1);
    }
  } else if (type == PkgManagerType::Apt) {
    QRegularExpression re("openjdk-(\\d+)-jdk");
    QRegularExpressionMatch match = re.match(packageName);
    if (match.hasMatch()) {
      version = match.captured(1);
    }
  } else if (type == PkgManagerType::Pacman) {
    QRegularExpression re1("java-(\\d+)-openjdk");
    QRegularExpression re2("jdk(\\d+)-openjdk");
    QRegularExpressionMatch match1 = re1.match(packageName);
    QRegularExpressionMatch match2 = re2.match(packageName);

    if (match1.hasMatch()) {
      version = match1.captured(1);
    } else if (match2.hasMatch()) {
      version = match2.captured(1);
    } else if (packageName == "jdk-openjdk") {
      QProcessResult infoResult = Utils::executeProcess("pacman", {"-Si", packageName}, 2000);
      if (infoResult.exitCode == 0) {
        QString info = infoResult.stdOut;
        QRegularExpression versionRe("Version\\s*:\\s*([0-9.]+)");
        QRegularExpressionMatch versionMatch = versionRe.match(info);
        if (versionMatch.hasMatch()) {
          QString versionStr = versionMatch.captured(1);
          QRegularExpression majorRe("^(\\d+)");
          QRegularExpressionMatch majorMatch = majorRe.match(versionStr);
          if (majorMatch.hasMatch()) {
            version = majorMatch.captured(1);
          } else {
            version = "latest";
          }
        } else {
          version = "latest";
        }
      } else {
        version = "latest";
      }
    }
  }

  if (version.isEmpty()) {
    qWarning() << "Could not extract version from package:" << packageName;
    return false;
  }

  if (!version.isEmpty()) {
    QDir jvmDir("/usr/lib/jvm");
    if (jvmDir.exists()) {
      QStringList entries = jvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      QStringList searchPatterns;

      if (type == PkgManagerType::Dnf) {
        searchPatterns << QString("java-%1-openjdk").arg(version) << QString("*%1*").arg(version);
      } else if (type == PkgManagerType::Apt) {
        searchPatterns << QString("java-%1-openjdk").arg(version) << QString("openjdk-%1").arg(version) << QString("*%1*").arg(version);
      } else if (type == PkgManagerType::Pacman) {
        searchPatterns << QString("java-%1-openjdk").arg(version) << QString("jdk-%1-openjdk").arg(version) << QString("*%1*").arg(version);
      } else {
        searchPatterns << QString("*%1*").arg(version);
      }

      for (const QString &entry : entries) {
        for (const QString &pattern : searchPatterns) {
          QString modifiedPattern = pattern;
          modifiedPattern.replace("*", ".*");
          QRegularExpression regex("^" + modifiedPattern + "$", QRegularExpression::CaseInsensitiveOption);

          if (regex.match(entry).hasMatch()) {
            QString javaPath = "/usr/lib/jvm/" + entry + "/bin/java";
            if (QFile::exists(javaPath)) {
              targetJavaPath = javaPath;
              qDebug() << "Found Java" << version << "at:" << targetJavaPath;
              break;
            }
          }
        }
        if (!targetJavaPath.isEmpty())
          break;
      }
    }

    if (targetJavaPath.isEmpty() && type != PkgManagerType::Pacman) {
      QProcessResult altResult = Utils::executeProcess("update-alternatives", {"--list", "java"}, 3000);
      if (altResult.exitCode == 0) {
        QString alternatives = altResult.stdOut;
        QStringList paths = alternatives.split('\n', Qt::SkipEmptyParts);

        for (const QString &path : paths) {
          if (path.contains(version)) {
            targetJavaPath = path;
            qDebug() << "Found Java in alternatives:" << targetJavaPath;
            break;
          }
        }
      }
    }

    if (targetJavaPath.isEmpty() && type == PkgManagerType::Pacman) {
      QStringList defaultPaths = {"/usr/lib/jvm/default-runtime/bin/java", "/usr/lib/jvm/default/bin/java", "/usr/lib/jvm/default-runtime"};

      for (const QString &path : defaultPaths) {
        if (QFile::exists(path)) {
          targetJavaPath = path;
          qDebug() << "Found Java in default symlink:" << targetJavaPath;
          break;
        }
      }
    }
  }

  if (targetJavaPath.isEmpty()) {
    qWarning() << "Could not find Java binary for version" << version;
    return false;
  }

  qDebug() << "Setting Java default to:" << targetJavaPath;

  if (type == PkgManagerType::Pacman) {
    QProcessResult whichResult = Utils::executeProcess("which", {"archlinux-java"}, 1000);
    if (whichResult.exitCode != 0) {
      qWarning() << "archlinux-java not found on Arch";
      return false;
    }

    QString javaEnv;

    if (version == "latest") {
      QProcessResult statusResult = Utils::executeProcess("archlinux-java", {"status"}, 2000);
      if (statusResult.exitCode == 0) {
        QString status = statusResult.stdOut;
        QStringList lines = status.split('\n', Qt::SkipEmptyParts);

        int highestVersion = 0;
        for (const QString &line : lines) {
          if (line.startsWith("java-")) {
            QString env = line.split(' ').first().trimmed();
            QRegularExpression re("java-(\\d+)-openjdk");
            QRegularExpressionMatch match = re.match(env);
            if (match.hasMatch()) {
              int ver = match.captured(1).toInt();
              if (ver > highestVersion) {
                highestVersion = ver;
                javaEnv = env;
              }
            }
          }
        }
      }
    } else if (!version.isEmpty()) {
      javaEnv = QString("java-%1-openjdk").arg(version);
    }

    if (javaEnv.isEmpty()) {
      qWarning() << "Could not determine Java environment for:" << packageName;
      return false;
    }

    qDebug() << "Setting Java via archlinux-java:" << javaEnv;
    QProcessResult setResult = Utils::executeProcess("sudo", {"archlinux-java", "set", javaEnv}, 10000);

    if (setResult.exitCode == 0) {
      qDebug() << "Successfully set Java via archlinux-java";
      return true;
    } else {
      qWarning() << "archlinux-java failed:" << setResult.stdErr;
      return false;
    }
  }

  bool needsRegistration = true;
  QProcessResult listResult = Utils::executeProcess("update-alternatives", {"--list", "java"}, 3000);
  if (listResult.exitCode == 0) {
    QString alternatives = listResult.stdOut;
    if (alternatives.contains(targetJavaPath)) {
      needsRegistration = false;
      qDebug() << "Java already registered as alternative";
    }
  }

  QString command;
  if (needsRegistration) {
    command = QString("update-alternatives --install /usr/bin/java java %1 %2 && "
                      "update-alternatives --set java %1")
                  .arg(targetJavaPath)
                  .arg(version.toInt() * 100);
  } else {
    command = QString("update-alternatives --set java %1").arg(targetJavaPath);
  }

  qDebug() << "Running command:" << command;
  QProcessResult pkexecResult = Utils::executeProcess("pkexec", {"sh", "-c", command}, 15000);

  if (pkexecResult.exitCode == 0) {
    qDebug() << "Successfully set Java default on Linux";
    return true;
  } else {
    qWarning() << "Failed to set Java default on Linux, exit code:" << pkexecResult.exitCode;
    qDebug() << "Error:" << pkexecResult.stdErr;
    return false;
  }

#elif defined(Q_OS_MAC)

  QString version;
  QRegularExpression re("temurin[^\\d]*(\\d+)", QRegularExpression::CaseInsensitiveOption);
  QRegularExpressionMatch match = re.match(packageName);

  if (match.hasMatch()) {
    version = match.captured(1);
  } else {

    re.setPattern("(\\d+)");
    match = re.match(packageName);
    if (match.hasMatch()) {
      version = match.captured(1);
    }
  }

  if (version.isEmpty()) {
    qWarning() << "Could not extract version from package name:" << packageName;
    return false;
  }

  qDebug() << "Setting Java" << version << "as default on macOS";

  QString javaHome;

  QProcessResult javaHomeResult = Utils::executeProcess("/usr/libexec/java_home", {"-v", version}, 2000);
  if (javaHomeResult.exitCode == 0) {
    javaHome = javaHomeResult.stdOut.trimmed();
  } else {

    javaHomeResult = Utils::executeProcess("/usr/libexec/java_home", {"-v", QString("1.%1").arg(version)}, 2000);
    if (javaHomeResult.exitCode == 0) {
      javaHome = javaHomeResult.stdOut.trimmed();
    }
  }

  if (javaHome.isEmpty()) {
    qWarning() << "Could not find Java" << version << "installation";
    return false;
  }

  qDebug() << "Found Java at:" << javaHome;

  QString zshrcPath = QDir::homePath() + "/.zshrc";
  QFile zshrcFile(zshrcPath);

  if (zshrcFile.open(QIODevice::ReadWrite | QIODevice::Text)) {
    QString content = zshrcFile.readAll();

    content.remove(QRegularExpression("^\\s*export\\s+JAVA_HOME\\s*=.*$", QRegularExpression::MultilineOption));
    content.remove(QRegularExpression("^\\s*export\\s+PATH\\s*=\\$JAVA_HOME/bin:.*$", QRegularExpression::MultilineOption));

    QString newSetting = QString("\n# Java %1 (set by Annode Wizard)\n").arg(version) + QString("export JAVA_HOME=\"%1\"\n").arg(javaHome) +
                         QString("export PATH=\"$JAVA_HOME/bin:$PATH\"\n");

    content += newSetting;

    zshrcFile.resize(0);
    zshrcFile.write(content.toUtf8());
    zshrcFile.close();

    qDebug() << "Updated ~/.zshrc with JAVA_HOME";
  }

  qputenv("JAVA_HOME", javaHome.toUtf8());

  QString currentPath = qgetenv("PATH");
  QString javaBinPath = javaHome + "/bin";
  if (!currentPath.contains(javaBinPath)) {
    QString newPath = QString("%1:%2").arg(javaBinPath).arg(currentPath);
    qputenv("PATH", newPath.toUtf8());
  }

  QProcessResult whichJava = Utils::executeProcess("which", {"java"}, 1000);
  qDebug() << "Java executable found at:" << whichJava.stdOut.trimmed();

  QProcessResult javaVersion = Utils::executeProcess("java", {"-version"}, 2000);
  qDebug() << "Java version output:" << javaVersion.stdErr;

  return true;
#elif defined(Q_OS_WIN)
  QString version;
  QRegularExpression re("Temurin\\.(\\d+)");
  QRegularExpressionMatch match = re.match(packageName);
  if (match.hasMatch()) {
    version = match.captured(1);
  }

  if (version.isEmpty()) {
    qWarning() << "Could not extract version from package name:" << packageName;
    return false;
  }

  qDebug() << "Looking for Java" << version << "installation";

  QString javaHome;
  QStringList searchPaths = {"C:\\Program Files\\Eclipse Adoptium", "C:\\Program Files\\AdoptOpenJDK", "C:\\Program Files\\Java", "C:\\Program Files\\Microsoft\\jdk"};

  for (const QString &basePath : searchPaths) {
    QDir dir(basePath);
    if (dir.exists()) {
      QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

      for (const QString &entry : entries) {
        if (entry.contains(QString("jdk-%1.").arg(version)) || entry.contains(QString("java-%1.").arg(version))) {

          QString candidatePath = dir.absoluteFilePath(entry);
          QString javaExePath = candidatePath + "\\bin\\java.exe";

          if (QFile::exists(javaExePath)) {
            javaHome = candidatePath;
            qDebug() << "Found Java" << version << "at:" << javaHome;
            break;
          }
        }
      }
      if (!javaHome.isEmpty())
        break;
    }
  }

  if (javaHome.isEmpty()) {
    qWarning() << "Could not find Java" << version << "installation";
    return false;
  }

  QString javaBinPath = javaHome + "\\bin";

  QString currentPath = qgetenv("PATH");
  qDebug() << "Current PATH from environment:" << currentPath;

  QStringList pathParts = currentPath.split(';', Qt::SkipEmptyParts);
  QStringList cleanParts;

  for (QString &part : pathParts) {

    part.replace(QRegularExpression("\\\\+"), "\\");

    part.replace("/", "\\");

    if (part.endsWith("\\") && part.length() > 3 && !part.endsWith(":\\")) {
      part.chop(1);
    }
  }

  for (const QString &part : pathParts) {
    if (part.isEmpty())
      continue;

    QString normalizedPart = part.toLower();

    bool isJavaPath = normalizedPart.contains("adoptopenjdk") || normalizedPart.contains("adoptium") || normalizedPart.contains("eclipse adoptium") ||
                      (normalizedPart.contains("java") && normalizedPart.contains("jdk")) || normalizedPart.contains("microsoft\\jdk") ||
                      (normalizedPart.contains("\\java") && normalizedPart.endsWith("\\bin"));

    bool isOurJavaPath = QDir::cleanPath(part).compare(QDir::cleanPath(javaBinPath), Qt::CaseInsensitive) == 0;

    if (!isJavaPath || isOurJavaPath) {
      cleanParts.append(part);
    } else {
      qDebug() << "Removing Java path from PATH:" << part;
    }
  }

  if (!cleanParts.contains(javaBinPath, Qt::CaseInsensitive)) {
    cleanParts.prepend(javaBinPath);
    qDebug() << "Added Java bin to PATH:" << javaBinPath;
  }

  QString newPath = cleanParts.join(";");

  newPath.replace(QRegularExpression(";+"), ";");
  newPath.replace(QRegularExpression("\\\\+"), "\\");

  qDebug() << "Cleaned new PATH:" << newPath;
  qDebug() << "New PATH length:" << newPath.length();

  QProcessResult setxResult = Utils::executeProcess("setx", {"PATH", newPath, "/M"}, 5000);
  if (setxResult.exitCode != 0) {
    qWarning() << "setx failed for system PATH, trying user PATH...";
    setxResult = Utils::executeProcess("setx", {"PATH", newPath}, 5000);
  }

  QProcessResult javaHomeResult = Utils::executeProcess("setx", {"JAVA_HOME", javaHome, "/M"}, 5000);
  if (javaHomeResult.exitCode != 0) {
    javaHomeResult = Utils::executeProcess("setx", {"JAVA_HOME", javaHome}, 5000);
  }

  QString escapedPathForReg = newPath;
  escapedPathForReg.replace("\\", "\\\\");
  QString escapedJavaHomeForReg = javaHome;
  escapedJavaHomeForReg.replace("\\", "\\\\");

  QString regPathCmd = QString("reg add \"HKCU\\Environment\" /v \"PATH\" /t REG_EXPAND_SZ /d "
                               "\"%1\" /f && "
                               "reg add \"HKCU\\Environment\" /v \"JAVA_HOME\" /t REG_EXPAND_SZ "
                               "/d \"%2\" /f")
                           .arg(escapedPathForReg, escapedJavaHomeForReg);

  qDebug() << "Registry command (with escapes):" << regPathCmd;

  QProcessResult regResult = Utils::executeProcess("cmd", {"/c", regPathCmd}, 5000);

  qputenv("JAVA_HOME", javaHome.toUtf8());
  qputenv("PATH", newPath.toUtf8());

  QProcessResult psResult = Utils::executeProcess("powershell",
                                                  {"-Command", "[System.Environment]::SetEnvironmentVariable('PATH', $env:PATH, 'User'); "
                                                               "[System.Environment]::SetEnvironmentVariable('JAVA_HOME', $env:JAVA_HOME, 'User'); "
                                                               "Write-Output 'Environment variables updated'"},
                                                  3000);

  QProcessResult verifyResult = Utils::executeProcess("java", {"-version"}, 2000);
  QString verifyOutput = verifyResult.stdErr;
  qDebug() << "Immediate verification output:" << verifyOutput;

  QRegularExpression verifyRe("version \"([^\"]+)\"");
  QRegularExpressionMatch verifyMatch = verifyRe.match(verifyOutput);

  if (verifyMatch.hasMatch()) {
    QString actualVersion = verifyMatch.captured(1);
    QStringList parts = actualVersion.split('.');
    QString actualMajor = (parts[0] == "1" && parts.size() > 1) ? parts[1] : parts[0];

    if (actualMajor == version) {
      qDebug() << "SUCCESS: Java" << version << "is now default";
      return true;
    } else {
      qWarning() << "FAILED: Java version is" << actualMajor << "but expected" << version;

      QProcessResult whereResult = Utils::executeProcess("where", {"java.exe"}, 2000);
      if (whereResult.exitCode == 0) {
        qDebug() << "WHERE java.exe finds:" << whereResult.stdOut;
      }

      return false;
    }
  }

  qDebug() << "Environment variables set, but need new shell to take effect";
  return true;
#else
  qWarning() << "Unsupported OS for setJavaDefault:" << osName;
  return false;
#endif
}

bool JavaUtils::tryAutoSelectJava(const QString &version) {
  QProcessResult queryResult = Utils::executeProcess("update-alternatives", {"--query", "java"}, 3000);
  if (queryResult.exitCode != 0) {
    return false;
  }

  QString output = queryResult.stdOut;
  QStringList lines = output.split('\n', Qt::SkipEmptyParts);
  QMap<QString, QString> alternatives;
  QString currentPath;

  for (const QString &line : lines) {
    if (line.startsWith("Value:")) {
      currentPath = line.mid(7).trimmed();
    } else if (line.startsWith("Alternative:")) {
      QString path = line.mid(13).trimmed();
      QString name = QFileInfo(path).fileName();
      alternatives[name] = path;
    }
  }

  for (auto it = alternatives.begin(); it != alternatives.end(); ++it) {
    QString path = it.value();
    if (!version.isEmpty() && path.contains(version)) {
      QProcessResult setResult = Utils::executeProcess("update-alternatives", {"--set", "java", path}, 3000);
      return setResult.exitCode == 0;
    }
  }

  if (!alternatives.isEmpty()) {
    QString firstPath = alternatives.first();
    QProcessResult setResult = Utils::executeProcess("update-alternatives", {"--set", "java", firstPath}, 3000);
    return setResult.exitCode == 0;
  }

  return false;
}

JavaCheckResult JavaUtils::checkSystemJava() {
  JavaCheckResult result;
  result.isJavaAvailable = false;
  result.isCompleteJDK = false;
  result.version = "";
  result.majorVersion = 0;
  result.javaPath = "";
  result.javacPath = "";

#ifdef Q_OS_WIN
  QProcessResult whereJavaResult = Utils::executeProcess("where", {"java.exe"}, 1000);
  if (whereJavaResult.exitCode == 0) {
    result.javaPath = whereJavaResult.stdOut.split('\n').first().trimmed();
    QProcessResult javaVersionResult = Utils::executeProcess(result.javaPath, {"-version"}, 2000);
    QString output = javaVersionResult.stdErr;

    QRegularExpression re("version \"([^\"]+)\"");
    QRegularExpressionMatch match = re.match(output);

    if (match.hasMatch()) {
      result.isJavaAvailable = true;
      result.version = match.captured(1);

      if (result.version.startsWith("1.")) {
        result.majorVersion = result.version.mid(2).split('.').first().toInt();
      } else {
        result.majorVersion = result.version.split('.').first().toInt();
      }
    }
  }

  QProcessResult whereJavacResult = Utils::executeProcess("where", {"javac.exe"}, 1000);
  if (whereJavacResult.exitCode == 0) {
    result.javacPath = whereJavacResult.stdOut.split('\n').first().trimmed();
    result.isCompleteJDK = true;
  }

#else
  QProcessResult whichJavaResult = Utils::executeProcess("which", {"java"}, 1000);
  if (whichJavaResult.exitCode == 0) {
    result.javaPath = whichJavaResult.stdOut.trimmed();
    QProcessResult javaVersionResult = Utils::executeProcess("java", {"-version"}, 2000);
    QString output = javaVersionResult.stdOut + javaVersionResult.stdErr;

    QRegularExpression re("(?:java|openjdk)\\s+version\\s+[\"']?([^\"'\\s]+)");
    QRegularExpressionMatch match = re.match(output);

    if (match.hasMatch()) {
      result.isJavaAvailable = true;
      result.version = match.captured(1);

      if (result.version.startsWith("1.")) {
        result.majorVersion = result.version.mid(2).split('.').first().toInt();
      } else {
        result.majorVersion = result.version.split('.').first().toInt();
      }
    }
  }

  QProcessResult whichJavacResult = Utils::executeProcess("which", {"javac"}, 1000);
  if (whichJavacResult.exitCode == 0) {
    result.javacPath = whichJavacResult.stdOut.trimmed();
    result.isCompleteJDK = true;
  } else {
#ifdef Q_OS_LINUX
    QDir jvmDir("/usr/lib/jvm");
    if (jvmDir.exists()) {
      QStringList jdkDirs = jvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
      for (const QString &dir : jdkDirs) {
        QString javacPath = "/usr/lib/jvm/" + dir + "/bin/javac";
        if (QFile::exists(javacPath)) {
          result.javacPath = javacPath;
          result.isCompleteJDK = true;
          break;
        }
      }
    }
#endif
  }
#endif
  return result;
}

bool JavaUtils::verifyJDKInstallation(const QString &packageName, PkgManagerType type) {
  if (type == PkgManagerType::Dnf || type == PkgManagerType::Pacman) {
    return true;
  }

  JavaCheckResult systemJava = checkSystemJava();
  if (systemJava.isCompleteJDK) {
    QRegularExpression re("(\\d+)");
    QRegularExpressionMatch match = re.match(packageName);
    if (match.hasMatch()) {
      int packageVersion = match.captured(1).toInt();
      return systemJava.majorVersion >= packageVersion;
    }
    return true;
  }

  return false;
}
