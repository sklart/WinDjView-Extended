# WinDjView Extended 4.1

WinDjView Extended is a native Windows DjVu viewer written in C++ with MFC.
The bundled `src/libdjvu` directory is built as a static library.

## License

The source code is distributed under the GNU General Public License, version 2
or later. See [src/license](src/license).

## Build Win32

The supported baseline is a 32-bit Release build. You need Visual Studio 2022
or later with the **Desktop development with C++** and **MFC** components.

Open a Developer Command Prompt for Visual Studio and run:

```bat
cd src\libdjvu
nmake /nologo /f makefile
cd ..
nmake /nologo /f makefile
```

The resulting executable is `src\Release\WinDjView.exe`.

## Build x64 (experimental)

Use the x64 Developer Command Prompt and pass `X64=1` to both makefiles:

```bat
cd src\libdjvu
nmake /nologo /f makefile X64=1
cd ..
nmake /nologo /f makefile X64=1
```

This produces `src\Release_x64\WinDjView.exe`. It is not release-ready:
the inherited `libdjvu` code currently emits pointer- and size-truncation
warnings on x64 and requires a dedicated portability audit.

## Continuous integration

GitHub Actions builds the Win32 Release configuration for every push and pull
request. Generated objects, libraries and executables are intentionally ignored
by Git.
