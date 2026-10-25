"""Temporary GSM fault hooks; production source is restored by the builder."""

SOURCE_PATHS = (
    "Application/gsm/at_engine2.c", "Application/gsm/gsm_wtd.c",
    "Application/gsm/gsm_process.c", "Application/app_main.c", "Release/makefile")


def replace_once(source, old, new):
    assert source.count(old) == 1, repr(old)
    return source.replace(old, new, 1)


def fixture_sources(original):
    engine, watchdog, process, app, make = original
    declarations = b"""
bool gsm_fault_pause_rx(void);
void gsm_fault_override_result(void);
bool gsm_fault_drop_completion(void);
"""
    engine = replace_once(engine, b"static void log_response(const char *p_result_str);",
        declarations + b"\nstatic void log_response(const char *p_result_str);")
    engine = replace_once(engine, b"static void consume_rx_data(bool check_response)\n{",
        b"static void consume_rx_data(bool check_response)\n{\n"
        b"    if (gsm_fault_pause_rx()) { return; }")
    engine = engine.replace(b"check_response_complete();",
        b"check_response_complete();\n            gsm_fault_override_result();")
    engine = replace_once(engine, b"void at_engine_dma_tx_complete_callback(void)\n{",
        b"void at_engine_dma_tx_complete_callback(void)\n{\n"
        b"    if (gsm_fault_drop_completion()) { return; }")
    engine += b'\n#include "../../test/system/gsm_fault/fixture.h"\n'
    watchdog += b"""
void gsm_fault_age_liveness(void);
void gsm_fault_age_busy(void);
void gsm_fault_age_liveness(void)
{
    s_liveness_last_activity = gsm_get_tick() - GSM_WTD_LIVENESS_TIMEOUT_MS;
}
void gsm_fault_age_busy(void)
{
    gsm_set_busy();
    s_wtd_last_activity = gsm_get_tick() - GSM_WTD_TIMEOUT_MS;
}
"""
    process = replace_once(process, b"void gsm_process_old(void)\n{",
        b"extern bool gsm_fault_stall_process(void);\n"
        b"void gsm_process_old(void)\n{\n"
        b"    if (gsm_fault_stall_process()) { gsm_wtd_check(); return; }")
    app = replace_once(app, b"\tapp_ipc_init();",
        b"\tapp_ipc_init();\n    extern void gsm_fault_fixture_init(void);\n"
        b"    gsm_fault_fixture_init();")
    make = replace_once(make, b"-Wl,--gc-sections",
        b"-Wl,--gc-sections -Wl,--wrap=HAL_UART_Transmit_DMA")
    return [engine, watchdog, process, app, make]
