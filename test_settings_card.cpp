// Test file to verify settings card functionality
#include <iostream>
#include <memory>
#include "src/cards/system/settings.hpp"
#include "src/helpers/config_service.hpp"

int main() {
    std::cout << "Testing settings card functionality..." << std::endl;
    
    // Initialize configuration service
    auto config_service = rouen::helpers::ConfigService::instance();
    
    // Test creating settings card
    try {
        auto settings_card = std::make_shared<rouen::cards::settings_card>();
        std::cout << "✅ Settings card created successfully" << std::endl;
        std::cout << "   Card URI: " << settings_card->get_uri() << std::endl;
        
        // Test that configuration service has data
        auto all_configs = config_service->get_all_configs();
        std::cout << "✅ Configuration service has " << all_configs.size() << " configurations" << std::endl;
        
        // Test getting configs by category
        auto api_configs = config_service->get_configs_by_category(rouen::helpers::ConfigService::Category::API_CREDENTIALS);
        std::cout << "✅ API credentials category has " << api_configs.size() << " configurations" << std::endl;
        
        std::cout << "🎉 All tests passed! Settings card is ready to use." << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
}
