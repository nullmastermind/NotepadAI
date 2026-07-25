# Installing NotepadAI on Linux

NotepadAI is available for Linux in three pre-built formats and can also be built from source.

## AppImage (Universal — all distributions)

AppImage runs on any x86_64 Linux distribution without installation.

**Requirements:** FUSE 2 (`libfuse2`) must be available. Ubuntu 22.04+ and Fedora 38+ include it. On Ubuntu 24.04 install it with:

```bash
sudo apt-get install libfuse2
```

**Steps:**

```bash
# Download the AppImage from the GitHub releases page
chmod +x NotepadAI-*.AppImage
./NotepadAI-*.AppImage
```

To integrate with your desktop launcher, move the file to `~/.local/bin/` and create a `.desktop` shortcut, or use a tool like `appimaged`.

---

## DEB package (Ubuntu 24.04+ / Debian 12+)

The .deb package installs NotepadAI as a native system package and integrates with `apt`.

**Supported distributions:**

- Ubuntu 24.04 (Noble) and later
- Debian 12 (Bookworm) and later

These distributions ship Qt 6.5+ in their official repositories, which is required by the package.
For older Ubuntu/Debian versions use the AppImage instead.

**Install:**

```bash
# Download NotepadAI-*-amd64.deb from the GitHub releases page
sudo apt install ./NotepadAI-*-amd64.deb
```

**Uninstall:**

```bash
sudo apt remove notepadai
```

---

## RPM package (Fedora 38+ / openSUSE Tumbleweed)

**Supported distributions:**

- Fedora 38 and later
- openSUSE Tumbleweed

**Install on Fedora:**

```bash
# Download NotepadAI-*-x86_64.rpm from the GitHub releases page
sudo dnf install ./NotepadAI-*-x86_64.rpm
```

**Install on openSUSE:**

```bash
sudo zypper install ./NotepadAI-*-x86_64.rpm
```

**Uninstall on Fedora:**

```bash
sudo dnf remove notepadai
```

**Uninstall on openSUSE:**

```bash
sudo zypper remove notepadai
```

---

## Build from source

**Build dependencies:**

- CMake 3.21 or later
- Ninja
- Qt 6.5 or later (qtbase, qtnetwork, qt5compat modules)
- A C++17-capable compiler (GCC 11+ or Clang 13+)
- `libxkbcommon-dev`, `libxcb-cursor-dev`

On Ubuntu/Debian:

```bash
sudo apt-get install cmake ninja-build \
  qt6-base-dev qt6-base-private-dev libqt6core5compat6-dev \
  libxkbcommon-dev libxcb-cursor-dev \
  libcups2-dev libcurl4-openssl-dev
```

On Fedora:

```bash
sudo dnf install cmake ninja-build \
  qt6-qtbase-devel qt6-qt5compat-devel \
  libxkbcommon-devel libxcb-devel
```

**Build and install:**

```bash
git clone https://github.com/nullmastermind/NotepadAI.git
cd NotepadAI
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
sudo cmake --install build --prefix /usr/local
```

The binary will be at `/usr/local/bin/NotepadAI`.
