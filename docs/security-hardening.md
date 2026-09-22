# Security hardening

The current hardening work rejects malformed document settings and unsafe
external URI schemes, validates page/rectangle bounds, limits XML parser depth
and node counts, validates JPEG dimensions and DjVu chunk sizes, and restores
bookmark ownership links after copy/load/import.

The application permits only `http`, `https`, and `mailto` for external links
originating in documents. Internal DjVu navigation remains handled by the
viewer. Settings input is size-limited before parsing.

The project also has a deterministic malformed-DjVu corpus and a separate x64
MSVC AddressSanitizer regression. It decodes each fixture through the
production DjVu path in an isolated, time-limited process, requires controlled
failure for malformed inputs, and runs as a blocking Native ASan x64 CI job.

Follow-up work includes mutation/libFuzzer fuzzing, expanding the malformed
corpus, and runtime validation on target Windows versions.
Tree-node deletion now releases detached nodes and clears bookmark reverse links before UI destruction, preventing both repeated-delete leaks and dangling bookmark handles.
