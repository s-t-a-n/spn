# Spine

![CI](https://github.com/s-t-a-n/spn/workflows/CI/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Zephyr](https://img.shields.io/badge/zephyr-compatible-green.svg)
![C++](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

C++20 components library for Zephyr 3.7

## Rationale

Driving Zephyr through modern c++ is a blast and truly deserving of being unleashed on this beautiful HAL.
This library builts c++ primitives such as queues/timing/allocators/etc on top of Zephyr's C interface to harvest the
power of abstraction. This code is the sideproduct of repeated patterns in client work, standardized and tested to a
reasonable degree.

Open an issue or pull request, it will be greatly appreciated :)

## Zephyr -> ETL -> SPN

This library is built on top of components from the ETL, making it a hard dependency. ETL (Embedded Template Library) is
an STL alternative written with the static needs of embedded work in mind. The spn api generally doesnt force you to use
ETL, as the api consists largely of return codes, but etl::result/etl::expect are used in certain locations.

## Modules

- core: holds basic types, traits and functions, like inplace (header friendly) logging
- debugging:
- dependency_injector

## Requirements

- Zephyr RTOS 3.7 or newer
- C++20 toolchain: tested on GCC 14
- West (Zephyr meta-tool)

## Get started

### using library

```yaml
manifest:
  projects:
    - name: spn
      url: https://github.com/s-t-a-n/spn
      revision: main
      path: modules/spn
      west-commands: modules/spn/west-commands.yml # add if developing spn
```

### developing library

#### spn development workspace

```
[change to your zephyr workspace folder]
git clone https://github.com/s-t-a-n/spn-workspace.git manifest
west init -l manifest
west update
```

you should be able to run 'west spn', if not, check if manifest includes `west-commands` mentioned above

#### spn development building & testing

```
west spn build src/<library>/samples/<sample> # build specific test/sample
west spn build # build all samples
west spn test src/<library>/tests # build and run specific tests
west spn test # build and run all tests
west spn menuconfig src/<library>/samples/<sample>
west spn run src/<library>/samples/<sample>
west spn asan # Run tests with asan/ubsan/stackcanaries
west spn tidy # Run clang-tidy static tests
west spn ci # Run CI pipeline
```

## License

This project is available under the MIT License. See `LICENSE` for the full text.
