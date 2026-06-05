/**
 * @file html_resources.c
 * @brief Default HTML resource implementation
 * 
 * Provides fallback 404 page when requested resource is not found
 */

#include "html_resources.h"
#include <stddef.h>

// Default 404 Not Found page
static const uint8_t default_html_data[] = 
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
        "<meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
        "<title>404 Not Found</title>"
        "<style>"
            "body { font-family: Arial, sans-serif; text-align: center; padding: 50px; background: #f5f5f5; }"
            "h1 { color: #d32f2f; font-size: 72px; margin: 0; }"
            "h2 { color: #333; font-weight: normal; }"
            "a { color: #1976d2; text-decoration: none; }"
            "a:hover { text-decoration: underline; }"
        "</style>"
    "</head>"
    "<body>"
        "<h1>404</h1>"
        "<h2>Page Not Found</h2>"
        "<p>The requested page could not be found.</p>"
        "<p><a href=\"/\">Return to Home</a></p>"
    "</body>"
    "</html>";

static const html_resource_t default_html_resource = {
    .data = default_html_data,
    .length = sizeof(default_html_data) - 1,  // Exclude null terminator
    .is_gzipped = 0,
    .content_type = "text/html"
};

const html_resource_t* get_default_html(void) {
    return &default_html_resource;
}
