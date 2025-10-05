#ifndef slic3r_DarkmoonConfigApp_hpp_
#define slic3r_DarkmoonConfigApp_hpp_

#include <string>
#include "DarkmoonUtil.hpp"

namespace Slic3r {

class DynamicPrintConfig;

/**
 * Minimal Darkmoon Config Application
 * 
 * Provides backward compatibility for darkmoon configuration.
 * The actual temperature values are now stored statically in filament presets.
 */
class DarkmoonConfigApp {
public:
    // Backward compatibility functions (mostly stubs now)
    static bool apply_dynamic_config(DynamicPrintConfig &config, 
                                   const std::string &filament_type = "",
                                   size_t extruder_count = 1,
                                   const DynamicPrintConfig *printer_config = nullptr);

    static bool apply_dynamic_config_if_missing(DynamicPrintConfig &config,
                                              const std::string &filament_type,
                                              size_t extruder_count,
                                              const DynamicPrintConfig *printer_config = nullptr);

    // Still used by GUI for display of placeholder values
    static int get_display_temperature(const DynamicPrintConfig &config,
                                     const std::string &darkmoon_temp_key,
                                     int stored_value);

    // Helper functions
    static std::string determine_filament_type(const DynamicPrintConfig &config);
    static bool is_darkmoon_supported_manufacturer(const DynamicPrintConfig *printer_config);
};

} // namespace Slic3r

#endif // slic3r_DarkmoonConfigApp_hpp_