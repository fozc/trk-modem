/* fw_update_html.h - embedded fw_update.html header
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 */
#ifndef FW_UPDATE_HTML_H
#define FW_UPDATE_HTML_H

#include "html_resources.h"

#define FW_UPDATE_HTML_DATA_SIZE 9116U

/**
 * @brief Get fw update page HTML resource
 * @return Pointer to fw_update HTML resource structure
 */
const html_resource_t* get_fw_update_html(void);

#endif /* FW_UPDATE_HTML_H */
