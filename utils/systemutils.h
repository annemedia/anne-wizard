#ifndef SYSTEMUTILS_H
#define SYSTEMUTILS_H

#include <QDir>
#include <QString>
#include <QStringList>
#ifdef Q_OS_UNIX
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

class SystemUtils {
public:
  static bool createDesktopIntegration(const QString &installDir, bool createDesktopShortcut, bool createMenuEntry, bool launchOnBoot, bool runHeadless, bool launchNow,
                                       QStringList &details, QString &summary);
  static QString getRealUserHome();
  static QString getInstallPath();
#ifdef Q_OS_WIN
  static bool setWindowsPermissions(const QString &path);
#endif
#ifdef Q_OS_UNIX
  static uid_t getRealUserUid();
  static gid_t getRealUserGid();
  static void chownUser(QString targetFile, bool chmod = false);
#endif
    static QString findBrew();

private:
#ifdef Q_OS_LINUX
  static bool createLinuxDesktopFile(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool menu,
                                     bool autostart, bool headless, QStringList &details);
  static bool createLinuxSystemdService(const QString &installDir, bool enable, bool headless, QStringList &details);
#endif
#ifdef Q_OS_WIN
  static bool createWindowsShortcut(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool startMenu,
                                    bool autostart, bool headless, QStringList &details);
  static QString getShortPath(const QString &longPath);
  static void refreshShortcutIcon(const QString &shortcutPath);
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  static bool createMacOsIntegration(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool autostart,
                                     bool headless, QStringList &details);
#endif
  static bool createDesktopEntry(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool createDesktopShortcut,
                                 bool createMenuEntry, bool launchOnBoot, bool headless, QStringList &details);
  static QString findHasherExecutable(const QDir &hasherDir);
  static QString findMinerExecutable(const QDir &minerDir);
  static QString installUserIcon(const QString &appId, const QString &resourcePath, QStringList *details = nullptr);

#ifdef Q_OS_UNIX
  static QString findJava();
  static bool shouldBeExecutable(const QString &filePath);
  static void chmodRecursive(const QString &dirPath);
  static void chmodSingleFile(const QString &filePath);
  static bool runGioAsUser(const QString &path);
  #endif
};

#endif