# EIM

## Build

### Prerequisites

- Microsoft Visual Studio 2022
- [CMake](https://cmake.org/)

### Generate project

```bash
git clone --recursive https://github.com/EchoInMirror/EIMPluginHost.git

cd EIMPluginHost

mkdir build

cd build

cmake -G "Visual Studio 17 2022" -A x64 .. -DCMAKE_TOOLCHAIN_FILE=<VCPkg install location>/scripts/buildsystems/vcpkg.cmake
```

Then open **EIMPluginHost/build/EIMPluginHost.sln**

## License

[AGPL-3.0](./LICENSE)

## Author

Shirasawa
