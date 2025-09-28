# Dynamic Config Application for DarkmoonUtil.cpp

## Problem Statement

The BambuStudio application was experiencing issues where darkmoon plate temperatures were falling back to 45°C due to missing configuration values in filament presets like:

- PLA Basic (`Bambu PLA Basic @base.json`)
- PLA Matte (`Bambu PLA Matte @base.json`) 
- PETG Basic (`Bambu PETG Basic @base.json`)
- PETG High Speed (`Generic PETG HF @base.json`)

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

## Solution: Dynamic Config Application

### Architecture

The solution introduces a `DarkmoonConfigApp` class that:

1. **Detects missing darkmoon temperature configurations**
2. **Calculates appropriate temperatures based on filament type**
3. **Uses existing temperature calculation logic from `DarkmoonUtil.cpp`**
4. **Ensures no inappropriate 45°C fallbacks**

### Key Components

#### `DarkmoonConfigApp` Class (`src/libslic3r/DarkmoonConfigApp.hpp/.cpp`)

**Main Functions:**

- `apply_dynamic_config()` - Applies dynamic darkmoon configuration to ensure proper temperatures
- `generate_darkmoon_temperatures()` - Generates appropriate temperatures for each darkmoon plate type  
- `has_missing_darkmoon_temperatures()` - Detects missing or placeholder temperature values
- `validate_darkmoon_config()` - Validates darkmoon configuration
- `get_recommended_temperatures()` - Provides temperature recommendations

#### Integration Points

Modified `PresetBundle.cpp` at four key locations:

1. **Single filament preset loading** (~line 122)
2. **Multi-filament preset loading** (~line 149)  
3. **Full config construction** (~line 220)
4. **FFF config construction** (~line 2951)

Each location now calls `DarkmoonConfigApp::apply_dynamic_config()` before `ensure_darkmoon_bed_temps()`.

### Temperature Examples

Instead of falling back to 45°C, the system now calculates appropriate temperatures:

#### PLA Basic:
- Darkmoon G10 Garolite: **55°C** (was falling back to 45°C)
- Darkmoon CFX: **65°C** (was falling back to 45°C)
- Darkmoon Satin: **60°C** (was falling back to 45°C)
- Darkmoon Lux: **60°C** (was falling back to 45°C)
- Darkmoon Ice: **40°C** (was falling back to 45°C)

#### PETG Basic:
- Darkmoon G10 Garolite: **70°C** (was falling back to 45°C)
- Darkmoon CFX: **80°C** (was falling back to 45°C)
- Darkmoon Satin: **80°C** (was falling back to 45°C)
- Darkmoon Lux: **80°C** (was falling back to 45°C)
- Darkmoon Ice: **45°C** (correct for PETG, now calculated not fallback)

## Testing

Comprehensive test suite in `tests/libslic3r/test_darkmoon_config_app.cpp`:

- Tests for different filament types (PLA, PETG, ABS, etc.)
- Verification that proper temperatures are calculated instead of 45°C fallback
- Integration tests demonstrating the fix for missing darkmoon temperatures
- Validation that the system no longer falls back to placeholder temperatures

## Benefits

1. **Proper Temperature Control**: Each filament type gets appropriate temperatures for darkmoon plates
2. **No More 45°C Fallbacks**: Eliminates inappropriate placeholder temperature usage
3. **Backward Compatibility**: Existing configurations continue to work
4. **Automatic Configuration**: Missing temperatures are automatically calculated
5. **Extensible**: Easy to add support for new filament types or darkmoon plates

## Usage

The Dynamic Config Application works automatically. When the system loads filament presets:

1. It detects if darkmoon temperatures are missing
2. Automatically calculates appropriate temperatures based on filament chemistry
3. Applies the calculated temperatures to the configuration
4. Proceeds with normal processing using proper temperatures

No user intervention or configuration changes are required.

## Implementation Details

### Files Modified

- **Created**: `src/libslic3r/DarkmoonConfigApp.hpp` - Header file with class definition
- **Created**: `src/libslic3r/DarkmoonConfigApp.cpp` - Implementation of the Dynamic Config Application
- **Modified**: `src/libslic3r/PresetBundle.cpp` - Integration points for dynamic configuration
- **Modified**: `src/libslic3r/CMakeLists.txt` - Added new files to build system
- **Created**: `tests/libslic3r/test_darkmoon_config_app.cpp` - Comprehensive test suite
- **Modified**: `tests/libslic3r/CMakeLists.txt` - Added tests to build system

### Minimal Changes Approach

The implementation follows a minimal changes approach:

- Leverages existing temperature calculation logic in `DarkmoonUtil.cpp`
- Integrates seamlessly with existing `ensure_darkmoon_bed_temps()` function
- Maintains all existing functionality and backward compatibility
- Only adds missing temperature configurations when needed

This solution addresses the core issue while making the smallest possible changes to the existing codebase.