# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](http://semver.org/spec/v2.0.0.html).

<!--

## [Unreleased] - DATE

### Added

### Changed

### Fixed

### Removed

-->

## [0.3.1] - 2025-07-08

### Added

- Added zephyr/Kconfig to control builddefines in Zephyr's tooling
- Added to_uint32 string parser
- Added BidirectionalList<T> and corresponding BidirectionalLink<T> to structure/bidirectional_list.hpp

### Changed

- Changed builddefine `SPINE_DEBUG_BUFFER_SIZE` to `SPINE_LOGGING_MAX_MSG_SIZE`

### Fixed

- `spn_assert` was not printing the file, linenumber and function because of use of the `SPN_ERR()` call. This fixes
  that by making spn_assert print through `SPN_DBG()`

## [0.3.0] - 2024-10-22

### Added

- Add cubic_metre to structure/units
- Adds `new_reading()` override to PID class to allow passing an arbitrary time for simulated time passed.
- Adds extra lambda arguments to PID::Autotune to allow arbitrary sleep/uptime functions to be passed
- Added SPN_PLATFORM_CAP_DOUBLE build flag that can be set to use doubles where
  applicable (default: not set). For now this is only used by structure/units.

### Changed

- Only print filename/line/function in calls to SPN_DBG (so not for SPN_LOG/SPN_WARN)
- Change convert_to_kelvin to to_kelvin for consistency in structure/units/temperature.hpp
- Changed all doubles to floats for performance and memory reasons. Adds non-default build flag
  `SPN_PLATFORM_CAP_DOUBLE` to use doubles where applicable (only in structure/units at the moment).

### Fixed

- Fixed minor CI-problems such as cache validation

## [0.2.0] - 2024-10-16

### Added

- Units: add compound type to express relations like litre per minute
- Added remaining SI-units

### Changed

- Change `time` to `k_time` (uint32 that tracks the 'kernel' uptime timer overflow) and add a new `time` that uses a
  floating point for algorithmic use
- Revamped `Unit` base class for all SI-units to publish a sane set of arithmetics
- More verbose exception handling

## [0.1.0] - 2024-10-12

### Added

- debugging: Add `spn_expect`-macro that allows for logging an error without throwing an exception
- platform: Add logic to set UART RX/TX buffer size
- structure/Result: Expanded functionality with more Rust sauce
- platform: Added Zephyr as platform. To accomplish this, a beautiful melange of preprocessor macros was added to the
  platform code.
- controller/pid: Added setter functions to properly choose between ON_ERROR and ON_MEASUREMENT modality of proportional
  response.

### Changed

- core: Moved traits/enum stuff to core/meta
- debugging: changed `assert` to `spn_assert`
- logging: changed `LOG` to `SPN_LOG` and respectively for `WARN`, `ERR` and `DBG` whilst defining the original macros
  if not already defined.
- logging: changed default logbuffer size `SPINE_DEBUG_BUFFER_SIZE` from 512 to 256 as 512 seemed a bit overkill
- platformio: cleaned up buildflags. Added stricter formatting errors. Added -Os to deployment builds

### Fixed

- eventsystem: added missing forward of EventData argument for `trigger()` function

### Removed

- Removed all leftovers from ArduinoFakeIT as all mocking is now handled by platform/mock

## [0.0.1] - 2024-09-23

### Added

- **Core**: Provides (SI) units and datetime utilities, debugging tools, exception handling, and logging.
- **Event System**: Manages runtime through an event-driven architecture.
- **Filters**: Includes bandpass, EWMA, inverting, transposition, and passthrough filters bundled in a filter stack.
- **I/O**: Implements buffered streams and newline-terminated 'transactions.'
- **Platform Abstraction**: Layers for GPIO, vendor HAL, and stdio; supports Arduino and native x86.
- **Data Structures**: Array, ring buffer, vector, stack, pool, static string, and a result type adhering to the single
  allocation principle.
- **Controllers**: Includes a PID controller and SR-Latch.

