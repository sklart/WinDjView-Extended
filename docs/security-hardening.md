# Security hardening

The current hardening work rejects malformed document settings and unsafe
external URI schemes, validates page/rectangle bounds, limits XML parser depth
and node counts, validates JPEG dimensions and DjVu chunk sizes, and restores
bookmark ownership links after copy/load/import.

The application permits only `http`, `https`, and `mailto` for external links
originating in documents. Internal DjVu navigation remains handled by the
viewer. Settings input is size-limited before parsing.

Fuzzing with a dedicated malformed-DjVu corpus and runtime testing on target
Windows versions remain follow-up work.
Tree-node deletion now releases detached nodes and clears bookmark reverse links before UI destruction, preventing both repeated-delete leaks and dangling bookmark handles.
