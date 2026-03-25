#include "osinfo.h"
#include "systemutils.h"
#include "utils.h"
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTextStream>

QString pkgManagerTypeToString(PkgManagerType type) {
  switch (type) {
  case PkgManagerType::Dnf:
    return "dnf";
  case PkgManagerType::Apt:
    return "apt";
  case PkgManagerType::Pacman:
    return "pacman";
  case PkgManagerType::Winget:
    return "winget";
  case PkgManagerType::Homebrew:
    return "brew";
  case PkgManagerType::Unknown:
  default:
    return "unknown";
  }
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
void setupMacOSEnvironment() {

  qDebug() << "=== Setting up macOS environment ===";

  QString originalPath = qgetenv("PATH");
  qDebug() << "Original PATH:" << originalPath;

  QProcess shellProc;
  shellProc.start("bash", {"-l", "-c", "echo $PATH"});
  shellProc.waitForFinished(3000);
  QString userPath = QString::fromLocal8Bit(shellProc.readAllStandardOutput()).trimmed();

  if (!userPath.isEmpty()) {
    qDebug() << "User's shell PATH:" << userPath;

    QStringList currentPaths = originalPath.split(":");
    QStringList userPaths = userPath.split(":");
    QStringList newPaths = userPaths;

    for (const QString &path : currentPaths) {
      if (!newPaths.contains(path) && !path.isEmpty()) {
        newPaths.append(path);
      }
    }

    QString newPath = newPaths.join(":");
    qputenv("PATH", newPath.toUtf8());
    qDebug() << "New PATH:" << newPath;
  }

  shellProc.start("bash", {"-l", "-c", "echo $HOME"});
  shellProc.waitForFinished(1000);
  QString userHome = QString::fromLocal8Bit(shellProc.readAllStandardOutput()).trimmed();
  if (!userHome.isEmpty() && qgetenv("HOME") != userHome) {
    qputenv("HOME", userHome.toUtf8());
    qDebug() << "Set HOME to:" << userHome;
  }

  shellProc.start("bash", {"-l", "-c", "echo $HOMEBREW_PREFIX"});
  shellProc.waitForFinished(1000);
  QString brewPrefix = QString::fromLocal8Bit(shellProc.readAllStandardOutput()).trimmed();
  if (!brewPrefix.isEmpty()) {
    qputenv("HOMEBREW_PREFIX", brewPrefix.toUtf8());
    qDebug() << "Set HOMEBREW_PREFIX to:" << brewPrefix;
  }

  qDebug() << "=== macOS environment setup complete ===";
}
#endif
OSInfo detectOSInfo() {
  qDebug() << "=== ENTERING detectOSInfo ===";
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  setupMacOSEnvironment();
#endif
  OSInfo info;
  info.osType = QSysInfo::productType();
  QString kernel = QSysInfo::kernelType();
  info.isLinux = (kernel == "linux");
  QString cpuArch = QSysInfo::currentCpuArchitecture();
  info.arch = (cpuArch == "x86_64") ? "x64" : "aarch64";
  info.pkgType = PkgManagerType::Unknown;
  info.ptype = "unknown";
  info.codename = "unknown";
  info.pkgManagerAvailable = false;
  info.pkgVersion = "";


  qDebug() << "Raw OS info: productType=" << info.osType << ", kernelType=" << kernel << ", cpuArch=" << cpuArch;

  if (info.osType.contains("macos", Qt::CaseInsensitive) || info.osType.contains("osx", Qt::CaseInsensitive)) {
    qDebug() << "Detected macOS, checking for Homebrew...";

    info.pkgType = PkgManagerType::Homebrew;
    info.ptype = "brew";

    QString brewPath = SystemUtils::findBrew();
    if (!brewPath.isEmpty()) {
      info.pkgManagerAvailable = true;
      QProcessResult brewResult = Utils::executeProcess(brewPath, {"--version"});
      if (brewResult.QPSuccess && brewResult.exitCode == 0) {
        QString output = brewResult.stdOut;
        QRegularExpression re("Homebrew\\s+([0-9.]+)");
        QRegularExpressionMatch match = re.match(output);
        if (match.hasMatch()) {
          info.pkgVersion = match.captured(1);
        }
      }
      qDebug() << "Detected Homebrew package manager at:" << brewPath;
    } else {
      info.pkgManagerAvailable = false;
      qDebug() << "Homebrew not found";
    }
  }

  else if (info.osType.contains("windows", Qt::CaseInsensitive)) {
    qDebug() << "Detected Windows, checking for Winget...";
    info.pkgType = PkgManagerType::Winget;
    info.ptype = "winget";
    QProcessResult whereResult = Utils::executeProcess("where", {"winget"});
    if (whereResult.QPSuccess && whereResult.exitCode == 0) {
      info.pkgManagerAvailable = true;
      
      QProcessResult wingetResult = Utils::executeProcess("winget", {"--version"});
      if (wingetResult.QPSuccess && wingetResult.exitCode == 0) {
        info.pkgVersion = wingetResult.stdOut.trimmed();
      }
      qDebug() << "Detected Winget package manager.";
    }  else {
      info.pkgManagerAvailable = false;
      qDebug() << "Winget not found";
    }
  }

  else if (info.isLinux) {
    qDebug() << "Kernel is linux, detecting package manager...";

    QProcessResult dnfResult = Utils::executeProcess("which", {"dnf"});
    if (dnfResult.QPSuccess && dnfResult.exitCode == 0) {
      info.pkgManagerAvailable = true;
      info.pkgType = PkgManagerType::Dnf;
      info.ptype = "dnf";
      qDebug() << "Detected DNF package manager.";
    }

    else {
      QProcessResult aptResult = Utils::executeProcess("which", {"apt"});
      if (aptResult.QPSuccess && aptResult.exitCode == 0) {
        info.pkgManagerAvailable = true;
        info.pkgType = PkgManagerType::Apt;
        info.ptype = "apt";
        qDebug() << "Detected APT package manager.";

        QFile file("/etc/os-release");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
          QTextStream in(&file);
          while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.startsWith("VERSION_CODENAME=")) {
              QString val = line.mid(17).trimmed();
              if (val.startsWith("\"") && val.endsWith("\"")) {
                val = val.mid(1, val.size() - 2);
              } else if (val.startsWith("'") && val.endsWith("'")) {
                val = val.mid(1, val.size() - 2);
              }
              info.codename = val;
              qDebug() << "Parsed VERSION_CODENAME:" << info.codename;
              break;
            }
          }
          file.close();
        }
      }

      else {
        QProcessResult pacmanResult = Utils::executeProcess("which", {"pacman"});
        if (pacmanResult.QPSuccess && pacmanResult.exitCode == 0) {
          info.pkgManagerAvailable = true;
          info.pkgType = PkgManagerType::Pacman;
          info.ptype = "pacman";
          qDebug() << "Detected PACMAN package manager.";
        } else {
          qDebug() << "No known package manager detected.";
        }
      }
    }

    if (info.pkgManagerAvailable) {
      QString versionCommand;

      switch (info.pkgType) {
      case PkgManagerType::Dnf:
        versionCommand = "dnf --version";
        break;
      case PkgManagerType::Apt:
        versionCommand = "apt --version";
        break;
      case PkgManagerType::Pacman:
        versionCommand = "pacman --version";
        break;
      default:
        break;
      }

      if (!versionCommand.isEmpty()) {
        QProcessResult versionResult = Utils::executeProcess("sh", {"-c", versionCommand});
        if (versionResult.QPSuccess && versionResult.exitCode == 0) {
          QString output = versionResult.stdOut;
          info.pkgVersion = output.split('\n').first().trimmed();
        }
      }
    }
  } else {
    qDebug() << "Non-Linux kernel, pkg manager remains unknown.";
  }

  qDebug() << "=== EXITING detectOSInfo ===";
  return info;
}

const OSInfo &detectedOSInfo() {
  static const OSInfo info = detectOSInfo();
  return info;
}
