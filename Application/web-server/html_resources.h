/**
 * @file html_resources.h
 * @brief HTML resource management with wrapper functions
 * 
 * Provides type-safe access to HTML resources with automatic fallback
 * to default 404 page when resource is not found.
 */

#ifndef HTML_RESOURCES_H
#define HTML_RESOURCES_H

#include <stdint.h>

/* Compression types */
#define HTML_COMPRESS_NONE   0  /**< Uncompressed */
#define HTML_COMPRESS_GZIP   1  /**< Gzip compressed */
#define HTML_COMPRESS_BR     2  /**< Brotli compressed */

/**
 * @brief HTML resource structure
 * 
 * Encapsulates HTML content with metadata for proper HTTP response handling
 */
typedef struct {
    const uint8_t* data;        /**< HTML content (compressed or plain) */
    uint32_t length;            /**< Data length in bytes */
    uint8_t is_gzipped;         /**< 0: plain, 1: gzip, 2: brotli */
    const char* content_type;   /**< MIME type (e.g., "text/html") */
} html_resource_t;

/**
 * @brief Get index/home page resource
 * @return Pointer to index HTML resource
 */
const html_resource_t* get_index_html(void);


/**
 * @brief Get default 404 page resource
 * @return Pointer to default HTML resource (never NULL)
 */
const html_resource_t* get_default_html(void);

#endif // HTML_RESOURCES_H
