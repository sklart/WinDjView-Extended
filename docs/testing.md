# Testing

Verified locally:

- Debug and Release Win32 NMAKE builds;
- Debug and Release x64 NMAKE builds, including a clean rebuild of the bundled x64 JPEG
  static library.

`tools\\tests\\run-jpegdecoder-regression.cmd` compiles and runs a Debug Win32
regression program against the built static libraries. It creates JPEG streams
through the classic libjpeg API and verifies the direct `JPEGDecoder` output:
baseline color, progressive color, grayscale channel expansion, BGR channel
order, bottom-to-top row mapping, and clean rejection of a truncated stream.
GitHub Actions runs this test after its Debug Win32 build.

GitHub Actions repeats these four builds and publishes the resulting
executables as workflow artifacts. No automated corpus of DjVu samples is
currently checked in, so opening real DjVu documents, printing, and GUI flows
remain manual regression tests.
