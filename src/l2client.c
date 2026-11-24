/*
 * l2client.c - asynchronous L2 command queue on top of l2direct.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Intrinsic.h>

#include "l2client.h"
#include "l2direct.h"
#include "demo.h"
#include "util.h"

typedef struct Job {
    char       *cmd;
    L2Done      cb;
    void       *ud;
    int         retried;
    /* the command's ;-separated parts, one at a time */
    const char *rest;           /* after the current part */
    char        part[1024];     /* the current part */
    int         busy;           /* times the L2 said it was busy with it */
    int         partsDone;
    DBuf        out;            /* the output of the parts done */
    struct Job *next;
} Job;

static XtAppContext     app;
static const AppConfig *cfg;
static int              demo;
static L2StatusFn       status_fn;

static Job         *head, *tail;
static int          running;        /* head job has been sent */
static XtInputId    input_id;
static int          input_active;
static XtIntervalId timer_id;
static int          timer_active;
static int          dispatch_scheduled;

static void dispatch(void);
static void direct_next_part(void);

static void status(const char *msg, int busy)
{
    if (status_fn) status_fn(msg, busy);
}

static void remove_timer(void)
{
    if (timer_active) XtRemoveTimeOut(timer_id);
    timer_active = 0;
}

static void remove_input(void)
{
    if (input_active) XtRemoveInput(input_id);
    input_active = 0;
}

static Job *pop(void)
{
    Job *j = head;
    if (j) {
        head = j->next;
        if (!head) tail = NULL;
    }
    running = 0;
    return j;
}

static void free_job(Job *j)
{
    free(j->cmd);
    dbuf_free(&j->out);
    free(j);
}

static void finish(int ok, const char *resp)
{
    Job *j = pop();
    remove_timer();
    /* status first, so the callback can replace the message */
    if (!head) status(ok ? "Ready" : resp, 0);
    if (j) {
        if (j->cb) j->cb(ok, resp, j->ud);
        free_job(j);
    }
    dispatch();
}

static void fail_all(const char *err)
{
    Job *j;
    remove_timer();
    while ((j = pop()) != NULL) {
        if (j->cb) j->cb(0, err, j->ud);
        free_job(j);
    }
    status(err, 0);
}

static void dead_connection(void)
{
    remove_input();
    l2d_close();
}

/* the connection broke while the head job ran: retry it once on a fresh
   one if nothing of it was answered yet */
static void lost_while_running(int nothing_received)
{
    char msg[300];
    dead_connection();
    remove_timer();
    if (head && !head->retried && nothing_received) {
        head->retried = 1;
        running = 0;
        dispatch();
        return;
    }
    sprintf(msg, "Connection to L2 lost: %.250s", l2d_error());
    finish(0, msg);
}

static void on_timeout(XtPointer cd, XtIntervalId *id)
{
    (void)cd;
    (void)id;
    timer_active = 0;
    if (!running) return;
    /* the session is out of sync now - start fresh next time */
    dead_connection();
    finish(0, "Timeout waiting for the L2's response");
}

/* the answer to the current part has arrived */
static void direct_part_done(void);

static void on_busy_retry(XtPointer cd, XtIntervalId *id)
{
    (void)cd;
    (void)id;
    timer_active = 0;
    if (!running || !head) return;
    if (!l2d_connected()) {
        lost_while_running(0);
        return;
    }
    /* send the same part again */
    switch (l2d_begin(head->part)) {
    case 0:
        timer_id = XtAppAddTimeOut(app, (unsigned long)cfg->timeout * 1000UL, on_timeout, NULL);
        timer_active = 1;
        break;
    case 1:
        direct_part_done();
        break;
    default:
        lost_while_running(0);
        break;
    }
}

static void direct_part_done(void)
{
    Job *j = head;
    char *resp;
    int st;

    remove_timer();
    resp = l2d_take_response(&st);
    if (l2d_is_busy(resp, st) && j->busy < L2D_BUSY_RETRIES) {
        j->busy++;
        free(resp);
        status("L2 is busy with another command, retrying ...", 1);
        timer_id = XtAppAddTimeOut(app, L2D_BUSY_WAIT_MS, on_busy_retry, NULL);
        timer_active = 1;
        return;
    }
    j->busy = 0;
    j->partsDone++;
    if (j->out.len && *resp) dbuf_appendc(&j->out, '\n');
    dbuf_appends(&j->out, resp);
    free(resp);
    direct_next_part();
}

/* send the head job's next part, or finish it */
static void direct_next_part(void)
{
    Job *j = head;
    char msg[300];

    if (!j) return;
    j->rest = l2d_next_part(j->rest, j->part, sizeof(j->part));
    if (!j->rest) {
        finish(1, j->out.data);
        return;
    }
    sprintf(msg, "L2: %.250s", j->part);
    status(msg, 1);
    switch (l2d_begin(j->part)) {
    case 0:
        timer_id = XtAppAddTimeOut(app, (unsigned long)cfg->timeout * 1000UL, on_timeout, NULL);
        timer_active = 1;
        break;
    case 1:                     /* (reboot: no response comes) */
        direct_part_done();
        break;
    case -2:
        finish(0, "Command too long for the L2");
        break;
    default:
        lost_while_running(j->partsDone == 0);
        break;
    }
}

static void on_input(XtPointer cd, int *fd, XtInputId *id)
{
    int rc;
    (void)cd;
    (void)fd;
    (void)id;

    rc = l2d_pump();
    if (!running) {
        /* idle: events from the L2, or it closed the connection */
        if (rc == L2D_EOF || !l2d_connected()) {
            dead_connection();
            status("Connection to L2 lost (reconnects on demand)", 0);
        }
        return;
    }
    if (rc == L2D_AGAIN) return;
    if (rc == L2D_DONE) {
        if (!l2d_connected()) remove_input();   /* (answered, then closed) */
        direct_part_done();
        return;
    }
    /* EOF while running: retry once on a fresh connection */
    lost_while_running(head->partsDone == 0);
}

static int ensure_connected(void)
{
    char err[256], msg[400];
    int rc;
    if (l2d_connected()) {
        if (!input_active) {
            input_id = XtAppAddInput(app, l2d_fd(), (XtPointer)XtInputReadMask, on_input, NULL);
            input_active = 1;
        }
        return 1;
    }
    dead_connection();

    sprintf(msg, "Connecting to %.200s:%d ...", cfg->host, cfg->l2port);
    status(msg, 1);
    rc = l2d_connect(cfg->host, cfg->l2port, cfg->password, 5000, err, sizeof(err));
    if (rc < 0) {
        l2d_close();
        sprintf(msg, "Connect to %.100s failed: %.250s", cfg->host, err);
        fail_all(msg);
        return 0;
    }
    input_id = XtAppAddInput(app, l2d_fd(), (XtPointer)XtInputReadMask, on_input, NULL);
    input_active = 1;
    sprintf(msg, "Connected to %.200s", cfg->host);
    status(msg, 1);
    return 1;
}

/* demo mode: answer from the simulator after a short delay */
static void demo_reply(XtPointer cd, XtIntervalId *id)
{
    char *resp;
    (void)cd;
    (void)id;
    timer_active = 0;
    if (!head) return;
    resp = demo_exec(head->cmd);
    finish(1, resp);
    free(resp);
}

static void do_dispatch(XtPointer cd, XtIntervalId *id)
{
    char msg[300];
    (void)cd;
    (void)id;
    dispatch_scheduled = 0;
    if (running || !head) return;

    if (demo) {
        running = 1;
        sprintf(msg, "L2 (demo): %.250s", head->cmd);
        status(msg, 1);
        timer_id = XtAppAddTimeOut(app, 120, demo_reply, NULL);
        timer_active = 1;
        return;
    }
    if (!ensure_connected()) return;
    running = 1;
    /* (from the first part again, after a retry) */
    head->rest = head->cmd;
    head->busy = 0;
    head->partsDone = 0;
    dbuf_clear(&head->out);
    direct_next_part();
}

/* run from the event loop so callbacks never nest */
static void dispatch(void)
{
    if (dispatch_scheduled || running || !head) return;
    dispatch_scheduled = 1;
    XtAppAddTimeOut(app, 0, do_dispatch, NULL);
}

void l2c_init(XtAppContext a, const AppConfig *c, int d, L2StatusFn fn)
{
    app = a;
    cfg = c;
    demo = d;
    status_fn = fn;
    if (demo) demo_init();
}

void l2c_exec(const char *cmd, L2Done cb, void *ud)
{
    Job *j = (Job *)xmalloc(sizeof(Job));
    memset(j, 0, sizeof(*j));
    j->cmd = s_dup(cmd);
    j->cb = cb;
    j->ud = ud;
    j->rest = j->cmd;
    dbuf_init(&j->out);
    j->next = NULL;
    if (tail) tail->next = j;
    else head = j;
    tail = j;
    dispatch();
}

int l2c_pending(void)
{
    return head != NULL;
}

int l2c_connected(void)
{
    return demo || l2d_connected();
}

void l2c_disconnect(void)
{
    if (demo) return;
    if (running) {
        dead_connection();
        finish(0, "Disconnected");
    }
    dead_connection();
    status("Disconnected", 0);
}

const char *l2c_prompt(void)
{
    return demo ? "L2-demo>" : "L2>";
}
