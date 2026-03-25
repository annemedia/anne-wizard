#include "utils.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QVersionNumber>
#include <QWizard>

Wizard *Utils::m_wiz = nullptr;

Wizard *Utils::getWizard() {
  if (!m_wiz) {
    m_wiz = qobject_cast<Wizard *>(QApplication::activeWindow());
  }
  return m_wiz;
}

void Utils::setWizardButtons(bool next, bool back, bool cancel) {
  Wizard *wiz = getWizard();

  if (wiz) {
    wiz->button(QWizard::NextButton)->setEnabled(next);
    wiz->button(QWizard::BackButton)->setEnabled(back);
    wiz->button(QWizard::CancelButton)->setEnabled(cancel);
    if (next) {
      wiz->button(QWizard::NextButton)->setFocus();
    }
  }
}

QString Utils::getPlatformCommand(PkgManagerType type, const QString &package) {
  switch (type) {
  case PkgManagerType::Apt:
    return QString("apt update -qq && apt install -y %1").arg(package);
  case PkgManagerType::Dnf:
    return QString("dnf check-update -y && dnf install -y %1").arg(package);
  case PkgManagerType::Pacman:
    return QString("pacman -Syu --noconfirm %1").arg(package);
  default:
    return "";
  }
}

QString Utils::getPackageVersion(PkgManagerType type, const QString &, const QStringList &, const QString &pkgName) {
  if (type == PkgManagerType::Unknown)
    return QString();

  struct Query {
    QString cmd;
    QStringList args;
    std::function<QString(const QString &)> parser;
    int timeout;
  };

  QVector<Query> queries;

  switch (type) {
  case PkgManagerType::Apt:
    queries << Query{"dpkg-query", {"-W", "-f=${Version}", pkgName}, [](const QString &out) { return out.trimmed().split('-').first(); }, 1000}
            << Query{"apt-cache",
                     {"policy", pkgName},
                     [](const QString &out) {
                       QRegularExpression re(R"((?:Candidate|Installed):\s*([0-9][^\s\n]+))");
                       auto m = re.match(out);
                       return m.hasMatch() ? m.captured(1).split('-').first() : QString();
                     },
                     2000};
    break;

  case PkgManagerType::Dnf:
    queries << Query{"rpm",
                     {"-q", "--qf", "%{VERSION}", pkgName},
                     [](const QString &out) {
                       auto v = out.trimmed();
                       return (v.isEmpty() || v.contains("not installed")) ? QString() : v.split('-').first();
                     },
                     2000}
            << Query{"dnf",
                     {"list", "--quiet", pkgName},
                     [](const QString &out) {
                       QStringList lines = out.split('\n', Qt::SkipEmptyParts);
                       if (lines.size() > 1) {
                         QStringList parts = lines[1].split(' ', Qt::SkipEmptyParts);
                         if (parts.size() > 1) {
                           QString version = parts[1].split('-').first();
                           if (version.contains('.') && !version.contains("Error")) {
                             return version;
                           }
                         }
                       }
                       return QString();
                     },
                     5000}
            << Query{"dnf",
                     {"list", "--available", "--quiet", pkgName},
                     [pkgName](const QString &out) -> QString {
                       if (!out.trimmed().isEmpty() && out.contains(pkgName)) {
                         return "17.0.0";
                       }
                       return QString();
                     },
                     5000};
    break;

  case PkgManagerType::Pacman:
    queries << Query{"pacman",
                     {"-Qi", pkgName},
                     [](const QString &out) {
                       QRegularExpression re(R"(Version\s*:\s*([0-9][^\s\n]+))");
                       auto m = re.match(out);
                       return m.hasMatch() ? m.captured(1).split('-').first() : QString();
                     },
                     1000}
            << Query{"pacman",
                     {"-Si", pkgName},
                     [](const QString &out) {
                       QRegularExpression re(R"(Version\s*:\s*([0-9][^\s\n]+))");
                       auto m = re.match(out);
                       return m.hasMatch() ? m.captured(1).split('-').first() : QString();
                     },
                     2000};
    break;

  default:
    return QString();
  }

  for (const auto &q : queries) {
    qDebug() << "Running package check:" << q.cmd << q.args;

    QProcessResult result = Utils::executeProcess(q.cmd, q.args, q.timeout);
    qDebug() << "Command finished, exit code:" << result.exitCode;

    if (!result.stdOut.isEmpty())
      qDebug() << "Output:" << result.stdOut;
    if (!result.stdErr.isEmpty())
      qDebug() << "Error:" << result.stdErr;

    if (result.exitCode == 0) {
      QString parsedResult = q.parser(result.stdOut);
      if (!parsedResult.isEmpty()) {
        qDebug() << "Instant detection:" << pkgName << "→" << parsedResult << "via" << q.cmd;
        return parsedResult;
      }
    }
  }

  return QString();
}


void Utils::parseInstallOutput(const QString &data, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                               double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer, PkgManagerType currentPkgType) {
  static qint64 lastUpdateTime = 0;
  static int lastProgressValue = 0;
  static int consecutiveNoProgressLines = 0;
  static int simulatedProgressIncrement = 1;

  static qint64 lastDownloadedBytes = 0;
  static QTime lastSpeedUpdateTime;
  static QVector<double> speedSamples;

  if (data.isEmpty())
    return;

  QString cleanData = data;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QRegularExpression ansiEscape(R"(\x1B\[[0-9;]*[mK])");
  cleanData.remove(ansiEscape);
  qDebug() << "[DEBUG] macOS - removed ANSI codes";
#else
  cleanData.remove(QRegularExpression(R"(\x1B\[[0-9;]*[mK])"));
  cleanData.replace('\r', '\n');
  qDebug() << "[DEBUG] Linux/Windows - removed ANSI and replaced \\r";
#endif

  QStringList lines;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QStringList initialLines = cleanData.split('\n', Qt::SkipEmptyParts);

  for (const QString &line : initialLines) {
    if (line.contains('\r')) {
      QStringList parts = line.split('\r', Qt::SkipEmptyParts);
      if (!parts.isEmpty()) {
        QString lastPart = parts.last().trimmed();
        if (!lastPart.isEmpty()) {
          lines.append(lastPart);
        }
      }
    } else {
      QString trimmed = line.trimmed();
      if (!trimmed.isEmpty()) {
        lines.append(trimmed);
      }
    }
  }
#else
  lines = cleanData.split('\n', Qt::SkipEmptyParts);
#endif

  int currentProgress = bar->value();

  qint64 now = QDateTime::currentMSecsSinceEpoch();
  bool foundProgressIndicator = false;

  qDebug() << "[MAIN-DEBUG] ====== parseInstallOutput called ======";
  qDebug() << "[MAIN-DEBUG] Data length:" << data.length() << "Clean data length:" << cleanData.length();
  qDebug() << "[MAIN-DEBUG] Number of lines:" << lines.size();
  qDebug() << "[MAIN-DEBUG] Current PkgManagerType enum value:" << static_cast<int>(currentPkgType);
  qDebug() << "[MAIN-DEBUG] inDownloadPhase:" << inDownloadPhase;
  qDebug() << "[MAIN-DEBUG] Current progress bar value:" << currentProgress;
  
  for (int i = 0; i < lines.size(); i++) {
    QString line = lines[i].trimmed();
    if (line.isEmpty())
      continue;

    qDebug() << "[DEBUG] RAW line:" << line;

    if (line.length() == 1 && (line == "-" || line == "\\" || line == "|" || line == "/")) {
      continue;
    }

    if (!lastProcessedLine.isEmpty() && line == lastProcessedLine)
      continue;
    lastProcessedLine = line;

    qDebug() << "[MAIN-DEBUG] Processing line" << i << "of" << lines.size() << ":" << line.left(100);

    consecutiveNoProgressLines = 0;
    foundProgressIndicator = true;

    qDebug() << "[MAIN-DEBUG] Before switch, currentPkgType = " << static_cast<int>(currentPkgType);
    
    switch (currentPkgType) {
      case PkgManagerType::Homebrew:
        qDebug() << "[MAIN-DEBUG] Switching to Homebrew/Brew parser";
        processBrewOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                         downloadFileCount, completedFiles, totalDownloadBytes, 
                         lastProcessedLine, bar, progressTimer);
        continue;
        
      case PkgManagerType::Apt:
        qDebug() << "[MAIN-DEBUG] Switching to Apt parser";
        processAptOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                        downloadFileCount, completedFiles, totalDownloadBytes, 
                        lastProcessedLine, bar, progressTimer);
        continue;
        
      case PkgManagerType::Pacman:
        qDebug() << "[MAIN-DEBUG] Switching to Pacman parser";
        processPacmanOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                           downloadFileCount, completedFiles, totalDownloadBytes, 
                           lastProcessedLine, bar, progressTimer);
        continue;
        
      case PkgManagerType::Dnf:
        qDebug() << "[MAIN-DEBUG] Switching to Dnf parser - CALLING processDnfOutput";
        processDnfOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                        downloadFileCount, completedFiles, totalDownloadBytes, 
                        lastProcessedLine, bar, progressTimer);
        continue;
        
      case PkgManagerType::Winget:
        qDebug() << "[MAIN-DEBUG] Switching to Winget parser";
        processWingetOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                           downloadFileCount, completedFiles, totalDownloadBytes, 
                           lastProcessedLine, bar, progressTimer);
        continue;
        
      default:
        qDebug() << "[MAIN-DEBUG] Switching to Generic parser (default case)";
        processGenericOutput(line, inDownloadPhase, downloadProgress, downloadLineCount, 
                            downloadFileCount, completedFiles, totalDownloadBytes, 
                            lastProcessedLine, bar, progressTimer);
        continue;
    }
  }

  qDebug() << "[MAIN-DEBUG] Finished processing all lines. foundProgressIndicator:" << foundProgressIndicator;
  
  if (!foundProgressIndicator && inDownloadPhase) {
    consecutiveNoProgressLines++;

    if (consecutiveNoProgressLines > 5 && (now - lastUpdateTime) > 2000) {
      if (currentProgress < 70) {
        simulatedProgressIncrement = qMax(1, simulatedProgressIncrement);
        int target = qMin(currentProgress + simulatedProgressIncrement, 70);
        Utils::animateProgress(bar, target);
        lastUpdateTime = now;
        qDebug() << "Simulating progress during stall:" << target << "%";
      }
    }
  } else if (foundProgressIndicator) {
    consecutiveNoProgressLines = 0;
    simulatedProgressIncrement = 1;
    lastUpdateTime = now;
  }

  if (inDownloadPhase && progressTimer && progressTimer->isActive()) {
    qDebug() << "[MAIN-DEBUG] Timer is active for download phase";
  }
  
  qDebug() << "[MAIN-DEBUG] ====== parseInstallOutput exiting ======";
}

void Utils::processBrewOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                             int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                             double &totalDownloadBytes, QString &lastProcessedLine, 
                             QProgressBar *bar, QTimer *progressTimer) {
  static bool isFormulaInstallation = false;
  static int formulaDepCount = 0;
  static int formulaDepsInstalled = 0;
  static int totalExpectedDeps = 10;
  
  QString processedLine = line;
  
  if (line.contains('\r')) {
    QStringList parts = line.split('\r', Qt::SkipEmptyParts);
    if (!parts.isEmpty()) {
      processedLine = parts.last().trimmed();
    }
  }

  qDebug() << "[BREW-PROCESSED] Line:" << processedLine;

  // Formula installation detection
  if (processedLine.contains("[WRAPPER-INFO] Installing MariaDB via Homebrew...")) {
    isFormulaInstallation = true;
    formulaDepCount = 0;
    formulaDepsInstalled = 0;
    Utils::animateProgress(bar, 5);
    return;
  }
  
  if (isFormulaInstallation) {
    processBrewFormulaInstallation(processedLine, isFormulaInstallation, formulaDepCount, 
                                  formulaDepsInstalled, totalExpectedDeps, inDownloadPhase, 
                                  bar, progressTimer);
    return;
  }
  
  // Cask installation parsing
  if (processedLine.contains("==> Downloading")) {
    qDebug() << "[BREW] Download started";
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      if (progressTimer) {
        progressTimer->stop();
      }
      Utils::animateProgress(bar, 10);
    }
    return;
  }

  if (inDownloadPhase && processedLine.contains('M') && processedLine.contains('k')) {
    QStringList parts = processedLine.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    if (parts.size() >= 4) {
      bool ok;
      int percent = parts[0].toInt(&ok);

      if (ok && percent >= 0 && percent <= 100) {
        int progress = 10 + (percent * 60 / 100);
        int currentProgress = bar->value();

        if (progress > currentProgress && progress <= 70) {
          Utils::animateProgress(bar, progress);

          QString totalSize = parts[1];
          QString downloaded = parts[3];
          QString speed = parts.size() > 6 ? parts[6] : "";

          qDebug() << "[BREW] Progress:" << percent << "% (" << downloaded << "/" << totalSize << ") at" << speed << "-> overall:" << progress << "%";
        }
      }
    }
    return;
  }

  if (inDownloadPhase) {

    QRegularExpression re(R"(^\s*(\d+)\s*%)");
    QRegularExpressionMatch match = re.match(processedLine);

    if (match.hasMatch()) {
      int percent = match.captured(1).toInt();
      if (percent >= 0 && percent <= 100) {
        int progress = 10 + (percent * 60 / 100);
        int currentProgress = bar->value();

        if (progress > currentProgress && progress <= 70) {
          Utils::animateProgress(bar, progress);
          qDebug() << "[BREW] Progress from regex:" << percent << "% -> overall:" << progress << "%";
        }
      }
    }
    return;
  }

  if (processedLine.contains("==> Installing Cask") || processedLine.contains("==> Running installer")) {
    if (inDownloadPhase) {
      inDownloadPhase = false;
    }
    Utils::animateProgress(bar, 70);
    qDebug() << "[BREW] Installation phase";
    return;
  }

  if (processedLine.contains("🍺") || processedLine.contains("successfully installed")) {
    if (!isFormulaInstallation) {
      Utils::animateProgress(bar, 100);
      inDownloadPhase = false;
      if (progressTimer) {
        progressTimer->stop();
      }
      qDebug() << "[BREW] Complete!";
    }
    return;
  }
}

void Utils::processAptOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                            int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                            double &totalDownloadBytes, QString &lastProcessedLine, 
                            QProgressBar *bar, QTimer *progressTimer) {
  int currentProgress = bar->value();
  
  if (line.contains("Reading package lists") || line.contains("Building dependency tree") || 
      line.contains("Loading repository") || line.contains("Refreshing repository") || 
      line.contains("Synchronizing") || line.contains("Resolving dependencies") || 
      line.contains("Checking") || line.contains("Updating") || 
      line.contains("Hit:") || line.contains("Ign:") || 
      (line.contains("Get:") && line.contains("[") && !line.contains("%"))) {

    if (currentProgress < 20) {
      int target = qMin(currentProgress + 3, 20);
      Utils::animateProgress(bar, target);
    }
    return;
  }

  if ((line.contains("Need to get") || (line.contains("Get:") && line.contains("%")))) {
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      if (progressTimer)
        progressTimer->start(100);
      Utils::animateProgress(bar, 25);
      qDebug() << "[APT] Download phase started";
    }
    return;
  }

  if (inDownloadPhase && line.contains("Get:") && line.contains("%")) {
    QRegularExpression percentPattern(R"(Get:\d+\s+.*?(\d{1,3})%)");
    QRegularExpressionMatch match = percentPattern.match(line);
    
    if (match.hasMatch()) {
      int detectedPercent = match.captured(1).toInt();
      int progress = 25 + (detectedPercent * 65 / 100);
      
      if (progress > currentProgress) {
        Utils::animateProgress(bar, qMin(progress, 90));
        qDebug() << "[APT] Download progress:" << detectedPercent << "% -> overall:" << progress << "%";
      }
    }
    return;
  }

  if (line.contains("Fetched") || line.contains("Download complete") || 
      line.contains("All packages are already installed") || 
      line.contains("Nothing to do") || line.contains("No packages marked for update")) {

    inDownloadPhase = false;
    if (progressTimer)
      progressTimer->stop();
    Utils::animateProgress(bar, 75);
    qDebug() << "[APT] Download phase ended";
    return;
  }

  if (line.contains("Unpacking") || line.contains("Setting up") || 
      line.contains("Preparing") || line.contains("Configuring")) {

    if (currentProgress < 95) {
      int increment = line.contains("Setting up") || line.contains("Configuring") ? 2 : 5;
      int target = qMin(currentProgress + increment, 95);
      Utils::animateProgress(bar, target);
      qDebug() << "[APT] Installation activity -> progress:" << target << "%";
    }
    return;
  }

  if (line.contains("Processing triggers") || line.contains("Complete!") || 
      line.contains("Finished.") || line.contains("All done") || 
      line.contains("installation completed")) {

    Utils::animateProgress(bar, 100);
    inDownloadPhase = false;
    if (progressTimer)
      progressTimer->stop();
    qDebug() << "[APT] Installation complete -> 100%";
    return;
  }
}

void Utils::processPacmanOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                               int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                               double &totalDownloadBytes, QString &lastProcessedLine, 
                               QProgressBar *bar, QTimer *progressTimer) {
  int currentProgress = bar->value();
  
  if (line.contains("Checking") || line.contains("Loading") || 
      line.contains("Resolving") || line.contains("Synchronizing")) {

    if (currentProgress < 20) {
      int target = qMin(currentProgress + 3, 20);
      Utils::animateProgress(bar, target);
    }
    return;
  }

  if (line.contains("downloading") || (line.contains("Downloading") && !inDownloadPhase)) {
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      if (progressTimer)
        progressTimer->start(100);
      Utils::animateProgress(bar, 25);
      qDebug() << "[PACMAN] Download phase started";
    }
    return;
  }

  if (inDownloadPhase) {
    // Pacman uses percentage in brackets like [12%]
    QRegularExpression percentPattern(R"(\[(\d{1,3})%\])");
    QRegularExpressionMatch match = percentPattern.match(line);
    
    if (match.hasMatch()) {
      int detectedPercent = match.captured(1).toInt();
      int progress = 25 + (detectedPercent * 65 / 100);
      
      if (progress > currentProgress) {
        Utils::animateProgress(bar, qMin(progress, 90));
        qDebug() << "[PACMAN] Download progress:" << detectedPercent << "% -> overall:" << progress << "%";
      }
    }
    return;
  }

  if (line.contains("checking") || line.contains("installing") || 
      line.contains("upgrading") || line.contains("removing")) {

    if (currentProgress < 95) {
      int target = qMin(currentProgress + 5, 95);
      Utils::animateProgress(bar, target);
      qDebug() << "[PACMAN] Processing packages -> progress:" << target << "%";
    }
    return;
  }

  if (line.contains("finished") || line.contains("done") || 
      line.contains("completed successfully")) {

    Utils::animateProgress(bar, 100);
    inDownloadPhase = false;
    if (progressTimer)
      progressTimer->stop();
    qDebug() << "[PACMAN] Installation complete -> 100%";
    return;
  }
}

void Utils::processDnfOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                            int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                            double &totalDownloadBytes, QString &lastProcessedLine, 
                            QProgressBar *bar, QTimer *progressTimer) {
  qDebug() << "[DNF-ENTER] Processing line:" << line;
  
  int currentProgress = bar->value();
  
  if (line.contains("Updating and loading repositories")) {
    qDebug() << "[DNF] Repository updates -> 5%";
    Utils::animateProgress(bar, 5);
    return;
  }
  
  if (line.contains("Repositories loaded")) {
    qDebug() << "[DNF] Repositories loaded -> 10%";
    Utils::animateProgress(bar, 10);
    return;
  }
  
  if (line.contains("Package") && line.contains("Arch") && line.contains("Version")) {
    qDebug() << "[DNF] Package info -> 15%";
    Utils::animateProgress(bar, 15);
    return;
  }
  
  if (line.contains("Transaction Summary:") || line.contains("Total size of inbound packages")) {
    qDebug() << "[DNF] Transaction summary -> 25%";
    Utils::animateProgress(bar, 25);
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      if (progressTimer) {
        progressTimer->start(100);
        qDebug() << "[DNF] Started download timer";
      }
    }
    return;
  }
  
  if (line.contains("[") && line.contains("/") && line.contains("]") && line.contains("%")) {
    qDebug() << "[DNF] Found download pattern:" << line.left(80);
    
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      if (progressTimer) {
        progressTimer->start(100);
        qDebug() << "[DNF] Started download timer (late start)";
      }
    }
    
    QRegularExpression re(R"(\[\s*(\d+)\s*/\s*(\d+)\])");
    QRegularExpressionMatch match = re.match(line);
    
    if (match.hasMatch()) {
      int currentPackage = match.captured(1).toInt();
      int totalPackages = match.captured(2).toInt();
      
      if (totalPackages > 0) {

        double packageProgress = (currentPackage * 100.0) / totalPackages;
        int progress = 25 + static_cast<int>((packageProgress * 50) / 100);
        
        if (line.contains("100%")) {
          progress = qMin(progress, 75);
        } else {
          QRegularExpression percentRe(R"((\d{1,3})%)");
          QRegularExpressionMatch percentMatch = percentRe.match(line);
          if (percentMatch.hasMatch()) {
            int packagePercent = percentMatch.captured(1).toInt();
            double adjustedProgress = ((currentPackage - 1) + (packagePercent / 100.0)) * 100 / totalPackages;
            progress = 25 + static_cast<int>((adjustedProgress * 50) / 100);
          }
        }
        
        progress = qMin(progress, 75);
        
        qDebug() << "[DNF] Download: package" << currentPackage << "/" << totalPackages 
                 << "->" << progress << "%";
        
        if (progress > currentProgress) {
          Utils::animateProgress(bar, progress);
        }
      }
    } else {
      if (currentProgress < 75) {
        int newProgress = qMin(currentProgress + 5, 75);
        Utils::animateProgress(bar, newProgress);
        qDebug() << "[DNF] Download progress (fallback) ->" << newProgress << "%";
      }
    }
    return;
  }
  
  if (line.contains("100%") && line.contains("KiB/s") && !line.contains("[")) {
    if (currentProgress < 10) {
      Utils::animateProgress(bar, 10);
      qDebug() << "[DNF] Repository update complete -> 10%";
    }
    return;
  }
  
  if (line.contains("Running transaction")) {
    qDebug() << "[DNF] Running transaction -> 75%";
    Utils::animateProgress(bar, 75);
    inDownloadPhase = false;
    if (progressTimer) {
      progressTimer->stop();
    }
    return;
  }
  
  if (line.contains("Complete!")) {
    qDebug() << "[DNF] Complete -> 100%";
    Utils::animateProgress(bar, 100);
    inDownloadPhase = false;
    if (progressTimer) {
      progressTimer->stop();
    }
    return;
  }
  
  qDebug() << "[DNF-EXIT] No match for line";
}

void Utils::processWingetOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                               int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                               double &totalDownloadBytes, QString &lastProcessedLine, 
                               QProgressBar *bar, QTimer *progressTimer) {
  static qint64 lastDownloadedBytes = 0;
  static QTime lastSpeedUpdateTime;
  static QVector<double> speedSamples;
  
  int currentProgress = bar->value();

  if (line.contains("MB /", Qt::CaseInsensitive)) {
    qDebug() << "[WINGET] Line contains 'MB /' pattern!";

    QRegularExpression wingetPattern1(R"((\d+(?:\.\d+)?).*?MB.*?/\s*(\d+(?:\.\d+)?).*?MB)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression wingetPattern2(R"((\d+(?:\.\d+)?)\s*MB\s*/\s*(\d+(?:\.\d+)?)\s*MB)", QRegularExpression::CaseInsensitiveOption);

    bool wingetProgressFound = false;
    double currentMB = 0;
    double totalMB = 0;

    QRegularExpressionMatch match2 = wingetPattern2.match(line);
    if (match2.hasMatch()) {
      currentMB = match2.captured(1).toDouble();
      totalMB = match2.captured(2).toDouble();
      wingetProgressFound = true;
      qDebug() << "[WINGET] Pattern 2 matched:" << currentMB << "MB /" << totalMB << "MB";
    }

    if (!wingetProgressFound) {
      QRegularExpressionMatch match1 = wingetPattern1.match(line);
      if (match1.hasMatch()) {
        currentMB = match1.captured(1).toDouble();
        totalMB = match1.captured(2).toDouble();
        wingetProgressFound = true;
        qDebug() << "[WINGET] Pattern 1 matched:" << currentMB << "MB /" << totalMB << "MB";
      }
    }

    if (!wingetProgressFound) {
      qDebug() << "[WINGET] Regex failed, trying manual parse...";

      int mbSlashIndex = line.indexOf("MB /", 0, Qt::CaseInsensitive);
      if (mbSlashIndex > 0) {
        QString beforePart = line.left(mbSlashIndex);
        QRegularExpression numBeforeRe(R"((\d+(?:\.\d+)?)\s*[a-zA-Z]*$)");
        QRegularExpressionMatch beforeMatch = numBeforeRe.match(beforePart);

        QString afterPart = line.mid(mbSlashIndex + 4);
        QRegularExpression numAfterRe(R"((\d+(?:\.\d+)?))");
        QRegularExpressionMatch afterMatch = numAfterRe.match(afterPart);

        if (beforeMatch.hasMatch() && afterMatch.hasMatch()) {
          currentMB = beforeMatch.captured(1).toDouble();
          totalMB = afterMatch.hasMatch() ? afterMatch.captured(1).toDouble() : 0;
          wingetProgressFound = (totalMB > 0);
          qDebug() << "[WINGET] Manual parse:" << currentMB << "MB /" << totalMB << "MB";
        }
      }
    }

    if (wingetProgressFound && totalMB > 0) {
      int detectedPercent = qRound((currentMB * 100) / totalMB);
      qDebug() << "[WINGET] PROGRESS FOUND:" << detectedPercent << "% (" << currentMB << "MB /" << totalMB << "MB)";

      if (!inDownloadPhase) {
        inDownloadPhase = true;
        if (progressTimer) {
          progressTimer->stop();
        }
        Utils::animateProgress(bar, 10);
        qDebug() << "[WINGET] Download phase started";
      }

      int progress = 10 + (detectedPercent * 80 / 100);

      if (progress > currentProgress) {
        Utils::animateProgress(bar, qMin(progress, 90));
        qDebug() << "[WINGET] Setting progress:" << detectedPercent << "% -> overall:" << progress << "%";
      }

      totalDownloadBytes = totalMB * 1024 * 1024;

      if (totalDownloadBytes > 0) {
        qint64 downloadedBytes = (totalDownloadBytes * detectedPercent) / 100;
        QTime currentTime = QTime::currentTime();

        if (lastSpeedUpdateTime.isValid() && downloadedBytes > lastDownloadedBytes) {
          int elapsedMs = lastSpeedUpdateTime.msecsTo(currentTime);
          if (elapsedMs > 0) {
            double bytesPerSec = (downloadedBytes - lastDownloadedBytes) * 1000.0 / elapsedMs;

            speedSamples.append(bytesPerSec);
            if (speedSamples.size() > 5)
              speedSamples.removeFirst();

            double avgSpeed = 0;
            for (double s : speedSamples)
              avgSpeed += s;
            avgSpeed /= speedSamples.size();

            QString speedStr;
            if (avgSpeed > 1024 * 1024) {
              speedStr = QString::number(avgSpeed / (1024 * 1024), 'f', 1) + " MB/s";
            } else if (avgSpeed > 1024) {
              speedStr = QString::number(avgSpeed / 1024, 'f', 1) + " KB/s";
            } else {
              speedStr = QString::number(avgSpeed, 'f', 1) + " B/s";
            }

            qDebug() << "[WINGET] Download speed:" << speedStr;
          }
        }

        lastDownloadedBytes = downloadedBytes;
        lastSpeedUpdateTime = currentTime;
      }
      return;
    } else {
      qDebug() << "[WINGET] Line contains 'MB /' but couldn't extract numbers!";
      qDebug() << "[WINGET] Line was:" << line;
    }
  }

  if (line.contains("Found installed package") || line.contains("Found installer")) {
    if (currentProgress < 5) {
      Utils::animateProgress(bar, 5);
    }
    return;
  }

  if (line.contains("Starting package install") || line.contains("Beginning install")) {
    if (!inDownloadPhase) {
      inDownloadPhase = true;
      Utils::animateProgress(bar, 10);
    }
    return;
  }

  if (line.contains("Successfully installed") || line.contains("Installation completed successfully")) {
    Utils::animateProgress(bar, 100);
    inDownloadPhase = false;
    if (progressTimer) {
      progressTimer->stop();
    }
    qDebug() << "[WINGET] Installation complete -> 100%";
    return;
  }
}

// fallback parsing
void Utils::processGenericOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, 
                                int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                                double &totalDownloadBytes, QString &lastProcessedLine, 
                                QProgressBar *bar, QTimer *progressTimer) {
  int currentProgress = bar->value();
  
  if ((line.contains("Downloading") || line.contains("Fetching")) && !inDownloadPhase) {
    inDownloadPhase = true;
    if (progressTimer)
      progressTimer->start(100);
    Utils::animateProgress(bar, 25);
    qDebug() << "[GENERIC] Download phase started";
    return;
  }

  if (inDownloadPhase) {
    QRegularExpression percentPattern(R"(\b(\d{1,3})%\b)");
    QRegularExpressionMatch match = percentPattern.match(line);
    
    if (match.hasMatch()) {
      int detectedPercent = match.captured(1).toInt();
      int progress = 25 + (detectedPercent * 65 / 100);
      
      if (progress > currentProgress) {
        Utils::animateProgress(bar, qMin(progress, 90));
        qDebug() << "[GENERIC] Progress:" << detectedPercent << "% -> overall:" << progress << "%";
      }
      return;
    }
  }

  if (line.contains("Complete!") || line.contains("Finished") || 
      line.contains("Success") || line.contains("Done")) {
    Utils::animateProgress(bar, 100);
    inDownloadPhase = false;
    if (progressTimer)
      progressTimer->stop();
    qDebug() << "[GENERIC] Operation complete -> 100%";
    return;
  }

  if (line.contains("Installing") || line.contains("Processing") || 
      line.contains("Configuring") || line.contains("Setting up")) {
    if (currentProgress < 95) {
      int target = qMin(currentProgress + 5, 95);
      Utils::animateProgress(bar, target);
      qDebug() << "[GENERIC] Activity detected -> progress:" << target << "%";
    }
    return;
  }
}

void Utils::processBrewFormulaInstallation(const QString &line, bool &isFormulaInstallation, 
                                          int &formulaDepCount, int &formulaDepsInstalled, 
                                          int &totalExpectedDeps, bool &inDownloadPhase, 
                                          QProgressBar *bar, QTimer *progressTimer) {
  qDebug() << "[BREW-FORMULA] Processing:" << line;
  
  if (line.contains("==> Fetching downloads for:") || line.contains("✔︎ Bottle Manifest")) {
    if (bar->value() < 20) {
      Utils::animateProgress(bar, qMin(bar->value() + 1, 20));
    }
    return;
  }
  
  if (line.contains("==> Installing dependencies for mariadb:")) {
    QString depsText = line;
    depsText = depsText.mid(depsText.indexOf(":") + 1);
    QStringList deps = depsText.split(',', Qt::SkipEmptyParts);
    totalExpectedDeps = deps.size() + 1;
    formulaDepCount = 0;
    Utils::animateProgress(bar, 25);
    return;
  }
  
  if (line.contains("==> Installing mariadb dependency:")) {
    formulaDepCount++;
    int progress = 25 + (25 * formulaDepCount / totalExpectedDeps);
    if (progress > bar->value()) {
      Utils::animateProgress(bar, qMin(progress, 50));
    }
    return;
  }
  
  if (line.contains("==> Pouring") && !line.contains("mariadb")) {
    return;
  }
  
  if (line.contains("🍺") && !line.contains("mariadb")) {
    formulaDepsInstalled++;
    int progress = 50 + (20 * formulaDepsInstalled / totalExpectedDeps);
    if (progress > bar->value()) {
      Utils::animateProgress(bar, qMin(progress, 70));
    }
    return;
  }
  
  if (line.contains("==> Pouring mariadb")) {
    Utils::animateProgress(bar, 75);
    return;
  }
  
  if (line.contains("🍺") && line.contains("mariadb")) {
    Utils::animateProgress(bar, 80);
    return;
  }
  
  if (line.contains("=== HOMEBREW INSTALL COMPLETE ===")) {
    Utils::animateProgress(bar, 85);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Starting MariaDB service")) {
    Utils::animateProgress(bar, 87);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Waiting for MariaDB")) {
    Utils::animateProgress(bar, 89);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Setting root password")) {
    Utils::animateProgress(bar, 91);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Securing installation")) {
    Utils::animateProgress(bar, 93);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Testing root connection")) {
    Utils::animateProgress(bar, 95);
    return;
  }
  
  if (line.contains("[WRAPPER-INFO] Restarting MariaDB service")) {
    Utils::animateProgress(bar, 97);
    return;
  }
  
  if (line.contains("[WRAPPER-SUCCESS] MariaDB fully installed")) {
    Utils::animateProgress(bar, 100);
    isFormulaInstallation = false;
    inDownloadPhase = false;
    if (progressTimer) progressTimer->stop();
    return;
  }
}

void Utils::animateProgress(QProgressBar *bar, int target) {
  if (!bar || target <= bar->value())
    return;
  if (target > 100)
    target = 100;

  int current = bar->value();
  int difference = target - current;

  QPropertyAnimation *anim = new QPropertyAnimation(bar, "value");

  int duration = 1000;
  if (difference <= 5)
    duration = 300;
  else if (difference <= 10)
    duration = 500;
  else if (difference <= 20)
    duration = 700;

  anim->setDuration(duration);
  anim->setStartValue(current);
  anim->setEndValue(target);
  anim->setEasingCurve(QEasingCurve::OutQuad);
  anim->start(QPropertyAnimation::DeleteWhenStopped);
}

QString Utils::cleanPackageOutput(const QString &raw) {
  QString clean = raw;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

  clean = clean.replace(QRegularExpression(R"(\x1B\[[0-9;]*m)"), "");

#else

  clean = clean.replace(QRegularExpression(R"(\x1B\[[0-9;]*m)"), "");
  clean = clean.replace('\r', '\n');
#endif

  clean = clean.replace('\r', '\n');

  QRegularExpression ansiRe(R"(\x1B(?:\[[0-?]*[ -/]*[@-~]|\(B|\)[0-9]))");
  clean.remove(ansiRe);
  clean.remove(QRegularExpression(R"([\x00-\x1F\x7F])"));

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

#else
  clean.remove(QRegularExpression(R"([─│┌┐└┘▓▒░☐☑✓✗])"));
#endif

  clean.replace(QRegularExpression(R"(\r\n|\r)"), "\n");

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

#else
  if (clean.contains("http")) {
    clean = clean.replace(QRegularExpression(R"(http[^ ]+ )"), "[repo] ");
  }
#endif

  return clean.trimmed();
}

void Utils::updateStatusFromOutput(const QString &accumulated, QString &lastStatus, QLabel *label, const QString &currentPhase) {
  if (accumulated.isEmpty())
    return;

  QString statusMsg = "Processing installation...";
  if (!currentPhase.isEmpty()) {

    if (currentPhase.contains("Downloading") || currentPhase.contains("Get:")) {
      statusMsg = "Downloading packages...";
    } else if (currentPhase.contains("Installing") || currentPhase.contains("Unpacking") || currentPhase.contains("Setting up")) {
      statusMsg = "Installing components...";
    } else if (currentPhase.contains("Updating") || currentPhase.contains("Hit:")) {
      statusMsg = "Updating package lists...";
    } else if (currentPhase.contains("Fetched") || currentPhase.contains("Complete")) {
      statusMsg = "Finishing installation...";
    } else if (currentPhase.contains("Resolving") || currentPhase.contains("Dependencies")) {
      statusMsg = "Resolving dependencies...";
    }
  }

  if (statusMsg == lastStatus)
    return;

  lastStatus = statusMsg;
  Utils::updateStatus(label, statusMsg, StatusType::Progress);
}

QString Utils::detectCurrentPhase(bool inDownloadPhase, const QString &accumulatedOutput) {
  QString phase = "Processing";
  QStringList lines = accumulatedOutput.split('\n', Qt::SkipEmptyParts);
  bool hasHit = false, hasGet = false;
  for (const QString &line : lines) {
    QString lc = line.toLower();
    if (lc.startsWith("hit:"))
      hasHit = true;
    if (lc.startsWith("get:") || lc.contains("%"))
      hasGet = true;
  }

  if (inDownloadPhase || hasGet) {
    phase = "Downloading";
  } else if (hasHit) {
    phase = "Updating";
  } else if (accumulatedOutput.contains("Unpacking") || accumulatedOutput.contains("Installing") || accumulatedOutput.contains("Setting up")) {
    phase = "Installing";
  } else if (accumulatedOutput.contains("Fetched") || accumulatedOutput.contains("Complete")) {
    phase = "Fetched";
  } else if (accumulatedOutput.contains("Resolving") || accumulatedOutput.contains("Dependencies")) {
    phase = "Resolving";
  }

  return phase;
}

bool Utils::fixLock(QLabel *statusLabel, PkgManagerType pkgType) {
  if (!statusLabel) {
    qWarning() << "fixLock: statusLabel is null";
    return false;
  }

  if (pkgType == PkgManagerType::Winget) {
    qDebug() << "Windows/Winget: No lock recovery needed";
    return true;
  }

  if (pkgType == PkgManagerType::Homebrew) {
    qDebug() << "macOS/Homebrew: No lock recovery needed";
    return true;
  }
  Utils::updateStatus(statusLabel, "Checking for package manager locks...", StatusType::Progress);
  qDebug() << "=== Starting fixLock: detecting locks and recovering ===";

  if (pkgType == PkgManagerType::Unknown) {
    Utils::updateStatus(statusLabel, "Unsupported OS/package manager", StatusType::Error);
    return false;
  }

  bool success = true;
  QString pkgName = (pkgType == PkgManagerType::Apt)      ? "apt/dpkg"
                    : (pkgType == PkgManagerType::Dnf)    ? "dnf/rpm"
                    : (pkgType == PkgManagerType::Pacman) ? "pacman"
                                                          : "unknown";

  Utils::updateStatus(statusLabel, QString("Checking %1 locks...").arg(pkgName), StatusType::Progress);
  qDebug() << "Detected package manager:" << pkgName;

  QStringList lockFiles;
  QStringList recoveryCommands;

  QString dnfRefreshCmd;
  bool dnfRefreshAttempted = false;

  switch (pkgType) {
  case PkgManagerType::Apt:
    lockFiles << "/var/lib/dpkg/lock-frontend" << "/var/lib/dpkg/lock" << "/var/cache/apt/archives/lock" << "/var/lib/apt/lists/lock";
    recoveryCommands << "systemctl stop apt-daily.service apt-daily-upgrade.service unattended-upgrades.service snapd.service || true"
                     << "fuser -vk /var/lib/dpkg/lock-frontend /var/lib/dpkg/lock /var/cache/apt/archives/lock /var/lib/apt/lists/lock || true"
                     << "rm -f /var/lib/dpkg/lock-frontend /var/lib/dpkg/lock /var/cache/apt/archives/lock /var/lib/apt/lists/lock"
                     << "dpkg --configure -a --force-confold || true"
                     << "sleep 5";
    break;

  case PkgManagerType::Dnf:
    lockFiles << "/var/cache/dnf/*.lock" << "/var/lib/rpm/.rpm.lock";
    recoveryCommands << "systemctl stop packagekit || true" << "fuser -vk /var/cache/dnf || true" << "rm -f /var/cache/dnf/*.lock" << "rpm --rebuilddb || true"
                     << "dnf clean all || true" << "sleep 5";

    Utils::updateStatus(statusLabel, "Refreshing DNF repositories and fixing permissions...", StatusType::Progress);

    dnfRefreshCmd = "dnf makecache --refresh --setopt=persistdir=/var/cache/dnf "
                    "--setopt=cachedir=/var/cache/dnf && "
                    "find /var/cache/dnf -type f -exec chmod 644 {} \\; && "
                    "find /var/cache/dnf -type d -exec chmod 755 {} \\;";

    dnfRefreshAttempted = true;
    break;

  case PkgManagerType::Pacman:
    lockFiles << "/var/lib/pacman/db.lck";
    recoveryCommands << "fuser -vk /var/lib/pacman/db.lck || true" << "rm -f /var/lib/pacman/db.lck" << "pacman -Syy --noconfirm || true" << "sleep 5";
    break;

  default:
    Utils::updateStatus(statusLabel, "No lock handling for this package manager", StatusType::Error);
    qWarning() << "fixLock: No lock recovery implemented for pkg type";
    return false;
  }

  if (dnfRefreshAttempted && !dnfRefreshCmd.isEmpty()) {
    QProcessResult dnfResult = Utils::executeProcess("pkexec", {"sh", "-c", dnfRefreshCmd}, 15000);

    if (dnfResult.exitCode == 0) {
      Utils::updateStatus(statusLabel, "Repositories refreshed", StatusType::Success);
    } else {
      qWarning() << "DNF repository refresh failed:" << dnfResult.stdErr.trimmed();
    }
  }

  QStringList holdingPids;
  for (const QString &lock : std::as_const(lockFiles)) {
    QString pattern = lock.contains("*") ? lock.left(lock.indexOf("*")) : lock;
    QProcessResult fuserResult = Utils::executeProcess("fuser", {pattern}, 5000);

    if (fuserResult.exitCode == 0) {
      QString output = fuserResult.stdOut.trimmed();
      if (!output.isEmpty()) {
        QStringList pids = output.split(' ', Qt::SkipEmptyParts);
        for (const QString &pid : pids) {
          if (!holdingPids.contains(pid)) {
            holdingPids.append(pid);
          }
        }
      }
    }
  }

  if (holdingPids.isEmpty()) {
    Utils::updateStatus(statusLabel, "No locks detected — system is clean", StatusType::Success);
    qDebug() << "No package manager locks found. All good.";
    return true;
  }

  Utils::updateStatus(statusLabel, QString("Killing %1 lock holder(s): %2").arg(holdingPids.size()).arg(holdingPids.join(", ")), StatusType::Progress);
  qWarning() << "Package manager lock held by PID(s):" << holdingPids;

  QStringList killArgs = {"-9"};
  killArgs.append(holdingPids);
  QProcessResult killResult = Utils::executeProcess("kill", killArgs, 5000);

  Utils::updateStatus(statusLabel, "Releasing locks and recovering package state...", StatusType::Progress);
  qDebug() << "Running recovery commands...";

  for (const QString &cmd : recoveryCommands) {
    qDebug() << "Recovery >" << cmd;
    QProcessResult cmdResult = Utils::executeProcess("bash", {"-c", cmd}, 30000);

    if (cmdResult.exitCode != 0 && !cmdResult.stdErr.isEmpty()) {
      qWarning() << "Recovery command failed:" << cmd << "→" << cmdResult.stdErr.trimmed();
      success = false;
    }
  }

  bool stillLocked = false;
  for (const QString &lockPattern : std::as_const(lockFiles)) {
    if (lockPattern.contains('*')) {
      QString dirPath = lockPattern.left(lockPattern.indexOf('*'));
      QDir dir(dirPath);
      QString pattern = QFileInfo(lockPattern).fileName();
      dir.setNameFilters({pattern});
      dir.setFilter(QDir::Files);
      if (!dir.entryList().isEmpty()) {
        stillLocked = true;
        qWarning() << "DNF lock files still present in:" << dirPath;
        break;
      }
    } else {
      if (QFile::exists(lockPattern)) {
        stillLocked = true;
        qWarning() << "Lock file still exists:" << lockPattern;
        break;
      }
    }
  }

  if (stillLocked) {
    Utils::updateStatus(statusLabel, "Failed to fully release package manager locks", StatusType::Error);
    qCritical() << "fixLock: Some locks still exist after cleanup!";
    success = false;
  } else {
    Utils::updateStatus(statusLabel, "Package manager locks successfully cleared", StatusType::Success);
    qDebug() << "=== fixLock completed successfully ===";
  }

  return success;
}
void Utils::setProgressColor(QProgressBar *progressBar, bool isYellow) {
  if (isYellow) {
    progressBar->setStyleSheet("QProgressBar {"
                               "    color: #fd71b5;"
                               "    text-align: center;"
                               "}"
                               "QProgressBar::chunk {"
                               "    background-color: #fefe17;"
                               "}");
    progressBar->setAlignment(Qt::AlignCenter);
  } else {
    progressBar->setStyleSheet("QProgressBar {"
                               "    color: #fefe17;"
                               "    text-align: center;"
                               "}"
                               "QProgressBar::chunk {"
                               "    background-color: #fd71b5;"
                               "}");
  }
}


bool Utils::installPackageManager(QWidget *parent) {
  QString osName = QSysInfo::productType().toLower();
  QProcessResult result;
  
  qDebug() << "installPackageManager called for OS:" << osName;

  if (osName.contains("macos") || osName == "osx") {
    QString command = "NONINTERACTIVE=1 /bin/bash -c \"$(curl -fsSL "
                      "https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"";
    qDebug() << "Running brew install command:" << command;
    result = Utils::executeProcess("/bin/sh", {"-c", command}, 300000);
    qDebug() << "Brew install result - exitCode:" << result.exitCode 
             << "QPSuccess:" << result.QPSuccess;

  } else if (osName.contains("windows")) {
    QString command = "powershell -Command \""
                      "Add-AppxPackage -RegisterByFamilyName -MainPackage "
                      "Microsoft.DesktopAppInstaller_8wekyb3d8bbwe; "
                      "Start-Sleep -Seconds 3\"";
    result = Utils::executeProcess("cmd.exe", {"/c", command}, 300000);
  } else {
    qDebug() << "Unsupported OS for package manager installation";
    return true;
  }

  if (parent) {
    QElapsedTimer timer;
    timer.start();
    while (!result.QPSuccess && timer.elapsed() < 300000) {
      QCoreApplication::processEvents();
      QThread::msleep(100);
    }
  }

  bool success = (result.exitCode == 0);
  qDebug() << "installPackageManager returning:" << success;
  return success;
}

qint64 Utils::getPackageSize(const QString &packageName, PkgManagerType pkgType) {
  QProcessResult result;

  switch (pkgType) {
  case PkgManagerType::Apt:
    result = Utils::executeProcess("apt-cache", {"show", packageName}, 5000);
    break;
  case PkgManagerType::Dnf:
    result = Utils::executeProcess("dnf", {"info", packageName}, 5000);
    break;
  case PkgManagerType::Pacman:
    result = Utils::executeProcess("pacman", {"-Si", packageName}, 5000);
    break;
  case PkgManagerType::Winget:
    result = Utils::executeProcess("winget", {"show", "--id", packageName}, 5000);
    break;
  default:
    return 0;
  }

  if (!result.QPSuccess || result.exitCode != 0) {
    return 0;
  }

  QString output = result.stdOut;

  QVector<QPair<QString, QString>> patterns = {{"Size\\s*:\\s*([\\d,]+)", ""},

                                               {"Download Size\\s*:\\s*([\\d,.]+)\\s*(\\w+)", ""},

                                               {"Download size\\s*:\\s*([\\d,.]+)\\s*(\\w+)", ""},

                                               {"Download Size\\s*:\\s*([\\d,.]+)\\s*(\\w+)", ""},

                                               {"Size\\s*:\\s*([\\d,.]+)\\s*(\\w+)", ""}};

  for (const auto &pattern : patterns) {
    QRegularExpression re(pattern.first);
    QRegularExpressionMatch match = re.match(output);

    if (match.hasMatch()) {
      QString sizeValue = match.captured(1).remove(',');
      QString unit = match.capturedLength(2) > 0 ? match.captured(2).toLower() : "b";

      double size = sizeValue.toDouble();

      if (unit.startsWith("kib")) {
        return static_cast<qint64>(size * 1024);
      } else if (unit.startsWith("kb") || unit.startsWith("k")) {
        return static_cast<qint64>(size * 1000);
      } else if (unit.startsWith("mib")) {
        return static_cast<qint64>(size * 1024 * 1024);
      } else if (unit.startsWith("mb") || unit.startsWith("m")) {
        return static_cast<qint64>(size * 1000 * 1000);
      } else if (unit.startsWith("gib")) {
        return static_cast<qint64>(size * 1024 * 1024 * 1024);
      } else if (unit.startsWith("gb") || unit.startsWith("g")) {
        return static_cast<qint64>(size * 1000 * 1000 * 1000);
      } else if (unit.startsWith("tib")) {
        return static_cast<qint64>(size * 1024LL * 1024 * 1024 * 1024);
      } else if (unit.startsWith("tb") || unit.startsWith("t")) {
        return static_cast<qint64>(size * 1000LL * 1000 * 1000 * 1000);
      } else {

        return static_cast<qint64>(size);
      }
    }
  }

  return 0;
}

QString Utils::formatBytes(qint64 bytes, bool includeUnit) {
  const qint64 KB = 1024;
  const qint64 MB = KB * 1024;
  const qint64 GB = MB * 1024;

  if (bytes >= GB) {
    double gb = bytes / (double)GB;
    if (includeUnit) {
      return QString::number(gb, 'f', 1) + " GiB";
    }
    return QString::number(gb, 'f', 1);
  } else if (bytes >= MB) {
    double mb = bytes / (double)MB;
    if (includeUnit) {
      return QString::number(mb, 'f', 1) + " MiB";
    }
    return QString::number(mb, 'f', 1);
  } else if (bytes >= KB) {
    double kb = bytes / (double)KB;
    if (includeUnit) {
      return QString::number(kb, 'f', 1) + " KiB";
    }
    return QString::number(kb, 'f', 1);
  } else {
    if (includeUnit) {
      return QString::number(bytes) + " B";
    }
    return QString::number(bytes);
  }
}

QString Utils::generateSecurePassword(int length) {
  static const QString chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  QString password;
  for (int i = 0; i < length; ++i) {
    password += chars[QRandomGenerator::global()->bounded(chars.length())];
  }
  return password;
}

void Utils::updateStatus(QLabel *statusLabel, const QString &text, StatusType type) {
  if (!statusLabel) {
    qDebug() << "statusLabel is null";
    return;
  }

  if (!statusLabel->metaObject()) {
    qDebug() << "statusLabel is deleted/invalid";
    return;
  }

  QString emo = "";
  QColor textColor;

  switch (type) {
  case StatusType::Success:
    textColor = Qt::green;
    emo = "✅";
    break;
  case StatusType::Error:
    textColor = Qt::red;
    emo = "❌";
    break;
  case StatusType::Warning:
    textColor = QColor("#FF8800");
    emo = "⚠️";
    break;
  case StatusType::Progress:
    textColor = QColor("#FEFE17");
    emo = "⌛️";
    break;
  case StatusType::Normal:
    emo = "➡️";
    textColor = QColor("#00FEFE");
    break;
  }

  QString cleanup = text;

  static const QRegularExpression emojiRegex("^\\s*(✅|❌|⚠️|⌛️|➡️)\\s*");

  cleanup.remove(emojiRegex);

  cleanup = cleanup.trimmed();

  statusLabel->setText(emo + (emo.isEmpty() ? "" : " ") + cleanup);

  QPalette pal = statusLabel->palette();
  pal.setColor(QPalette::WindowText, textColor);
  statusLabel->setPalette(pal);
  statusLabel->update();
}

QProcessResult Utils::executeProcess(const QString &program, const QStringList &arguments, int timeoutMs, QProcess::ProcessChannelMode channelMode) {
  QProcess process;
  process.setProcessChannelMode(channelMode);

  QProcessResult result;
  result.QPSuccess = false;

  process.start(program, arguments);

  if (!process.waitForStarted(timeoutMs)) {

    result.exitCode = -1;
    result.exitStatus = QProcess::CrashExit;
    result.stdErr = QString("Failed to start process '%1': %2").arg(program).arg(process.errorString());
    return result;
  }

  result.QPSuccess = true;

  if (!process.waitForFinished(timeoutMs)) {

    process.kill();
    process.waitForFinished(1000);
    result.exitCode = -2;
    result.exitStatus = QProcess::CrashExit;
    result.stdErr = QString("Process '%1' timed out after %2ms").arg(program).arg(timeoutMs);
  } else {

    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();
  }

  result.stdOut = QString::fromUtf8(process.readAllStandardOutput());
  result.stdErr = QString::fromUtf8(process.readAllStandardError());

  if (process.error() != QProcess::UnknownError) {
    if (!result.stdErr.isEmpty()) {
      result.stdErr += "\n";
    }
    result.stdErr += QString("Process error: %1").arg(process.errorString());
  }

  return result;
}
