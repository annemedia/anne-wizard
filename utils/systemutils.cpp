#include "systemutils.h"
#include "utils.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

#ifdef Q_OS_WIN
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#include <winuser.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#include <QCoreApplication>
#endif

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

QString SystemUtils::getRealUserHome() {
#ifdef Q_OS_UNIX
  if (geteuid() != 0) {
    return QDir::homePath();
  }

  struct passwd *pw = nullptr;

  QByteArray sudoUid = qgetenv("SUDO_UID");
  if (!sudoUid.isEmpty()) {
    pw = getpwuid(sudoUid.toUInt());
  }

  if (!pw) {
    QByteArray pkexecUid = qgetenv("PKEXEC_UID");
    if (!pkexecUid.isEmpty()) {
      pw = getpwuid(pkexecUid.toUInt());
    }
  }

  if (!pw) {
    QByteArray sudoUser = qgetenv("SUDO_USER");
    if (!sudoUser.isEmpty() && sudoUser != "root") {
      pw = getpwnam(sudoUser.constData());
    }
  }

  if (!pw) {
    QByteArray logname = qgetenv("LOGNAME");
    if (logname.isEmpty())
      logname = qgetenv("USER");
    if (!logname.isEmpty() && logname != "root") {
      pw = getpwnam(logname.constData());
    }
  }

  if (pw && pw->pw_dir && strlen(pw->pw_dir) > 0) {
    return QString::fromLocal8Bit(pw->pw_dir);
  }
#endif

  return QDir::homePath();
}

QString SystemUtils::getInstallPath() {
#ifdef Q_OS_WIN
  wchar_t programFilesPath[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, programFilesPath))) {
    return QDir(QString::fromWCharArray(programFilesPath)).filePath("ANNE");
  } else {
    return "C:/Program Files/ANNE";
  }
#else

  return QDir(SystemUtils::getRealUserHome()).filePath("ANNE");
#endif
}

bool SystemUtils::createDesktopIntegration(const QString &installDir, bool createDesktopShortcut, bool createMenuEntry, bool launchOnBoot, bool runHeadless,
                                           bool launchNow, QStringList &details, QString &summary) {
  details.clear();

  QDir baseDir(installDir);
  if (!baseDir.exists()) {
    details << "Installation directory does not exist: " + installDir;
    summary = "Desktop integration failed – install directory missing.";
    return false;
  }

  bool allOk = true;

  QDir annodeDir(installDir + QDir::separator() + "annode");
  QString annodeJar = annodeDir.filePath("anne-node.jar");
  bool annodeAvailable = annodeDir.exists() && QFileInfo(annodeJar).isFile();

  if (annodeAvailable) {
    if (!createDesktopEntry("Annode", "annode", annodeJar, annodeDir.absolutePath(), createDesktopShortcut, createMenuEntry, launchOnBoot, runHeadless, details)) {
      allOk = false;
    }
  } else {
    details << "Annode not found in installation directory";
  }

  QDir hasherDir(installDir + QDir::separator() + "annehasher");
  QString hasherExecutable = findHasherExecutable(hasherDir);
  bool hasherAvailable = hasherDir.exists() && !hasherExecutable.isEmpty();

  if (hasherAvailable) {
    if (!createDesktopEntry("ANNE Hasher", "annehasher", hasherExecutable, hasherDir.absolutePath(), createDesktopShortcut, createMenuEntry, false, false, details)) {
      allOk = false;
    }
  } else {
    details << "ANNE Hasher not found in installation directory";
  }

  QDir minerDir(installDir + QDir::separator() + "anneminer");
  QString minerExecutable = findMinerExecutable(minerDir);
  bool minerAvailable = minerDir.exists() && !minerExecutable.isEmpty();

  if (minerAvailable) {
    if (!createDesktopEntry("ANNE Miner", "anneminer", minerExecutable, minerDir.absolutePath(), createDesktopShortcut, createMenuEntry, false, false, details)) {
      allOk = false;
    }
  } else {
    details << "ANNE Miner not found in installation directory";
  }

  if (launchNow && annodeAvailable) {
    QStringList args{"-jar", annodeJar};
    if (runHeadless)
      args << "--headless";

#ifdef Q_OS_UNIX
    if (geteuid() == 0) {
      uid_t targetUid = getRealUserUid();
      if (targetUid == 0 || targetUid == static_cast<uid_t>(-1)) {
        if (QProcess::startDetached("java", args, annodeDir.absolutePath())) {
          details << "Annode launched successfully";
        } else {
          details << "Failed to launch Annode (java not found?)";
          allOk = false;
        }
      } else {
        struct passwd *pw = getpwuid(targetUid);
        if (pw && pw->pw_name) {
          QString username = QString::fromLocal8Bit(pw->pw_name);
          QStringList sudoArgs{"-u", username, "java"};
          sudoArgs << args;
          if (QProcess::startDetached("sudo", sudoArgs, annodeDir.absolutePath())) {
            details << "Annode launched successfully as user '" + username + "'";
          } else {
            details << "Failed to launch Annode with user privileges";
            allOk = false;
          }
        } else {
          details << "Cannot determine username for launching";
          allOk = false;
        }
      }
    } else {
      if (QProcess::startDetached("java", args, annodeDir.absolutePath())) {
        details << "Annode launched successfully";
      } else {
        details << "Failed to launch Annode (java not found?)";
        allOk = false;
      }
    }
#else
    if (QProcess::startDetached("java", args, annodeDir.absolutePath())) {
      details << "Annode launched successfully";
    } else {
      details << "Failed to launch Annode (java not found?)";
      allOk = false;
    }
#endif
  }

  if (details.isEmpty()) {
    summary = "No desktop integration was requested.";
  } else if (allOk) {
    summary = "<b>Desktop integration completed successfully!</b><br><br>"
              "• " +
              details.join("<br>• ");
  } else {
    summary = "<b>Desktop integration finished with errors:</b><br><br>"
              "• " +
              details.join("<br>• ");
  }
  qDebug() << "Checking install directory:" << installDir;
  qDebug() << "Annode dir exists:" << annodeDir.exists() << "at:" << annodeDir.absolutePath();
  qDebug() << "Hasher dir exists:" << hasherDir.exists() << "at:" << hasherDir.absolutePath();
  qDebug() << "Miner dir exists:" << minerDir.exists() << "at:" << minerDir.absolutePath();
  return allOk;
}

QString SystemUtils::findHasherExecutable(const QDir &hasherDir) {
  if (!hasherDir.exists())
    return QString();

  QStringList filters;
#ifdef Q_OS_WIN
  filters << "anne-hasher-gui.exe";
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  filters << "Anne Hasher.app";
#else
  filters << "anne-hasher";
#endif

  QStringList files = hasherDir.entryList(filters, QDir::Files | QDir::Executable | QDir::NoDotAndDotDot);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QStringList appBundles = hasherDir.entryList(QStringList() << "*.app", QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &appBundle : appBundles) {
    if (appBundle.contains("hasher", Qt::CaseInsensitive)) {
      return hasherDir.filePath(appBundle);
    }
  }
#endif

  if (!files.isEmpty()) {

    for (const QString &file : files) {
      if (file.contains("gui", Qt::CaseInsensitive)) {
        return hasherDir.filePath(file);
      }
    }

    return hasherDir.filePath(files.first());
  }

  return QString();
}

QString SystemUtils::findMinerExecutable(const QDir &minerDir) {
  if (!minerDir.exists())
    return QString();

  QStringList filters;
#ifdef Q_OS_WIN
  filters << "anne-miner.exe" << "anne-miner-gui.exe" << "anne-miner*.exe";
#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  filters << "anne-miner" << "anne-miner-gui" << "anne-miner*.app";
#else
  filters << "anne-miner" << "anneminer" << "anne-miner-gui";
#endif

  QStringList files = minerDir.entryList(filters, QDir::Files | QDir::Executable | QDir::NoDotAndDotDot);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  QStringList appBundles = minerDir.entryList(QStringList() << "*.app", QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &appBundle : appBundles) {
    if (appBundle.contains("miner", Qt::CaseInsensitive)) {
      return minerDir.filePath(appBundle);
    }
  }
#endif

  if (!files.isEmpty()) {

    for (const QString &file : files) {
      if (file.contains("gui", Qt::CaseInsensitive)) {
        return minerDir.filePath(file);
      }
    }

    return minerDir.filePath(files.first());
  }

  return QString();
}

bool SystemUtils::createDesktopEntry(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool createDesktopShortcut,
                                     bool createMenuEntry, bool launchOnBoot, bool headless, QStringList &details) {
  bool success = true;

#ifdef Q_OS_LINUX
  success = createLinuxDesktopFile(appName, appId, executable, workingDir, createDesktopShortcut, createMenuEntry, launchOnBoot, headless, details);
#endif

#ifdef Q_OS_WIN
  success = createWindowsShortcut(appName, appId, executable, workingDir, createDesktopShortcut, createMenuEntry, launchOnBoot, headless, details);
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  success = createMacOsIntegration(appName, appId, executable, workingDir, createDesktopShortcut, launchOnBoot, headless, details);
#endif

  return success;
}

#ifdef Q_OS_LINUX
bool SystemUtils::createLinuxDesktopFile(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool menu,
                                         bool autostart, bool headless, QStringList &details) {
  QString exec;

  if (executable.endsWith(".jar")) {

    QString javaBin = findJava();
    exec = QString("env -C \"%1\" %2 -jar \"%3\"").arg(workingDir, javaBin, executable);

    if (headless) {
      QString terminalCmd;
      QStringList terminals = {"gnome-terminal|gnome-terminal -- bash -c %1",
                               "konsole|konsole -e bash -c %1",
                               "xfce4-terminal|xfce4-terminal -x bash -c %1",
                               "xterm|xterm -e bash -c %1",
                               "lxterminal|lxterminal -e bash -c %1",
                               "qterminal|qterminal -e bash -c %1",
                               "terminator|terminator -x bash -c %1",
                               "tilix|tilix -a session-new -x bash -c %1",
                               "kitty|kitty bash -c %1",
                               "alacritty|alacritty -e bash -c %1",
                               "foot|foot bash -c %1",
                               "wezterm|wezterm start -- bash -c %1"};

      for (const QString &entry : terminals) {
        QStringList parts = entry.split('|');
        QString exe = parts[0];
        QString cmd = parts[1];
        if (QFile::exists("/usr/bin/" + exe) || QFile::exists("/usr/local/bin/" + exe) || QFile::exists("/bin/" + exe)) {
          terminalCmd = cmd;
          break;
        }
      }

      if (terminalCmd.isEmpty()) {
        terminalCmd = "bash -c %1";
      }

      QString innerCmd = QString("echo '=== %1 starting ==='; "
                                 "cd \"%2\" && "
                                 "%3 -jar \"%4\" --headless; "
                                 "echo; echo '────────────────────────────────────'; "
                                 "echo '%1 has stopped.'; "
                                 "echo 'Press Enter to close this window...'; read")
                             .arg(appName, workingDir, javaBin, executable);

      QString escaped = innerCmd.replace("\"", "\\\"");
      exec = terminalCmd.arg("\"" + escaped + "\"");
    }
  } else {

    exec = QString("\"%1\"").arg(executable);
  }
  QString iconResource = ":/assets/" + appId + ".png";

  QString iconPath = installUserIcon(appId, iconResource, &details);

  if (iconPath.isEmpty()) {
    iconPath = "applications-utilities";
  }

  QString categories;
  if (appId == "annode") {
    categories = "Network;P2P;";
  } else {
    categories = "Utility;";
  }

  QString content = QString("[Desktop Entry]\n"
                            "Version=1.0\n"
                            "Type=Application\n"
                            "Name=%1\n"
                            "Comment=%2\n"
                            "Exec=%3\n"
                            "Icon=%4\n"
                            "Terminal=%5\n"
                            "Categories=%6\n")
                        .arg(appName)
                        .arg(appName + " - ANNE Network")
                        .arg(exec)
                        .arg(iconPath)
                        .arg(executable.endsWith(".jar") && headless ? "true" : "false")
                        .arg(categories);

  if (autostart && appId == "annode") {
    content += "X-GNOME-Autostart-enabled=true\n";
  }

  const QString fileName = appId + ".desktop";

  auto writeFile = [&details](const QString &path, const QString &data, const QString &desc) -> bool {
    qDebug() << "Creating" << desc << "at:" << path;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      details << QString("Failed to create %1: Cannot open file for writing").arg(desc);
      return false;
    }

    if (f.write(data.toUtf8()) == -1) {
      details << QString("Failed to write %1").arg(desc);
      f.close();
      return false;
    }
    f.close();

    QFile::Permissions perms = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;

    if (!f.setPermissions(perms)) {
      details << QString("%1 created but not executable (permissions failed)").arg(desc);
      return false;
    }

    SystemUtils::chownUser(path, true);
    runGioAsUser(path);

    details << QString("%1 created").arg(desc);
    return true;
  };

  bool allOk = true;
  QString realHome = SystemUtils::getRealUserHome();

  if (desktop) {
    QString path = realHome + "/Desktop/" + fileName;
    allOk &= writeFile(path, content, QString("Desktop shortcut for %1").arg(appName));
  }

  if (menu) {
    QString path = realHome + "/.local/share/applications/" + fileName;
    QDir().mkpath(realHome + "/.local/share/applications");
    allOk &= writeFile(path, content, QString("Applications menu entry for %1").arg(appName));
    QProcess::execute("update-desktop-database", {realHome + "/.local/share/applications"});
  }

  if (autostart && appId == "annode") {
    QString path = realHome + "/.config/autostart/" + fileName;
    QDir().mkpath(realHome + "/.config/autostart");
    allOk &= writeFile(path, content, QString("Autostart entry for %1").arg(appName));
  }

  if (geteuid() == 0) {
    uid_t uid = getRealUserUid();
    if (uid != 0 && uid != static_cast<uid_t>(-1)) {
      struct passwd *pw = getpwuid(uid);
      gid_t gid = pw ? pw->pw_gid : static_cast<gid_t>(-1);

      auto fix = [uid, gid](const QString &path) {
        if (QFile::exists(path)) {
          if (::chown(path.toLocal8Bit().constData(), uid, gid) == -1) {
            qWarning() << "Failed to change ownership of" << path << ":" << strerror(errno);
          }
        }
      };

      if (desktop)
        fix(realHome + "/Desktop/" + fileName);
      if (menu)
        fix(realHome + "/.local/share/applications/" + fileName);
      if (autostart && appId == "annode")
        fix(realHome + "/.config/autostart/" + fileName);
    }
  }
  QProcess::execute("update-desktop-database", {realHome + "/.local/share/applications"});
  return allOk;
}

bool SystemUtils::createLinuxSystemdService(const QString &installDir, bool enable, bool headless, QStringList &details) {
  QString realHome = SystemUtils::getRealUserHome();
  QString serviceDir = realHome + "/.config/systemd/user";
  QDir().mkpath(serviceDir);
  QString servicePath = serviceDir + "/annode.service";
  qDebug() << "Creating systemd service at:" << servicePath;

  QString jarPath = QDir(installDir).absoluteFilePath("anne-node.jar");
  QString exec = "/usr/bin/java -jar \"" + jarPath + "\"";
  if (headless)
    exec += " --headless";

  QString content = "[Unit]\n"
                    "Description=Annode P2P Node\n"
                    "After=graphical-session.target\n"
                    "PartOf=graphical-session.target\n"
                    "\n"
                    "[Service]\n"
                    "Type=simple\n"
                    "ExecStart=" +
                    exec +
                    "\n"
                    "WorkingDirectory=" +
                    installDir +
                    "\n"
                    "Restart=on-failure\n"
                    "RestartSec=10\n"
                    "\n"
                    "[Install]\n"
                    "WantedBy=graphical-session.target\n";

  QFile f(servicePath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    details << "Failed to write systemd service file";
    return false;
  }
  f.write(content.toUtf8());
  f.close();

  f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
  details << "Systemd service file created";

  if (!enable) {
    details << "Autostart via systemd disabled by user";
    return true;
  }

  QProcessResult result = Utils::executeProcess("systemctl", {"--user", "daemon-reload"}, 8000);
  if (!result.QPSuccess || result.exitCode != 0) {
    details << "systemctl daemon-reload failed";
    return false;
  }

  result = Utils::executeProcess("systemctl", {"--user", "enable", "--now", "annode.service"}, 15000);
  if (!result.QPSuccess) {
    details << "systemctl enable --now timed out";
    return false;
  }

  if (result.exitCode == 0) {
    details << "Annode started and enabled at boot (systemd)";
    return true;
  } else {
    details << QString("systemctl failed: %1").arg(result.stdErr.trimmed());
    return false;
  }
}
#endif

#ifdef Q_OS_WIN

void SystemUtils::refreshShortcutIcon(const QString &shortcutPath) {

  std::wstring wpath = shortcutPath.toStdWString();

  SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW | SHCNF_FLUSHNOWAIT, wpath.c_str(), nullptr);

  HWND hwnd = FindWindow(L"Progman", nullptr);
  if (hwnd) {

    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"ShellIconSize", SMTO_ABORTIFHUNG, 1000, nullptr);
  }

  Sleep(100);
}

bool SystemUtils::setWindowsPermissions(const QString &path) {
  qDebug() << "Setting Windows permissions recursively for:" << path;

  if (!QFile::exists(path)) {
    qWarning() << "Path does not exist:" << path;
    return false;
  }

  QString nativePath = QDir::toNativeSeparators(path);

  QStringList removeInheritanceArgs;
  removeInheritanceArgs << nativePath << "/inheritance:r";

  QProcess process1;
  process1.start("icacls", removeInheritanceArgs);

  if (!process1.waitForFinished(30000)) {
    qWarning() << "Remove inheritance command timed out";
    return false;
  }

  if (process1.exitCode() != 0) {
    qWarning() << "Failed to remove inheritance";
    return false;
  }

  qDebug() << "Inheritance removed successfully";

  QStringList grantPermissionsArgs;
  grantPermissionsArgs << nativePath << "/grant:r" << "BUILTIN\\Users:(OI)(CI)F" << "/T";

  QProcess process2;
  process2.start("icacls", grantPermissionsArgs);

  if (!process2.waitForFinished(60000)) {
    qWarning() << "Grant permissions command timed out";
    return false;
  }

  bool success = (process2.exitCode() == 0);

  if (success) {
    qDebug() << "Windows permissions set successfully for:" << path;
  } else {
    qWarning() << "Failed to grant permissions";
  }

  return success;
}

QString SystemUtils::getShortPath(const QString &longPath) {
  wchar_t shortPath[MAX_PATH];
  std::wstring wLongPath = longPath.toStdWString();

  if (GetShortPathNameW(wLongPath.c_str(), shortPath, MAX_PATH) > 0) {
    return QString::fromWCharArray(shortPath);
  }

  qWarning() << "Failed to get short path for:" << longPath << "Error:" << GetLastError();
  return QString();
}

bool SystemUtils::createWindowsShortcut(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool startMenu,
                                        bool autostart, bool headless, QStringList &details) {
  CoInitialize(nullptr);

  QString target = executable;
  QString args = "";

  if (executable.endsWith(".jar")) {

    target = headless ? "java.exe" : "javaw.exe";

    QProcessResult findJavaResult = Utils::executeProcess("where", QStringList() << target, 3000);
    if (findJavaResult.QPSuccess && findJavaResult.exitCode == 0) {
      QString output = findJavaResult.stdOut.trimmed();
      if (!output.isEmpty()) {
        target = output.split('\n').first().trimmed();
      }
    }

    args = QString("-jar \"%1\"").arg(executable);
    if (headless)
      args += " --headless";
  }

  QString iconResource = ":/assets/" + appId + ".ico";

  QString iconPath = installUserIcon(appId, iconResource, &details);

  auto createLnk = [&](const QString &lnkPath) -> bool {
    HRESULT hr;
    IShellLinkW *psl = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&psl);
    if (FAILED(hr))
      return false;

    std::wstring wtarget = target.toStdWString();
    std::wstring wargs = args.toStdWString();
    std::wstring wworkingDir = workingDir.toStdWString();
    std::wstring wdescription = (appName + " - ANNE Network").toStdWString();

    psl->SetPath(wtarget.c_str());
    if (!args.isEmpty()) {
      psl->SetArguments(wargs.c_str());
    }
    psl->SetWorkingDirectory(wworkingDir.c_str());
    psl->SetDescription(wdescription.c_str());

    if (!iconPath.isEmpty() && QFile::exists(iconPath)) {
      std::wstring wiconPath = iconPath.toStdWString();
      psl->SetIconLocation(wiconPath.c_str(), 0);
    }

    IPersistFile *ppf = nullptr;
    hr = psl->QueryInterface(IID_IPersistFile, (void **)&ppf);
    if (SUCCEEDED(hr)) {
      QDir().mkpath(QFileInfo(lnkPath).absolutePath());
      std::wstring wlnkPath = lnkPath.toStdWString();
      hr = ppf->Save(wlnkPath.c_str(), TRUE);
      ppf->Release();
    }
    psl->Release();
    return SUCCEEDED(hr);
  };

  bool ok = true;
  QString shortcutName = appName + ".lnk";

  if (desktop) {
    QString path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + shortcutName;
    QFile::remove(path);
    if (createLnk(path)) {
      details << QString("Desktop shortcut for %1 created").arg(appName);
      refreshShortcutIcon(path);
    } else {
      details << QString("Failed to create desktop shortcut for %1").arg(appName);
      ok = false;
    }
  }

  if (startMenu) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir().mkpath(dir);
    QString path = dir + "/" + shortcutName;
    QFile::remove(path);
    if (createLnk(path)) {
      details << QString("Start Menu entry for %1 created").arg(appName);
      refreshShortcutIcon(path);
    } else {
      details << QString("Failed to create Start Menu entry for %1").arg(appName);
      ok = false;
    }
  }

  if (autostart && appId == "annode") {
    wchar_t *startupPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_Startup, 0, nullptr, &startupPath);
    if (SUCCEEDED(hr) && startupPath) {
      QString path = QString::fromWCharArray(startupPath) + "\\" + shortcutName;
      CoTaskMemFree(startupPath);
      QDir().mkpath(QFileInfo(path).absolutePath());
      QFile::remove(path);

      if (createLnk(path)) {
        details << QString("Autostart entry for %1 created").arg(appName);
        refreshShortcutIcon(path);
      } else {
        details << QString("Failed to create autostart entry for %1").arg(appName);
        ok = false;
      }
    } else {
      details << QString("Failed to create autostart entry for %1").arg(appName);
      ok = false;
    }
  }

  CoUninitialize();
  return ok;
}

#endif

QString SystemUtils::installUserIcon(const QString &appId, const QString &resourcePath, QStringList *details) {
#ifdef Q_OS_LINUX
  QString iconName = appId + ".png";

  QString target = SystemUtils::getRealUserHome() + "/.local/share/icons/" + iconName;
  QDir().mkpath(QFileInfo(target).absolutePath());

  if (QFile::exists(target)) {

    QString backup = target + ".old";
    if (!QFile::rename(target, backup)) {

      QFile::remove(target);
    } else {

      QFile::remove(backup);
    }
  }

  bool copied = false;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (QFile::copy(resourcePath, target)) {
      copied = true;
      break;
    }
    if (attempt == 0) {
      QThread::msleep(50);
    }
  }

  if (copied) {

    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);

    if (geteuid() == 0) {
      uid_t targetUid = getRealUserUid();
      if (targetUid != 0 && targetUid != static_cast<uid_t>(-1)) {
        struct passwd *pw = getpwuid(targetUid);
        gid_t targetGid = pw ? pw->pw_gid : targetUid;
        ::chown(target.toUtf8().constData(), targetUid, targetGid);
      }
    }

    QProcess::startDetached("gtk-update-icon-cache", {"-f", "-t", QFileInfo(target).absolutePath()});

    return target;
  }

  QDir iconDir(QFileInfo(target).absolutePath());
  QStringList existingIcons = iconDir.entryList(QStringList() << appId + "*.png", QDir::Files);
  for (const QString &existingIcon : existingIcons) {
    QString fullPath = iconDir.absoluteFilePath(existingIcon);
    if (QFileInfo(fullPath).size() > 0 && !existingIcon.endsWith(".old")) {
      return fullPath;
    }
  }

  return "applications-utilities";

#elif defined(Q_OS_WIN)
  QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QString iconDir = appDataDir + "/ANNE/icons";
  QString iconName = appId + ".ico";

  QString iconPath = iconDir + "/" + iconName;
  QDir().mkpath(iconDir);

  if (QFile::exists(iconPath)) {
    QString backup = iconPath + ".old";

    if (!QFile::rename(iconPath, backup)) {

      QFile::remove(iconPath);
    }

    QFile::remove(backup);
  }

  bool copied = false;
  for (int attempt = 0; attempt < 2; attempt++) {
    QFile resource(resourcePath);
    if (resource.copy(iconPath)) {
      copied = true;
      break;
    }
    if (attempt == 0) {
      QThread::msleep(50);
    }
  }

  if (copied) {

    QFile::setPermissions(iconPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    return iconPath;
  }

  QDir iconDirObj(iconDir);
  QStringList existingIcons = iconDirObj.entryList(QStringList() << appId + "*.ico", QDir::Files);
  for (const QString &existingIcon : existingIcons) {
    QString fullPath = iconDir + "/" + existingIcon;
    if (QFileInfo(fullPath).size() > 0 && !existingIcon.endsWith(".old")) {
      return fullPath;
    }
  }

  return QString();

#elif defined(Q_OS_MACOS) || defined(Q_OS_MAC)
  return ":/assets/" + appId + ".icns";
#endif
}

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
bool SystemUtils::createMacOsIntegration(const QString &appName, const QString &appId, const QString &executable, const QString &workingDir, bool desktop, bool autostart,
                                         bool headless, QStringList &details) {
  QString appPath = QDir::homePath() + "/Applications/" + appName + ".app";
  QString contents = appPath + "/Contents";
  QString macos = contents + "/MacOS";
  QString resources = contents + "/Resources";

  QDir().mkpath(resources);

  QString iconFile = appId + ".icns";
  QString iconResource = ":/assets/" + appId + ".icns";
  QString iconPath = resources + "/" + iconFile;

  if (QFile::exists(iconResource)) {
    if (!QFile::copy(iconResource, iconPath)) {
      qDebug() << "Failed to copy icon for " << appName;
    }
  }

  QString script;
  if (executable.endsWith(".jar")) {

    script = QString("#!/bin/bash\n"
                     "cd \"%1\"\n"
                     "# Set the Java Dock icon and name for macOS\n"
                     "if [[ \"$OSTYPE\" == \"darwin\"* ]]; then\n"
                     "    # -Xdock options must be passed directly to java command\n"
                     "    /usr/bin/java -Xdock:name=\"%4\" -Xdock:icon=\"%5\" -jar \"%2\"%3\n"
                     "else\n"
                     "    # For non-macOS systems (though this shouldn't happen)\n"
                     "    /usr/bin/java -jar \"%2\"%3\n"
                     "fi\n")
                 .arg(workingDir)
                 .arg(QFileInfo(executable).fileName())
                 .arg(headless ? " --headless" : "")
                 .arg(appName)
                 .arg(iconPath);
  } else if (executable.endsWith(".app")) {

    script = QString("#!/bin/bash\n"
                     "open \"%1\"\n")
                 .arg(executable);
  } else {

    script = QString("#!/bin/bash\n"
                     "cd \"%1\"\n"
                     "\"./%2\"\n")
                 .arg(workingDir)
                 .arg(QFileInfo(executable).fileName());
  }

  QFile scriptFile(macos + "/" + appName);
  QDir().mkpath(macos);
  if (!scriptFile.open(QIODevice::WriteOnly)) {
    details << QString("Failed to create macOS app for %1").arg(appName);
    return false;
  }
  scriptFile.write(script.toUtf8());
  scriptFile.setPermissions(scriptFile.permissions() | QFile::ExeOwner);
  scriptFile.close();

  QString plist = QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                          "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                          "<plist version=\"1.0\"><dict>\n"
                          "  <key>CFBundleExecutable</key><string>%1</string>\n"
                          "  <key>CFBundleName</key><string>%1</string>\n"
                          "  <key>CFBundleIconFile</key><string>%2</string>\n"
                          "  <key>CFBundleIdentifier</key><string>com.anne.%3</string>\n"
                          "  <key>CFBundleDisplayName</key><string>%1</string>\n"
                          "  <key>CFBundlePackageType</key><string>APPL</string>\n"
                          "  <key>CFBundleVersion</key><string>1.0</string>\n"
                          "  <key>CFBundleShortVersionString</key><string>1.0</string>\n"
                          "  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>\n"
                          "  <key>NSHighResolutionCapable</key><true/>\n")
                      .arg(appName)
                      .arg(iconFile)
                      .arg(appId);

  if (headless && appId == "annode") {
    plist += "  <key>LSUIElement</key><true/>\n";
  }

  plist += "</dict></plist>\n";

  QFile plistFile(contents + "/Info.plist");
  if (!plistFile.open(QIODevice::WriteOnly)) {
    details << QString("Failed to create Info.plist for %1").arg(appName);
    return false;
  }
  plistFile.write(plist.toUtf8());
  plistFile.close();

  QFile pkgInfoFile(contents + "/PkgInfo");
  if (pkgInfoFile.open(QIODevice::WriteOnly)) {
    pkgInfoFile.write("APPL????");
    pkgInfoFile.close();
  }

  if (desktop) {
    QString desktopLink = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + "/" + appName + ".app";
    QFile::remove(desktopLink);
    if (QFile::link(appPath, desktopLink)) {
      details << QString("Desktop shortcut for %1 created").arg(appName);
    } else {

      QProcess::execute("/bin/rm", {"-rf", desktopLink});
      QProcess::execute("/bin/cp", {"-R", appPath, desktopLink});
      details << QString("Desktop shortcut for %1 created (copied)").arg(appName);
    }
  }

  if (autostart && appId == "annode") {
    QString command = QString("tell application \"System Events\" to make login item at end "
                              "with properties {path:\"%1\", hidden:%2}")
                          .arg(appPath)
                          .arg(headless ? "true" : "false");

    QProcess process;
    process.start("osascript", {"-e", command});
    if (process.waitForFinished(5000) && process.exitCode() == 0) {
      details << QString("Autostart entry for %1 created").arg(appName);
    } else {
      details << QString("Failed to create autostart entry for %1").arg(appName);
    }
  }

  QProcess::execute("/usr/bin/touch", {appPath});
  QProcess::execute("/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister", {"-f", appPath});

  details << QString("macOS application for %1 created with proper Dock icon").arg(appName);
  return true;
}
#endif

#ifdef Q_OS_UNIX
QString SystemUtils::findJava() {
  QProcessResult result = Utils::executeProcess("whereis", QStringList() << "java", 3000);
  QString output = result.stdOut.trimmed();
  QStringList parts = output.split(' ', Qt::SkipEmptyParts);

  if (parts.size() >= 2) {
    QString path = parts[1];
    if (QFileInfo::exists(path)) {
      return path;
    }
  }

  return "java";
}

uid_t SystemUtils::getRealUserUid() {
  QByteArray sudo = qgetenv("SUDO_UID");
  if (!sudo.isEmpty())
    return sudo.toUInt();

  QByteArray pkexec = qgetenv("PKEXEC_UID");
  if (!pkexec.isEmpty())
    return pkexec.toUInt();

  QByteArray logname = qgetenv("LOGNAME");
  if (!logname.isEmpty() && logname != "root") {
    struct passwd *pw = getpwnam(logname.constData());
    if (pw)
      return pw->pw_uid;
  }

  return geteuid();
}

gid_t SystemUtils::getRealUserGid() {
  QByteArray sudoGid = qgetenv("SUDO_GID");
  if (!sudoGid.isEmpty())
    return sudoGid.toUInt();

  uid_t uid = getRealUserUid();
  if (uid != 0 && uid != static_cast<uid_t>(-1)) {
    struct passwd *pw = getpwuid(uid);
    if (pw)
      return pw->pw_gid;
  }

  return getegid();
}

void SystemUtils::chownUser(QString targetFile, bool chmod) {
  if (geteuid() == 0) {
    uid_t targetUid = getRealUserUid();
    if (targetUid != 0 && targetUid != static_cast<uid_t>(-1)) {
      struct passwd *pw = getpwuid(targetUid);
      gid_t targetGid = pw ? pw->pw_gid : targetUid;

      QFileInfo fileInfo(targetFile);

      if (fileInfo.isDir()) {
        QString cmd = QString("chown -R %1:%2 \"%3\"").arg(targetUid).arg(targetGid).arg(targetFile);
        int result = system(cmd.toUtf8().constData());
        if (result != 0) {
          qWarning() << "chown command may have failed for directory" << targetFile;
        }

        if (chmod) {
          chmodRecursive(targetFile);
        }
      } else {
        if (::chown(targetFile.toUtf8().constData(), targetUid, targetGid) == -1) {
          qWarning() << "Failed to chown file" << targetFile << "error:" << strerror(errno);
        }

        if (chmod) {
          chmodSingleFile(targetFile);
        }
      }
    }
  }
}

void SystemUtils::chmodSingleFile(const QString &filePath) {
  QFileInfo fileInfo(filePath);
  mode_t mode = 0664;

  if (fileInfo.isExecutable() || shouldBeExecutable(filePath)) {
    mode = 0775;
  }

  if (::chmod(filePath.toUtf8().constData(), mode) == -1) {
    qWarning() << "Failed to chmod file" << filePath << "error:" << strerror(errno);
  }
}

void SystemUtils::chmodRecursive(const QString &dirPath) {
  QDir dir(dirPath);

  if (::chmod(dirPath.toUtf8().constData(), 0775) == -1) {
    qWarning() << "Failed to chmod directory" << dirPath << "error:" << strerror(errno);
  }

  QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);

  for (const QFileInfo &entry : entries) {
    QString entryPath = entry.absoluteFilePath();

    if (entry.isDir()) {

      chmodRecursive(entryPath);
    } else if (entry.isFile()) {

      chmodSingleFile(entryPath);
    }
  }
}

bool SystemUtils::shouldBeExecutable(const QString &filePath) {

  QFileInfo fileInfo(filePath);
  QString suffix = fileInfo.suffix().toLower();
  QString fileName = fileInfo.fileName().toLower();

  static const QStringList executableExtensions = {"sh", "bash", "py", "pl", "rb", "exe", "bin", "app", "run", "jar", "miner", "hasher"};

  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QByteArray firstLine = file.readLine(100);
    if (firstLine.startsWith("#!")) {
      file.close();
      return true;
    }
    file.close();
  }

  if (executableExtensions.contains(suffix)) {
    return true;
  }

  if (fileName.contains("install") || fileName.contains("setup") || fileName.contains("start") || fileName.contains("stop") || fileName.contains("script") ||
      fileName.startsWith("run") || fileName.endsWith(".so") || fileName.endsWith(".dylib")) {
    return true;
  }

  return false;
}
bool SystemUtils::runGioAsUser(const QString &path) {
  if (geteuid() == 0) {
    uid_t uid = getRealUserUid();
    if (uid == 0 || uid == static_cast<uid_t>(-1))
      return false;

    struct passwd *pw = getpwuid(uid);
    if (!pw)
      return false;

    QString username = QString::fromLocal8Bit(pw->pw_name);
    QString dbusPath = QString("/run/user/%1/bus").arg(uid);

    QString command = QString("DBUS_SESSION_BUS_ADDRESS=unix:path=%1 gio set \"%2\" metadata::trusted true").arg(dbusPath, path);
    QStringList args{"-", username, "-c", command};
    return QProcess::startDetached("su", args);
  } else {
    return QProcess::startDetached("gio", {"set", path, "metadata::trusted", "true"});
  }
}
#endif

QString SystemUtils::findBrew() {
  QString brewPath = QStandardPaths::findExecutable("brew");
  if (!brewPath.isEmpty() && QFile::exists(brewPath)) {
    return brewPath;
  }

  return QString();
}
