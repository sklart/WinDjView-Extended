# DjVuLibre baseline

Before the upgrade, the embedded `src/libdjvu` tree matched the DjVuLibre 3.5
line and carried WinDjView-specific changes. Comparison during the migration
identified 3.5.27 as the most likely upstream baseline (medium confidence):
the tree had downstream Windows/MFC integration and extended annotation
behaviour not present in a pristine release.

The update imported the 3.5.30 stable release and then restored the required
local behaviour deliberately rather than copying the old tree wholesale.
Important carried-forward areas include extended annotation parsing,
Windows-thread configuration, container and metadata compatibility, GUI stream
safeguards, and empty text-zone handling. The resulting migration and risks
are documented in [djvulibre-upgrade.md](djvulibre-upgrade.md).