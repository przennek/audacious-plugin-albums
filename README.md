# Audacious Plugins - Album Browser Fork

This is a fork of [audacious-plugins](https://github.com/audacious-media-player/audacious-plugins) that adds the **Album Browser** plugin.

## What is this?

This repository contains a custom plugin for Audacious that provides an album-centric view of your music library. Instead of just seeing playlists, you get a beautiful grid of album covers that you can browse and search.

## Why a fork?

The Album Browser plugin is a third-party addition that's not part of the official Audacious plugins. This fork:

- Maintains the full audacious-plugins codebase
- Adds the Album Browser plugin in `src/album-browser/`
- Provides standalone build tools for easy installation
- Can be kept in sync with upstream audacious-plugins

## Quick Start

You don't need to build all of audacious-plugins. Just build the Album Browser plugin:

```bash
git clone https://github.com/przennek/audacious-plugin-albums.git
cd audacious-plugin-albums/src/album-browser
make -f Makefile.standalone
make -f Makefile.standalone install
```

Then restart Audacious and enable: Tools → Settings → Plugins → General → Album Browser

## What's the Album Browser Plugin?

A GTK3 plugin that displays your music collection as a grid of albums with:

- **Grid view** - Album covers in a responsive grid layout
- **Search** - Real-time filtering by title, artist, year, or path
- **Cover art** - From files or embedded in audio files
- **Smart playlists** - Special "Album" playlist behavior
- **Auto-scanning** - Monitors your music directory for changes
- **Caching** - Fast startup with cached album data
- **Side-by-side** - Works alongside your regular playlists

See [src/album-browser/README.md](src/album-browser/README.md) for full documentation.

## Repository Structure

```
audacious-plugins/
├── src/
│   ├── album-browser/          # The Album Browser plugin (NEW)
│   │   ├── album-browser.cc    # Main plugin code
│   │   ├── scanner.cc          # Directory scanning
│   │   ├── metadata.cc         # Metadata extraction
│   │   ├── Makefile.standalone # Standalone build system
│   │   ├── PKGBUILD           # Arch Linux package
│   │   ├── README.md          # Plugin documentation
│   │   └── INSTALL.md         # Installation guide
│   └── [other plugins]/        # All standard Audacious plugins
└── [standard audacious-plugins structure]
```

## Keeping in Sync with Upstream

This fork has two remotes:

- `origin` - This fork (https://github.com/przennek/audacious-plugin-albums)
- `upstream` - Official audacious-plugins (https://github.com/audacious-media-player/audacious-plugins)

To pull updates from upstream:

```bash
git fetch upstream
git merge upstream/master
# Resolve any conflicts in src/album-browser/ if needed
git push origin master
```

## Building the Full Plugin Suite

If you want to build all audacious-plugins (including Album Browser):

```bash
meson setup builddir
meson compile -C builddir
sudo meson install -C builddir
```

The Album Browser plugin is integrated into the meson build system.

## Platform Support

- **Linux** - Fully supported (tested on Arch Linux)
- **macOS** - Supported (requires Homebrew dependencies)
- **Windows** - Not tested

## Installation Options

1. **Standalone** - Build just the Album Browser plugin (recommended)
2. **AUR Package** - Use the provided PKGBUILD for Arch Linux
3. **Full Build** - Build all audacious-plugins including Album Browser
4. **Manual** - Copy the .so file to your Audacious plugins directory

See [src/album-browser/INSTALL.md](src/album-browser/INSTALL.md) for detailed instructions.

## Contributing

This is a personal fork for the Album Browser plugin. For issues or improvements:

- Album Browser plugin: Open an issue here
- Other plugins: Report to [upstream audacious-plugins](https://github.com/audacious-media-player/audacious-plugins)

## License

- Album Browser plugin: BSD 2-Clause (same as Audacious)
- All other plugins: Original audacious-plugins licenses

## Credits

- **Audacious Team** - For the excellent music player and plugin framework
- **Album Browser Plugin** - Created for personal use, shared for others who want an album-centric view

## Links

- [Audacious Media Player](https://audacious-media-player.org/)
- [Upstream audacious-plugins](https://github.com/audacious-media-player/audacious-plugins)
- [Album Browser Plugin Documentation](src/album-browser/README.md)
