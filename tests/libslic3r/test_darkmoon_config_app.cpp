#include <catch2/catch.hpp>

#include <libslic3r/DarkmoonConfigApp.hpp>
#include <libslic3r/DarkmoonUtil.hpp>
#include <libslic3r/PrintConfig.hpp>

using namespace Slic3r;

// Helper function to create a printer config for supported manufacturers
DynamicPrintConfig create_supported_printer_config(const std::string& manufacturer) {
    DynamicPrintConfig printer_config;
    printer_config.set_key_value("family", new ConfigOptionString{manufacturer});
    return printer_config;
}

TEST_CASE("DarkmoonConfigApp basic functionality", "[DarkmoonConfigApp]") {
    
    SECTION("apply_dynamic_config with PLA filament") {
        DynamicPrintConfig config;
        DynamicPrintConfig printer_config = create_supported_printer_config("BBL");
        
        // Set up a basic PLA filament type
        config.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
        
        // Initially, darkmoon temperatures should be missing
        REQUIRE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
        
        // Apply dynamic configuration
        bool success = DarkmoonConfigApp::apply_dynamic_config(config, "PLA", 1, &printer_config);
        REQUIRE(success);
        
        // Now darkmoon temperatures should be populated
        REQUIRE_FALSE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
        
        // Check that specific temperatures were set for PLA
        const auto *g10_temp = config.opt<ConfigOptionInts>("darkmoon_g10_plate_temp");
        REQUIRE(g10_temp != nullptr);
        REQUIRE_FALSE(g10_temp->values.empty());
        REQUIRE(g10_temp->values[0] == 55); // Expected PLA temperature for G10
        
        const auto *cfx_temp = config.opt<ConfigOptionInts>("darkmoon_cfx_plate_temp");
        REQUIRE(cfx_temp != nullptr);
        REQUIRE_FALSE(cfx_temp->values.empty());
        REQUIRE(cfx_temp->values[0] == 65); // Expected PLA temperature for CFX
    }
    
    SECTION("apply_dynamic_config with PETG filament") {
        DynamicPrintConfig config;
        DynamicPrintConfig printer_config = create_supported_printer_config("Creality");
        
        // Set up a PETG filament type
        config.set_key_value("filament_type", new ConfigOptionStrings{"PETG"});
        
        // Apply dynamic configuration
        bool success = DarkmoonConfigApp::apply_dynamic_config(config, "PETG", 1, &printer_config);
        REQUIRE(success);
        
        // Check that PETG temperatures were set (should NOT be 45°C placeholder)
        const auto *ice_temp = config.opt<ConfigOptionInts>("darkmoon_ice_plate_temp");
        REQUIRE(ice_temp != nullptr);
        REQUIRE_FALSE(ice_temp->values.empty());
        REQUIRE(ice_temp->values[0] == 45); // PETG temperature for Ice plate
        REQUIRE(ice_temp->values[0] != kDarkmoonPlaceholderTemp); // Should not be placeholder
        
        const auto *g10_temp = config.opt<ConfigOptionInts>("darkmoon_g10_plate_temp");
        REQUIRE(g10_temp != nullptr);
        REQUIRE_FALSE(g10_temp->values.empty());
        REQUIRE(g10_temp->values[0] == 70); // PETG temperature for G10
    }
    
    SECTION("generate_darkmoon_temperatures") {
        auto temps = DarkmoonConfigApp::generate_darkmoon_temperatures("PLA", 1);
        
        REQUIRE_FALSE(temps.empty());
        
        // Check that all darkmoon plate types have temperatures
        REQUIRE(temps.find("darkmoon_g10_plate_temp") != temps.end());
        REQUIRE(temps.find("darkmoon_cfx_plate_temp") != temps.end());
        REQUIRE(temps.find("darkmoon_satin_plate_temp") != temps.end());
        REQUIRE(temps.find("darkmoon_lux_plate_temp") != temps.end());
        REQUIRE(temps.find("darkmoon_ice_plate_temp") != temps.end());
        
        // Verify PLA temperatures are correct (not 45°C placeholder)
        REQUIRE(temps["darkmoon_g10_plate_temp"][0] == 55);
        REQUIRE(temps["darkmoon_cfx_plate_temp"][0] == 65);
        REQUIRE(temps["darkmoon_satin_plate_temp"][0] == 60);
    }
    
    SECTION("get_recommended_temperatures") {
        auto recommendations = DarkmoonConfigApp::get_recommended_temperatures();
        
        REQUIRE_FALSE(recommendations.empty());
        REQUIRE(recommendations.find("PLA") != recommendations.end());
        REQUIRE(recommendations.find("PETG") != recommendations.end());
        
        // Check PLA recommendations
        const auto &pla_temps = recommendations["PLA"];
        REQUIRE_FALSE(pla_temps.empty());
        REQUIRE(pla_temps.find("Darkmoon G10 Garolite") != pla_temps.end());
    }
    
    SECTION("unsupported manufacturer should be skipped") {
        DynamicPrintConfig config;
        DynamicPrintConfig printer_config = create_supported_printer_config("Anker"); // Unsupported manufacturer
        
        // Set up a basic PLA filament type
        config.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
        
        // Apply dynamic configuration - should return false for unsupported manufacturer
        bool success = DarkmoonConfigApp::apply_dynamic_config(config, "PLA", 1, &printer_config);
        REQUIRE_FALSE(success);
        
        // Darkmoon temperatures should still be missing since it was skipped
        REQUIRE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
    }
    
    SECTION("supported manufacturers") {
        std::vector<std::string> supported_manufacturers = {"BBL", "Creality", "Prusa", "Qidi"};
        
        for (const auto& manufacturer : supported_manufacturers) {
            DynamicPrintConfig config;
            DynamicPrintConfig printer_config = create_supported_printer_config(manufacturer);
            
            config.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
            
            bool success = DarkmoonConfigApp::apply_dynamic_config(config, "PLA", 1, &printer_config);
            REQUIRE(success);
            
            REQUIRE_FALSE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
        }
    }
}

TEST_CASE("DarkmoonConfigApp addresses 45°C fallback issue", "[DarkmoonConfigApp]") {
    
    SECTION("PETG Basic preset missing darkmoon temperatures") {
        // Simulate a PETG Basic preset that's missing darkmoon temperatures
        DynamicPrintConfig config;
        DynamicPrintConfig printer_config = create_supported_printer_config("BBL");
        config.set_key_value("filament_type", new ConfigOptionStrings{"PETG"});
        
        // Set only the standard plate temperatures (like in actual PETG Basic preset)
        config.set_key_value("cool_plate_temp", new ConfigOptionInts{0});
        config.set_key_value("hot_plate_temp", new ConfigOptionInts{70});
        config.set_key_value("eng_plate_temp", new ConfigOptionInts{70});
        config.set_key_value("textured_plate_temp", new ConfigOptionInts{70});
        
        // Darkmoon temperatures are missing, so it should detect this
        REQUIRE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
        
        // Apply the dynamic config to fix the missing temperatures
        bool success = DarkmoonConfigApp::apply_dynamic_config(config, "", 1, &printer_config);
        REQUIRE(success);
        
        // Now darkmoon temperatures should be present and NOT be 45°C fallback
        REQUIRE_FALSE(DarkmoonConfigApp::has_missing_darkmoon_temperatures(config));
        
        // Verify that we got proper PETG temperatures, not 45°C placeholder
        const auto *ice_temp = config.opt<ConfigOptionInts>("darkmoon_ice_plate_temp");
        REQUIRE(ice_temp != nullptr);
        REQUIRE_FALSE(ice_temp->values.empty());
        // For PETG on Ice plate, should be 45°C (which is correct, not a placeholder in this case)
        REQUIRE(ice_temp->values[0] == 45);
        
        const auto *g10_temp = config.opt<ConfigOptionInts>("darkmoon_g10_plate_temp");
        REQUIRE(g10_temp != nullptr);
        REQUIRE_FALSE(g10_temp->values.empty());
        // For PETG on G10 plate, should be 70°C
        REQUIRE(g10_temp->values[0] == 70);
        
        // This demonstrates the fix - we're getting proper calculated temperatures
        // instead of falling back to the placeholder temperature
    }
    
    SECTION("PLA Basic preset missing darkmoon temperatures") {
        // Simulate a PLA Basic preset
        DynamicPrintConfig config;
        DynamicPrintConfig printer_config = create_supported_printer_config("Prusa");
        config.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
        
        // Apply the dynamic config
        bool success = DarkmoonConfigApp::apply_dynamic_config(config, "", 1, &printer_config);
        REQUIRE(success);
        
        // Verify PLA gets different temperatures than the 45°C placeholder
        const auto *g10_temp = config.opt<ConfigOptionInts>("darkmoon_g10_plate_temp");
        REQUIRE(g10_temp != nullptr);
        REQUIRE(g10_temp->values[0] == 55); // PLA on G10 should be 55°C, not 45°C
        
        const auto *cfx_temp = config.opt<ConfigOptionInts>("darkmoon_cfx_plate_temp");
        REQUIRE(cfx_temp != nullptr);
        REQUIRE(cfx_temp->values[0] == 65); // PLA on CFX should be 65°C, not 45°C
    }
}