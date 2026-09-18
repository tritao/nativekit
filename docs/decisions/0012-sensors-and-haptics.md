# ADR 0012: Raw sensors and haptics

## Status

Accepted

## Context

NativeKit already exposes normalized device orientation and mapped joystick
input, but applications also need raw motion samples, system vibration, and
controller rumble. These facilities have different platform permissions,
lifetimes, and availability rules, so they must remain separate API families
with shared NativeKit conventions.

## Decisions

### Sensors

- Raw sensors use generation-checked `nk_sensor` handles and are independent
  from `nk_system_get_orientation()`.
- NativeKit's device-local right-handed coordinate system is +X to the right,
  +Y toward the top edge, and +Z out of the display toward the user. Backends
  transform native values into this space before publication.
- Acceleration and gravity use m/s², angular velocity uses radians/sec,
  magnetic field uses microtesla, and rotation vectors use normalized
  quaternion x/y/z/w values.
- Samples use `nk_time_now_ns()` monotonic timestamps, a per-sensor sequence,
  fixed four-value storage, and an accuracy/status value. Sensor update events
  coalesce by sensor handle; `nk_sensor_get_latest()` remains authoritative.
- Sampling interval, batching latency, and coordinate space are requested per
  sensor. The first coordinate-space version is the device-local space above.
- Permission requests are asynchronous and complete with a fixed payload that
  distinguishes granted, denied, unavailable, and not-required results.

### Haptics

- System haptics expose one replacement vibration output. Duration is the
  total effect duration, period is a best-effort repeating pulse period, and
  intensity is normalized to [0, 1]. Backends may approximate unsupported
  waveform details and return `NK_ERROR_UNSUPPORTED` when no facility exists.
- A new system vibration replaces the previous one. Shutdown and application
  backgrounding stop it.
- Gamepad rumble is a separate operation on the existing `nk_joystick` handle.
  Low- and high-frequency motor intensities are normalized to [0, 1], a new
  effect replaces the previous effect, and disconnect or shutdown stops it.

### Capabilities and unsupported behavior

- `NK_CAP_SENSORS` means the selected backend has a raw-sensor path;
  enumeration still determines whether a physical sensor is present.
- `NK_CAP_HAPTICS` means a system haptic path exists. `NK_CAP_GAMEPAD_RUMBLE`
  means the backend has a controller-rumble path; an individual controller or
  browser actuator may still return `NK_ERROR_UNSUPPORTED`.
- Backends without a reliable native source keep the ABI available and return
  `NK_ERROR_UNSUPPORTED`; they do not synthesize sensor data or advertise fake
  desktop sensor support.

## Platform scope

Android uses `SensorManager` and `Vibrator`/controller vibrators. iOS uses
`CMMotionManager` and UIKit feedback, and applications must provide
`NSMotionUsageDescription`. Web uses `DeviceMotionEvent`,
`navigator.vibrate()`, and `Gamepad.vibrationActuator`, subject to secure
context, user activation, browser policy, and hardware availability.

Windows uses XInput rumble, Linux uses evdev `EV_FF` rumble, and iOS/macOS use
GameController/Core Haptics where a controller exposes it. Desktop raw sensors
and generic macOS system vibration remain unadvertised until a reliable backend
exists.
