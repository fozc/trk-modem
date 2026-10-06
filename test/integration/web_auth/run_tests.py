"""Host checks using current production functions and hardware/transport doubles.

Function bodies are extracted from the repository, never copied as fixtures.
Changes to the extraction shape fail explicitly and require harness review.
These checks do not exercise real hardware or the complete HTTP/AT processes.
"""

from pathlib import Path
import argparse
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="gcc", help="Host C compiler executable")
parser.add_argument("--clean", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
audit = root / "test/build/web_auth"
if args.clean:
    if audit.resolve().parent != (root / "test/build").resolve():
        raise RuntimeError("Unexpected test output directory")
    if audit.exists():
        shutil.rmtree(audit)
    raise SystemExit(0)
audit.mkdir(parents=True, exist_ok=True)
def extract(path,name):
    source=(root/path).read_text(encoding='utf-8-sig')
    match=re.search(r'^(?:static )?(?:bool|void|int|uint32_t) '+name+r'\([^;]*?\)\n\{.*?\n\}',source,re.M|re.S)
    if not match: raise RuntimeError(name)
    return match.group(0)
preamble=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "http_session_token.h"
#include "bsp_random.h"
#define USER_ROLE_ADMIN "admin"
#define USER_ROLE_USER "user"
#define HTTP_SESSION_TIMEOUT_MS (15UL * 60UL * 1000UL)
#define LOGIN_MAX_FAILED_ATTEMPTS (5U)
#define LOGIN_LOCKOUT_MS (60UL * 1000UL)
#define CSLOG(...) ((void)0)
#define CSLOG_ERR(...) ((void)0)
#define CSLOG_WARN(...) ((void)0)
static uint32_t fallback_state=1U;
static bool fallback_active;
static uint32_t random_accumulator=0xE6B3E419U;
#define xsnprintf snprintf
static uint32_t test_tick=123456U, random_counter=1U;
static bool entropy_ok=true;
static int response_status;
static char output[512];
static struct {
 bool is_authenticated, login_lock_active;
 uint32_t last_activity_tick, login_lock_start_tick;
 uint8_t login_fail_count;
 char username[16], session_token[HTTP_SESSION_TOKEN_SIZE];
 char *tx_buffer;
 int tx_buffer_size;
} handler_state;
static uint32_t bsp_get_tick(void) { return test_tick; }
static bool read_hardware_word(uint32_t *value) {
 if (!entropy_ok) return false;
 *value=random_counter++;
 return true;
}
uint32_t bsp_get_random_accumulator(void) { return 0xE6B3E419U; }
static void gsm_get_rxtx_counters(uint32_t *tx,uint32_t *rx) {
 *tx=4096U;*rx=1024U;
}
static uint32_t gsm_get_ip_addr(void) { return 0x0A000018U; }
static uint32_t gsm_get_web_client_ip(void) { return 0U; }
static void elog_log_web_login_fail(uint32_t ip) { (void)ip; }
static void http_send_json(const char *body,int len) {
 (void)body; (void)len; response_status=200;
}
static void http_send_response(int status,const char *reason,
 const char *type,const char *body,int len) {
 (void)reason;(void)type;(void)body;(void)len;response_status=status;
}
'''
names=['login_lock_is_active','login_lock_register_failure','login_lock_reset','http_handlers_set_auth_from_token','http_handlers_is_admin','handle_post_login','handle_post_logout']
code=preamble+extract('Application/bsp/bsp_random.c','bsp_random_fallback_seed')+extract('Application/bsp/bsp_random.c','generate_fallback_word')+extract('Application/bsp/bsp_random.c','bsp_random_word')+'\n\n'.join(extract('Application/web-server/http_handlers.c',n) for n in names)
code+=r'''
int main(void) {
 handler_state.tx_buffer=output; handler_state.tx_buffer_size=sizeof(output);
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(http_handlers_is_admin());
 assert(strlen(handler_state.session_token)==32U);
 char first_token[HTTP_SESSION_TOKEN_SIZE], query[40];
 strcpy(first_token,handler_state.session_token);
 snprintf(query,sizeof(query),"t=%s",first_token);
 handler_state.is_authenticated=false;
 http_handlers_set_auth_from_token(query); assert(http_handlers_is_admin());
 puts("PASS: unchanged IP password and new token authenticate");
 http_handlers_set_auth_from_token("t=5A5BE221");
 assert(!http_handlers_is_admin());
 puts("PASS: old tick-derived 8-digit token rejected");
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(http_handlers_is_admin());
 assert(strcmp(first_token,handler_state.session_token)!=0);
 http_handlers_set_auth_from_token(query); assert(!http_handlers_is_admin());
 puts("PASS: relogin invalidates previous token");
 char saved_token[HTTP_SESSION_TOKEN_SIZE];
 strcpy(saved_token,handler_state.session_token);
 snprintf(query,sizeof(query),"t=%s",saved_token);
 entropy_ok=false;
 handle_post_login("{\"username\":\"user\",\"password\":\"user11\"}");
 assert(response_status==200 && handler_state.is_authenticated);
 assert(!http_handlers_is_admin());
 assert(strcmp(saved_token,handler_state.session_token)!=0);
 assert(strcmp(handler_state.username,"user")==0);
 http_handlers_set_auth_from_token(query);assert(!handler_state.is_authenticated);
 snprintf(query,sizeof(query),"t=%s",handler_state.session_token);
 http_handlers_set_auth_from_token(query);
 assert(handler_state.is_authenticated && !http_handlers_is_admin());
 puts("PASS: RNG failure creates authenticated user fallback and replaces old token");
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(response_status==200 && http_handlers_is_admin());
 puts("PASS: valid admin credentials can create fallback session");
 strcpy(saved_token,handler_state.session_token);
 handle_post_login("{\"username\":\"admin\",\"password\":\"wrong\"}");
 assert(strcmp(saved_token,handler_state.session_token)==0);
 puts("PASS: invalid credentials do not create a fallback session");
 handle_post_logout();
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(response_status==200 && http_handlers_is_admin());
 assert(strlen(handler_state.session_token)==32U);
 strcpy(saved_token,handler_state.session_token);
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(strcmp(saved_token,handler_state.session_token)!=0);
 puts("PASS: repeated fallback logins at the same tick create different tokens");
 entropy_ok=true;
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 snprintf(query,sizeof(query),"t=%s",handler_state.session_token);
 test_tick+=HTTP_SESSION_TIMEOUT_MS+1U;
 http_handlers_set_auth_from_token(query); assert(!http_handlers_is_admin());
 assert(handler_state.session_token[0]=='\0');
 puts("PASS: expired token rejected and cleared");
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 handle_post_logout(); assert(!http_handlers_is_admin());
 assert(handler_state.session_token[0]=='\0');
 puts("PASS: logout clears session");
 for (unsigned int i=0U;i<5U;i++)
  handle_post_login("{\"username\":\"admin\",\"password\":\"wrong\"}");
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(!http_handlers_is_admin());
 test_tick+=LOGIN_LOCKOUT_MS;
 handle_post_login("{\"username\":\"admin\",\"password\":\"admin25\"}");
 assert(http_handlers_is_admin());
 puts("PASS: login lockout still works and expires");
 return 0;
}
'''
(audit/'web_auth_changed_repro.c').write_text(code,encoding='utf8')
log_preamble=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#define GSM_LOG_VERBOSE 2
#define XCOLOR_CYAN "cyan"
#define XCOLOR_RED "red"
static char logs[4096];
static struct { uint8_t cmd[128], response_buffer[1280];
 uint16_t cmd_len, response_buffer_len, srecv_payload_end; } at_engine;
static int gsm_log_get_level(void) { return GSM_LOG_VERBOSE; }
static void log_append(const char *format,...) {
 va_list args;va_start(args,format);
 size_t used=strlen(logs);
 vsnprintf(logs+used,sizeof(logs)-used,format,args);va_end(args);
}
#define CCSLOG(color,...) log_append(__VA_ARGS__)
#define CSLOG_NODT(...) log_append(__VA_ARGS__)
'''
log_code=log_preamble+extract('Application/gsm/at_engine2.c','log_response')+r'''
int main(void) {
 strcpy((char *)at_engine.cmd,"AT#SRECV=1,1024\r");
 at_engine.cmd_len=(uint16_t)strlen((char *)at_engine.cmd);
 strcpy((char *)at_engine.response_buffer,"password=admin25&t=SECRET_TOKEN");
 at_engine.response_buffer_len=(uint16_t)strlen((char *)at_engine.response_buffer);
 log_response("OK");
 assert(strstr(logs,"admin25")==NULL && strstr(logs,"SECRET_TOKEN")==NULL);
 assert(strstr(logs,"payload omitted")!=NULL);
 puts("PASS: complete socket response secrets omitted");
 logs[0]='\0'; log_response("TIMEOUT");
 assert(strstr(logs,"admin25")==NULL && strstr(logs,"SECRET_TOKEN")==NULL);
 puts("PASS: partial socket response secrets omitted on timeout");
 logs[0]='\0';strcpy((char *)at_engine.cmd,"AT+CSQ\r");
 at_engine.cmd_len=(uint16_t)strlen((char *)at_engine.cmd);
 strcpy((char *)at_engine.response_buffer,"+CSQ: 20,99");
 at_engine.response_buffer_len=(uint16_t)strlen((char *)at_engine.response_buffer);
 log_response("OK");assert(strstr(logs,"+CSQ: 20,99")!=NULL);
 puts("PASS: ordinary modem diagnostics preserved");
 logs[0]='\0';strcpy((char *)at_engine.cmd,"AT#SRECV=3,1024\r");
 at_engine.cmd_len=(uint16_t)strlen((char *)at_engine.cmd);
 const uint8_t iec_frame[]={0x68,0x04,0x07,0,0,0};
 memcpy(at_engine.response_buffer,iec_frame,sizeof(iec_frame));
 at_engine.response_buffer_len=sizeof(iec_frame);
 at_engine.srecv_payload_end=sizeof(iec_frame);
 log_response("OK");
 assert(strstr(logs,"68 04 07 00 00 00")!=NULL);
 assert(strstr(logs,"payload omitted")==NULL);
 puts("PASS: IEC104 socket bytes visible in hex");
 logs[0]='\0';strcpy((char *)at_engine.cmd,"AT#SRECV=30,1024\r");
 at_engine.cmd_len=(uint16_t)strlen((char *)at_engine.cmd);
 log_response("OK");assert(strstr(logs,"payload omitted")!=NULL);
 puts("PASS: IEC104 prefix does not reveal other sockets");
 return 0;
}
'''
(audit/'at_socket_log_repro.c').write_text(log_code,encoding='utf8')

bsp_preamble=r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#define RESET 0U
#define HAL_OK 0U
#define RNG_FLAG_CECS 1U
#define RNG_FLAG_SECS 2U
#define RNG_IT_CEI 4U
#define RNG_IT_SEI 8U
static unsigned int flags, hal_calls, hal_status;
static bool inject_clock_error;
static int hrng;
#define __HAL_RNG_GET_FLAG(handle,flag) (flags & (flag))
#define __HAL_RNG_GET_IT(handle,flag) (flags & (flag))
static unsigned int HAL_RNG_GenerateRandomNumber(void *handle,uint32_t *value)
{
 (void)handle;hal_calls++;
 if (hal_status!=HAL_OK) return hal_status;
 flags &= ~(RNG_FLAG_SECS | RNG_IT_SEI);
 if (inject_clock_error) flags |= RNG_FLAG_CECS;
 *value=0x12345678U;
 return HAL_OK;
}
"""
bsp_source=(root/'Application/bsp/bsp_random.c').read_text(encoding='utf-8')
accumulator=re.search(r'^static uint32_t random_accumulator = .*?;',bsp_source,re.M)
if accumulator is None: raise RuntimeError("Missing RNG accumulator")
bsp_code=bsp_preamble+accumulator.group(0)+'\nstatic uint32_t fallback_state=1U;\nstatic bool fallback_active;\nstatic unsigned int fallback_logs;\n#define CSLOG_WARN(...) ((void)++fallback_logs)\n'+'\n'.join([
 extract('Application/bsp/bsp_random.c','bsp_get_random_accumulator'),
 extract('Application/bsp/bsp_random.c','read_hardware_word'),
 extract('Application/bsp/bsp_random.c','generate_fallback_word'),
 extract('Application/bsp/bsp_random.c','bsp_random_word')])+r"""
int main(void) {
 uint32_t value=99U;
 uint32_t initial=bsp_get_random_accumulator();
 assert(!bsp_random_word(NULL) && hal_calls==0U);
 flags=RNG_FLAG_CECS;
 assert(bsp_random_word(&value) && value!=0U && hal_calls==0U);
 flags=RNG_IT_CEI;
 assert(bsp_random_word(&value) && value!=0U && hal_calls==0U);
 assert(fallback_logs==1U);
 puts("PASS: clock errors select fallback without a HAL read");
 flags=RNG_FLAG_SECS | RNG_IT_SEI;
 assert(bsp_random_word(&value) && hal_calls==1U);
 assert(value==0x12345678U);
 assert(bsp_get_random_accumulator()==initial+value);
 puts("PASS: seed error reaches existing HAL recovery path");
 hal_status=1U;
 assert(bsp_random_word(&value) && value!=0U);
 puts("PASS: HAL failure selects availability fallback");
 hal_status=HAL_OK;inject_clock_error=true;
 assert(bsp_random_word(&value) && value!=0U);
 assert(fallback_logs==2U);
 puts("PASS: clock error after HAL read selects fallback without accumulating");
 assert(bsp_get_random_accumulator()==initial+0x12345678U);
 flags=0U;inject_clock_error=false;
 random_accumulator=UINT32_MAX-0x12345678U+2U;
 assert(bsp_random_word(&value));
 assert(bsp_get_random_accumulator()==1U);
 puts("PASS: RNG accumulator accepts only successful words and wraps unsigned");
 hal_status=1U;assert(bsp_random_word(&value));
 assert(fallback_logs==3U);
 assert(bsp_random_word(&value) && fallback_logs==3U);
 puts("PASS: fallback logs once per transition and re-arms after hardware recovery");
 return 0;
}
"""
(audit/'bsp_rng_repro.c').write_text(bsp_code,encoding='utf8')


# Use the actual formatter and production version/status handlers.
length_preamble = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "xprintf.h"
#define CSLOG(...) ((void)0)
static const char *fw_version=NULL, *fw_build_date="2026-10-03";
static const char *fw_hardware="TROIKA-SCB-v1";
static struct {char *tx_buffer; int tx_buffer_size;} handler_state;
static struct {
 bool in_progress;
 uint32_t received_bytes, total_size, file_hash;
} fw_state;
static int response_length;
static void http_send_json(const char *body,int length) {
 assert(length >= 0 && length < handler_state.tx_buffer_size);
 assert((size_t)length == strlen(body));
 response_length=length;
}
"""
handler_source=(root/'Application/web-server/http_handlers.c').read_text(encoding='utf8')
# Keep the real default initializer and setter in the regression harness.
match=re.search(r'^static const char \*fw_version = ([^;]+);', handler_source,re.M)
if not match: raise RuntimeError('fw_version default initializer missing')
length_preamble=length_preamble.replace('fw_version=NULL,',
                                      'fw_version='+match.group(1)+',')
length_code='#include "version.h"\n'+length_preamble
setter=re.search(r'^void fw_update_set_version_info\([^;]*?\)\s*\{.*?\n\}',
                 handler_source,re.M|re.S)
if not setter: raise RuntimeError('fw_update_set_version_info')
length_code+=setter.group(0)+'\n'
for function in ['handle_get_fw_version', 'handle_get_fw_status']:
 match=re.search(r'^void '+function+r'\(void\)\s*\{.*?\n\}',handler_source,re.M|re.S)
 if not match: raise RuntimeError(function)
 length_code += match.group(0)+'\n'
length_code+=r"""
static void check_response(void (*handler)(void), const char *expected) {
 static char canvas[8194];
 const int capacities[]={1,2,8,32,128,8192};
 for(size_t i=0;i<sizeof(capacities)/sizeof(capacities[0]);i++) {
  int capacity=capacities[i];
  memset(canvas,0x55,sizeof(canvas));
  canvas[0]='*';canvas[capacity+1]='*';
  handler_state.tx_buffer=canvas+1;handler_state.tx_buffer_size=capacity;
  handler();
  size_t length=strlen(expected);
  if(length>(size_t)(capacity-1))length=(size_t)(capacity-1);
  assert(response_length==(int)length);
  assert(memcmp(canvas+1,expected,length)==0);
  assert(canvas[length+1]=='\0');
  assert(canvas[0]=='*' && canvas[capacity+1]=='*');
 }
}
int main(void) {
 char expected[256];
 snprintf(expected,sizeof(expected),
  "{\"version\":\"%u.%u.%u\",\"buildDate\":\"2026-10-03\",\"hardware\":\"TROIKA-SCB-v1\"}",
  (unsigned int)VERSION_MAJOR,(unsigned int)VERSION_MINOR,
  (unsigned int)VERSION_PATCH);
 check_response(handle_get_fw_version,expected);
 fw_update_set_version_info("2.3.4","2026-10-04","CUSTOM");
 check_response(handle_get_fw_version,
  "{\"version\":\"2.3.4\",\"buildDate\":\"2026-10-04\",\"hardware\":\"CUSTOM\"}");
 fw_update_set_version_info(NULL,NULL,NULL);
 check_response(handle_get_fw_version,
  "{\"version\":\"2.3.4\",\"buildDate\":\"2026-10-04\",\"hardware\":\"CUSTOM\"}");
 check_response(handle_get_fw_status,"{\"active\":false}");
 fw_state.in_progress=true;fw_state.received_bytes=4294967295U;
 fw_state.total_size=4294967295U;fw_state.file_hash=4294967295U;
 check_response(handle_get_fw_status,
  "{\"active\":true,\"received\":4294967295,\"total\":4294967295,\"fh\":4294967295}");
 fw_state.in_progress=false;
 check_response(handle_get_fw_status,
  "{\"active\":false,\"ready\":true,\"received\":4294967295,\"total\":4294967295,\"fh\":4294967295}");
 puts("PASS: actual HTTP handlers and formatter preserve bytes, bounded lengths and guards");
 return 0;
}
"""
(audit/'http_lengths_repro.c').write_text(length_code,encoding='utf8')

board_code = r"""
/*
 * board_signal_repro.c
 *
 *  Created on: Oct 4, 2026
 *      Author: Fatih Ozcan
 *              fatihozcan@gmail.com
 *
 * Exercise the actual board HTTP handler with deterministic telemetry.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "system_status.h"
#include "system_status_json.h"
#include "gsm_info.h"
#include "xprintf.h"
#define CSLOG(...) ((void)0)
#define CSLOG_ERR(...) ((void)0)
static system_status_t board;
static char canvas[4098];
static struct { char *tx_buffer; int tx_buffer_size; } handler_state;
static uint8_t rxlev, rscp, rsrp, rsrq, creg, cgreg, cereg;
const system_status_t *system_status_get(void) { return &board; }
uint8_t gsm_info_get_signal_quality_2G(void) { return rxlev; }
uint8_t gsm_info_get_signal_quality_3G(void) { return rscp; }
uint8_t gsm_info_get_signal_quality_4G(void) { return rsrp; }
uint8_t gsm_info_get_4G_rsrq(void) { return rsrq; }
gsm_net_reg_state_t gsm_info_get_creg(void)
{ return (gsm_net_reg_state_t)creg; }
gsm_net_reg_state_t gsm_info_get_cgreg(void)
{ return (gsm_net_reg_state_t)cgreg; }
gsm_net_reg_state_t gsm_info_get_cereg(void)
{ return (gsm_net_reg_state_t)cereg; }
static void http_send_error(int code, const char *message)
{ (void)code; (void)message; assert(false); }
static void http_send_json(const char *data, int length)
{
    assert(length > 0 && length < 4096);
    assert(strlen(data) == (size_t)length);
    assert(data[0] == '{' && data[length - 1] == '}');
    puts(data);
}
""" + extract('Application/web-server/http_handlers.c',
              'handle_get_board_status_json') + r"""
int main(void)
{
    memset(canvas, '*', sizeof(canvas));
    handler_state.tx_buffer = canvas + 1;
    handler_state.tx_buffer_size = 4096;
    board.gsm_signal = 13;
    board.gsm_rat = 4;
    rxlev = 99; rscp = 255; rsrp = 41; rsrq = 20;
    creg = 0; cgreg = 0; cereg = 1;
    handle_get_board_status_json();
    board.gsm_rat = 2;
    rxlev = 51; rscp = 255; rsrp = 255; rsrq = 255;
    creg = 1; cgreg = 5; cereg = 0;
    handle_get_board_status_json();
    assert(canvas[0] == '*' && canvas[4097] == '*');
    return 0;
}
/*** end of file ***/
"""
(audit/'board_signal_repro.c').write_text(board_code, encoding='utf8')

outputs=[]
for name in ['web_auth_changed_repro','at_socket_log_repro','bsp_rng_repro','http_lengths_repro','board_signal_repro']:
 extra=[str(root/'Application/libs/xprintf.c'),'-I'+str(root/'Application/libs'),'-lm'] if name in ['http_lengths_repro', 'board_signal_repro'] else []
 if name == 'board_signal_repro':
  extra += [str(root/'Application/web-server/system_status_json.c'),
            '-I'+str(root/'Application/gsm'),
            '-I'+str(root/'Application/rf'),
            '-I'+str(root/'Application/power_board'),
            '-I'+str(root/'Application/cslog'),
            '-I'+str(root/'Application/libscp'),
            '-I'+str(root/'Application/libefw'),
            '-I'+str(root/'contiki-kernel'),
            '-I'+str(root/'test/support')]
 run=subprocess.run([args.cc,'-std=c11','-O0','-I'+str(root/'Application/web-server'),'-I'+str(root/'Application/bsp'),'-I'+str(root/'Application'),str(audit/(name+'.c')),*extra,'-o',str(audit/(name+'.exe'))],capture_output=True,text=True)
 if run.returncode: print(run.stderr);raise SystemExit(run.returncode)
 run=subprocess.run([str(audit/(name+'.exe'))],capture_output=True,text=True)
 if name == 'board_signal_repro' and run.returncode == 0:
  import json
  values = [json.loads(line) for line in run.stdout.splitlines()]
  assert len(values) == 2
  expected = [dict(GsmRAT=4,GsmRxlev=99,GsmRscp=255,GsmRsrp=41,
                   GsmRsrq=20,GsmCREG=0,GsmCGREG=0,GsmCEREG=1),
              dict(GsmRAT=2,GsmRxlev=51,GsmRscp=255,GsmRsrp=255,
                   GsmRsrq=255,GsmCREG=1,GsmCGREG=5,GsmCEREG=0)]
  for data, fields in zip(values, expected):
   assert data['GsmSig'] == 13
   for key, value in fields.items(): assert data[key] == value, key
  run.stdout = 'PASS: actual board HTTP handler preserves separate GSM measurements and registration\n'

 outputs.append(run.stdout+run.stderr)
 print(run.stdout)
 if run.returncode: raise SystemExit(run.returncode)
(audit/'web-auth-integration-repro.log').write_text('\n'.join(outputs),encoding='utf8')
