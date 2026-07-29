# Testing

Verified locally:

- Debug and Release Win32 NMAKE builds;
- Debug and Release x64 NMAKE builds, including a clean rebuild of the bundled x64 JPEG
  static library.

GitHub Actions repeats these four builds and publishes the resulting
executables as workflow artifacts. No automated corpus of DjVu samples is
currently checked in, so opening real DjVu documents, printing, and GUI flows
remain manual regression tests.