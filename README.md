 <img width="80" height="80" src="https://8upload.com/image/e90a1dc58217981b/annewizard.png" alt="image">
 
#  ANNE Wizard

[![License](https://img.shields.io/badge/license-Unlicense-blue.svg)](LICENSE)  
[![Qt6](https://img.shields.io/badge/Qt-6.10+-green.svg)](https://www.qt.io)  
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)]()

**ANNE Wizard** is a cross-platform installation suite that automates the setup of the ANNE ecosystem. It handles dependencies installation, database configuration with a pre‑imported chain snapshot, firewall rules, desktop shortcuts, and more. All in a single graphical wizard.

## Features

- **Easy installation** of ANNE Node, ANNE Hasher, and ANNE Miner.
- **Automated dependency handling** (Java, MariaDB) using native package managers.
- **Datachain snapshot import** – downloads and imports the latest chain snapshot into MariaDB.
- **Automatic configuration** – sets up `node.properties`, miner settings (`config.yaml`), firewall rules (port 9115), and desktop/menu shortcuts.
- **Cross‑platform** – runs on Linux (dnf/apt/pacman), and macOS 10.15+ (Catalina+), and Windows 10/11

*For non-supported platforms, or if you prefer manual installation or your OS is headless, see the [Installation Guide](https://anne.media/personal-server-setup-anne-installation-guide). It is intended for advanced users, those who want to learn what it takes to install ANNE manually, or anyone needing to run a headless annode on a server without a desktop environment.

## Get Started with ANNE in Minutes

### What the Wizard Does For You

- **Install ANNE for the first time** – complete setup from scratch.
- **Upgrade existing installations** – run the wizard again to update ANNE Node, Hasher, and Miner to the latest versions. Your existing configuration and keys will be preserved.
- **Re-import the chain snapshot** – if you need to reset your database or recover from an inconsistent database, run the wizard again and choose to re-import the snapshot (this will **not** affect your ANNE Node configuration and keys).
- **Create another instance** – run the wizard in a **different directory** to set up a second independent ANNE Node (e.g., for testing or separate networks). Use the “Custom Install Location” option when available.

**Note:** Because ANNEWizard is open‑source software distributed without a paid code‑signing certificate, Windows and macOS may show security warnings when you first run it. This is normal. The app is safe, and the warnings only appear because our build is not signed with a widely‑trusted certificate.

### Download the Latest Release

Head over to the [Releases](https://github.com/annemedia/anne-wizard/releases) page and download the installer for your operating system

#### Linux
Make sure the AppImage has executable permissions and open it from your file explorer.

#### macOS
Open the dmg and drag the app to your Applications folder, run it from there. If you get an unidentified developer warning, either change the settings under System Preferences > Security & Privacy > General > Allow applications downloaded from: to Anywhere or in a Terminal, OR strip the xattr com.apple.quarantine attribute from the downloaded file, like so:

```shell
cd /Applications
xattr -dr com.apple.quarantine "Anne Wizard.app"
```

Alternatively, you can bypass the quarantine by downloading the app via Terminal, like so:

```shell
curl -L -o  anne-wizard-macos.zip https://github.com/annemedia/anne-wizard/releases/download/anne-wizard-v1.0/anne-wizard-macos.zip
```

#### Windows
Run the exe, and if you get a warning "Windows Defender SmartScreen prevented an unrecognized app from starting. Running this app might put your PC at risk.", click on More info and Run anyway.

Alternatively, you can avert the SmartScreen by downloading the app via PowerShell, like so:

```shell
curl -o  anne-wizard-windows.zip https://github.com/annemedia/anne-wizard/releases/download/anne-wizard-v1.0/anne-wizard-windows.zip
```

or Command Prompt:

```shell
curl -L -o  anne-wizard-windows.zip https://github.com/annemedia/anne-wizard/releases/download/anne-wizard-v1.0/anne-wizard-windows.zip
```

### Important: Your Node Identity (Keys)

A **critical** part of your ANNE Node is its cryptographic identity: the **Seed** (secret) and **Account ID (NID)**. Once your ANNE Node has been running and has peers, **changing these keys may break peering**. Nodes you've previously peered with may refuse to connect with your peer because they remember your IP+port against the old NID. To attain bi-directional communication with those peers, you would need to either change IP or Port, or request manual intervention from the remote peer ends through community channels, or wait for them to recover preferences for your peer on their end. **Best if you do not change your node’s identity after it has been running.**

### What Happens When You Run the Wizard Again?

- **If your installation folder (e.g., `~/ANNE` or `C:\Program Files\ANNE`) still exists** and contains the encrypted `node.properties` file, the wizard **will skip the configuration pages** (the ones where you enter keys and network settings). It will simply update the binaries and snapshots without touching your existing node identity.
- **If you delete the installation folder** (or choose a new empty folder), the wizard treats it as a **fresh install** and will show all configuration pages, allowing you to create new keys.
- **If you want to keep your existing keys but need to reconfigure something**, you have two options:
  1. **Run the wizard and accept the defaults** – your keys will be changed. **After the wizard finishes, replace the `node.properties` file with a backup of your original `node.properties`** before starting the node.
  2. **Manually edit `node.properties`** after installation, simply run your ANNE Node and edit the keys in the provided Text Editor. Or, if properties are not yet encrypted, you may edit them ahead of time, found in the following default locations:
     - Linux/macOS: `~/ANNE/annode/conf/node.properties`
     - Windows: `C:\Program Files\ANNE\annode\conf\node.properties`


### After Installation

- Start ANNE Node from the desktop or start menu shortcut.
- Once synced, open your browser to `http://localhost:9116/ANNE.html` to access the main applications, eg. Web Wallet, Annex, LUKAT, or `http://localhost:9116/aon.html` to access Numiner or Swaps.
- To mine, run ANNE Hasher to plot your drives, then start ANNE Miner.

### Verifying the Download

If you prefer to verify the software before running, you can inspect the source code, build it yourself, or compare the checksum of the downloaded binary against the one published in the release notes.

## Prerequisites

To build ANNE Wizard from source you need:

- **CMake** (3.20 or later)
- **Qt6** (6.10.1 recommended) with the following components:
  - Core
  - Widgets
  - Network
  - Qml
  - Concurrent
  - DBus
- A **C++17** compiler (MinGW (recommended), MSVC, GCC, Clang)
- **Ninja** (optional)


## Building from Source

### General CMake Build

```bash
git clone https://github.com/annemedia/anne-wizard.git
cd anne-wizard
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable will be placed in `build/annewizard`

### Platform‑Specific Instructions

Get [QT Online Instaler](https://download.qt.io/official_releases/online_installers/) or see setup.sh provided in the repository.


#### Linux (AppImage)

Use the provided script `build_linux.sh` (adjust paths)

Get:
- linuxdeployqt – [linuxdeployqt](https://github.com/probonopd/linuxdeployqt)
or
- appimagetool – [appimagetool](https://github.com/AppImage/appimagetool)
both.


#### macOS (DMG)

Use the provided script `build_mac.sh` (adjust paths)


#### Windows

##### Dynamic Build

```powershell
# Ensure Qt6 is in PATH or set QT6_DIR
$env:QT6_DIR = "C:\Qt\6.10.1\mingw_64"   # adjust to your Qt installation
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Deploy Qt libraries
C:\Qt\6.10.1\mingw_64\bin\windeployqt.exe build\annewizard.exe
```

##### Static Build

Build a static Qt6, see instructions in build_windows file provided in the repository (adjust paths)

## Contributing

We welcome contributions! You can help by:

- Reporting bugs or suggesting features via GitHub Issues
- Submitting pull requests for bug fixes or improvements
- Improving documentation
- Adding translations

For discussions, join our community at [ANNE Forum](https://annetalk.org).

## Limitations

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

## Acknowledgements

- Qt framework – [qt.io](https://www.qt.io)
- linuxdeployqt – [linuxdeployqt](https://github.com/probonopd/linuxdeployqt)
- appimagetool – [appimagetool](https://github.com/AppImage/appimagetool)
- The ANNE community – [anne.media](https://anne.media)

<pre style="color: #000; background: whitesmoke; font-size: 34px; line-height: 1.2; font-weight: 800; text-align: center;">
      __/\__
. _   \\''//
-( )--/_||_\\
 .'.  \_()_/
  |    | . \
  |anne| .  \  
 .'.  ,\_____'.

  Power to the people.
</pre>
