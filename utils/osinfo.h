#ifndef OSINFO_H
#define OSINFO_H

#include <QString>

enum class PkgManagerType {
    Unknown,
    Dnf,
    Apt,
    Pacman,
    Winget,
    Homebrew
};

struct OSInfo {
    QString osType;
    bool isLinux = false;
    PkgManagerType pkgType = PkgManagerType::Unknown;
    QString ptype = "unknown";
    QString pkgVersion;
    QString codename = "unknown";
    QString arch;
    bool pkgManagerAvailable = false;
};

OSInfo detectOSInfo();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
void setupMacOSEnvironment();
#endif
const OSInfo& detectedOSInfo();
QString pkgManagerTypeToString(PkgManagerType type);

#endif