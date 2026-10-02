#include "ui_automation_explorer.hpp"

#include <format>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#if defined(__APPLE__)
#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ImageIO/ImageIO.h>
#include <libproc.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>
#include <gdiplus.h>
#endif

namespace rouen::helpers {

#if defined(__APPLE__)

static std::string get_process_executable_path(pid_t pid) {
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    int res = proc_pidpath(pid, pathbuf, sizeof(pathbuf));
    if (res > 0) {
        return std::string(pathbuf);
    }
    return "";
}

static std::string get_app_bundle_dir(const std::string& exec_path) {
    size_t app_pos = exec_path.find(".app/");
    if (app_pos != std::string::npos) {
        return exec_path.substr(0, app_pos + 5);
    }
    return "";
}

static void collect_child_pids_recursive(pid_t parent_pid, std::vector<pid_t>& out_pids, int max_depth = 4) {
    if (max_depth <= 0) return;
    int num_children = proc_listchildpids(parent_pid, nullptr, 0);
    if (num_children <= 0) return;

    std::vector<pid_t> children(static_cast<size_t>(num_children));
    int bytes_returned = proc_listchildpids(parent_pid, children.data(), static_cast<int>(children.size() * sizeof(pid_t)));
    if (bytes_returned <= 0) return;

    size_t count = static_cast<size_t>(bytes_returned) / sizeof(pid_t);
    for (size_t i = 0; i < count; ++i) {
        pid_t child_pid = children[i];
        if (child_pid <= 0) continue;
        if (std::find(out_pids.begin(), out_pids.end(), child_pid) == out_pids.end()) {
            out_pids.push_back(child_pid);
            collect_child_pids_recursive(child_pid, out_pids, max_depth - 1);
        }
    }
}

static std::vector<pid_t> get_related_pids(pid_t parent_pid) {
    std::vector<pid_t> related;

    // 1. Recursive child PIDs
    collect_child_pids_recursive(parent_pid, related, 4);

    // 2. Bundle PIDs (other processes running inside the same .app bundle directory)
    std::string main_path = get_process_executable_path(parent_pid);
    std::string bundle_dir = get_app_bundle_dir(main_path);

    if (!bundle_dir.empty()) {
        int num_pids = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (num_pids > 0) {
            std::vector<pid_t> all_pids(static_cast<size_t>(num_pids));
            int bytes = proc_listpids(PROC_ALL_PIDS, 0, all_pids.data(), static_cast<int>(all_pids.size() * sizeof(pid_t)));
            if (bytes > 0) {
                size_t total = static_cast<size_t>(bytes) / sizeof(pid_t);
                for (size_t i = 0; i < total; ++i) {
                    pid_t p = all_pids[i];
                    if (p <= 0 || p == parent_pid) continue;
                    if (std::find(related.begin(), related.end(), p) != related.end()) continue;

                    std::string p_path = get_process_executable_path(p);
                    if (!p_path.empty() && p_path.starts_with(bundle_dir)) {
                        related.push_back(p);
                    }
                }
            }
        }
    }

    return related;
}

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

static std::vector<AXUIElementRef> get_ax_child_elements(AXUIElementRef element, bool is_app) {
    std::vector<AXUIElementRef> children;

    auto add_children_from_attr = [&](CFStringRef attr_name) {
        CFTypeRef type_ref = nullptr;
        if (AXUIElementCopyAttributeValue(element, attr_name, &type_ref) == kAXErrorSuccess && type_ref) {
            if (CFGetTypeID(type_ref) == CFArrayGetTypeID()) {
                CFArrayRef arr = static_cast<CFArrayRef>(type_ref);
                CFIndex count = CFArrayGetCount(arr);
                for (CFIndex i = 0; i < count; ++i) {
                    AXUIElementRef child = static_cast<AXUIElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(arr, i)));
                    bool exists = false;
                    for (AXUIElementRef existing : children) {
                        if (CFEqual(existing, child)) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        CFRetain(child);
                        children.push_back(child);
                    }
                }
            }
            CFRelease(type_ref);
        }
    };

    add_children_from_attr(kAXChildrenAttribute);
    if (is_app) {
        add_children_from_attr(kAXWindowsAttribute);
    }

    return children;
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
            
            // Skip structural and circular references that trigger heavy IPC/recursion
            if (name_str == "AXChildren" || name_str == "AXParent" || 
                name_str == "AXTopLevelUIElement" || name_str == "AXWindow" ||
                name_str == "AXServesAsTitleForUIElements" || name_str == "AXLinkedUIElements" ||
                name_str == "AXSharedFocusElements" || name_str == "AXVisibleChildren") {
                continue;
            }

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
        bool is_app = (depth == 0) || (node.role == "Application");
        std::vector<AXUIElementRef> children = get_ax_child_elements(element, is_app);
        size_t limit = (std::min)(children.size(), static_cast<size_t>(max_children));
        for (size_t i = 0; i < children.size(); ++i) {
            if (i < limit) {
                ui_element_node child_node;
                populate_ax_element(children[i], child_node, depth + 1, max_depth, max_children, total_count);
                node.children.push_back(std::move(child_node));
            }
            CFRelease(children[i]);
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

struct Win32ScopeSearchContext {
    DWORD pid{0};
    std::string title_pattern;
    std::string class_pattern;
    HWND matched_hwnd{NULL};
    std::string matched_title;
    std::string matched_class;
};

static BOOL CALLBACK EnumWindowsScopeCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<Win32ScopeSearchContext*>(lParam);
    if (!hwnd || !IsWindow(hwnd)) return TRUE;

    DWORD wnd_pid = 0;
    GetWindowThreadProcessId(hwnd, &wnd_pid);
    if (ctx->pid > 0 && wnd_pid != ctx->pid) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    char class_buf[256] = {0};
    GetClassNameA(hwnd, class_buf, sizeof(class_buf));
    std::string class_name(class_buf);

    if (!ctx->class_pattern.empty()) {
        auto it = std::search(
            class_name.begin(), class_name.end(),
            ctx->class_pattern.begin(), ctx->class_pattern.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }
        );
        if (it == ctx->class_pattern.end()) {
            return TRUE;
        }
    }

    char title_buf[512] = {0};
    GetWindowTextA(hwnd, title_buf, sizeof(title_buf));
    std::string title(title_buf);

    if (!ctx->title_pattern.empty()) {
        auto it = std::search(
            title.begin(), title.end(),
            ctx->title_pattern.begin(), ctx->title_pattern.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }
        );
        if (it == ctx->title_pattern.end()) {
            return TRUE;
        }
    } else {
        if (title.empty() && ctx->pid == 0) {
            return TRUE;
        }
    }

    ctx->matched_hwnd = hwnd;
    ctx->matched_title = title;
    ctx->matched_class = class_name;
    return FALSE; // stop enum
}

static HWND find_window_by_scope(DWORD pid, std::string_view title_pattern, std::string_view class_pattern) {
    Win32ScopeSearchContext ctx;
    ctx.pid = pid;
    ctx.title_pattern = std::string(title_pattern);
    ctx.class_pattern = std::string(class_pattern);
    EnumWindows(EnumWindowsScopeCallback, reinterpret_cast<LPARAM>(&ctx));
    return ctx.matched_hwnd;
}

struct Win32ChildSearchContext {
    int target_id{0};
    std::string query;
    HWND matched_hwnd{NULL};
    std::string matched_text;
};

static BOOL CALLBACK EnumChildWindowsScopeCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<Win32ChildSearchContext*>(lParam);
    if (!hwnd || !IsWindow(hwnd)) return TRUE;

    int ctrl_id = GetDlgCtrlID(hwnd);
    if (ctx->target_id != 0 && ctrl_id == ctx->target_id) {
        ctx->matched_hwnd = hwnd;
        return FALSE; // found by control ID
    }

    char text_buf[512] = {0};
    GetWindowTextA(hwnd, text_buf, sizeof(text_buf));
    std::string text(text_buf);

    if (!ctx->query.empty()) {
        auto it = std::search(
            text.begin(), text.end(),
            ctx->query.begin(), ctx->query.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }
        );
        if (it != text.end()) {
            ctx->matched_hwnd = hwnd;
            ctx->matched_text = text;
            return FALSE;
        }
    }
    return TRUE;
}

static HWND find_child_control(HWND parent_hwnd, int ctrl_id, std::string_view query) {
    if (!parent_hwnd || !IsWindow(parent_hwnd)) return NULL;
    if (ctrl_id != 0) {
        HWND direct = GetDlgItem(parent_hwnd, ctrl_id);
        if (direct && IsWindow(direct)) return direct;
    }
    Win32ChildSearchContext ctx;
    ctx.target_id = ctrl_id;
    ctx.query = std::string(query);
    EnumChildWindows(parent_hwnd, EnumChildWindowsScopeCallback, reinterpret_cast<LPARAM>(&ctx));
    return ctx.matched_hwnd;
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

ui_automation_result ui_automation_explorer::inspect_process(int64_t pid, int max_depth, int max_children_per_node, const ui_window_scope& scope) {
    ui_automation_result result;
    if (pid <= 0 && scope.hwnd == 0) {
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

    // Enable accessibility engine in Electron/Chromium applications
    AXUIElementSetAttributeValue(app_ref, CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
    AXUIElementSetAttributeValue(app_ref, CFSTR("AXManualAccessibility"), kCFBooleanTrue);

    result.root.role = "Application";
    result.root.name = std::format("Process ({})", pid);
    populate_ax_element(app_ref, result.root, 0, max_depth, max_children_per_node, result.total_node_count);
    CFRelease(app_ref);

    // If main process exposed no window elements (common for multi-process apps like Teams, Chrome, Slack),
    // automatically search child helper processes for windows
    bool has_windows = false;
    for (const auto& child : result.root.children) {
        if (child.role == "Window") {
            has_windows = true;
            break;
        }
    }

    if (!has_windows) {
        std::vector<pid_t> related_pids = get_related_pids(static_cast<pid_t>(pid));
        for (pid_t rel_pid : related_pids) {
            AXUIElementRef rel_app_ref = AXUIElementCreateApplication(rel_pid);
            if (!rel_app_ref) continue;

            AXUIElementSetAttributeValue(rel_app_ref, CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
            AXUIElementSetAttributeValue(rel_app_ref, CFSTR("AXManualAccessibility"), kCFBooleanTrue);

            std::vector<AXUIElementRef> ax_windows = get_ax_child_elements(rel_app_ref, true);
            for (size_t i = 0; i < ax_windows.size(); ++i) {
                CFTypeRef role_ref = nullptr;
                std::string role_str;
                if (AXUIElementCopyAttributeValue(ax_windows[i], kAXRoleAttribute, &role_ref) == kAXErrorSuccess && role_ref) {
                    role_str = cftype_to_string(role_ref);
                    if (role_str.starts_with("AX")) role_str = role_str.substr(2);
                    CFRelease(role_ref);
                }

                if (role_str != "MenuBar" && role_str != "Unknown") {
                    ui_element_node window_node;
                    populate_ax_element(ax_windows[i], window_node, 1, max_depth, max_children_per_node, result.total_node_count);
                    result.root.children.push_back(std::move(window_node));
                    has_windows = true;
                }
                CFRelease(ax_windows[i]);
            }

            CFRelease(rel_app_ref);
        }
    }

    if (!scope.window_title.empty()) {
        std::vector<ui_element_node> filtered_children;
        for (auto& child : result.root.children) {
            auto it = std::search(child.name.begin(), child.name.end(),
                scope.window_title.begin(), scope.window_title.end(),
                [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
            if (it != child.name.end()) {
                filtered_children.push_back(std::move(child));
            }
        }
        if (filtered_children.empty()) {
            result.success = false;
            result.error_message = std::format("No window matching title '{}' found in PID {}", scope.window_title, pid);
            return result;
        }
        result.root.children = std::move(filtered_children);
    }

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

    HWND target_hwnd = (scope.hwnd != 0) ? reinterpret_cast<HWND>(scope.hwnd) : NULL;
    if (!target_hwnd && (!scope.window_title.empty() || !scope.window_class.empty())) {
        target_hwnd = find_window_by_scope(static_cast<DWORD>(pid), scope.window_title, scope.window_class);
    }

    IUIAutomationTreeWalker* walker = nullptr;
    automation->get_ControlViewWalker(&walker);

    result.root.role = "Application";
    result.root.name = std::format("Process ({})", pid);

    if (scope.hwnd != 0 || !scope.window_title.empty() || !scope.window_class.empty()) {
        if (!target_hwnd) {
            if (walker) walker->Release();
            automation->Release();
            if (SUCCEEDED(hr_co)) CoUninitialize();
            result.error_message = "Scoped window not found matching title/class/HWND";
            return result;
        }

        IUIAutomationElement* scoped_elem = nullptr;
        if (SUCCEEDED(automation->ElementFromHandle(reinterpret_cast<UIA_HWND>(target_hwnd), &scoped_elem)) && scoped_elem) {
            ui_element_node child_node;
            populate_uia_element(scoped_elem, walker, child_node, 1, max_depth, max_children_per_node, result.total_node_count);
            result.root.children.push_back(std::move(child_node));
            scoped_elem->Release();
        }
    } else {
        VARIANT var;
        var.vt = VT_I4;
        var.lVal = static_cast<LONG>(pid);

        IUIAutomationCondition* cond = nullptr;
        automation->CreatePropertyCondition(UIA_ProcessIdPropertyId, var, &cond);

        IUIAutomationElement* root_elem = nullptr;
        automation->GetRootElement(&root_elem);

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

        if (root_elem) root_elem->Release();
        if (cond) cond->Release();
    }

    if (walker) walker->Release();
    automation->Release();

    if (SUCCEEDED(hr_co)) CoUninitialize();

    result.success = true;
    if (result.total_node_count == 0) result.total_node_count = 1;

#else
    (void)max_depth;
    (void)max_children_per_node;
    (void)scope;
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

std::vector<ui_element_value_info> ui_automation_explorer::extract_process_values(int64_t pid, bool edit_boxes_only, int max_depth, const ui_window_scope& scope) {
    auto res = inspect_process(pid, max_depth, 100, scope);
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

    bool is_app = (depth == 0);
    if (!is_app) {
        CFTypeRef role_ref = nullptr;
        if (AXUIElementCopyAttributeValue(parent, kAXRoleAttribute, &role_ref) == kAXErrorSuccess && role_ref) {
            if (CFGetTypeID(role_ref) == CFStringGetTypeID()) {
                if (CFStringCompare(static_cast<CFStringRef>(role_ref), CFSTR("AXApplication"), 0) == kCFCompareEqualTo) {
                    is_app = true;
                }
            }
            CFRelease(role_ref);
        }
    }

    AXUIElementRef found_elem = nullptr;
    std::vector<AXUIElementRef> children = get_ax_child_elements(parent, is_app);
    for (AXUIElementRef child : children) {
        if (!found_elem) {
            found_elem = find_ax_element_recursive(child, query, depth + 1, max_depth, out_id, out_name, out_role);
        }
        CFRelease(child);
    }

    return found_elem;
}

#endif

ui_manipulation_result ui_automation_explorer::perform_control_action(int64_t pid, std::string_view identifier_name_or_path, std::string_view action, std::string_view value, const ui_window_scope& scope) {
    ui_manipulation_result result;
    if (pid <= 0 && scope.hwnd == 0) {
        result.error_message = "Invalid process PID";
        return result;
    }
    if (identifier_name_or_path.empty()) {
        result.error_message = "Target element identifier/name is required";
        return result;
    }

    std::string act_lower(action);
    std::transform(act_lower.begin(), act_lower.end(), act_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool is_win32_action = (act_lower == "win32_click" ||
                            act_lower == "win32_set_text" ||
                            act_lower == "win32_command" ||
                            act_lower == "win32_msg");

#if !defined(_WIN32)
    if (is_win32_action) {
        result.error_message = std::format("Win32 message action '{}' is only supported on Windows.", action);
        return result;
    }
#endif

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

    // Enable accessibility engine in Electron/Chromium applications
    AXUIElementSetAttributeValue(app_ref, CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
    AXUIElementSetAttributeValue(app_ref, CFSTR("AXManualAccessibility"), kCFBooleanTrue);

    AXUIElementRef search_root = app_ref;
    AXUIElementRef scoped_window_ref = nullptr;
    if (!scope.window_title.empty()) {
        CFTypeRef windows_ref = nullptr;
        if (AXUIElementCopyAttributeValue(app_ref, kAXWindowsAttribute, &windows_ref) == kAXErrorSuccess && windows_ref) {
            auto win_list = static_cast<CFArrayRef>(windows_ref);
            CFIndex win_count = CFArrayGetCount(win_list);
            for (CFIndex i = 0; i < win_count; ++i) {
                auto win_elem = static_cast<AXUIElementRef>(CFArrayGetValueAtIndex(win_list, i));
                if (!win_elem) continue;
                CFTypeRef title_ref = nullptr;
                std::string title_str;
                if (AXUIElementCopyAttributeValue(win_elem, kAXTitleAttribute, &title_ref) == kAXErrorSuccess && title_ref) {
                    title_str = cftype_to_string(title_ref);
                    CFRelease(title_ref);
                }
                auto it = std::search(title_str.begin(), title_str.end(),
                    scope.window_title.begin(), scope.window_title.end(),
                    [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
                if (it != title_str.end()) {
                    scoped_window_ref = win_elem;
                    CFRetain(scoped_window_ref);
                    break;
                }
            }
            CFRelease(windows_ref);
        }
        if (!scoped_window_ref) {
            CFRelease(app_ref);
            result.error_message = std::format("No window matching title '{}' found in PID {}", scope.window_title, pid);
            return result;
        }
        search_root = scoped_window_ref;
    }

    std::string matched_id, matched_name, matched_role;
    AXUIElementRef target_elem = find_ax_element_recursive(search_root, identifier_name_or_path, 0, 8, matched_id, matched_name, matched_role);

    // If not found in main process, search related helper processes
    if (!target_elem && scope.window_title.empty()) {
        std::vector<pid_t> related_pids = get_related_pids(static_cast<pid_t>(pid));
        for (pid_t rel_pid : related_pids) {
            AXUIElementRef rel_app_ref = AXUIElementCreateApplication(rel_pid);
            if (!rel_app_ref) continue;

            AXUIElementSetAttributeValue(rel_app_ref, CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
            AXUIElementSetAttributeValue(rel_app_ref, CFSTR("AXManualAccessibility"), kCFBooleanTrue);

            target_elem = find_ax_element_recursive(rel_app_ref, identifier_name_or_path, 0, 8, matched_id, matched_name, matched_role);
            CFRelease(rel_app_ref);
            if (target_elem) break;
        }
    }

    if (scoped_window_ref) CFRelease(scoped_window_ref);
    CFRelease(app_ref);

    if (!target_elem) {
        result.error_message = std::format("No UI element matching '{}' found in PID {}", identifier_name_or_path, pid);
        return result;
    }

    result.matched_element_id = matched_id;
    result.matched_element_name = matched_name;
    result.matched_element_role = matched_role;

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
    HWND target_hwnd = (scope.hwnd != 0) ? reinterpret_cast<HWND>(scope.hwnd) : NULL;
    if (!target_hwnd && (!scope.window_title.empty() || !scope.window_class.empty())) {
        target_hwnd = find_window_by_scope(static_cast<DWORD>(pid), scope.window_title, scope.window_class);
    }
    if (!target_hwnd && is_win32_action) {
        target_hwnd = find_window_by_scope(static_cast<DWORD>(pid), "", "");
    }

    if (scope.hwnd != 0 || !scope.window_title.empty() || !scope.window_class.empty()) {
        if (!target_hwnd) {
            result.error_message = "Scoped window not found matching title/class/HWND";
            return result;
        }
    }

    if (is_win32_action) {
        if (!target_hwnd) {
            result.error_message = "Target window for Win32 action could not be found";
            return result;
        }

        int ctrl_id = 0;
        bool is_id_numeric = false;
        try {
            size_t idx = 0;
            ctrl_id = std::stoi(std::string(identifier_name_or_path), &idx);
            if (idx == identifier_name_or_path.size()) is_id_numeric = true;
        } catch (...) {}

        if (act_lower == "win32_command") {
            if (!is_id_numeric) {
                result.error_message = "Target must be a numeric control or command ID for win32_command";
                return result;
            }
            PostMessageA(target_hwnd, WM_COMMAND, MAKEWPARAM(ctrl_id, 0), 0);
            result.success = true;
            result.action_performed = "win32_command";
            result.matched_element_id = std::to_string(ctrl_id);
            return result;
        }

        HWND hCtrl = find_child_control(target_hwnd, is_id_numeric ? ctrl_id : 0, identifier_name_or_path);

        if (act_lower == "win32_click" || act_lower == "win32_msg") {
            if (hCtrl) {
                char cname[256] = {0};
                GetWindowTextA(hCtrl, cname, sizeof(cname));
                result.matched_element_name = cname;
                int cid = GetDlgCtrlID(hCtrl);
                result.matched_element_id = std::to_string(cid);

                SendMessageA(hCtrl, BM_CLICK, 0, 0);
                PostMessageA(hCtrl, BM_CLICK, 0, 0);

                RECT cr{};
                GetClientRect(hCtrl, &cr);
                LPARAM lp = MAKELPARAM((std::max)(1, static_cast<int>(cr.right - cr.left)/2), (std::max)(1, static_cast<int>(cr.bottom - cr.top)/2));
                PostMessageA(hCtrl, WM_LBUTTONDOWN, MK_LBUTTON, lp);
                PostMessageA(hCtrl, WM_LBUTTONUP, 0, lp);

                HWND parent = GetParent(hCtrl);
                if (!parent) parent = target_hwnd;
                if (cid != 0 && parent) {
                    PostMessageA(parent, WM_COMMAND, MAKEWPARAM(cid, BN_CLICKED), reinterpret_cast<LPARAM>(hCtrl));
                }

                result.success = true;
                result.action_performed = "win32_click";
                return result;
            } else if (is_id_numeric) {
                PostMessageA(target_hwnd, WM_COMMAND, MAKEWPARAM(ctrl_id, BN_CLICKED), 0);
                result.success = true;
                result.action_performed = "win32_command_fallback";
                result.matched_element_id = std::to_string(ctrl_id);
                return result;
            } else {
                result.error_message = std::format("No Win32 control matching '{}' found in target window", identifier_name_or_path);
                return result;
            }
        } else if (act_lower == "win32_set_text") {
            if (hCtrl) {
                std::wstring wval(value.begin(), value.end());
                SendMessageW(hCtrl, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wval.c_str()));
                int cid = GetDlgCtrlID(hCtrl);
                HWND parent = GetParent(hCtrl);
                if (!parent) parent = target_hwnd;
                if (cid != 0 && parent) {
                    PostMessageA(parent, WM_COMMAND, MAKEWPARAM(cid, EN_CHANGE), reinterpret_cast<LPARAM>(hCtrl));
                }
                result.success = true;
                result.action_performed = "win32_set_text";
                result.matched_element_id = std::to_string(cid);
                return result;
            } else {
                result.error_message = std::format("No Win32 control matching '{}' found in target window", identifier_name_or_path);
                return result;
            }
        }
    }

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

    IUIAutomationElement* search_root = root_elem;
    IUIAutomationElement* scoped_elem = nullptr;
    if (target_hwnd) {
        if (SUCCEEDED(automation->ElementFromHandle(reinterpret_cast<UIA_HWND>(target_hwnd), &scoped_elem)) && scoped_elem) {
            search_root = scoped_elem;
        }
    }

    IUIAutomationElement* target_elem = nullptr;
    if (search_root) {
        std::wstring wquery(identifier_name_or_path.begin(), identifier_name_or_path.end());
        BSTR bstr_query = SysAllocString(wquery.c_str());

        IUIAutomationCondition *cond_id = nullptr, *cond_name = nullptr, *cond_or = nullptr, *cond_match = nullptr;
        automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, VARIANT{ .vt = VT_BSTR, .bstrVal = bstr_query }, &cond_id);
        automation->CreatePropertyCondition(UIA_NamePropertyId, VARIANT{ .vt = VT_BSTR, .bstrVal = bstr_query }, &cond_name);
        if (cond_id && cond_name) {
            automation->CreateOrCondition(cond_id, cond_name, &cond_or);
        }
        if (cond_or) {
            if (search_root == root_elem && cond) {
                automation->CreateAndCondition(cond, cond_or, &cond_match);
            } else {
                cond_match = cond_or;
                cond_match->AddRef();
            }
        }

        if (cond_match) {
            search_root->FindFirst(TreeScope_Subtree, cond_match, &target_elem);
        }

        if (cond_id) cond_id->Release();
        if (cond_name) cond_name->Release();
        if (cond_or) cond_or->Release();
        if (cond_match) cond_match->Release();

        if (!target_elem) {
            IUIAutomationCondition* find_cond = (search_root == root_elem) ? cond : nullptr;
            if (!find_cond) {
                automation->CreateTrueCondition(&find_cond);
            } else {
                find_cond->AddRef();
            }
            IUIAutomationElementArray* element_array = nullptr;
            if (SUCCEEDED(search_root->FindAll(TreeScope_Subtree, find_cond, &element_array)) && element_array) {
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
            if (find_cond) find_cond->Release();
        }
        SysFreeString(bstr_query);
    }

    if (!target_elem) {
        if (scoped_elem) scoped_elem->Release();
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
            UIA_HWND uctrl = 0;
            if (SUCCEEDED(target_elem->get_CurrentNativeWindowHandle(&uctrl)) && uctrl) {
                HWND hCtrl = reinterpret_cast<HWND>(uctrl);
                SendMessageA(hCtrl, BM_CLICK, 0, 0);
                PostMessageA(hCtrl, BM_CLICK, 0, 0);
                result.success = true;
                result.action_performed = "win32_click";
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
    if (scoped_elem) scoped_elem->Release();
    if (root_elem) root_elem->Release();
    if (cond) cond->Release();
    automation->Release();
    if (SUCCEEDED(hr_co)) CoUninitialize();
    return result;
#else
    (void)scope;
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

#if defined(_WIN32)
static bool write_bmp_file(const std::string& path, int width, int height, const uint8_t* bgra_data) {
    if (width <= 0 || height <= 0 || !bgra_data) return false;
    #pragma pack(push, 1)
    struct BmpHeader {
        uint16_t bfType{0x4D42};
        uint32_t bfSize{0};
        uint16_t bfReserved1{0};
        uint16_t bfReserved2{0};
        uint32_t bfOffBits{54};
        uint32_t biSize{40};
        int32_t  biWidth{0};
        int32_t  biHeight{0};
        uint16_t biPlanes{1};
        uint16_t biBitCount{32};
        uint32_t biCompression{0};
        uint32_t biSizeImage{0};
        int32_t  biXPelsPerMeter{2835};
        int32_t  biYPelsPerMeter{2835};
        uint32_t biClrUsed{0};
        uint32_t biClrImportant{0};
    } hdr;
    #pragma pack(pop)

    hdr.biWidth = width;
    hdr.biHeight = -height; // top-down DIB
    hdr.biSizeImage = static_cast<uint32_t>(width * height * 4);
    hdr.bfSize = sizeof(BmpHeader) + hdr.biSizeImage;

    std::filesystem::path fpath(path);
    if (fpath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fpath.parent_path(), ec);
    }

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(bgra_data), hdr.biSizeImage);
    return ofs.good();
}

struct Win32WindowSearchContext {
    DWORD pid{0};
    std::string title_pattern;
    HWND matched_hwnd{NULL};
    std::string matched_title;
};

static BOOL CALLBACK EnumWindowsCaptureCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<Win32WindowSearchContext*>(lParam);
    if (!hwnd || !IsWindow(hwnd)) return TRUE;

    DWORD wnd_pid = 0;
    GetWindowThreadProcessId(hwnd, &wnd_pid);
    if (ctx->pid > 0 && wnd_pid != ctx->pid) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    RECT r{};
    GetWindowRect(hwnd, &r);
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) {
        return TRUE;
    }

    char title_buf[512] = {0};
    GetWindowTextA(hwnd, title_buf, sizeof(title_buf));
    std::string title(title_buf);

    if (!ctx->title_pattern.empty()) {
        auto it = std::search(
            title.begin(), title.end(),
            ctx->title_pattern.begin(), ctx->title_pattern.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }
        );
        if (it == ctx->title_pattern.end()) {
            return TRUE;
        }
    } else {
        if (title.empty() && ctx->pid == 0) {
            return TRUE;
        }
    }

    ctx->matched_hwnd = hwnd;
    ctx->matched_title = title;
    return FALSE; // stop enum
}

window_screenshot_result ui_automation_explorer::capture_window_screenshot(
    int64_t pid,
    uint64_t hwnd,
    std::string_view title_pattern,
    const std::string& filename
) {
    window_screenshot_result result;
    result.file = filename;

    HWND target_hwnd = (hwnd != 0) ? reinterpret_cast<HWND>(hwnd) : NULL;
    std::string matched_title;

    if (target_hwnd) {
        if (!IsWindow(target_hwnd)) {
            result.error = "Specified HWND is not a valid window";
            return result;
        }
        char title_buf[512] = {0};
        GetWindowTextA(target_hwnd, title_buf, sizeof(title_buf));
        matched_title = title_buf;
    } else {
        Win32WindowSearchContext ctx;
        ctx.pid = static_cast<DWORD>(pid);
        ctx.title_pattern = std::string(title_pattern);
        EnumWindows(EnumWindowsCaptureCallback, reinterpret_cast<LPARAM>(&ctx));
        if (!ctx.matched_hwnd) {
            result.error = "Target window not found";
            return result;
        }
        target_hwnd = ctx.matched_hwnd;
        matched_title = ctx.matched_title;
    }

    result.hwnd = reinterpret_cast<uint64_t>(target_hwnd);
    result.title = matched_title;

    RECT rect{};
    GetWindowRect(target_hwnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        result.error = "Window has zero or invalid dimensions";
        return result;
    }
    result.width = width;
    result.height = height;

    HDC hdcScreen = GetDC(NULL);
    if (!hdcScreen) {
        result.error = "Failed to get screen DC";
        return result;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem) {
        ReleaseDC(NULL, hdcScreen);
        result.error = "Failed to create compatible DC";
        return result;
    }

    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, width, height);
    if (!hbm) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        result.error = "Failed to create compatible bitmap";
        return result;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hbm);

    // PW_RENDERFULLCONTENT = 2
    BOOL pw_ok = PrintWindow(target_hwnd, hdcMem, 2);
    if (!pw_ok) {
        pw_ok = PrintWindow(target_hwnd, hdcMem, 0);
    }
    if (!pw_ok) {
        BitBlt(hdcMem, 0, 0, width, height, hdcScreen, rect.left, rect.top, SRCCOPY);
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4));
    GetDIBits(hdcMem, hbm, 0, static_cast<UINT>(height), pixels.data(), &bi, DIB_RGB_COLORS);

    SelectObject(hdcMem, hOld);
    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    bool saved = false;
    if (filename.ends_with(".bmp")) {
        saved = write_bmp_file(filename, width, height, pixels.data());
    } else {
        // Try Gdiplus for PNG
        ULONG_PTR gdiplusToken = 0;
        Gdiplus::GdiplusStartupInput gdiplusStartupInput;
        if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
            {
                Gdiplus::Bitmap gdiBmp(width, height, width * 4, PixelFormat32bppARGB, pixels.data());
                CLSID pngClsid = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
                std::wstring wpath(filename.begin(), filename.end());
                std::filesystem::path fpath(filename);
                if (fpath.has_parent_path()) {
                    std::error_code ec;
                    std::filesystem::create_directories(fpath.parent_path(), ec);
                }
                saved = (gdiBmp.Save(wpath.c_str(), &pngClsid, NULL) == Gdiplus::Ok);
            }
            Gdiplus::GdiplusShutdown(gdiplusToken);
        }
        if (!saved) {
            saved = write_bmp_file(filename, width, height, pixels.data());
        }
    }

    if (saved) {
        result.success = true;
        result.message = "Window snapshot captured successfully";
    } else {
        result.error = "Failed to write image file";
    }
    return result;
}
#elif defined(__APPLE__)
window_screenshot_result ui_automation_explorer::capture_window_screenshot(
    int64_t pid,
    uint64_t hwnd,
    std::string_view title_pattern,
    const std::string& filename
) {
    window_screenshot_result result;
    result.file = filename;

    CFArrayRef window_list = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!window_list) {
        result.error = "Failed to copy window list";
        return result;
    }

    CFIndex count = CFArrayGetCount(window_list);
    CGWindowID matched_wid = 0;
    std::string matched_title;
    int matched_w = 0;
    int matched_h = 0;

    for (CFIndex i = 0; i < count; ++i) {
        auto info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(window_list, i));
        if (!info) continue;

        CGWindowID wid = 0;
        auto num_ref = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
        if (num_ref) {
            CFNumberGetValue(num_ref, kCFNumberSInt32Type, &wid);
        }

        pid_t wpid = 0;
        auto pid_ref = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
        if (pid_ref) {
            CFNumberGetValue(pid_ref, kCFNumberIntType, &wpid);
        }

        std::string wtitle;
        auto name_ref = static_cast<CFStringRef>(CFDictionaryGetValue(info, kCGWindowName));
        if (name_ref) {
            char buf[512] = {0};
            if (CFStringGetCString(name_ref, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                wtitle = buf;
            }
        }

        int w = 0, h = 0;
        auto bounds_ref = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        if (bounds_ref) {
            CGRect rect;
            if (CGRectMakeWithDictionaryRepresentation(bounds_ref, &rect)) {
                w = static_cast<int>(rect.size.width);
                h = static_cast<int>(rect.size.height);
            }
        }

        if (hwnd != 0) {
            if (static_cast<uint64_t>(wid) == hwnd) {
                matched_wid = wid;
                matched_title = wtitle;
                matched_w = w;
                matched_h = h;
                break;
            }
            continue;
        }

        if (pid > 0 && static_cast<int64_t>(wpid) != pid) {
            continue;
        }

        if (!title_pattern.empty()) {
            auto it = std::search(
                wtitle.begin(), wtitle.end(),
                title_pattern.begin(), title_pattern.end(),
                [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }
            );
            if (it == wtitle.end()) {
                continue;
            }
        } else {
            if (w <= 10 || h <= 10) continue;
            if (wtitle.empty() && pid <= 0) continue;
        }

        matched_wid = wid;
        matched_title = wtitle;
        matched_w = w;
        matched_h = h;
        break;
    }
    CFRelease(window_list);

    if (matched_wid == 0) {
        result.error = "Target window not found";
        return result;
    }

    result.hwnd = matched_wid;
    result.title = matched_title;

    CGImageRef img = CGWindowListCreateImage(
        CGRectNull,
        kCGWindowListOptionIncludingWindow,
        matched_wid,
        kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution
    );

    if (!img) {
        result.error = "Failed to capture window image (check screen recording permissions)";
        return result;
    }

    size_t img_w = CGImageGetWidth(img);
    size_t img_h = CGImageGetHeight(img);
    if (img_w > 0) matched_w = static_cast<int>(img_w);
    if (img_h > 0) matched_h = static_cast<int>(img_h);
    result.width = matched_w;
    result.height = matched_h;

    std::filesystem::path fpath(filename);
    if (fpath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fpath.parent_path(), ec);
    }

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(filename.c_str()),
        static_cast<CFIndex>(filename.size()),
        false
    );

    bool saved = false;
    if (url) {
        CFStringRef uti = CFSTR("public.png");
        if (filename.ends_with(".bmp")) {
            uti = CFSTR("com.microsoft.bmp");
        }
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL(url, uti, 1, nullptr);
        if (dest) {
            CGImageDestinationAddImage(dest, img, nullptr);
            saved = CGImageDestinationFinalize(dest);
            CFRelease(dest);
        }
        CFRelease(url);
    }
    CGImageRelease(img);

    if (saved) {
        result.success = true;
        result.message = "Window snapshot captured successfully";
    } else {
        result.error = "Failed to write image file";
    }
    return result;
}
#else
window_screenshot_result ui_automation_explorer::capture_window_screenshot(
    int64_t /*pid*/,
    uint64_t /*hwnd*/,
    std::string_view /*title_pattern*/,
    const std::string& /*filename*/
) {
    window_screenshot_result result;
    result.error = "Window screenshot capture is not supported on this platform";
    return result;
}
#endif

} // namespace rouen::helpers
