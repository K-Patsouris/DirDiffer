# DirDiffer

A utility to monitor a given directory, detecting file creations and deletiosn from run to run. The changes are compiled into a text report and emailed.

## Building

### Requirements

- git
- cmake
- vcpkg
- Visual Studio 2022 with v143 C++ toolset (C++ Desktop Development package)

```
git clone https://github.com/K-Patsouris/DirDiffer
cd DirDiffer
cmake --preset vcpkg
```

Visual Studio solution files will be written in /build. You can edit `CMakePresets.json` to add new presets, if you don't have/want Visual Studio 2022.

