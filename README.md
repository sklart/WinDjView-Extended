# WinDjView Extended 4.1

WinDjView Extended is a native Windows DjVu viewer written in C++ with MFC.
The bundled `src/libdjvu` directory is linked statically.

## Upstream and acknowledgements

This repository is based on the WinDjView Extended sources published by
[N.M.E. (nme3001) on SourceForge](https://sourceforge.net/projects/windjviewextended/).
Many thanks to N.M.E. for maintaining and making WinDjView Extended available,
and to Andrew Zhezherun, the original author of WinDjView.

## License

The source code is distributed under the GNU General Public License, version 2
or later. See [src/license](src/license). The bundled DjVuLibre 3.5.30 code is
also GPL-2.0-or-later; see its source notices and
[upgrade notes](docs/djvulibre-upgrade.md).

## Building

Verified NMAKE builds are available for Release Win32 and Release x64 with
Visual Studio C++ and MFC. Exact commands, prerequisites, and current Debug /
Windows 7 limitations are in [BUILDING.md](BUILDING.md).

## Continuous integration

GitHub Actions builds Release Win32 and Release x64 for every push and pull
request, verifies the executable, and publishes it as a workflow artifact.

## Project notes

- [Build baseline](docs/build-baseline.md)
- [Security hardening](docs/security-hardening.md)
- [Encoding policy](docs/encoding.md)
- [Testing](docs/testing.md)
- [Changelog](CHANGELOG.md)