#include <gtest/gtest.h>
#include "../src/helpers/ui_automation_explorer.hpp"
#include <unistd.h>

TEST(UIAutomationExplorerTest, ResultExtractsValuesFromTree) {
    rouen::helpers::ui_automation_result res;
    res.success = true;
    res.total_node_count = 4;

    res.root.role = "Window";
    res.root.name = "Main Window";
    res.root.id = "win_1";

    rouen::helpers::ui_element_node edit_node;
    edit_node.role = "Edit";
    edit_node.name = "Username Field";
    edit_node.id = "txt_username";
    edit_node.value = "john_doe";
    edit_node.description = "Enter your username";

    rouen::helpers::ui_element_node btn_node;
    btn_node.role = "Button";
    btn_node.name = "Submit";
    btn_node.id = "btn_submit";

    rouen::helpers::ui_element_node doc_node;
    doc_node.role = "Document";
    doc_node.name = "Editor Content";
    doc_node.id = "doc_body";
    doc_node.value = "Hello World Text";

    res.root.children.push_back(edit_node);
    res.root.children.push_back(btn_node);
    res.root.children.push_back(doc_node);

    auto extracted_edit_boxes = res.extract_values(true);
    EXPECT_EQ(extracted_edit_boxes.size(), 2u);

    EXPECT_EQ(extracted_edit_boxes[0].id, "txt_username");
    EXPECT_EQ(extracted_edit_boxes[0].value, "john_doe");
    EXPECT_EQ(extracted_edit_boxes[0].description, "Enter your username");

    EXPECT_EQ(extracted_edit_boxes[1].id, "doc_body");
    EXPECT_EQ(extracted_edit_boxes[1].value, "Hello World Text");
}

TEST(UIAutomationExplorerTest, RequestControlValueLookup) {
    int64_t pid = ::getpid();
    auto val = rouen::helpers::ui_automation_explorer::request_control_value(pid, "NonExistentControlId12345");
    EXPECT_FALSE(val.has_value());
}
