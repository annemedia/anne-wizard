#ifndef JAVAUTILS_H
#define JAVAUTILS_H
#include "osinfo.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSysInfo>

struct JavaCheckResult {
    bool isJavaAvailable;        
    bool isCompleteJDK;          
    QString version;             
    int majorVersion;            
    QString javaPath;            
    QString javacPath;           
};
struct JavaPackageInfo {
  QString packageName;
  QString displayName;
  int majorVersion;
  bool isInstalled;
  bool isDefault;
  QString installedVersion;

  JavaPackageInfo() : majorVersion(0), isInstalled(false), isDefault(false) {}
  JavaPackageInfo(const QString &pkg, const QString &display, int major)
      : packageName(pkg), displayName(display), majorVersion(major),
        isInstalled(false), isDefault(false) {}
};

class JavaUtils : public QObject {
  Q_OBJECT

public:
  explicit JavaUtils(QObject *parent = nullptr);
  static JavaCheckResult checkSystemJava();
  static bool verifyJDKInstallation(const QString &packageName, PkgManagerType type);
  static QList<JavaPackageInfo> getAvailableJavaPackages(PkgManagerType type);

  static QString getCurrentDefaultJava();
  static bool setJavaDefault(const QString &packageName, PkgManagerType type);
  static bool tryAutoSelectJava(const QString &version);

private:
  static QList<JavaPackageInfo>
  getAvailableJavaPackagesLinux(PkgManagerType type);
  static QList<JavaPackageInfo> getAvailableJavaPackagesMacOS();
  static QList<JavaPackageInfo> getAvailableJavaPackagesWindows();
};

#endif
