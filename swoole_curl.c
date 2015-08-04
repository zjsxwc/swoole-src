/*
  +----------------------------------------------------------------------+
  | Swoole                                                               |
  +----------------------------------------------------------------------+
  | This source file is subject to version 2.0 of the Apache license,    |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.apache.org/licenses/LICENSE-2.0.html                      |
  | If you did not receive a copy of the Apache2.0 license and are unable|
  | to obtain it through the world-wide-web, please send a note to       |
  | license@swoole.com so we can mail you a copy immediately.            |
  +----------------------------------------------------------------------+
  | Author: Tianfeng Han  <mikan.tenny@gmail.com>                        |
  +----------------------------------------------------------------------+
*/

#include "php_swoole.h"
#include "swoole_http.h"

#include <ext/curl/php_curl.h>
#include <curl/curl.h>
#define MSG_OUT stdout

/* Global information, common to all connections */
typedef struct _GlobalInfo
{
  CURLM *multi;
  int still_running;
  FILE* input;
} GlobalInfo;

static GlobalInfo SwooleCURLG;

/* Information associated with a specific easy handle */
typedef struct _ConnInfo
{
  CURL *easy;
  char *url;
  GlobalInfo *global;
  char error[CURL_ERROR_SIZE];
} ConnInfo;


/* Information associated with a specific socket */
typedef struct _SockInfo
{
  curl_socket_t sockfd;
  CURL *easy;
  int action;
  long timeout;
  int evset;
  GlobalInfo *global;
} SockInfo;

static void curl_onRead(swReactor *reactor, swEvent *event);

static int multi_timer_cb(CURLM *multi, long timeout_ms, GlobalInfo *g)
{
    struct timeval timeout;
    (void) multi; /* unused */

    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    fprintf(MSG_OUT, "multi_timer_cb: Setting timeout to %ld ms\n", timeout_ms);

    return 0;
}

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code)
{
    if (CURLM_OK != code)
    {
        const char *s;
        switch (code)
        {
        case CURLM_BAD_HANDLE:
            s = "CURLM_BAD_HANDLE";
            break;
        case CURLM_BAD_EASY_HANDLE:
            s = "CURLM_BAD_EASY_HANDLE";
            break;
        case CURLM_OUT_OF_MEMORY:
            s = "CURLM_OUT_OF_MEMORY";
            break;
        case CURLM_INTERNAL_ERROR:
            s = "CURLM_INTERNAL_ERROR";
            break;
        case CURLM_UNKNOWN_OPTION:
            s = "CURLM_UNKNOWN_OPTION";
            break;
        case CURLM_LAST:
            s = "CURLM_LAST";
            break;
        default:
            s = "CURLM_unknown";
            break;
        case CURLM_BAD_SOCKET:
            s = "CURLM_BAD_SOCKET";
            fprintf(MSG_OUT, "ERROR: %s returns %s\n", where, s);
            /* ignore this error */
            return;
        }
        fprintf(MSG_OUT, "ERROR: %s returns %s\n", where, s);
        exit(code);
    }
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info(GlobalInfo *g)
{
    char *eff_url;
    CURLMsg *msg;
    int msgs_left;
    ConnInfo *conn;
    CURL *easy;
    CURLcode res;

    fprintf(MSG_OUT, "REMAINING: %d\n", g->still_running);
    while ((msg = curl_multi_info_read(g->multi, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            easy = msg->easy_handle;
            res = msg->data.result;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
            curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
            fprintf(MSG_OUT, "DONE: %s => (%d) %s\n", eff_url, res, conn->error);
            curl_multi_remove_handle(g->multi, easy);
            free(conn->url);
            curl_easy_cleanup(easy);
            free(conn);
        }
    }
}

static void curl_onRead(swReactor *reactor, swEvent *event)
{
    CURLMcode rc;
    int action = CURL_CSELECT_IN;

    rc = curl_multi_socket_action(SwooleCURLG.multi, event->fd, action, &SwooleCURLG.still_running);
    mcode_or_die("event_cb: curl_multi_socket_action", rc);
    check_multi_info(&SwooleCURLG);

    if (SwooleCURLG.still_running <= 0)
    {
        fprintf(MSG_OUT, "last transfer done, kill timeout\n");
//        if (evtimer_pending(g->timer_event, NULL))
//        {
//            evtimer_del(g->timer_event);
//        }
    }
}

/* Called by libevent when our timeout expires */
static void timer_cb(int fd, short kind, void *userp)
{
    GlobalInfo *g = (GlobalInfo *) userp;
    CURLMcode rc;
    (void) fd;
    (void) kind;

    rc = curl_multi_socket_action(g->multi,
    CURL_SOCKET_TIMEOUT, 0, &g->still_running);
    mcode_or_die("timer_cb: curl_multi_socket_action", rc);
    check_multi_info(g);
}

/* Clean up the SockInfo structure */
static void remsock(SockInfo *f)
{
    if (f)
    {
        if (f->evset)
        {
            SwooleG.main_reactor->del(SwooleG.main_reactor, f->sockfd);
        }
        free(f);
    }
}

/* Assign information to a SockInfo structure */
static void setsock(SockInfo*f, curl_socket_t s, CURL*e, int act, GlobalInfo*g)
{
    int kind = (act & CURL_POLL_IN ? SW_EVENT_READ : 0) | (act & CURL_POLL_OUT ? SW_EVENT_WRITE : 0);

    f->sockfd = s;
    f->action = act;
    f->easy = e;

    if (f->evset)
    {
        SwooleG.main_reactor->set(SwooleG.main_reactor, s, PHP_SW_FD_CURL | kind);
        f->evset = 1;
    }
    else
    {
        SwooleG.main_reactor->add(SwooleG.main_reactor, s, PHP_SW_FD_CURL | kind);
        f->evset = 1;
    }
}


/* Initialize a new SockInfo structure */
static void addsock(curl_socket_t s, CURL *easy, int action, GlobalInfo *g)
{
    SockInfo *fdp = calloc(sizeof(SockInfo), 1);

    fdp->global = g;
    setsock(fdp, s, easy, action, g);
    curl_multi_assign(g->multi, s, fdp);
}

/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
    GlobalInfo *g = (GlobalInfo*) cbp;
    SockInfo *fdp = (SockInfo*) sockp;
    const char *whatstr[] =
    { "none", "IN", "OUT", "INOUT", "REMOVE" };

    fprintf(MSG_OUT, "socket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);
    if (what == CURL_POLL_REMOVE)
    {
        fprintf(MSG_OUT, "\n");
        remsock(fdp);
    }
    else
    {
        if (!fdp)
        {
            fprintf(MSG_OUT, "Adding data: %s\n", whatstr[what]);
            addsock(s, e, what, g);
        }
        else
        {
            fprintf(MSG_OUT, "Changing action from %s to %s\n", whatstr[fdp->action], whatstr[what]);
            setsock(fdp, s, e, what, g);
        }
    }
    return 0;
}

static size_t curl_onWrite(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t realsize = size * nmemb;
    ConnInfo *conn = (ConnInfo*) data;
    (void) ptr;
    (void) conn;
    return realsize;
}

/* CURLOPT_PROGRESSFUNCTION */
static int prog_cb(void *p, double dltotal, double dlnow, double ult, double uln)
{
    ConnInfo *conn = (ConnInfo *) p;
    (void) ult;
    (void) uln;

    fprintf(MSG_OUT, "Progress: %s (%g/%g)\n", conn->url, dlnow, dltotal);
    return 0;
}

PHP_FUNCTION(swoole_curl_init)
{
    zval *z_mh;
    php_curlm *mh;
    zval tmp_val;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &z_mh) == FAILURE)
    {
        return;
    }

    ZEND_FETCH_RESOURCE(mh, php_curlm *, &z_mh, -1, le_curl_multi_handle_name, le_curl_multi_handle);

    bzero(&SwooleCURLG, sizeof(SwooleCURLG));
    SwooleCURLG.multi = mh->multi;

    /* setup the generic multi interface options we want */
    curl_multi_setopt(SwooleCURLG.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
    curl_multi_setopt(SwooleCURLG.multi, CURLMOPT_SOCKETDATA, &SwooleCURLG);
    curl_multi_setopt(SwooleCURLG.multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
    curl_multi_setopt(SwooleCURLG.multi, CURLMOPT_TIMERDATA, &SwooleCURLG);

    SwooleG.main_reactor->setHandle(SwooleG.main_reactor, PHP_SW_FD_CURL | SW_EVENT_READ, curl_onRead);
    SwooleG.main_reactor->setHandle(SwooleG.main_reactor, PHP_SW_FD_CURL | SW_EVENT_WRITE, curl_onWrite);
}
