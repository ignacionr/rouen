#include "dummy_jira_card.hpp"

// This is a temporary placeholder for the JIRA search card
// It will be replaced with a full implementation once the JIRA model is fully integrated

namespace rouen::cards {

class jira_search_dummy_card : public dummy_jira_card {
public:
    jira_search_dummy_card(std::string_view query = "") {
        name("JIRA Search");
        query_ = query;
    }
    
    std::string get_uri() const override {
        return "jira-search";
    }
    
    void set_message() override {
        if (!query_.empty()) {
            message_ = std::format("JIRA Search for \"{}\" will be available soon!", query_);
        } else {
            message_ = "JIRA Search will be available soon!";
        }
    }
    
    // Static factory method for creating a search card with an initial query
    static ptr create_with_query(std::string_view query) {
        return std::make_shared<jira_search_dummy_card>(query);
    }
    
private:
    std::string query_;
};

} // namespace rouen::cards