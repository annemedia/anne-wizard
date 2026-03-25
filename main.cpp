#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef Q_OS_UNIX
#include <sys/types.h>
#include <unistd.h>
#endif

#include <QAbstractButton>
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QGroupBox>
#include <QGuiApplication>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QRect>
#include <QScreen>
#include <QSharedMemory>
#include <QSize>
#include <QStyleFactory>
#include <QTemporaryFile>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <fstream>
#include "utils/systemutils.h"
#include "wizard.h"

static QString getAskpassScriptPath() {
  const QString scriptPath = QDir::tempPath() + "/annewizard_askpass.sh";

  QFile file(scriptPath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {

    QString binDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    QString libPath = binDir + "/lib";
    if (!QDir(libPath).exists()) {
      libPath = binDir;
    }

    const QString content = "#!/bin/sh\n"

                            "export LD_LIBRARY_PATH=\"" +
                            libPath +
                            ":$LD_LIBRARY_PATH\"\n"
                            "# ANNE Wizard sudo-askpass helper – auto-generated\n"
                            "\"" +
                            QCoreApplication::applicationFilePath().toUtf8() + "\" --askpass\n";

    file.write(content.toUtf8());
    file.close();

    QFile::setPermissions(scriptPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  } else {
    qWarning() << "Failed to create askpass script:" << scriptPath << file.errorString();
  }

  return scriptPath;
}

static bool requestElevation(const QStringList &originalArgs) {
  if (originalArgs.contains("--elevated")) {
    return true;
  }
  QString binary = QCoreApplication::applicationFilePath();

#ifdef Q_OS_WIN
  bool isElevated = false;
  HANDLE hToken = NULL;
  if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
    TOKEN_ELEVATION elevation;
    DWORD dwSize;
    if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
      isElevated = elevation.TokenIsElevated;
    }
    CloseHandle(hToken);
  }
  if (isElevated) {
    return true;
  }

  QStringList elevatedArgs = originalArgs;
  elevatedArgs.removeFirst();

  if (!elevatedArgs.contains("--elevated")) {
    elevatedArgs.prepend("--elevated");
  }

  QString parameters;

  for (const QString &arg : elevatedArgs) {
    if (!parameters.isEmpty())
      parameters += " ";

    if (arg.contains(" ") || arg.contains("\t")) {
      QString escapedArg = arg;
      escapedArg.replace("\"", "\\\"");
      parameters += "\"" + escapedArg + "\"";
    } else {
      parameters += arg;
    }
  }

  SHELLEXECUTEINFOW sei = {sizeof(sei)};
  sei.lpVerb = L"runas";
  sei.lpFile = (LPCWSTR)binary.utf16();
  sei.lpParameters = parameters.isEmpty() ? nullptr : (LPCWSTR)parameters.utf16();
  sei.nShow = SW_NORMAL;
  sei.fMask = SEE_MASK_NOCLOSEPROCESS;

  if (ShellExecuteExW(&sei)) {

    if (sei.hProcess) {
      WaitForSingleObject(sei.hProcess, 1500);
      CloseHandle(sei.hProcess);
    }
    return false;
  }

  DWORD error = GetLastError();
  qDebug() << "ShellExecuteEx failed with error:" << error;

  if (error != ERROR_CANCELLED) {
    QMessageBox::critical(nullptr, "Error",
                          "Administrator privileges are required to run ANNE Wizard.\n\n"
                          "Please click 'Yes' in the User Account Control dialog.");
  }
  return false;

#elif defined(Q_OS_MACOS)

  qDebug() << "macOS: Skipping full-app elevation (will elevate per-operation)";

  if (geteuid() == 0) {
    qDebug() << "✓ Already running as root (user ran with sudo)";
    QString sudoUser = qgetenv("SUDO_USER");
    if (!sudoUser.isEmpty()) {
      qputenv("USER", sudoUser.toUtf8());
      qputenv("HOME", QString("/Users/%1").arg(sudoUser).toUtf8());
      qputenv("LOGNAME", sudoUser.toUtf8());
    }
  } else {
    qDebug() << "Running as normal user (euid =" << geteuid() << ")";
    qDebug() << "Java/MariaDB installation will prompt for password when needed";
  }

  return true;

#else

  const QString askpassPath = getAskpassScriptPath();

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

  QString xauth = qEnvironmentVariable("XAUTHORITY");
  if (!xauth.isEmpty() && QFile::exists(xauth)) {
    env.insert("XAUTHORITY", xauth);
  } else {

    QString userHome = qEnvironmentVariable("HOME");
    xauth = userHome + "/.Xauthority";
    if (QFile::exists(xauth)) {
      env.insert("XAUTHORITY", xauth);
    }
  }

  env.insert("DISPLAY", qEnvironmentVariable("DISPLAY"));
  env.insert("XDG_CURRENT_DESKTOP", qEnvironmentVariable("XDG_CURRENT_DESKTOP"));

  env.insert("SUDO_ASKPASS", askpassPath);
  env.insert("SUDO_PROMPT", "");

  QString elevatedBinary = binary;
  QString appImagePath = qEnvironmentVariable("APPIMAGE");
  if (!appImagePath.isEmpty()) {
    elevatedBinary = appImagePath;
    qDebug() << "AppImage mode: Relaunching" << appImagePath;
  }

  QStringList sudoArgs;
  sudoArgs << "-E" << "-A" << "--" << elevatedBinary << "--elevated";

  for (int i = 1; i < originalArgs.size(); ++i) {
    sudoArgs << originalArgs.at(i);
  }

  QProcess p;
  p.setProgram("sudo");
  p.setArguments(sudoArgs);
  p.setProcessEnvironment(env);

  if (p.startDetached()) {
    qDebug() << "Elevated launch success";
    return false;
  }

  qDebug() << "Sudo failed:" << p.errorString();
  QMessageBox::critical(nullptr, "Error", "Administrator privileges required");
  return false;
#endif
}

static void applyDarkTheme(QApplication &app) {

  app.setStyle(QStyleFactory::create("Fusion"));

  class MessageBoxAutoSizer : public QObject {
  public:
    bool eventFilter(QObject *obj, QEvent *event) override {
      if (event->type() == QEvent::Show && qobject_cast<QMessageBox *>(obj)) {
        QTimer::singleShot(0, obj, [obj]() {
          if (auto *msgBox = qobject_cast<QMessageBox *>(obj)) {

            QFontMetrics fm(msgBox->font());
            QString text = msgBox->text();

            int lines = text.count('\n') + 1;
            int textWidth = fm.horizontalAdvance(text);
            if (textWidth > 400) {
              lines += (textWidth / 400);
            }

            int neededHeight = qMax(150, lines * 25 + 80);
            msgBox->setMinimumHeight(neededHeight);
          }
        });
      }
      return false;
    }
  };

  static MessageBoxAutoSizer *sizer = new MessageBoxAutoSizer();
  app.installEventFilter(sizer);

  QFont defaultFont;

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)

  QFont systemFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);

  int targetPointSize = qMax(11, systemFont.pointSize());
  defaultFont.setPointSize(targetPointSize);

#else

  defaultFont.setFamily("Segoe UI");
  defaultFont.setPointSize(9);
#endif

  app.setFont(defaultFont);

  QFont widgetFont = defaultFont;

  QPalette dark;
  dark.setColor(QPalette::Window, QColor(17, 17, 17));
  dark.setColor(QPalette::WindowText, Qt::white);
  dark.setColor(QPalette::Base, QColor(25, 25, 25));
  dark.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
  dark.setColor(QPalette::ToolTipBase, QColor(25, 25, 25));
  dark.setColor(QPalette::ToolTipText, Qt::white);
  dark.setColor(QPalette::Text, Qt::white);
  dark.setColor(QPalette::Button, QColor(64, 64, 64));
  dark.setColor(QPalette::ButtonText, Qt::white);
  dark.setColor(QPalette::BrightText, Qt::red);
  dark.setColor(QPalette::Link, QColor(42, 130, 218));
  dark.setColor(QPalette::Highlight, QColor(42, 130, 218));
  dark.setColor(QPalette::HighlightedText, Qt::black);
  dark.setColor(QPalette::PlaceholderText, QColor(200, 200, 200));

  dark.setColor(QPalette::Disabled, QPalette::Button, QColor(45, 45, 45));
  dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(100, 100, 100));
  dark.setColor(QPalette::Disabled, QPalette::Text, QColor(130, 130, 130));
  dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(130, 130, 130));

  app.setPalette(dark);
}

static void centerWindow(QWidget *window, int delayMs = 0) {
  if (!window)
    return;

  if (delayMs > 0) {
    QTimer::singleShot(delayMs, window, [window]() { centerWindow(window, 0); });
    return;
  }

  if (!window->isVisible()) {
    window->show();
  }

  QScreen *screen = window->screen();
  if (!screen) {
    screen = QGuiApplication::primaryScreen();
  }

  if (screen) {
    QRect screenGeom = screen->availableGeometry();
    QRect windowGeom = window->frameGeometry();

    int x = screenGeom.x() + (screenGeom.width() - windowGeom.width()) / 2;
    int y = screenGeom.y() + (screenGeom.height() - windowGeom.height()) / 2;

    window->move(x, y);
  }
}

#ifdef Q_OS_LINUX
static void forceGnomeShellRefresh(const QString &userName) {
  qDebug() << "Forcing GNOME Shell refresh for user:" << userName;

  QString userApplications = "/home/" + userName + "/.local/share/applications";
  if (QDir(userApplications).exists()) {
    QProcess::startDetached("sudo", {"-u", userName, "update-desktop-database", userApplications});
  }

  QString desktopPath = "/home/" + userName + "/.local/share/applications/annewizard_portable.desktop";
  if (QFile::exists(desktopPath)) {
    QProcess::execute("sudo", {"-u", userName, "touch", desktopPath});
  }
  SystemUtils::chownUser(desktopPath);


  QString userIconsDir = "/home/" + userName + "/.local/share/icons";
  if (QDir(userIconsDir).exists()) {
    QProcess::startDetached("sudo", {"-u", userName, "gtk-update-icon-cache", "-f", "-t", userIconsDir});
  }

  qDebug() << "GNOME Shell refresh commands executed";
}
#endif

int main(int argc, char *argv[]) {

#ifdef Q_OS_WIN
#ifndef NDEBUG
  if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    std::ios::sync_with_stdio();
  }
#endif
#endif

  QStringList cmdLineArgs;
  for (int i = 0; i < argc; ++i) {
    cmdLineArgs << QString::fromLocal8Bit(argv[i]);
  }

  QLoggingCategory::defaultCategory()->setEnabled(QtDebugMsg, true);
  QLoggingCategory::defaultCategory()->setEnabled(QtWarningMsg, true);

  qSetMessagePattern("%{time yyyy-MM-dd hh:mm:ss.zzz} "
                     "%{if-debug}DEBUG%{endif}"
                     "%{if-warning}WARN %{endif}"
                     "%{if-critical}CRITICAL%{endif}"
                     "%{if-fatal}FATAL%{endif} "
                     "%{function}: %{message}");

  QApplication app(argc, argv);

  qDebug() << "=== ANNE Wizard starting ===";
  qDebug() << "Arguments:" << QCoreApplication::arguments();
  qDebug() << "App path:" << QCoreApplication::applicationFilePath();

  applyDarkTheme(app);

#ifdef Q_OS_LINUX
  if (getuid() == 0) {
    QString safeDir = QDir::tempPath() + "/annewizard-root-" + QString::number(getpid());
    QDir().mkpath(safeDir);
    qputenv("XDG_RUNTIME_DIR", safeDir.toUtf8());
    qputenv("XDG_CACHE_HOME", safeDir.toUtf8());

    QString userName = qEnvironmentVariable("SUDO_USER");
    if (userName.isEmpty())
      userName = qEnvironmentVariable("USER");
    if (userName.isEmpty() || userName == "root")
      userName = qEnvironmentVariable("LOGNAME");
    qDebug() << "MAIN.CPP userName:" << userName;
    if (!userName.isEmpty() && userName != "root") {
      QString userHome = SystemUtils::getRealUserHome();
      qDebug() << "MAIN.CPP userHome:" << userHome;
      if (!QFile::exists(userHome + "/.local")) {
        userHome = "/home/" + userName;
      }

      QString userLocalShare = userHome + "/.local/share";
      QString userApplications = userLocalShare + "/applications";
      QString userIconsDir = userLocalShare + "/icons";

      QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
      if (!dataDirs.contains(userLocalShare)) {
        if (dataDirs.isEmpty())
          dataDirs = "/usr/local/share/:/usr/share/";
        dataDirs = userLocalShare + ":" + dataDirs;
        qputenv("XDG_DATA_DIRS", dataDirs.toUtf8());
      }
      qputenv("XDG_DATA_HOME", userLocalShare.toUtf8());

      QDir().mkpath(userApplications);
      QDir().mkpath(userIconsDir);
      SystemUtils::chownUser(userApplications);
      SystemUtils::chownUser(userIconsDir);
      QString fullBinaryPath = QCoreApplication::applicationFilePath();
      QString escapedPath = fullBinaryPath.replace(" ", "\\ ");

      QString desktopFilePath = userApplications + "/annewizard_portable.desktop";

      QFile f(desktopFilePath);
      if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&f);
        out << "[Desktop Entry]\n";
        out << "Name=ANNE Wizard\n";
        out << "Comment=ANNE Network Node Setup Wizard\n";
        out << "Exec=" << escapedPath << " %U\n";
        out << "Icon=annewizard_portable\n";
        out << "Type=Application\n";
        out << "Terminal=false\n";
        out << "Categories=Utility;System;\n";
        out << "StartupWMClass=ANNEWizard\n";
        out << "StartupNotify=true\n";
        f.close();

        QFile::setPermissions(desktopFilePath, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);

        qDebug() << "Desktop file created:" << desktopFilePath;

        SystemUtils::chownUser(desktopFilePath);
      }

      QString iconPath = userIconsDir + "/annewizard_portable.png";
      if (!QFile::exists(iconPath)) {
        QIcon appIcon(":/assets/annewizard.png");
        if (!appIcon.isNull()) {
          QPixmap pm = appIcon.pixmap(128, 128);
          pm.save(iconPath, "PNG", 100);
          qDebug() << "Icon created:" << iconPath;
        }
      }

#ifdef Q_OS_UNIX
      SystemUtils::chownUser(iconPath);
#endif
      qputenv("XDG_CONFIG_HOME", (userHome + "/.config").toUtf8());

      QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP", "").toLower();
      QString session = qEnvironmentVariable("DESKTOP_SESSION", "").toLower();

      if (desktop.contains("gnome") || session.contains("gnome")) {
        qDebug() << "GNOME desktop detected, forcing shell refresh";
        forceGnomeShellRefresh(userName);
      } else {
        qDebug() << "Non-GNOME desktop:" << desktop << session << "- skipping GNOME refresh";
      }
    }
  }
#endif

  app.setApplicationName("ANNE Wizard");
  app.setApplicationDisplayName("ANNE Wizard");
  app.setOrganizationName("ANNE");
  app.setOrganizationDomain("anne.network");
  app.setDesktopFileName("annewizard_portable.desktop");

  QIcon appIcon(":/assets/annewizard.png");
  app.setWindowIcon(appIcon);

  QApplication::setWindowIcon(appIcon);

  bool isAskpass = false;
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--askpass") == 0) {
      isAskpass = true;
      break;
    }
  }

  if (isAskpass) {
    QDialog dialog;
    dialog.setWindowTitle("ANNE Wizard");
    dialog.setWindowIcon(appIcon);

    dialog.setModal(true);
    dialog.resize(460, 360);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 20);

    QLabel *mainLabel = new QLabel("Authentication is required to run <b>ANNE "
                                   "Wizard</b> as administrator");
    mainLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    mainLabel->setAlignment(Qt::AlignCenter);
    mainLabel->setWordWrap(true);
    layout->addWidget(mainLabel);

    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
      user = qEnvironmentVariable("LOGNAME");
    if (user.isEmpty())
      user = "user";

    QLabel *userLabel = new QLabel(QString("Authenticating as %1").arg(user));
    userLabel->setStyleSheet("color: #d3d3d3; font-size: 12px;");
    userLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(userLabel);

    layout->addSpacing(10);

    QLineEdit *passEdit = new QLineEdit;
    passEdit->setEchoMode(QLineEdit::Password);
    passEdit->setPlaceholderText("Enter password");
    passEdit->setMinimumWidth(300);
    layout->addWidget(passEdit, 0, Qt::AlignCenter);

    layout->addSpacing(15);

    QGroupBox *detailsBox = new QGroupBox("ANNE Wizard requires administrator privileges to "
                                          "perform the following:");

    detailsBox->setStyleSheet("QGroupBox::title {"
                              "  font-size: 12px;"
                              "  font-weight: bold;"
                              "  color: palette(text);"
                              "}"
                              "QLabel { font-size: 13px; color: #d3d3d3; }");
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsBox);
    QLabel *detailsText = new QLabel("• Install or upgrade Java runtime\n"
                                     "• Install or upgrade MariaDB database\n"
                                     "• Install or upgrade annode components\n"
                                     "• Install extras (optional)\n"
                                     "• Install desktop shortcut and menu entries");

    detailsText->setWordWrap(true);
    detailsText->setStyleSheet("color: #fefefe;");
    detailsLayout->addWidget(detailsText);
    layout->addWidget(detailsBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);

    QList<QAbstractButton *> buttonList = buttons->buttons();
    for (QAbstractButton *btn : buttonList) {
      if (buttons->buttonRole(btn) == QDialogButtonBox::AcceptRole) {
        btn->setText("Authenticate");
        break;
      }
    }

    QObject::connect(buttons, &QDialogButtonBox::accepted, [&]() {
      printf("%s\n", passEdit->text().toUtf8().constData());
      fflush(stdout);
      dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    dialog.show();
    centerWindow(&dialog, 10);
    dialog.exec();
    return 0;
  }
  if (!requestElevation(cmdLineArgs)) {
    qDebug() << "DEBUG: Skipping elevation for debugging";
    return 0;
  }

  Wizard wizard;

  wizard.setWindowTitle("ANNE Wizard");
  wizard.setWindowIcon(appIcon);

  wizard.setObjectName("ANNEWizard");
  wizard.setAccessibleName("ANNE Wizard");
  wizard.setAccessibleDescription("ANNE Network Node Setup Wizard");

  wizard.setWindowFlags(wizard.windowFlags() & ~Qt::WindowContextHelpButtonHint);
  wizard.setFixedSize(580, 620);

  wizard.show();
  centerWindow(&wizard, 10);

  QTimer::singleShot(100, [&wizard, appIcon]() {
    wizard.setWindowIcon(appIcon);
    if (wizard.windowHandle()) {
      wizard.windowHandle()->setIcon(appIcon);
    }
  });
#ifdef Q_OS_UNIX
  qDebug() << "=== ELEVATION DEBUG ===";
  qDebug() << "getuid():" << getuid();
  qDebug() << "geteuid():" << geteuid();
  qDebug() << "SUDO_USER:" << qgetenv("SUDO_USER");
  qDebug() << "Running as root?" << (geteuid() == 0 ? "YES" : "NO");
#endif
  return app.exec();
}

