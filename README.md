# FovControl

Native **RED4ext** plugin for **Cyberpunk 2077** that provides:

- A **FOV lock** implemented via a safe, version-tolerant **pattern patch** (3-byte flip).
- A precise **FOV conversion** between the engine’s **internal FOV** and the **display FOV** shown in the game’s Graphics settings.
- A global **patching gate** to temporarily block any memory writes (Lock/Unlock/Toggle).

Works with the latest **RED4ext**. All native functions are exposed to **RedScript** and therefore callable from **CET**.

---

## Features

### 1) Patching gate (global write switch)
These natives let you allow or deny any memory patching performed by this plugin, making it possible to hard-block Lock/Unlock/Toggle in certain moments.

```julia
public static native func IsPatchingAllowed() -> Bool
public static native func PreventPatching() -> Bool   // returns true if blocking is now active (idempotent)
public static native func ReleasePatching() -> Bool   // returns true if patching is allowed again
```

Semantics:
- **IsPatchingAllowed**:
  - `true` → patching is supported and writes are allowed.
  - `false` → all write attempts are ignored.
- **PreventPatching**: sets the gate to **blocked**; safe to call repeatedly.
- **ReleasePatching**: sets the gate to **allowed**; safe to call repeatedly.

### 2) FOV lock control
Toggle the **3‑byte patch** that freezes the runtime FOV calculation. The patch location is found by scanning for a stable instruction pattern in `.text`.

```julia
public static native func IsLocked() -> Bool
public static native func Lock() -> Bool
public static native func Unlock() -> Bool
public static native func ToggleLock() -> Bool
```

Notes:
- All calls are **idempotent** (calling `Lock()` when already locked returns `true` and does nothing).
- If patching is currently **prevented**, Lock/Unlock/Toggle return `false` and do not write.
- Internally the plugin verifies the target address on first use and logs any failure (pattern not found, page protection error, etc.).

### 3) Internal ↔ Display FOV conversion
Convert between engine’s **internal FOV** and the **display FOV** seen in the Graphics settings.<br>
The mapping uses a **monotonic piecewise‑linear** curve handcrafted from measured values, and values between anchors are linearly interpolated.

```julia
public static native func ConvertFormat(fov: Float, isSettingsFormat: Bool) -> Float
```
- `isSettingsFormat = false` → interpret `fov` as **internal**, return **display** (UI) value.
- `isSettingsFormat = true`  → interpret `fov` as **display** (UI), return **internal** value.

RedScript Examples:
```julia
let displayValue: Float = FovControl.ConvertFormat(68.2379837, false); // internal → display
let engineValue: Float = FovControl.ConvertFormat(100.0, true );       // display  → internal
```

---

## RedScript integrations

### CameraComponent helpers
Convenience helpers to **get/set FOV in display units** directly on camera components.

```julia
@addMethod(CameraComponent)
public func GetDisplayFOV() -> Float

@addMethod(CameraComponent)
public func SetDisplayFOV(displayFov: Float) -> Void
```

- `GetDisplayFOV()` returns the **Graphics‑style** FOV for the current camera.
- `SetDisplayFOV()` accepts a **display FOV** and writes the corresponding **internal FOV** (with a safe guard band in internal space).

### Optional Codeware integration
If **Codeware** is present, add light helpers to access the **TPP** camera from both `PlayerPuppet` and `VehicleComponent`:

```julia
@addMethod(PlayerPuppet)
public func GetTPPCameraComponent() -> wref<vehicleTPPCameraComponent>

@addMethod(VehicleComponent)
public func GetCameraComponent() -> wref<vehicleTPPCameraComponent>
```

The helpers exist either way, but return `null` without Codeware.

CET (Lua) Examples:
```lua
local player = Game.GetPlayer()

-- Variant A
if player then
  local camera = player:GetTPPCameraComponent()
  camera:SetDisplayFOV(100)
end

-- Variant B
if player then
  local vehicle = Game.GetMountedVehicle(player)
  if vehicle then
    vehicle:GetCameraComponent():SetFOV(68.2379837)
  end
end
```

Notes:
- Both variants are effectively identical and set the internal engine FOV to `68.2379837` for TPP, with the exception that Variant A also works when no vehicle is available.
- In addition, `GetTPPCameraComponent()` now follows the same usage pattern as `GetFPPCameraComponent()`.

---

## Requirements

### For users
- **Cyberpunk 2077** (version 2.31)
- **RED4ext** (loader)

### For developers (to build from source)
- **Windows** (x64)
- **CMake ≥ 3.20**
- **MSVC 2022** (or compatible)
- **RED4ext.SDK** (latest)

**Optional**
- **CET** (Lua) for quick testing
- **Codeware** (for the optional CameraComponent helpers)

---

## Installation (User)

1. Drop the built DLL into:
   ```
   <Cyberpunk 2077>\red4ext\plugins\FovControl\FovControl.dll
   ```
2. Place the RedScript file:
   ```
   <Cyberpunk 2077>\r6\scripts\FovControl\FovControl.reds
   ```
3. Launch the game. Check logs at:
   ```
   <Cyberpunk 2077>\red4ext\logs\FovControl.log
   ```

---

## Build (Developer)

Project layout (relevant parts):
```
FovControl/
  CMakeLists.txt
  deps/
    red4ext.sdk/           (RED4ext.SDK as submodule or local checkout)
  src/red4ext/
    main.cpp               (plugin code)
  src/redscript/
    FovControl.reds        (RedScript API)
```

Typical build:
```bash
git submodule update --init --recursive
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The provided CMake script can stage the RedScript file into `bin/r6/scripts/FovControl/` and place plugin binaries into `bin/red4ext/FovControl/`.

---

## CET (Lua) smoke test (optional)

```lua
print("WasLocked:", FovControl.IsLocked(), "ToggleLock:", FovControl.ToggleLock(), "IsLocked:", FovControl.IsLocked())
```

---

## Logging

The plugin uses the **RED4ext logger**. You’ll find messages in:
```
<CP2077>\red4ext\logs\FovControl.log
```

Key entries:
- `EnsureTarget: patch target resolved` – pattern found, patch location ready.
- `Write3: failed to change page protection for patching` – OS denied page protection change.
- `Write3: wrote [.. .. ..] at 0x...` – write succeeded (trace).
- `ApplyPatch: write failed` – write suppressed (patching prevented) or failed.

---

## Troubleshooting

- **No effect / IsLocked() always false**
  - Check the log: Did `EnsureTarget` find the pattern?
- **Write fails**
  - Look for `VirtualProtect failed`; some overlays or AV can, in theory, interfere.
- **Patching prevented**
  - `PreventPatching()` blocks **all** Lock/Unlock/Toggle writes until `ReleasePatching()`.

---

## Versioning

- Targets the **latest** RED4ext loader/SDK and current game build.
- The scan is resilient, but **major codegen changes** in CP2077 could require updating the pattern.

---

## Contributing

- PRs welcome for:
  - Additional diagnostics in logs
  - Safer patterning / alt signatures
  - New FOV-related features or utility functions
  - CI packaging
