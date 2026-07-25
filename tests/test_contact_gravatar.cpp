#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "../src/models/contacts/contact.hpp"
#include "../src/helpers/md5.hpp"

using namespace rouen::models::contacts;

// Helper to perform HTTP HEAD/GET using libcurl to verify if URL returns an image
static bool fetch_http_image(const std::string& url, std::string& out_content_type) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // Header only
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    char* content_type = nullptr;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    if (content_type) {
        out_content_type = content_type;
    }

    curl_easy_cleanup(curl);
    return (res == CURLE_OK && response_code == 200);
}

TEST(ContactGravatarTest, LeadingEmailGravatarHash) {
    contact c;
    c.first_name = "Ignacio";
    c.last_name = "Rodriguez";
    c.display_name = "Ignacio Nicolas Rodriguez";
    // Several emails with ignacionr@gmail.com leading, separated by semicolon and angle brackets
    c.email = "Ignacio Nicolas Rodriguez <ignacionr@gmail.com>; ignacio.rodriguez@example.com; inr@domain.com";

    std::vector<std::string> emails = c.get_email_list();
    ASSERT_FALSE(emails.empty());
    EXPECT_EQ(emails.front(), "ignacionr@gmail.com");

    std::string avatar_url = c.get_avatar_url();
    // Known MD5 hash for ignacionr@gmail.com is 861237251912d3a0a45f6b0b4506363a
    std::string expected_hash = "861237251912d3a0a45f6b0b4506363a";
    EXPECT_NE(avatar_url.find(expected_hash), std::string::npos)
        << "Avatar URL should contain MD5 hash " << expected_hash << " but got: " << avatar_url;

    // Verify live Gravatar fetch returns image/jpeg
    std::string content_type;
    bool ok = fetch_http_image(avatar_url, content_type);
    EXPECT_TRUE(ok) << "Failed to fetch Gravatar image from " << avatar_url;
    EXPECT_NE(content_type.find("image"), std::string::npos) << "Content type should be image, got: " << content_type;
}

TEST(ContactGravatarTest, SemicolonSeparatedEmails) {
    contact c;
    c.display_name = "Ignacio Nicolas Rodriguez";
    c.email = "ignacionr@gmail.com; ignacio.rodriguez@example.com";

    std::vector<std::string> emails = c.get_email_list();
    ASSERT_FALSE(emails.empty());
    EXPECT_EQ(emails.front(), "ignacionr@gmail.com");

    std::string avatar_url = c.get_avatar_url();
    EXPECT_NE(avatar_url.find("861237251912d3a0a45f6b0b4506363a"), std::string::npos);
}

TEST(ContactGravatarTest, CleanEmailExtraction) {
    contact c;
    c.display_name = "Ignacio Nicolas Rodriguez";
    c.email = "  ignacionr@gmail.com  , secondary@gmail.com  ";

    std::vector<std::string> emails = c.get_email_list();
    ASSERT_FALSE(emails.empty());
    EXPECT_EQ(emails.front(), "ignacionr@gmail.com");

    std::string avatar_url = c.get_avatar_url();
    EXPECT_NE(avatar_url.find("861237251912d3a0a45f6b0b4506363a"), std::string::npos);
}
