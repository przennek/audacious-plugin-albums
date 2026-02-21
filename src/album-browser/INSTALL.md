# Installation Guide

## Quick Install (Linux)

```bash
# Clone the repository
git clone https://github.com/przennek/audacious-plugin-albums.git
cd audacious-plugin-albums/src/album-browser

# Build and install
make -f Makefile.standalone
make -f Makefile.standalone install

# Restart Audacious and enable the plugin:
# Tools → Settings → Plugins → General → Album Browser
```

## Quick Install (macOS)

```bash
# Install dependencies first
brew install audacious gtk+3 taglib pkg-config

# Clone and build
git clone https://github.com/przennek/audacious-plugin-albums.git
cd audacious-plugin-albums/src/album-browser

# Build and install
make -f Makefile.standalone
make -f Makefile.standalone install

# Restart Audacious and enable the plugin
```

## Arch Linux (AUR)

You can create a local AUR package:

```bash
cd audacious-plugin-albums/src/album-browser
makepkg -si
```

Or submit the PKGBUILD to AUR for others to use.

## System-wide Installation (Linux)

To install for all users:

```bash
sudo make -f Makefile.standalone INSTALL_DIR=/usr/lib/audacious/General install
```

## Uninstall

```bash
make -f Makefile.standalone uninstall
```

## Troubleshooting

### Plugin doesn't appear in Audacious

1. Check if the plugin file exists:
   - Linux: `~/.local/lib/audacious/General/album-browser.so`
   - macOS: `~/Library/Application Support/Audacious/Plugins/album-browser.dylib`

2. Check Audacious plugin paths:
   ```bash
   audacious --help | grep -i plugin
   ```

3. Check for errors:
   ```bash
   audacious --verbose
   ```

### Build errors

Make sure you have all dependencies installed:

**Arch Linux:**
```bash
sudo pacman -S audacious gtk3 taglib base-devel
```

**Ubuntu/Debian:**
```bash
sudo apt install audacious-dev libgtk-3-dev libtag1-dev build-essential pkg-config
```

**macOS:**
```bash
brew install audacious gtk+3 taglib pkg-config
```

### Missing pkg-config

If you get "pkg-config not found":
- Linux: Install `pkg-config` package
- macOS: `brew install pkg-config`
