# Dynamic Config Application for DarkmoonUtil.cpp

## Problem Statement

The BambuStudio application was experiencing issues where darkmoon plate temperatures were falling back to 45°C due to missing configuration values in filament presets like:

- PLA Basic (`Bambu PLA Basic @base.json`)
- PLA Matte (`Bambu PLA Matte @base.json`) 
- PETG Basic (`Bambu PETG Basic @base.json`)
- PETG High Speed (`Generic PETG HF @base.json`)

**UPDATE**: The Dynamic Config Application now only applies to supported manufacturers where darkmoon plates are available: **Creality, Prusa, Qidi, and Bambu Labs (BBL)**.

## Root Cause

The issue occurred because:

1. Filament preset files only contained standard plate temperature configurations:
   - `cool_plate_temp`
   - `hot_plate_temp`
   - `eng_plate_temp`
   - `textured_plate_temp`

2. They were missing darkmoon-specific temperature configurations:
   - `darkmoon_g10_plate_temp`
   - `darkmoon_ice_plate_temp`
   - `darkmoon_lux_plate_temp`
   - `darkmoon_cfx_plate_temp`
   - `darkmoon_satin_plate_temp`

3. When darkmoon plates were selected, the system fell back to `kDarkmoonPlaceholderTemp = 45`, which was inappropriate for many filament/plate combinations.

4. The configuration was being applied to all manufacturers, even those that don't support darkmoon plates.

## Solution: Dynamic Config Application with Manufacturer Filtering

### Architecture

The solution introduces a `DarkmoonConfigApp` class that:

1. **Checks manufacturer compatibility** - Only applies to Creality, Prusa, Qidi, and BBL
2. **Detects missing darkmoon temperature configurations**
3. **Calculates appropriate temperatures based on filament type**
4. **Uses existing temperature calculation logic from `DarkmoonUtil.cpp`**
5. **Ensures no inappropriate 45°C fallbacks**

### Key Components

#### `DarkmoonConfigApp` Class (`src/libslic3r/DarkmoonConfigApp.hpp/.cpp`)

**Main Functions:**

- `apply_dynamic_config()` - Applies dynamic darkmoon configuration with manufacturer filtering
- `is_darkmoon_supported_manufacturer()` - Checks if manufacturer supports darkmoon plates
- `generate_darkmoon_temperatures()` - Generates appropriate temperatures for each darkmoon plate type  
- `has_missing_darkmoon_temperatures()` - Detects missing or placeholder temperature values
- `validate_darkmoon_config()` - Validates darkmoon configuration
- `get_recommended_temperatures()` - Provides temperature recommendations

#### Manufacturer Filtering

**✅ SUPPORTED Manufacturers** (darkmoon configuration applied):
- **BBL** / **Bambu Lab**
- **Creality** 
- **Prusa**
- **Qidi**

**❌ UNSUPPORTED Manufacturers** (darkmoon configuration skipped):
- Anker, Anycubic, Elegoo, Geeetech, Tronxy, Vivedino, Voron, Voxelab

The system checks the printer's `family` field or `printer_model` to determine manufacturer compatibility.

#### Integration Points

Modified `PresetBundle.cpp` at four key locations:

1. **Single filament preset loading** (~line 123) - `&in_printer_preset.config`
2. **Multi-filament preset loading** (~line 149) - `&in_printer_preset.config`  
3. **Full config construction** (~line 219) - `&in_printer_preset.config`
4. **FFF config construction** (~line 2952) - `&this->printers.get_edited_preset().config`

Each location now calls `DarkmoonConfigApp::apply_dynamic_config()` with printer config parameter.

### Temperature Examples

For **SUPPORTED** manufacturers, instead of falling back to 45°C, the system now calculates appropriate temperatures:

#### PLA Basic (BBL/Creality/Prusa/Qidi):
- Darkmoon G10 Garolite: **55°C** (was falling back to 45°C)
- Darkmoon CFX: **65°C** (was falling back to 45°C)
- Darkmoon Satin: **60°C** (was falling back to 45°C)
- Darkmoon Lux: **60°C** (was falling back to 45°C)
- Darkmoon Ice: **40°C** (was falling back to 45°C)

#### PETG Basic (BBL/Creality/Prusa/Qidi):
- Darkmoon G10 Garolite: **70°C** (was falling back to 45°C)
- Darkmoon CFX: **80°C** (was falling back to 45°C)
- Darkmoon Satin: **80°C** (was falling back to 45°C)
- Darkmoon Lux: **80°C** (was falling back to 45°C)
- Darkmoon Ice: **45°C** (correct for PETG, now calculated not fallback)

For **UNSUPPORTED** manufacturers, no darkmoon configuration is applied (function returns false).

## Testing

Comprehensive test suite in `tests/libslic3r/test_darkmoon_config_app.cpp`:

- Tests for supported vs unsupported manufacturers
- Tests for different filament types (PLA, PETG, ABS, etc.)
- Verification that proper temperatures are calculated instead of 45°C fallback
- Integration tests demonstrating the fix for missing darkmoon temperatures
- Validation that unsupported manufacturers are properly skipped

## Benefits

1. **Manufacturer-Specific Application**: Only applies to printers that actually support darkmoon plates
2. **Proper Temperature Control**: Each filament type gets appropriate temperatures for darkmoon plates
3. **No More 45°C Fallbacks**: Eliminates inappropriate placeholder temperature usage
4. **Backward Compatibility**: Existing configurations continue to work
5. **Automatic Configuration**: Missing temperatures are automatically calculated
6. **Performance**: Skips unnecessary processing for unsupported manufacturers

## Usage

The Dynamic Config Application works automatically. When the system loads filament presets:

1. **Checks manufacturer compatibility** using printer config
2. **For supported manufacturers**: Detects if darkmoon temperatures are missing, calculates appropriate temperatures, applies them
3. **For unsupported manufacturers**: Skips darkmoon configuration entirely
4. Proceeds with normal processing

No user intervention or configuration changes are required.

## Implementation Details

### Files Modified

- **Modified**: `src/libslic3r/DarkmoonConfigApp.hpp` - Added manufacturer filtering
- **Modified**: `src/libslic3r/DarkmoonConfigApp.cpp` - Added manufacturer checking logic
- **Modified**: `src/libslic3r/PresetBundle.cpp` - Updated integration points with printer config
- **Modified**: `tests/libslic3r/test_darkmoon_config_app.cpp` - Updated tests with manufacturer filtering

### Minimal Changes Approach

The implementation follows a minimal changes approach:

- Leverages existing temperature calculation logic in `DarkmoonUtil.cpp`
- Integrates seamlessly with existing `ensure_darkmoon_bed_temps()` function
- Maintains all existing functionality and backward compatibility
- Only adds manufacturer filtering and missing temperature configurations when needed
- Skips processing entirely for unsupported manufacturers

This solution addresses the core issue while making the smallest possible changes to the existing codebase and ensuring darkmoon functionality is only applied where it's actually supported.