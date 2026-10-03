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


outputs=[]
for name in ['web_auth_changed_repro','at_socket_log_repro','bsp_rng_repro']:
 run=subprocess.run([args.cc,'-std=c11','-O0','-I'+str(root/'Application/web-server'),'-I'+str(root/'Application/bsp'),str(audit/(name+'.c')),'-o',str(audit/(name+'.exe'))],capture_output=True,text=True)
 if run.returncode: print(run.stderr);raise SystemExit(run.returncode)
 run=subprocess.run([str(audit/(name+'.exe'))],capture_output=True,text=True)
 outputs.append(run.stdout+run.stderr)
 print(run.stdout)
 if run.returncode: raise SystemExit(run.returncode)
(audit/'web-auth-integration-repro.log').write_text('\n'.join(outputs),encoding='utf8')
