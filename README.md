# Audio Path Inspector

Windows audio path inspection tool for APO and Audio Effects diagnostics.

## Status

This project is in an early implementation stage. It currently builds a Windows native wxWidgets application for inspecting active capture and render audio endpoints.

Current inspection features include:

- Capture and render endpoint enumeration
- Endpoint details and mix format display
- Registered APO detection from endpoint properties
- Inferred APO chain display grouped by Stream, Mode, and Endpoint effects
- Windows Audio Effects detection through `IAudioEffectsManager`
- Audio Enhancements state detection through endpoint properties
- Shared, Shared RAW, and Exclusive WASAPI open tests
- APO notification interface support diagnostics

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

For a release build, configure a Release build directory with the same toolchain and build it with CMake.

## Release Policy

Binary artifacts are not published from the repository. Future binaries may be distributed separately through GitHub Releases.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).