/* loading_html.h - embedded loading.html header
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#ifndef LOADING_HTML_H
#define LOADING_HTML_H

#include "html_resources.h"

#define LOADING_HTML_DATA_SIZE 2662U

/**
 * @brief Get loading page HTML resource
 * @return Pointer to loading HTML resource structure
 */
const html_resource_t* get_loading_html(void);

#endif /* LOADING_HTML_H */
