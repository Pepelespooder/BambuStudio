#ifndef slic3r_DarkmoonConfigApp_hpp_
#define slic3r_DarkmoonConfigApp_hpp_

#include <string>
#include <vector>
#include <map>
#include <optional>
#include "DarkmoonUtil.hpp"

namespace Slic3r {

class DynamicPrintConfig;

/**
 * Dynamic Config Application for Darkmoon Utilities
 * 
 * This application addresses the issue where temperatures fall back to 45°C 
 * due to missing darkmoon plate temperature configurations in filament presets
 * like PLA Basic, PLA Matte, PETG Basic, and PETG High Speed.
 */
class DarkmoonConfigApp {
public:
    /**
     * Apply dynamic darkmoon configuration to a filament config
     * This ensures that all darkmoon plate temperatures are properly populated
     * based on the filament type instead of falling back to placeholder values.
     * Only applies to supported manufacturers: Creality, Prusa, Qidi, and BBL.
     */
    static bool apply_dynamic_config(DynamicPrintConfig &config, 
                                   const std::string &filament_type = "",
                                   size_t extruder_count = 1,
                                   const DynamicPrintConfig *printer_config = nullptr);

    /**
     * Generate darkmoon temperature configuration for a specific filament type
     * Returns a map of darkmoon temperature keys to their appropriate values
     */
    static std::map<std::string, std::vector<int>> generate_darkmoon_temperatures(
        const std::string &filament_type, 
        size_t extruder_count = 1);

    /**
     * Check if a filament config has missing or placeholder darkmoon temperatures
     */
    static bool has_missing_darkmoon_temperatures(const DynamicPrintConfig &config);

    /**
     * Update filament preset files with missing darkmoon temperature configurations
     * This is used to fix existing preset files that lack darkmoon configurations
     */
    static bool update_filament_preset_file(const std::string &preset_path,
                                          const std::string &filament_type = "");

    /**
     * Validate darkmoon temperature configuration in a config
     */
    static bool validate_darkmoon_config(const DynamicPrintConfig &config,
                                       std::vector<std::string> &validation_errors);

    /**
     * Get recommended darkmoon temperatures for common filament types
     */
    static std::map<std::string, std::map<std::string, int>> get_recommended_temperatures();

private:
    /**
     * Check if printer manufacturer supports darkmoon plates
     */
    static bool is_darkmoon_supported_manufacturer(const DynamicPrintConfig *printer_config);

    /**
     * Internal helper to determine filament type from config if not provided
     */
    static std::string determine_filament_type(const DynamicPrintConfig &config);

    /**
     * Internal helper to populate specific darkmoon temperature keys
     */
    static void populate_darkmoon_key(DynamicPrintConfig &config,
                                    const std::string &darkmoon_key,
                                    const std::vector<int> &values);
};

} // namespace Slic3r

#endif // slic3r_DarkmoonConfigApp_hpp_