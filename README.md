# Audio Path Inspector

Windows audio capture path inspection tool for APO and Audio Effects diagnostics.

## Status

This project is in an early implementation stage. It currently builds a Windows native wxWidgets application and includes initial capture device inspection plumbing.

## Build

Install the required tools first:

- LLVM with `clang-cl`
- Visual Studio Build Tools 2022 with Windows 11 SDK
- CMake
- Ninja
- Git
- vcpkg

Install wxWidgets:

```powershell
vcpkg install wxwidgets:x64-windows
```

Configure and build:

```powershell
cmake --preset windows-clang-vcpkg
cmake --build --preset windows-clang-vcpkg
```

The initial implementation opens a wxWidgets window and enumerates active capture devices through Core Audio.

## Release Policy

Binary artifacts are not published from the repository. Future binaries may be distributed separately through GitHub Releases.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
