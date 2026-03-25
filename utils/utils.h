#ifndef UTILS_H
#define UTILS_H
#include "../wizard.h"
#include "osinfo.h"
#include <QLabel>
#include <QList>
#include <QObject>
#include <QPalette>
#include <QProcess>
#include <QProgressBar>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVersionNumber>
#include <utility>

struct PkgQuery {
  QString command;
  QStringList args;
  QString targetPkg;
  int timeoutMs = 5000;
};

enum class StatusType { Success, Error, Warning, Progress, Normal };
struct QProcessResult {
  int exitCode;
  QProcess::ExitStatus exitStatus;
  QString stdOut;
  QString stdErr;
  bool QPSuccess;
};
class Utils {

public:
  static qint64 getPackageSize(const QString &packageName, PkgManagerType pkgType);
  static bool installPackageManager(QWidget *parent = nullptr);

  static void setProgressColor(QProgressBar *progressBar, bool isYellow);
  static QString getPlatformCommand(PkgManagerType type, const QString &package);
  static QString getPackageVersion(PkgManagerType type, const QString &command, const QStringList &args, const QString &pkgName);
  static void animateProgress(QProgressBar *bar, int target);
  static void parseGenericOutput(const QString &line, bool &inDownloadPhase, QTimer *timer, QProgressBar *bar);
  static void parseInstallOutput(const QString &data, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount,
                                 int &completedFiles, double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer,
                                 PkgManagerType pkgType);
  static QString formatBytes(qint64 bytes, bool useIEC);
  static QString cleanPackageOutput(const QString &raw);
  static void updateStatusFromOutput(const QString &accumulated, QString &lastStatus, QLabel *label, const QString &currentPhase);
  static QString detectCurrentPhase(bool inDownloadPhase, const QString &accumulatedOutput);
  static bool fixLock(QLabel *statusLabel, PkgManagerType pkgType);
  static QString generateSecurePassword(int length = 30);
  static void updateStatus(QLabel *statusLabel, const QString &text, StatusType type);
  static QProcessResult executeProcess(const QString &program, const QStringList &arguments, int timeoutMs = -1,
                                       QProcess::ProcessChannelMode channelMode = QProcess::SeparateChannels);

  static void setWizardButtons(bool next = false, bool back = false, bool cancel = true);

private:
  static Wizard *getWizard();
  static Wizard *m_wiz;

  static void processBrewOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                         double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processAptOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                        double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processPacmanOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                           double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processDnfOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                        double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processWingetOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                           double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processGenericOutput(const QString &line, bool &inDownloadPhase, double &downloadProgress, int &downloadLineCount, int &downloadFileCount, int &completedFiles,
                            double &totalDownloadBytes, QString &lastProcessedLine, QProgressBar *bar, QTimer *progressTimer);

  static void processBrewFormulaInstallation(const QString &line, bool &isFormulaInstallation, int &formulaDepCount, int &formulaDepsInstalled, int &totalExpectedDeps,
                                      bool &inDownloadPhase, QProgressBar *bar, QTimer *progressTimer);
};

#endif
