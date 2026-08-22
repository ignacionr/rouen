#pragma once

// 1. Standard includes in alphabetic order
#include <memory>
#include <mutex>
#include <string>

// 2. Libraries used in the project, in alphabetic order
// None in this file

// 3. All other includes
// None in this file

namespace rouen::hosts {

/**
 * FootPrints Host Controller
 *
 * Talks to an on-prem BMC FootPrints instance (an old Perl CGI application with no
 * JSON API). Authentication is cookie-based: logging in POSTs credentials to
 * MRlogin.pl, which responds with a short-lived `session_key` cookie and, if the
 * user opts in to "remember me", a long-lived `Remember_Me` cookie - FootPrints'
 * own mechanism for silent re-authentication.
 *
 * The password is only ever held long enough to build the login request; it is
 * never written to disk. Only the base URL, username, and (opt-in) Remember_Me
 * cookie value are persisted, so a restart can attempt try_resume_session()
 * instead of prompting for credentials again.
 */
class footprints_host {
public:
    footprints_host();
    ~footprints_host() = default;

    // Logs in with a username/password against the given FootPrints base URL
    // (e.g. "http://fp.raptor.local"). Returns true if a session_key was obtained.
    bool login(const std::string& base_url, const std::string& username, const std::string& password, bool remember);

    // Attempts to re-establish a session from a previously saved Remember_Me cookie.
    // Called automatically on construction; returns false if there is nothing saved
    // or the remembered cookie has expired (the login form should be shown again).
    bool try_resume_session();

    // Clears the active session and any saved Remember_Me cookie.
    void logout();

    bool is_connected() const;
    std::string username() const;
    std::string base_url() const;

    // URL to the live FootPrints homepage, for opening in the system browser.
    std::string homepage_url() const;

    // True if a username/base URL was remembered from a previous session,
    // so the login form can be prefilled even when the session itself expired.
    bool has_saved_profile() const;

    std::string get_last_error() const { return last_error_; }

private:
    mutable std::mutex mutex_;
    bool connected_{false};
    std::string base_url_;
    std::string username_;
    std::string session_key_;
    std::string remember_cookie_;
    std::string last_error_;

    void set_error(const std::string& error);
    void clear_error();
};

// Global instance accessor for easy access from other parts of the program
std::shared_ptr<footprints_host> get_footprints_host();

} // namespace rouen::hosts
