/*
 * web_transport_resources.inc
 *
 *  Created on: Oct 31, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Migrated host scenarios and existing stateful hardware boundaries.
 */
#define get_index_html get_real_index_html
#include "../../Application/web-server/index_html.c"
#undef get_index_html
#define real_index (*get_real_index_html())
#define get_fw_update_html get_real_fw_update_html
#include "../../Application/web-server/fw_update_html.c"
#undef get_fw_update_html
#define real_fw_update (*get_real_fw_update_html())
#define get_loading_html get_real_loading_html
#include "../../Application/web-server/loading_html.c"
#undef get_loading_html
#define real_loading (*get_real_loading_html())

/*** end of file ***/
