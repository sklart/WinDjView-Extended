# Encoding policy

Legacy C++ source and resource files are preserved in their existing Windows
code pages (primarily Windows-1251). They must not be bulk-converted because
MFC resources and historical localized strings rely on that representation.

New Markdown, YAML, and build-documentation files use UTF-8 without a BOM.
When changing legacy source, preserve its existing encoding and line endings.