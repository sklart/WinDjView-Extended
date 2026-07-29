# Testing

Verified locally:

- Release Win32 NMAKE build;
- Release x64 NMAKE build, including a clean rebuild of the bundled x64 JPEG
  static library.

GitHub Actions repeats these two Release builds and publishes the resulting
executables as workflow artifacts. No automated corpus of DjVu samples is
currently checked in, so opening real DjVu documents, printing, and GUI flows
remain manual regression tests.