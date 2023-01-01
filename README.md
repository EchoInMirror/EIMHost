# EIM

## Usage

```bash
# Scan native audio plugins
EIMHost --scan <file>

# Load native audio plugins
EIMHost --load <plugin_description> [--handle [window_handle] --preset [preset_file]]

# Open native audio device
EIMHost --output [device_name] [--type [device_type] --bufferSize [buffer_size] --sampleRate [sample_rate]]
```

## Build

### Prerequisites

- Microsoft Visual Studio 2022
- [CMake](https://cmake.org/)

### Generate project

```bash
git clone --recursive https://github.com/EchoInMirror/EIMHost.git

cd EIMHost

mkdir build

cd build

cmake -G "Visual Studio 17 2022" -A x64 ..
```

Then open **EIMHost/build/EIMHost.sln**

## License

[AGPL-3.0](./LICENSE)

## Author

Shirasawa
