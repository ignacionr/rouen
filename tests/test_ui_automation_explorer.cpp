#include <gtest/gtest.h>
#include "../src/helpers/ui_automation_explorer.hpp"

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

TEST(UIAutomationExplorerTest, CaptureWindowScreenshotNonExistentTarget) {
    auto res = rouen::helpers::ui_automation_explorer::capture_window_screenshot(
        99999999, 0, "NonExistentWindowPattern_12345", "/tmp/non_existent.png"
    );
    EXPECT_FALSE(res.success);
    EXPECT_EQ(res.error, "Target window not found");
}

TEST(UIAutomationExplorerTest, CaptureWindowScreenshotInvalidHwnd) {
    auto res = rouen::helpers::ui_automation_explorer::capture_window_screenshot(
        0, 9999999999ULL, "", "/tmp/non_existent_hwnd.png"
    );
    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.error.empty());
}

TEST(UIAutomationExplorerTest, WindowScopeDefaultsAndWin32ActionGuards) {
    rouen::helpers::ui_window_scope scope;
    EXPECT_EQ(scope.hwnd, 0ULL);
    EXPECT_TRUE(scope.window_title.empty());
    EXPECT_TRUE(scope.window_class.empty());

    int64_t pid = ::getpid();

    // On non-Windows platforms, Win32 message actions return a platform error
#if !defined(_WIN32)
    auto click_res = rouen::helpers::ui_automation_explorer::perform_control_action(
        pid, "btn_submit", "win32_click", "", scope);
    EXPECT_FALSE(click_res.success);
    EXPECT_NE(click_res.error_message.find("only supported on Windows"), std::string::npos);

    auto text_res = rouen::helpers::ui_automation_explorer::perform_control_action(
        pid, "txt_field", "win32_set_text", "hello world", scope);
    EXPECT_FALSE(text_res.success);
    EXPECT_NE(text_res.error_message.find("only supported on Windows"), std::string::npos);

    auto cmd_res = rouen::helpers::ui_automation_explorer::perform_control_action(
        pid, "1001", "win32_command", "", scope);
    EXPECT_FALSE(cmd_res.success);
    EXPECT_NE(cmd_res.error_message.find("only supported on Windows"), std::string::npos);
#endif

    // Scoped extraction on non-existent window title returns empty list
    scope.window_title = "NonExistentWindow_998877";
    auto vals = rouen::helpers::ui_automation_explorer::extract_process_values(pid, true, 3, scope);
    EXPECT_TRUE(vals.empty());
}
