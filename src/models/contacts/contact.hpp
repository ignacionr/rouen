#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "../../helpers/glaze_include.hpp"
#include "../../helpers/md5.hpp"

namespace rouen::models::contacts {

struct contact {
    int64_t id{-1};
    std::string first_name;
    std::string last_name;
    std::string display_name;
    std::string organization;
    std::string job_title;
    std::string email;
    std::string phone;
    std::string address;
    std::string notes;
    std::string picture_url;
    std::string source{"manual"}; // "manual", "macos", "whatsapp"
    std::string created;
    std::string updated;

    std::string get_full_name() const {
        if (!display_name.empty()) return display_name;
        std::string full;
        if (!first_name.empty()) full += first_name;
        if (!last_name.empty()) {
            if (!full.empty()) full += " ";
            full += last_name;
        }
        if (full.empty()) full = "Unnamed Contact";
        return full;
    }

    std::vector<std::string> get_email_list() const {
        std::vector<std::string> list;
        if (email.empty()) return list;

        std::stringstream ss(email);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto start = item.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) continue;
            auto end = item.find_last_not_of(" \t\n\r");
            std::string clean = item.substr(start, end - start + 1);
            if (!clean.empty()) {
                list.push_back(clean);
            }
        }
        return list;
    }

    std::vector<std::string> get_phone_list() const {
        std::vector<std::string> list;
        if (phone.empty()) return list;

        std::stringstream ss(phone);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto start = item.find_first_not_of(" \t\n\r");
            if (start == std::string::npos) continue;
            auto end = item.find_last_not_of(" \t\n\r");
            std::string clean = item.substr(start, end - start + 1);
            if (!clean.empty()) {
                list.push_back(clean);
            }
        }
        return list;
    }

    std::string get_avatar_url() const {
        if (!picture_url.empty()) {
            return picture_url;
        }
        auto emails = get_email_list();
        if (!emails.empty()) {
            std::string clean_email = emails.front();
            std::transform(clean_email.begin(), clean_email.end(), clean_email.begin(), ::tolower);
            std::string hash = rouen::helpers::MD5::hash(clean_email);
            return "https://www.gravatar.com/avatar/" + hash + "?d=identicon&s=128";
        }
        return "";
    }
};

struct contact_dto {
    int64_t id{-1};
    std::string first_name;
    std::string last_name;
    std::string display_name;
    std::string organization;
    std::string job_title;
    std::string email;
    std::string phone;
    std::string address;
    std::string notes;
    std::string picture_url;
    std::string source;
    std::string created;
    std::string updated;

    struct glaze {
        using T = contact_dto;
        static constexpr auto value = glz::object(
            "id", &T::id,
            "first_name", &T::first_name,
            "last_name", &T::last_name,
            "display_name", &T::display_name,
            "organization", &T::organization,
            "job_title", &T::job_title,
            "email", &T::email,
            "phone", &T::phone,
            "address", &T::address,
            "notes", &T::notes,
            "picture_url", &T::picture_url,
            "source", &T::source,
            "created", &T::created,
            "updated", &T::updated
        );
    };
};

inline contact_dto to_dto(const contact& c) {
    return contact_dto{
        .id = c.id,
        .first_name = c.first_name,
        .last_name = c.last_name,
        .display_name = c.display_name,
        .organization = c.organization,
        .job_title = c.job_title,
        .email = c.email,
        .phone = c.phone,
        .address = c.address,
        .notes = c.notes,
        .picture_url = c.picture_url,
        .source = c.source,
        .created = c.created,
        .updated = c.updated
    };
}

inline contact from_dto(const contact_dto& dto) {
    contact c;
    c.id = dto.id;
    c.first_name = dto.first_name;
    c.last_name = dto.last_name;
    c.display_name = dto.display_name;
    c.organization = dto.organization;
    c.job_title = dto.job_title;
    c.email = dto.email;
    c.phone = dto.phone;
    c.address = dto.address;
    c.notes = dto.notes;
    c.picture_url = dto.picture_url;
    c.source = dto.source.empty() ? "manual" : dto.source;
    c.created = dto.created;
    c.updated = dto.updated;
    return c;
}

} // namespace rouen::models::contacts
