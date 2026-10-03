/* index_html.h - embedded index.html header
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#ifndef INDEX_HTML_H
#define INDEX_HTML_H

#include "html_resources.h"

/**
 * @brief Get index page HTML resource
 * @return Pointer to index HTML resource structure
 */
const html_resource_t* get_index_html(void);

#endif /* INDEX_HTML_H */
