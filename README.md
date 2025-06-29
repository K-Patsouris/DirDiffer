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

## Usage

Usage is pretty simple. Pass a file with SMTP info initially, with "-set path/to/file.txt", edit the config file to specify directory and filetypes to monitor, and then run the program whenever you want to see the changes since last run, or just put it on a scheduler. Pass "-h" for help, where there is an option to auto-generate a config file template with detailed documentation.
