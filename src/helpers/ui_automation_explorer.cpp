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
#include <windows.h>
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

    // Value
    CFTypeRef val_ref = nullptr;
    if (AXUIElementCopyAttributeValue(element, kAXValueAttribute, &val_ref) == kAXErrorSuccess && val_ref) {
        node.value = cftype_to_string(val_ref);
        CFRelease(val_ref);
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
                CFIndex limit = std::min(child_count, static_cast<CFIndex>(max_children));
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

    if (!node.id.empty()) node.attributes.push_back({"AutomationId", node.id});
    if (!node.subrole.empty()) node.attributes.push_back({"ClassName", node.subrole});

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
            for (int i = 0; i < std::min(length, max_children_per_node); ++i) {
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

} // namespace rouen::helpers
