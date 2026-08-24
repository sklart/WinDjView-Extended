# Release process

Every public release must include two user-facing documents in Russian:

- an up-to-date `README.md` describing the release in practical terms;
- a GitHub Release description following the same structure and tone.

Do not publish a release that only lists commits or implementation details.

## Required release-note structure

1. State the release version and the version from which it evolves.
2. Start with user-visible improvements: architecture support, DPI behaviour,
   performance, stability, and compatibility.
3. Explain important technical updates in plain language. Include concrete,
   reproducible measurements when available, with the old value, new value,
   test conditions, and a clear qualification that a benchmark is directional
   when it is not a complete application benchmark.
4. Summarize hardening and bug fixes by their user impact rather than by warning
   identifiers or internal function names.
5. List the build and regression coverage: Release and Debug, Win32 and x64,
   plus architecture and external-DLL checks when applicable.
6. State compatibility boundaries explicitly. Do not claim Windows-version
   support that was not tested on the target operating system.
7. State licensing and third-party-license status.
8. Finish with a short explanation of why an existing user should upgrade.

## Release assets

- Upload distinct portable Win32 and x64 archives containing `WinDjView.exe`
  and the license.
- Provide SHA-256 checksums for the archives.
- Upload each localization as a separate architecture-specific asset, not only
  inside an archive. Name it so the application discovers it next to the EXE;
  for example, `WinDjViewRU-Win32.dll` and `WinDjViewRU-x64.dll` both match the
  `WinDjView*.dll` discovery rule.
- Release notes must say which executable and localization architecture match.

## Publication gate

Before creating a non-draft GitHub Release, ensure the target commit is on
`main`, CI has passed, Release Win32 and x64 use SIMD, the PE machine values
are `014c` and `8664`, no external JPEG DLL is imported, and core/JPEG
regressions pass in all four configurations.
