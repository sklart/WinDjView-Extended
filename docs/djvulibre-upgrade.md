# DjVuLibre upgrade

The bundled library was updated to DjVuLibre 3.5.30 (release.3.5.30.1) in
commit `468eb6f`.

The import retained WinDjView-specific behaviour where it is required by the
viewer: extended annotation parsing, GUI stream/bitmap safeguards, Windows
thread configuration, empty text-zone tolerance, metadata and container
compatibility, and safe document stream cleanup. Subsequent commits harden
chunk sizes, directory names, JPEG dimensions and parser resource limits.

`src/libdjvu` is linked statically. The application builds its IJG JPEG 6b
dependency from source for x64; the historical Win32 library remains in the
source distribution for compatibility.

The embedded DjVuLibre code is GPL-2.0-or-later. Its notices remain in the
source headers and the application distribution must continue to comply with
that license.