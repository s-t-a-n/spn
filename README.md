# SPN - Zephyr RTOS Module

A collection of reusable C++20 components for Zephyr RTOS applications.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Zephyr](https://img.shields.io/badge/zephyr-compatible-green.svg)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

## Features

- **Modular Design**: Each library is self-contained with its own tests and samples
- **C++20 Support**: Modern C++ features for embedded development
- **Zephyr Integration**: Native Zephyr module with proper CMake and Kconfig integration
- **Testing Framework**: Comprehensive unit tests using Zephyr's testing framework
- **Sample Applications**: Ready-to-run examples for each library

## Requirements

- **Zephyr RTOS**: Version 3.4 or later
- **Toolchain**: Compatible C++20 compiler (GCC 11+, Clang 13+)
- **West**: Zephyr's meta-tool for project management

## Installation

### As a West Module

Add this repository to your West workspace manifest (`west.yml`):

```yaml
manifest:
  projects:
    - name: spn
      url: https://github.com/s-t-a-n/spn
      revision: main
      path: modules/spn
```

### Manual Integration

Clone the repository and add it to your project's CMakeLists.txt:

```cmake
get_filename_component(SPN_ROOT "path/to/spn" ABSOLUTE)
list(APPEND EXTRA_ZEPHYR_MODULES "${SPN_ROOT}")
```

## Usage

### Include Headers

```cpp
#include <spn/library_name/header.hpp>
```

### Link Libraries

In your application's CMakeLists.txt:

```cmake
target_link_libraries(app PRIVATE spn::library_name)
```

### Enable in Configuration

Add to your `prj.conf`:

```
CONFIG_SPN_LIBRARY_NAME=y
```

## Building

### Building Samples

Navigate to the module root and build any sample:

```bash
west build -b <board> src/<library>/samples/<sample_name>
```

Example for native simulation:

```bash
west build -b native_sim src/library_name/samples/basic
west build -t run
```

### Building Your Application

```bash
cd your_application
west build -b <target_board>
west flash  # if using real hardware
```

## Testing

Run all tests using Zephyr's Twister framework:

```bash
# Run all tests
west twister -T src/ -p native_sim

# Run tests for specific library
west twister -T src/library_name/tests -p native_sim

# Run with verbose output
west twister -T src/library_name/tests -p native_sim -v
```

## Project Structure

```
spn/
├── CMakeLists.txt          # Root CMake configuration
├── Kconfig                 # Root Kconfig file
├── zephyr/
│   └── module.yml          # Zephyr module configuration
├── src/
│   ├── CMakeLists.txt      # Source directory delegation
│   ├── Kconfig             # Source Kconfig delegation
│   └── library_name/       # Individual library
│       ├── CMakeLists.txt  # Library build configuration
│       ├── Kconfig         # Library configuration options
│       ├── include/
│       │   └── spn/
│       │       └── library_name/
│       │           └── *.hpp
│       ├── src/
│       │   └── *.cpp
│       ├── tests/
│       │   └── library_name/
│       │       ├── CMakeLists.txt
│       │       ├── prj.conf
│       │       ├── testcase.yaml
│       │       └── test_*.cpp
│       └── samples/
│           └── sample_name/
│               ├── CMakeLists.txt
│               ├── prj.conf
│               └── src/
│                   └── main.cpp
└── README.md
```

## Configuration Options

Each library provides Kconfig options prefixed with `CONFIG_SPN_`:

- `CONFIG_SPN_LIBRARY_NAME=y` - Enable the library
- Additional library-specific options as documented per library

## Adding New Libraries

1. Create directory structure under `src/your_library/`
2. Follow the pattern of existing libraries:
    - `include/spn/your_library/` for headers
    - `src/` for implementation
    - `tests/your_library/` for unit tests
    - `samples/basic/` for basic usage example
3. Add Kconfig options with `SPN_` prefix
4. Update module's `zephyr/module.yml` if adding new test/sample directories

## Contributing

1. Follow the existing code style and structure
2. Ensure all new code includes comprehensive tests
3. Provide sample applications for new features
4. Update documentation for API changes

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Support

For questions, issues, or contributions, please visit the project repository.