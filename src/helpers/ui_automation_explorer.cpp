#include "ui_automation_explorer.hpp"

#include <format>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#endif

namespace rouen::helpers {

#if defined(__APPLE__)

static std::string cfstring_to_utf8(CFStringRef cfstr) {
    if (!cfstr) return "";
    char buf[1024];
    if (CFStringGetCString(cfstr, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        return std::string(buf);
    }
    CFIndex len = CFStringGetLength(cfstr);
    if (len <= 0) return "";
    CFIndex max_len = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    if (max_len <= 0) return "";
    std::string result(static_cast<size_t>(max_len), '\0');
    if (CFStringGetCString(cfstr, result.data(), max_len, kCFStringEncodingUTF8)) {
        result.resize(std::strlen(result.c_str()));
        return result;
    }
    return "";
}

static std::string cftype_to_string(CFTypeRef val) {
    if (!val) return "";
    CFTypeID type = CFGetTypeID(val);
    if (type == CFStringGetTypeID()) {
        return cfstring_to_utf8(static_cast<CFStringRef>(val));
    } else if (type == CFBooleanGetTypeID()) {
        return CFBooleanGetValue(static_cast<CFBooleanRef>(val)) ? "true" : "false";
    } else if (type == CFNumberGetTypeID()) {
        double d = 0;
        if (CFNumberGetValue(static_cast<CFNumberRef>(val), kCFNumberDoubleType, &d)) {
            if (std::floor(d) == d) return std::to_string(static_cast<long long>(d));
            return std::format("{:.2f}", d);
        }
    } else if (type == AXValueGetTypeID()) {
        AXValueRef axval = static_cast<AXValueRef>(const_cast<void*>(val));
        AXValueType vtype = AXValueGetType(axval);
        if (vtype == kAXValueTypeCGPoint) {
            CGPoint pt;
            if (AXValueGetValue(axval, kAXValueTypeCGPoint, &pt)) {
                return std::format("({:.1f}, {:.1f})", pt.x, pt.y);
            }
        } else if (vtype == kAXValueTypeCGSize) {
            CGSize sz;
            if (AXValueGetValue(axval, kAXValueTypeCGSize, &sz)) {
                return std::format("{:.1f} x {:.1f}", sz.width, sz.height);
            }
        } else if (vtype == kAXValueTypeCGRect) {
            CGRect r;
            if (AXValueGetValue(axval, kAXValueTypeCGRect, &r)) {
                return std::format("({:.1f}, {:.1f}, {:.1f}, {:.1f})", r.origin.x, r.origin.y, r.size.width, r.size.height);
            }
        }
    } else if (type == AXUIElementGetTypeID()) {
        return "[AXUIElement]";
    } else if (type == CFArrayGetTypeID()) {
        CFArrayRef arr = static_cast<CFArrayRef>(val);
        CFIndex count = CFArrayGetCount(arr);
        return std::format("[Array ({})]", count);
    }
    return "[CFType]";
}

static void populate_ax_element(AXUIElementRef element, ui_element_node& node, int depth, int max_depth, int max_children, size_t& total_count) {
    total_count++;

    // Role
    CFTypeRef role_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXRoleAttribute, &role_ref) == kAXErrorSuccess && role_ref) {
        node.role = cftype_to_string(role_ref);
        if (node.role.starts_with("AX")) node.role = node.role.substr(2);
        CFRelease(role_ref);
    } else {
        node.role = "Unknown";
    }

    // Subrole
    CFTypeRef subrole_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXSubroleAttribute, &subrole_ref) == kAXErrorSuccess && subrole_ref) {
        node.subrole = cftype_to_string(subrole_ref);
        if (node.subrole.starts_with("AX")) node.subrole = node.subrole.substr(2);
        CFRelease(subrole_ref);
    }

    // Title / Name
    CFTypeRef title_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXTitleAttribute, &title_ref) == kAXErrorSuccess && title_ref) {
        node.name = cftype_to_string(title_ref);
        CFRelease(title_ref);
    }

    // Description
    CFTypeRef desc_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute, &desc_ref) == kAXErrorSuccess && desc_ref) {
        node.description = cftype_to_string(desc_ref);
        CFRelease(desc_ref);
    }
    if (node.description.empty()) {
        CFTypeRef help_ref = nullptr;
        if (AXUIElementCopyAttributeValue(element, kAXHelpAttribute, &help_ref) == kAXErrorSuccess && help_ref) {
            node.description = cftype_to_string(help_ref);
            CFRelease(help_ref);
        }
    }

    // Value
    CFTypeRef val_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXValueAttribute, &val_ref) == kAXErrorSuccess && val_ref) {
        node.value = cftype_to_string(val_ref);
        CFRelease(val_ref);
    }
    if (!node.value.empty()) {
        node.attributes.push_back({"Value", node.value});
    }

    // Identifier
    CFTypeRef id_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXIdentifierAttribute, &id_ref) == kAXErrorSuccess && id_ref) {
        node.id = cftype_to_string(id_ref);
        CFRelease(id_ref);
    }

    // Position
    CFTypeRef pos_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXPositionAttribute, &pos_ref) == kAXErrorSuccess && pos_ref) {
        if (CFGetTypeID(pos_ref) == AXValueGetTypeID()) {
            CGPoint pt;
            AXValueRef axpos = static_cast<AXValueRef>(const_cast<void*>(pos_ref));
            if (AXValueGetValue(axpos, kAXValueTypeCGPoint, &pt)) {
                node.x = static_cast<float>(pt.x);
                node.y = static_cast<float>(pt.y);
            }
        }
        CFRelease(pos_ref);
    }

    // Size
    CFTypeRef size_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXSizeAttribute, &size_ref) == kAXErrorSuccess && size_ref) {
        if (CFGetTypeID(size_ref) == AXValueGetTypeID()) {
            CGSize sz;
            AXValueRef axsz = static_cast<AXValueRef>(const_cast<void*>(size_ref));
            if (AXValueGetValue(axsz, kAXValueTypeCGSize, &sz)) {
                node.width = static_cast<float>(sz.width);
                node.height = static_cast<float>(sz.height);
            }
        }
        CFRelease(size_ref);
    }

    // Enabled
    CFTypeRef enabled_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXEnabledAttribute, &enabled_ref) == kAXErrorSuccess && enabled_ref) {
        if (CFGetTypeID(enabled_ref) == CFBooleanGetTypeID()) {
            node.enabled = (CFBooleanGetValue(static_cast<CFBooleanRef>(enabled_ref)) != 0);
        }
        CFRelease(enabled_ref);
    }

    // Focused
    CFTypeRef focused_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXFocusedAttribute, &focused_ref) == kAXErrorSuccess && focused_ref) {
        if (CFGetTypeID(focused_ref) == CFBooleanGetTypeID()) {
            node.focused = (CFBooleanGetValue(static_cast<CFBooleanRef>(focused_ref)) != 0);
        }
        CFRelease(focused_ref);
    }

    // Attribute list names
    CFArrayRef attr_names = nullptr;
    if (AXUIElementCopyAttributeNames(element, &attr_names) == kAXErrorSuccess && attr_names) {
        CFIndex count = CFArrayGetCount(attr_names);
        for (CFIndex i = 0; i < count; ++i) {
            CFStringRef attr_name = static_cast<CFStringRef>(CFArrayGetValueAtIndex(attr_names, i));
            std::string name_str = cfstring_to_utf8(attr_name);
            CFTypeRef attr_val = nullptr;
            if (AXUIElementCopyAttributeValue(element, attr_name, &attr_val) == kAXErrorSuccess && attr_val) {
                std::string val_str = cftype_to_string(attr_val);
                if (!val_str.empty() && !val_str.starts_with("[AXUIElement") && !val_str.starts_with("[Array")) {
                    node.attributes.push_back({name_str, val_str});
                }
                CFRelease(attr_val);
            }
        }
        CFRelease(attr_names);
    }

    // Children
    if (depth < max_depth) {
        CFTypeRef children_type_ref = nullptr;
        if (AXUIElementCopyAttributeValue(element, kAXChildrenAttribute, &children_type_ref) == kAXErrorSuccess && children_type_ref) {
            if (CFGetTypeID(children_type_ref) == CFArrayGetTypeID()) {
                CFArrayRef children_ref = static_cast<CFArrayRef>(children_type_ref);
                CFIndex child_count = CFArrayGetCount(children_ref);
                CFIndex limit = (std::min)(child_count, static_cast<CFIndex>(max_children));
                for (CFIndex i = 0; i < limit; ++i) {
                    AXUIElementRef child_elem = static_cast<AXUIElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(children_ref, i)));
                    ui_element_node child_node;
                    populate_ax_element(child_elem, child_node, depth + 1, max_depth, max_children, total_count);
                    node.children.push_back(std::move(child_node));
                }
            }
            CFRelease(children_type_ref);
        }
    }
}

#elif defined(_WIN32)

static std::string control_type_to_string(CONTROLTYPEID type_id) {
    switch (type_id) {
        case UIA_ButtonControlTypeId: return "Button";
        case UIA_CalendarControlTypeId: return "Calendar";
        case UIA_CheckBoxControlTypeId: return "CheckBox";
        case UIA_ComboBoxControlTypeId: return "ComboBox";
        case UIA_EditControlTypeId: return "Edit";
        case UIA_HyperlinkControlTypeId: return "Hyperlink";
        case UIA_ImageControlTypeId: return "Image";
        case UIA_ListItemControlTypeId: return "ListItem";
        case UIA_ListControlTypeId: return "List";
        case UIA_MenuControlTypeId: return "Menu";
        case UIA_MenuBarControlTypeId: return "MenuBar";
        case UIA_MenuItemControlTypeId: return "MenuItem";
        case UIA_ProgressBarControlTypeId: return "ProgressBar";
        case UIA_RadioButtonControlTypeId: return "RadioButton";
        case UIA_ScrollBarControlTypeId: return "ScrollBar";
        case UIA_SliderControlTypeId: return "Slider";
        case UIA_SpinnerControlTypeId: return "Spinner";
        case UIA_StatusBarControlTypeId: return "StatusBar";
        case UIA_TabControlTypeId: return "Tab";
        case UIA_TabItemControlTypeId: return "TabItem";
        case UIA_TextControlTypeId: return "Text";
        case UIA_ToolBarControlTypeId: return "ToolBar";
        case UIA_ToolTipControlTypeId: return "ToolTip";
        case UIA_TreeControlTypeId: return "Tree";
        case UIA_TreeItemControlTypeId: return "TreeItem";
        case UIA_CustomControlTypeId: return "Custom";
        case UIA_GroupControlTypeId: return "Group";
        case UIA_ThumbControlTypeId: return "Thumb";
        case UIA_DataGridControlTypeId: return "DataGrid";
        case UIA_DataItemControlTypeId: return "DataItem";
        case UIA_DocumentControlTypeId: return "Document";
        case UIA_SplitButtonControlTypeId: return "SplitButton";
        case UIA_WindowControlTypeId: return "Window";
        case UIA_PaneControlTypeId: return "Pane";
        case UIA_HeaderControlTypeId: return "Header";
        case UIA_HeaderItemControlTypeId: return "HeaderItem";
        case UIA_TableControlTypeId: return "Table";
        case UIA_TitleBarControlTypeId: return "TitleBar";
        case UIA_SeparatorControlTypeId: return "Separator";
        case UIA_SemanticZoomControlTypeId: return "SemanticZoom";
        case UIA_AppBarControlTypeId: return "AppBar";
        default: return std::format("Control({})", type_id);
    }
}

static std::string bstr_to_string(BSTR bstr) {
    if (!bstr) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, bstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string str(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, bstr, -1, str.data(), len, NULL, NULL);
    return str;
}

static void populate_uia_element(IUIAutomationElement* element, IUIAutomationTreeWalker* walker, ui_element_node& node, int depth, int max_depth, int max_children, size_t& total_count) {
    if (!element) return;
    total_count++;

    BSTR name_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentName(&name_bstr)) && name_bstr) {
        node.name = bstr_to_string(name_bstr);
        SysFreeString(name_bstr);
    }

    CONTROLTYPEID ct = 0;
    if (SUCCEEDED(element->get_CurrentControlType(&ct))) {
        node.role = control_type_to_string(ct);
    } else {
        node.role = "Unknown";
    }

    BSTR class_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentClassName(&class_bstr)) && class_bstr) {
        node.subrole = bstr_to_string(class_bstr);
        SysFreeString(class_bstr);
    }

    BSTR auto_id = nullptr;
    if (SUCCEEDED(element->get_CurrentAutomationId(&auto_id)) && auto_id) {
        node.id = bstr_to_string(auto_id);
        SysFreeString(auto_id);
    }

    BSTR help_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentHelpText(&help_bstr)) && help_bstr) {
        node.description = bstr_to_string(help_bstr);
        SysFreeString(help_bstr);
    }

    RECT rect{};
    if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect))) {
        node.x = static_cast<float>(rect.left);
        node.y = static_cast<float>(rect.top);
        node.width = static_cast<float>(rect.right - rect.left);
        node.height = static_cast<float>(rect.bottom - rect.top);
    }

    BOOL enabled = TRUE;
    if (SUCCEEDED(element->get_CurrentIsEnabled(&enabled))) {
        node.enabled = (enabled != FALSE);
    }

    BOOL focused = FALSE;
    if (SUCCEEDED(element->get_CurrentHasKeyboardFocus(&focused))) {
        node.focused = (focused != FALSE);
    }

    // 1. Try IUIAutomationValuePattern for Edit controls, Text fields, ComboBoxes, etc.
    IUIAutomationValuePattern* value_pattern = nullptr;
    if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, reinterpret_cast<void**>(&value_pattern))) && value_pattern) {
        BSTR val_bstr = nullptr;
        if (SUCCEEDED(value_pattern->get_CurrentValue(&val_bstr)) && val_bstr) {
            node.value = bstr_to_string(val_bstr);
            SysFreeString(val_bstr);
        }
        BOOL is_readonly = FALSE;
        if (SUCCEEDED(value_pattern->get_CurrentIsReadOnly(&is_readonly))) {
            node.attributes.push_back({"IsReadOnly", is_readonly ? "true" : "false"});
        }
        value_pattern->Release();
    }

    // 2. If node.value is still empty, try IUIAutomationTextPattern for RichEdit, Document controls, multi-line edit fields
    if (node.value.empty()) {
        IUIAutomationTextPattern* text_pattern = nullptr;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId, IID_IUIAutomationTextPattern, reinterpret_cast<void**>(&text_pattern))) && text_pattern) {
            IUIAutomationTextRange* doc_range = nullptr;
            if (SUCCEEDED(text_pattern->get_DocumentRange(&doc_range)) && doc_range) {
                BSTR text_bstr = nullptr;
                if (SUCCEEDED(doc_range->GetText(-1, &text_bstr)) && text_bstr) {
                    node.value = bstr_to_string(text_bstr);
                    SysFreeString(text_bstr);
                }
                doc_range->Release();
            }
            text_pattern->Release();
        }
    }

    // 3. If node.value is still empty, try IUIAutomationRangeValuePattern for Sliders, Spinners, ScrollBars
    if (node.value.empty()) {
        IUIAutomationRangeValuePattern* range_pattern = nullptr;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern, reinterpret_cast<void**>(&range_pattern))) && range_pattern) {
            double double_val = 0.0;
            if (SUCCEEDED(range_pattern->get_CurrentValue(&double_val))) {
                if (std::floor(double_val) == double_val) {
                    node.value = std::to_string(static_cast<long long>(double_val));
                } else {
                    node.value = std::format("{:.2f}", double_val);
                }
            }
            range_pattern->Release();
        }
    }

    // 4. Fallback property lookup via UIA_ValueValuePropertyId
    if (node.value.empty()) {
        VARIANT var_val;
        VariantInit(&var_val);
        if (SUCCEEDED(element->GetCurrentPropertyValue(UIA_ValueValuePropertyId, &var_val))) {
            if (var_val.vt == VT_BSTR && var_val.bstrVal) {
                node.value = bstr_to_string(var_val.bstrVal);
            }
            VariantClear(&var_val);
        }
    }

    // Additional UIA attributes
    BSTR status_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentItemStatus(&status_bstr)) && status_bstr) {
        std::string s = bstr_to_string(status_bstr);
        if (!s.empty()) node.attributes.push_back({"ItemStatus", s});
        SysFreeString(status_bstr);
    }

    BSTR type_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentItemType(&type_bstr)) && type_bstr) {
        std::string t = bstr_to_string(type_bstr);
        if (!t.empty()) node.attributes.push_back({"ItemType", t});
        SysFreeString(type_bstr);
    }

    BSTR loc_bstr = nullptr;
    if (SUCCEEDED(element->get_CurrentLocalizedControlType(&loc_bstr)) && loc_bstr) {
        std::string l = bstr_to_string(loc_bstr);
        if (!l.empty()) node.attributes.push_back({"LocalizedControlType", l});
        SysFreeString(loc_bstr);
    }

    if (!node.id.empty()) node.attributes.push_back({"AutomationId", node.id});
    if (!node.subrole.empty()) node.attributes.push_back({"ClassName", node.subrole});
    if (!node.value.empty()) node.attributes.push_back({"Value", node.value});
    if (!node.description.empty()) node.attributes.push_back({"HelpText", node.description});

    if (depth < max_depth && walker) {
        IUIAutomationElement* child = nullptr;
        if (SUCCEEDED(walker->GetFirstChildElement(element, &child)) && child) {
            int count = 0;
            while (child && count < max_children) {
                ui_element_node child_node;
                populate_uia_element(child, walker, child_node, depth + 1, max_depth, max_children, total_count);
                node.children.push_back(std::move(child_node));
                count++;

                IUIAutomationElement* next = nullptr;
                HRESULT hr = walker->GetNextSiblingElement(child, &next);
                child->Release();
                child = (SUCCEEDED(hr)) ? next : nullptr;
            }
            if (child) child->Release();
        }
    }
}

#endif

bool ui_automation_explorer::check_accessibility_permissions(bool prompt_if_missing) {
#if defined(__APPLE__)
    if (AXIsProcessTrusted()) return true;
    if (prompt_if_missing) {
        const void* keys[] = { kAXTrustedCheckOptionPrompt };
        const void* values[] = { kCFBooleanTrue };
        CFDictionaryRef options = CFDictionaryCreate(
            kCFAllocatorDefault, keys, values, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        bool trusted = AXIsProcessTrustedWithOptions(options);
        if (options) CFRelease(options);
        return trusted;
    }
    return false;
#else
    (void)prompt_if_missing;
    return true;
#endif
}

ui_automation_result ui_automation_explorer::inspect_process(int64_t pid, int max_depth, int max_children_per_node) {
    ui_automation_result result;
    if (pid <= 0) {
        result.error_message = "Invalid process PID";
        return result;
    }

#if defined(__APPLE__)
    if (!check_accessibility_permissions(false)) {
        result.permission_denied = true;
        result.error_message = "Accessibility permission is required to inspect UI Automation elements on macOS.";
        return result;
    }

    AXUIElementRef app_ref = AXUIElementCreateApplication(static_cast<pid_t>(pid));
    if (!app_ref) {
        result.error_message = std::format("Failed to create AXUIElement for PID {}", pid);
        return result;
    }

    result.root.role = "Application";
    result.root.name = std::format("Process ({})", pid);
    populate_ax_element(app_ref, result.root, 0, max_depth, max_children_per_node, result.total_node_count);
    CFRelease(app_ref);
    result.success = true;

#elif defined(_WIN32)
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&automation);
    if (FAILED(hr) || !automation) {
        result.error_message = "Failed to initialize Windows UI Automation COM interface";
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return result;
    }

    VARIANT var;
    var.vt = VT_I4;
    var.lVal = static_cast<LONG>(pid);

    IUIAutomationCondition* cond = nullptr;
    automation->CreatePropertyCondition(UIA_ProcessIdPropertyId, var, &cond);

    IUIAutomationElement* root_elem = nullptr;
    automation->GetRootElement(&root_elem);

    IUIAutomationTreeWalker* walker = nullptr;
    automation->get_ControlViewWalker(&walker);

    result.root.role = "Application";
    result.root.name = std::format("Process ({})", pid);

    if (root_elem && cond) {
        IUIAutomationElementArray* element_array = nullptr;
        if (SUCCEEDED(root_elem->FindAll(TreeScope_Children, cond, &element_array)) && element_array) {
            int length = 0;
            element_array->get_Length(&length);
            for (int i = 0; i < (std::min)(length, max_children_per_node); ++i) {
                IUIAutomationElement* child_elem = nullptr;
                if (SUCCEEDED(element_array->GetElement(i, &child_elem)) && child_elem) {
                    ui_element_node child_node;
                    populate_uia_element(child_elem, walker, child_node, 1, max_depth, max_children_per_node, result.total_node_count);
                    result.root.children.push_back(std::move(child_node));
                    child_elem->Release();
                }
            }
            element_array->Release();
        }
    }

    if (walker) walker->Release();
    if (root_elem) root_elem->Release();
    if (cond) cond->Release();
    automation->Release();

    if (SUCCEEDED(hr_co)) CoUninitialize();

    result.success = true;
    if (result.total_node_count == 0) result.total_node_count = 1;

#else
    result.error_message = "UI Automation explorer is not supported on this platform";
    result.root.role = "Application";
    result.root.name = std::format("Process ({})", pid);
    result.total_node_count = 1;
    result.success = false;
#endif

    return result;
}

static bool is_edit_or_value_node(const ui_element_node& node, bool edit_boxes_only) {
    if (!node.value.empty()) return true;
    
    std::string role_lower = node.role;
    std::transform(role_lower.begin(), role_lower.end(), role_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    std::string subrole_lower = node.subrole;
    std::transform(subrole_lower.begin(), subrole_lower.end(), subrole_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    
    if (role_lower == "edit" || role_lower == "text" || role_lower == "document" || 
        role_lower == "combobox" || role_lower == "spinner" || role_lower == "slider" ||
        role_lower == "textfield" || role_lower == "textarea") {
        return true;
    }
    
    if (subrole_lower.find("edit") != std::string::npos || 
        subrole_lower.find("text") != std::string::npos ||
        subrole_lower.find("field") != std::string::npos) {
        return true;
    }
    
    if (!edit_boxes_only) {
        return !node.name.empty() || !node.description.empty() || !node.id.empty();
    }
    
    return false;
}

static void collect_values_recursive(const ui_element_node& node, std::vector<const ui_element_node*>& current_path, std::vector<ui_element_value_info>& list, bool edit_boxes_only) {
    current_path.push_back(&node);
    if (is_edit_or_value_node(node, edit_boxes_only)) {
        list.push_back(ui_element_value_info{
            .id = node.id,
            .name = node.name,
            .role = node.role,
            .subrole = node.subrole,
            .value = node.value,
            .description = node.description,
            .path = ui_automation_explorer::format_tree_path(current_path)
        });
    }
    for (const auto& child : node.children) {
        collect_values_recursive(child, current_path, list, edit_boxes_only);
    }
    current_path.pop_back();
}

std::vector<ui_element_value_info> ui_automation_result::extract_values(bool edit_boxes_only) const {
    std::vector<ui_element_value_info> list;
    std::vector<const ui_element_node*> current_path;
    collect_values_recursive(root, current_path, list, edit_boxes_only);
    return list;
}

std::vector<ui_element_value_info> ui_automation_explorer::extract_process_values(int64_t pid, bool edit_boxes_only, int max_depth) {
    auto res = inspect_process(pid, max_depth);
    if (!res.success) return {};
    return res.extract_values(edit_boxes_only);
}

static const ui_element_node* find_node_by_id_or_name(const ui_element_node& node, std::string_view query) {
    auto matches_ic = [](std::string_view haystack, std::string_view needle) {
        if (needle.empty() || haystack.empty()) return false;
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    };

    if ((!node.id.empty() && node.id == query) ||
        (!node.name.empty() && matches_ic(node.name, query))) {
        return &node;
    }

    for (const auto& child : node.children) {
        if (auto found = find_node_by_id_or_name(child, query)) {
            return found;
        }
    }
    return nullptr;
}

std::optional<std::string> ui_automation_explorer::request_control_value(int64_t pid, std::string_view identifier_or_name) {
    if (identifier_or_name.empty()) return std::nullopt;
    auto res = inspect_process(pid, 8);
    if (!res.success) return std::nullopt;

    const ui_element_node* node = find_node_by_id_or_name(res.root, identifier_or_name);
    if (node) {
        if (!node->value.empty()) return node->value;
        if (!node->name.empty()) return node->name;
        if (!node->description.empty()) return node->description;
    }
    return std::nullopt;
}

static bool search_path_recursive(const ui_element_node& current, const ui_element_node* target, std::vector<const ui_element_node*>& path) {
    path.push_back(&current);
    if (&current == target) {
        return true;
    }
    for (const auto& child : current.children) {
        if (search_path_recursive(child, target, path)) {
            return true;
        }
    }
    path.pop_back();
    return false;
}

std::vector<const ui_element_node*> ui_automation_explorer::find_path_to_node(const ui_element_node& root, const ui_element_node* target) {
    std::vector<const ui_element_node*> path;
    if (!target) return path;
    search_path_recursive(root, target, path);
    return path;
}

std::string ui_automation_explorer::format_tree_path(const std::vector<const ui_element_node*>& path, std::string_view separator) {
    if (path.empty()) return "";
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i > 0) result += separator;
        const auto* n = path[i];
        std::string role = n->role.empty() ? "Element" : n->role;
        std::string label = std::format("[{}]", role);
        if (!n->name.empty()) {
            label += std::format(" \"{}\"", n->name);
        } else if (!n->id.empty()) {
            label += std::format(" (#{})", n->id);
        }
        result += label;
    }
    return result;
}

std::string ui_automation_explorer::format_tree_path_hierarchy(const std::vector<const ui_element_node*>& path) {
    if (path.empty()) return "";
    std::string result = "Tree Path:\n";
    for (size_t i = 0; i < path.size(); ++i) {
        const auto* n = path[i];
        std::string indent(i * 2, ' ');
        std::string role = n->role.empty() ? "Element" : n->role;
        std::string line = std::format("{}{}[{}]", indent, (i == 0 ? "" : "└─ "), role);
        if (!n->name.empty()) line += std::format(" \"{}\"", n->name);
        if (!n->subrole.empty()) line += std::format(" ({})", n->subrole);
        if (!n->id.empty()) line += std::format(" [ID: {}]", n->id);
        result += line + "\n";
    }
    return result;
}

std::string ui_automation_explorer::format_element_properties(const ui_element_node& node) {
    std::string out = "UI Element Properties:\n";
    out += std::format("  Role: {}\n", node.role.empty() ? "(none)" : node.role);
    if (!node.subrole.empty()) out += std::format("  Subrole: {}\n", node.subrole);
    if (!node.name.empty()) out += std::format("  Name: {}\n", node.name);
    if (!node.value.empty()) out += std::format("  Value: {}\n", node.value);
    if (!node.id.empty()) out += std::format("  ID: {}\n", node.id);
    if (!node.description.empty()) out += std::format("  Description: {}\n", node.description);
    if (node.width > 0.0f || node.height > 0.0f) {
        out += std::format("  Bounds: X:{:.1f}, Y:{:.1f}, W:{:.1f}, H:{:.1f}\n", node.x, node.y, node.width, node.height);
    }
    out += std::format("  Enabled: {}\n", node.enabled ? "true" : "false");
    out += std::format("  Focused: {}\n", node.focused ? "true" : "false");
    out += std::format("  Child Count: {}\n", node.children.size());

    if (!node.attributes.empty()) {
        out += "  Attributes:\n";
        for (const auto& attr : node.attributes) {
            out += std::format("    {}: {}\n", attr.name, attr.value);
        }
    }
    return out;
}

std::string ui_automation_explorer::format_full_element_info(const ui_element_node& node, const std::vector<const ui_element_node*>& path) {
    std::string out = "=== UI Element Info ===\n\n";
    if (!path.empty()) {
        out += format_tree_path_hierarchy(path) + "\n";
        out += std::format("Single-Line Path: {}\n\n", format_tree_path(path));
    }
    out += format_element_properties(node);
    return out;
}

#if defined(__APPLE__)

static bool match_ax_element(AXUIElementRef elem, std::string_view query, std::string& out_id, std::string& out_name, std::string& out_role) {
    if (!elem) return false;

    CFTypeRef title_ref = nullptr;
    if (AXUIElementCopyAttributeValue(elem, kAXTitleAttribute, &title_ref) == kAXErrorSuccess && title_ref) {
        out_name = cfstring_to_utf8(static_cast<CFStringRef>(title_ref));
        CFRelease(title_ref);
    }

    CFTypeRef id_ref = nullptr;
    if (AXUIElementCopyAttributeValue(elem, kAXIdentifierAttribute, &id_ref) == kAXErrorSuccess && id_ref) {
        out_id = cfstring_to_utf8(static_cast<CFStringRef>(id_ref));
        CFRelease(id_ref);
    }

    CFTypeRef role_ref = nullptr;
    if (AXUIElementCopyAttributeValue(elem, kAXRoleAttribute, &role_ref) == kAXErrorSuccess && role_ref) {
        out_role = cfstring_to_utf8(static_cast<CFStringRef>(role_ref));
        if (out_role.starts_with("AX")) out_role = out_role.substr(2);
        CFRelease(role_ref);
    }

    auto matches_ic = [](std::string_view haystack, std::string_view needle) {
        if (needle.empty() || haystack.empty()) return false;
        auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
            [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
        return it != haystack.end();
    };

    if (!out_id.empty() && (out_id == query || matches_ic(out_id, query))) return true;
    if (!out_name.empty() && (out_name == query || matches_ic(out_name, query))) return true;

    return false;
}

static AXUIElementRef find_ax_element_recursive(AXUIElementRef parent, std::string_view query, int depth, int max_depth, std::string& out_id, std::string& out_name, std::string& out_role) {
    if (!parent || depth > max_depth) return nullptr;

    if (depth > 0 && match_ax_element(parent, query, out_id, out_name, out_role)) {
        CFRetain(parent);
        return parent;
    }

    CFTypeRef children_ref = nullptr;
    if (AXUIElementCopyAttributeValue(parent, kAXChildrenAttribute, &children_ref) == kAXErrorSuccess && children_ref) {
        if (CFGetTypeID(children_ref) == CFArrayGetTypeID()) {
            CFArrayRef arr = static_cast<CFArrayRef>(children_ref);
            CFIndex count = CFArrayGetCount(arr);
            for (CFIndex i = 0; i < count; ++i) {
                AXUIElementRef child = static_cast<AXUIElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(arr, i)));
                AXUIElementRef found = find_ax_element_recursive(child, query, depth + 1, max_depth, out_id, out_name, out_role);
                if (found) {
                    CFRelease(children_ref);
                    return found;
                }
            }
        }
        CFRelease(children_ref);
    }

    return nullptr;
}

#endif

ui_manipulation_result ui_automation_explorer::perform_control_action(int64_t pid, std::string_view identifier_name_or_path, std::string_view action, std::string_view value) {
    ui_manipulation_result result;
    if (pid <= 0) {
        result.error_message = "Invalid process PID";
        return result;
    }
    if (identifier_name_or_path.empty()) {
        result.error_message = "Target element identifier/name is required";
        return result;
    }

#if defined(__APPLE__)
    if (!check_accessibility_permissions(false)) {
        result.permission_denied = true;
        result.error_message = "Accessibility permission is required for UI manipulation on macOS.";
        return result;
    }

    AXUIElementRef app_ref = AXUIElementCreateApplication(static_cast<pid_t>(pid));
    if (!app_ref) {
        result.error_message = std::format("Failed to create AXUIElement for PID {}", pid);
        return result;
    }

    std::string matched_id, matched_name, matched_role;
    AXUIElementRef target_elem = find_ax_element_recursive(app_ref, identifier_name_or_path, 0, 8, matched_id, matched_name, matched_role);
    CFRelease(app_ref);

    if (!target_elem) {
        result.error_message = std::format("No UI element matching '{}' found in PID {}", identifier_name_or_path, pid);
        return result;
    }

    result.matched_element_id = matched_id;
    result.matched_element_name = matched_name;
    result.matched_element_role = matched_role;

    std::string act_lower(action);
    std::transform(act_lower.begin(), act_lower.end(), act_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (act_lower == "click" || act_lower == "press") {
        AXError err = AXUIElementPerformAction(target_elem, kAXPressAction);
        if (err == kAXErrorSuccess) {
            result.success = true;
            result.action_performed = "press";
        } else {
            CGPoint pt{0, 0};
            CGSize sz{0, 0};
            CFTypeRef pos_ref = nullptr;
            if (AXUIElementCopyAttributeValue(target_elem, kAXPositionAttribute, &pos_ref) == kAXErrorSuccess && pos_ref) {
                if (CFGetTypeID(pos_ref) == AXValueGetTypeID()) {
                    AXValueGetValue(static_cast<AXValueRef>(const_cast<void*>(pos_ref)), kAXValueTypeCGPoint, &pt);
                }
                CFRelease(pos_ref);
            }
            CFTypeRef sz_ref = nullptr;
            if (AXUIElementCopyAttributeValue(target_elem, kAXSizeAttribute, &sz_ref) == kAXErrorSuccess && sz_ref) {
                if (CFGetTypeID(sz_ref) == AXValueGetTypeID()) {
                    AXValueGetValue(static_cast<AXValueRef>(const_cast<void*>(sz_ref)), kAXValueTypeCGSize, &sz);
                }
                CFRelease(sz_ref);
            }
            double cx = static_cast<double>(pt.x) + (static_cast<double>(sz.width) * 0.5);
            double cy = static_cast<double>(pt.y) + (static_cast<double>(sz.height) * 0.5);
            if (cx > 0 && cy > 0) {
                CGEventRef d = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, CGPointMake(static_cast<CGFloat>(cx), static_cast<CGFloat>(cy)), kCGMouseButtonLeft);
                CGEventRef u = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, CGPointMake(static_cast<CGFloat>(cx), static_cast<CGFloat>(cy)), kCGMouseButtonLeft);
                if (d && u) {
                    CGEventPost(kCGHIDEventTap, d);
                    CGEventPost(kCGHIDEventTap, u);
                    CFRelease(d);
                    CFRelease(u);
                    result.success = true;
                    result.action_performed = "click_at_bounds";
                }
            } else {
                result.error_message = std::format("AXPerformAction press failed (code {}) and element bounds unavailable", static_cast<int>(err));
            }
        }
    } else if (act_lower == "set_value" || act_lower == "value") {
        CFStringRef cfval = CFStringCreateWithCString(kCFAllocatorDefault, std::string(value).c_str(), kCFStringEncodingUTF8);
        AXError err = AXUIElementSetAttributeValue(target_elem, kAXValueAttribute, cfval);
        if (cfval) CFRelease(cfval);

        AXUIElementSetAttributeValue(target_elem, kAXFocusedAttribute, kCFBooleanTrue);
        if (err == kAXErrorSuccess || err == kAXErrorCannotComplete) {
            result.success = true;
            result.action_performed = "set_value";
        } else {
            result.error_message = std::format("Failed to set AXValue attribute (code {})", static_cast<int>(err));
        }
    } else if (act_lower == "focus") {
        AXError err = AXUIElementSetAttributeValue(target_elem, kAXFocusedAttribute, kCFBooleanTrue);
        if (err == kAXErrorSuccess) {
            result.success = true;
            result.action_performed = "focus";
        } else {
            result.error_message = std::format("Failed to set AXFocused attribute (code {})", static_cast<int>(err));
        }
    } else {
        std::string custom_act = std::string(action);
        if (!custom_act.starts_with("AX") && !custom_act.empty()) {
            custom_act[0] = static_cast<char>(std::toupper(custom_act[0]));
            custom_act = "AX" + custom_act;
        }
        CFStringRef act_ref = CFStringCreateWithCString(NULL, custom_act.c_str(), kCFStringEncodingUTF8);
        AXError err = AXUIElementPerformAction(target_elem, act_ref);
        if (act_ref) CFRelease(act_ref);
        if (err == kAXErrorSuccess) {
            result.success = true;
            result.action_performed = custom_act;
        } else {
            result.error_message = std::format("AXUIElementPerformAction('{}') failed (code {})", custom_act, static_cast<int>(err));
        }
    }

    CFRelease(target_elem);
    return result;

#elif defined(_WIN32)
    HRESULT hr_co = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void**)&automation);
    if (FAILED(hr) || !automation) {
        result.error_message = "Failed to initialize Windows UI Automation COM interface";
        if (SUCCEEDED(hr_co)) CoUninitialize();
        return result;
    }

    VARIANT var;
    var.vt = VT_I4;
    var.lVal = static_cast<LONG>(pid);

    IUIAutomationCondition* cond = nullptr;
    automation->CreatePropertyCondition(UIA_ProcessIdPropertyId, var, &cond);

    IUIAutomationElement* root_elem = nullptr;
    automation->GetRootElement(&root_elem);

    IUIAutomationElement* target_elem = nullptr;
    if (root_elem && cond) {
        std::wstring wquery(identifier_name_or_path.begin(), identifier_name_or_path.end());
        BSTR bstr_query = SysAllocString(wquery.c_str());

        IUIAutomationCondition *cond_id = nullptr, *cond_name = nullptr, *cond_or = nullptr, *cond_match = nullptr;
        automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, VARIANT{ .vt = VT_BSTR, .bstrVal = bstr_query }, &cond_id);
        automation->CreatePropertyCondition(UIA_NamePropertyId, VARIANT{ .vt = VT_BSTR, .bstrVal = bstr_query }, &cond_name);
        if (cond_id && cond_name) {
            automation->CreateOrCondition(cond_id, cond_name, &cond_or);
        }
        if (cond_or) {
            automation->CreateAndCondition(cond, cond_or, &cond_match);
        }

        if (cond_match) {
            root_elem->FindFirst(TreeScope_Subtree, cond_match, &target_elem);
        }

        if (cond_id) cond_id->Release();
        if (cond_name) cond_name->Release();
        if (cond_or) cond_or->Release();
        if (cond_match) cond_match->Release();

        if (!target_elem) {
            IUIAutomationElementArray* element_array = nullptr;
            if (SUCCEEDED(root_elem->FindAll(TreeScope_Subtree, cond, &element_array)) && element_array) {
                int len = 0;
                element_array->get_Length(&len);
                for (int i = 0; i < len; ++i) {
                    IUIAutomationElement* elem = nullptr;
                    if (SUCCEEDED(element_array->GetElement(i, &elem)) && elem) {
                        BSTR name_bstr = nullptr, id_bstr = nullptr;
                        std::string name_str, id_str;
                        if (SUCCEEDED(elem->get_CurrentName(&name_bstr)) && name_bstr) {
                            name_str = bstr_to_string(name_bstr);
                            SysFreeString(name_bstr);
                        }
                        if (SUCCEEDED(elem->get_CurrentAutomationId(&id_bstr)) && id_bstr) {
                            id_str = bstr_to_string(id_bstr);
                            SysFreeString(id_bstr);
                        }

                        auto matches_ic = [](std::string_view haystack, std::string_view needle) {
                            if (needle.empty() || haystack.empty()) return false;
                            auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                            return it != haystack.end();
                        };

                        if (matches_ic(id_str, identifier_name_or_path) || matches_ic(name_str, identifier_name_or_path)) {
                            target_elem = elem;
                            break;
                        }
                        elem->Release();
                    }
                }
                element_array->Release();
            }
        }
        SysFreeString(bstr_query);
    }

    if (!target_elem) {
        if (root_elem) root_elem->Release();
        if (cond) cond->Release();
        automation->Release();
        if (SUCCEEDED(hr_co)) CoUninitialize();
        result.error_message = std::format("No UI element matching '{}' found in PID {}", identifier_name_or_path, pid);
        return result;
    }

    BSTR tid_bstr = nullptr, tname_bstr = nullptr;
    if (SUCCEEDED(target_elem->get_CurrentAutomationId(&tid_bstr)) && tid_bstr) {
        result.matched_element_id = bstr_to_string(tid_bstr);
        SysFreeString(tid_bstr);
    }
    if (SUCCEEDED(target_elem->get_CurrentName(&tname_bstr)) && tname_bstr) {
        result.matched_element_name = bstr_to_string(tname_bstr);
        SysFreeString(tname_bstr);
    }

    std::string act_lower(action);
    std::transform(act_lower.begin(), act_lower.end(), act_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (act_lower == "click" || act_lower == "press") {
        IUIAutomationInvokePattern* inv = nullptr;
        if (SUCCEEDED(target_elem->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern, (void**)&inv)) && inv) {
            if (SUCCEEDED(inv->Invoke())) {
                result.success = true;
                result.action_performed = "invoke";
            }
            inv->Release();
        }
        if (!result.success) {
            IUIAutomationTogglePattern* tog = nullptr;
            if (SUCCEEDED(target_elem->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern, (void**)&tog)) && tog) {
                if (SUCCEEDED(tog->Toggle())) {
                    result.success = true;
                    result.action_performed = "toggle";
                }
                tog->Release();
            }
        }
        if (!result.success) {
            RECT rect{};
            if (SUCCEEDED(target_elem->get_CurrentBoundingRectangle(&rect))) {
                int cx = (rect.left + rect.right) / 2;
                int cy = (rect.top + rect.bottom) / 2;
                SetCursorPos(cx, cy);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                result.success = true;
                result.action_performed = "click_at_bounds";
            }
        }
    } else if (act_lower == "set_value" || act_lower == "value") {
        IUIAutomationValuePattern* val_pat = nullptr;
        if (SUCCEEDED(target_elem->GetCurrentPatternAs(UIA_ValuePatternId, IID_IUIAutomationValuePattern, (void**)&val_pat)) && val_pat) {
            std::wstring wval(value.begin(), value.end());
            BSTR bval = SysAllocString(wval.c_str());
            if (SUCCEEDED(val_pat->SetValue(bval))) {
                result.success = true;
                result.action_performed = "set_value";
            }
            SysFreeString(bval);
            val_pat->Release();
        }
        target_elem->SetFocus();
    } else if (act_lower == "focus") {
        if (SUCCEEDED(target_elem->SetFocus())) {
            result.success = true;
            result.action_performed = "focus";
        }
    }

    target_elem->Release();
    if (root_elem) root_elem->Release();
    if (cond) cond->Release();
    automation->Release();
    if (SUCCEEDED(hr_co)) CoUninitialize();
    return result;
#else
    result.error_message = "UI Automation manipulation is not supported on this platform";
    return result;
#endif
}

ui_manipulation_result ui_automation_explorer::click_control(int64_t pid, std::string_view identifier_name_or_path) {
    return perform_control_action(pid, identifier_name_or_path, "click");
}

ui_manipulation_result ui_automation_explorer::set_control_value(int64_t pid, std::string_view identifier_name_or_path, std::string_view value) {
    return perform_control_action(pid, identifier_name_or_path, "set_value", value);
}

ui_manipulation_result ui_automation_explorer::focus_control(int64_t pid, std::string_view identifier_name_or_path) {
    return perform_control_action(pid, identifier_name_or_path, "focus");
}

ui_manipulation_result ui_automation_explorer::click_at_coordinates(int64_t pid, float x, float y) {
    ui_manipulation_result result;
    if (pid <= 0) {
        result.error_message = "Invalid process PID";
        return result;
    }
#if defined(__APPLE__)
    if (!check_accessibility_permissions(false)) {
        result.permission_denied = true;
        result.error_message = "Accessibility permission required";
        return result;
    }
    CGEventRef d = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y)), kCGMouseButtonLeft);
    CGEventRef u = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, CGPointMake(static_cast<CGFloat>(x), static_cast<CGFloat>(y)), kCGMouseButtonLeft);
    if (d && u) {
        CGEventPost(kCGHIDEventTap, d);
        CGEventPost(kCGHIDEventTap, u);
        CFRelease(d);
        CFRelease(u);
        result.success = true;
        result.action_performed = "click_at_coordinates";
    } else {
        result.error_message = "Failed to create mouse CGEvent";
    }
#elif defined(_WIN32)
    SetCursorPos(static_cast<int>(x), static_cast<int>(y));
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    result.success = true;
    result.action_performed = "click_at_coordinates";
#else
    result.error_message = "Not supported on this platform";
#endif
    return result;
}

} // namespace rouen::helpers
