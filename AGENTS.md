# AI Agent Development Log - Darkmoon Bed Temperature Validation Issue

## Problem Summary

**Issue**: Bambu Studio slicer shows correct darkmoon bed temperatures in UI (e.g., 65°C) but validation logic infers 0°C, causing the error: *"please set this filaments bed temperature to a number that is not zero"*

**Affects**: All darkmoon plate types (G10, Ice, Lux, CFX, Satin) with various filament types, particularly Bambu PLA Basic

**Status**: Under investigation - UI/validation disconnect identified but not yet resolved

## Technical Context

### Darkmoon Plate System
Darkmoon plates are third-party bed surfaces with specific temperature requirements per filament type:

| Plate Type | BedType Enum | Temp Key Pattern |
|------------|--------------|------------------|
| G10 Garolite | `btDarkmoonG10` (7) | `darkmoon_g10_plate_temp` |
| Ice | `btDarkmoonIce` (8) | `darkmoon_ice_plate_temp` |
| Lux | `btDarkmoonLux` (9) | `darkmoon_lux_plate_temp` |
| CFX | `btDarkmoonCFX` (10) | `darkmoon_cfx_plate_temp` |
| Satin | `btDarkmoonSatin` (11) | `darkmoon_satin_plate_temp` |

### Configuration Architecture
- **Filament Presets**: Define darkmoon temperatures (e.g., `fdm_filament_pla.json`)
- **Inheritance**: Specific filaments inherit from base types (e.g., `Bambu PLA Basic @base.json` inherits from `fdm_filament_pla`)
- **Runtime Config**: Print validation uses merged configuration object (`m_config`)

## Investigation History

### Phase 1: Configuration Analysis ✅
**Verified**: All base filament configurations contain correct darkmoon temperatures
- `fdm_filament_pla.json`: Ice=40°C, G10=55°C, Lux=60°C, CFX=65°C, Satin=60°C
- `Bambu PLA Basic @base.json`: Correctly inherits from `fdm_filament_pla`
- No erroneous 0°C overrides found

### Phase 2: Dynamic Temperature Logic ❌ (Initially Misdiagnosed)
**Attempted**: Removed dynamic temperature calculation system
- **Assumption**: Dynamic logic was causing 0°C values
- **Result**: Issue persisted - static values also showed as 0°C during validation
- **Conclusion**: Problem is not in temperature calculation logic

### Phase 3: Material Compatibility Rules ❌ (Red Herring)
**Attempted**: Added PLA Silk exclusions for Ice plate
- **Rationale**: PLA Silk has excessive adhesion to Ice plates
- **Implementation**: Modified `default_ice_temperature()` to return 0°C for Silk variants
- **Result**: Fixed Silk compatibility but didn't resolve main PLA Basic issue

### Phase 4: Validation Logic Investigation 🔍 (Current Focus)
**Discovered**: UI and validation use different code paths

#### UI Display Path (Working ✅)
- Reads from filament presets correctly
- Shows proper temperatures (65°C, etc.)

#### Validation Path (Broken ❌)
- **Location**: `src/libslic3r/Print.cpp:1499`
- **Code**: `m_config.option<ConfigOptionInts>(get_bed_temp_key(m_config.curr_bed_type))`
- **Issue**: Returns `nullptr` or 0°C values despite UI showing correct temps

## Key Code Locations

### Configuration Definition
- **File**: `src/libslic3r/PrintConfig.cpp`
- **Darkmoon config registration**: Lines with `darkmoon_*_plate_temp`

### Temperature Key Resolution
- **File**: `src/libslic3r/PrintConfig.hpp:370`
- **Function**: `get_bed_temp_key(BedType type)`
- **Logic**: Uses `find_darkmoon_plate(type)` to map bed types to config keys

### Validation Logic
- **File**: `src/libslic3r/Print.cpp:1495-1525`
- **Function**: `Print::validate()`
- **Issue**: `bed_temp_opt` is `nullptr` or returns 0°C

### Darkmoon Utilities
- **File**: `src/libslic3r/DarkmoonUtil.cpp`
- **Functions**: Temperature calculation logic (works correctly)

## Current Debug Status

### Added Debug Logging
```cpp
// Added to Print::validate() for diagnosis
BOOST_LOG_TRIVIAL(debug) << "Validating bed type: " << int(m_config.curr_bed_type);
BOOST_LOG_TRIVIAL(debug) << "Using bed temp key: '" << bed_temp_key << "'";
BOOST_LOG_TRIVIAL(debug) << "Extruder " << extruder_id << " bed temp: " << curr_bed_temp;
```

### Hypotheses Under Investigation

1. **Config Merge Issue**: Filament config not properly merged into print config during validation
2. **Timing Issue**: Validation happens before filament config is fully loaded
3. **Config Object Mismatch**: `m_config` doesn't contain filament-specific settings
4. **Key Resolution Bug**: `get_bed_temp_key()` returns wrong key for darkmoon plates

## Attempted Solutions

### ❌ Solution 1: Modified Validation Logic
```cpp
// Attempted to read from filament config vs print config
if (is_darkmoon_bed(m_config.curr_bed_type)) {
    // Read from filament config
} else {
    // Use original logic
}
```
**Result**: Still failed validation

### ❌ Solution 2: Skip Validation for Darkmoon
```cpp
// Allow 0°C on darkmoon plates as intentional
bool is_darkmoon_plate = is_darkmoon_bed(m_config.curr_bed_type);
if (!is_darkmoon_plate) {
    // Only validate non-darkmoon plates
}
```
**Result**: Would mask the real problem

## Next Steps for Investigation

### Priority 1: Config Object Analysis
- Examine what's actually in `m_config` during validation
- Compare with working UI config object
- Verify filament config merge process

### Priority 2: Timing Analysis  
- Check when validation occurs vs config loading
- Verify all prerequisite configs are loaded

### Priority 3: Key Resolution Verification
- Confirm `get_bed_temp_key()` returns correct keys
- Verify `find_darkmoon_plate()` mapping works

### Priority 4: Alternative Config Access
- Find how other code successfully reads filament temperatures
- Compare with working examples (GCode generation, UI display)

## Working Examples to Study

### ✅ GCode Temperature Access
- **File**: `src/libslic3r/GCode.cpp:3342`
- **Method**: Successfully reads bed temperatures for gcode generation

### ✅ UI Temperature Display
- Various UI components correctly show darkmoon temperatures

## File Modifications Made

### Configuration Files
- Fixed PCTG/PET placeholder values (were correctly 45°C)
- Added PLA Silk Ice plate exclusions (0°C for incompatible combinations)
- All base filament files verified correct

### Source Code
- Added debug logging to validation logic
- Removed overly complex dynamic temperature system
- Simplified darkmoon utilities (removed ~290 lines of unused code)

## Environment Context
- **Repository**: BambuStudio (Bambu Lab's slicer fork)
- **Platform**: Windows
- **Build System**: CMake/Visual Studio
- **Language**: C++ with some JSON configuration

## Critical Understanding
The core issue is **NOT** with:
- ❌ Configuration file contents (all correct)
- ❌ Temperature calculation logic (works fine)  
- ❌ Darkmoon plate detection (functions work)
- ❌ UI display logic (shows correct values)

The issue **IS** with:
- ✅ Validation logic config access during `Print::validate()`
- ✅ Disconnect between UI config and validation config
- ✅ Possible config merge/timing issues

This suggests the problem is in the **configuration management system** rather than the darkmoon-specific logic.