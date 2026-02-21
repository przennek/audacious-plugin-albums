# Album Browser Plugin for Audacious

A GTK3 plugin for Audacious that displays your music collection as a grid of album covers with search functionality.

## Features

- Grid view of albums with cover art
- Automatic album detection from directory structure
- Search functionality (title, artist, year, path)
- Cover art from files or embedded metadata
- File system monitoring for automatic updates
- Album caching for fast startup
- Side-by-side layout with playlists
- Special "Album" playlist behavior:
  - Left-click: Updates "Album" playlist
  - Right-click: Adds to current playlist
  - Autoplay only when playing from "Album" playlist

## Requirements

### Linux
- Audacious (>= 4.0)
- GTK+ 3
- TagLib
- pkg-config

### macOS
- Audacious (>= 4.0)
- GTK+ 3
- TagLib
- pkg-config

Install dependencies:

**Arch Linux:**
```bash
sudo pacman -S audacious gtk3 taglib
```

**Ubuntu/Debian:**
```bash
sudo apt install audacious-dev libgtk-3-dev libtag1-dev pkg-config
```

**macOS (Homebrew):**
```bash
brew install audacious gtk+3 taglib pkg-config
```

## Building and Installing

### From the plugin directory:

```bash
cd src/album-browser
make -f Makefile.standalone
make -f Makefile.standalone install
```

### From the full audacious-plugins source:

```bash
meson setup builddir
meson compile -C builddir album-browser
sudo meson install -C builddir
```

## Uninstalling

```bash
cd src/album-browser
make -f Makefile.standalone uninstall
```

## Usage

1. Enable the plugin in Audacious: Tools → Settings → Plugins → General → Album Browser
2. The album browser will appear in the main window
3. Click the directory button to select your music folder (defaults to ~/Music)
4. Albums will be scanned and displayed automatically
5. Use the search bar to filter albums
6. Left-click an album to update the "Album" playlist
7. Right-click an album to add to the current playlist

## Configuration

- Music directory: Click the directory button in the toolbar
- Cache location: `~/.cache/audacious/album-browser-cache.dat`

## Album Detection

The plugin detects albums as leaf directories (no subdirectories) containing audio files. It supports:
- FLAC, MP3, OGG, Opus, M4A, AAC, WAV, WavPack, APE

Metadata is extracted from:
1. Directory names (patterns like "Artist - Year - Album")
2. Cover art files (cover.jpg, folder.jpg, etc.)
3. Embedded album art in audio files

## License

Same as Audacious (BSD 2-Clause)

## Repository

https://github.com/przennek/audacious-plugin-albums
