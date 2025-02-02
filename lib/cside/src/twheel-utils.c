#include <time.h>
#include "cnode.h"
#include "timeout.h"
#include "tboard.h"

// All time allowances are in microseconds
#define EARLY_TIME_FOR_SCHEDULE         1000
#define EARLY_TIME_FOR_RT               1000
#define EARLY_TIME_FOR_SY               1000



long int getcurtime()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000 +  ts.tv_nsec/1000;
}


struct timeouts *twheel_init() 
{
    timeout_error_t err;
    struct timeouts *tw = timeouts_open(0, &err);
    return tw;
}

void *clone_taskid(long int *task_id) 
{
    long int *t = (long int *)calloc(1, sizeof(long int));
    memcpy(t, task_id, sizeof(long int));
    return t;
}


bool twheel_add_event(tboard_t *tb, twheel_event_t type, void *arg, long int tval)
{
    // create an timeout event entry - note that just the entry is initialized
    struct timeout *t = calloc(1, sizeof(struct timeout));
    timeout_init(t, TIMEOUT_ABS);
    long int atval = tval;
    
    switch (type) {
        case TW_EVENT_INSTALL_SCHEDULE:
            t->callback.fn = dummy_next_schedule;
            t->callback.arg = arg;
            //atval -= EARLY_TIME_FOR_SCHEDULE;
        break;
        case TW_EVENT_RT_SCHEDULE:
            t->callback.fn = dummy_next_rt_slot;
            t->callback.arg = arg;
            atval -= EARLY_TIME_FOR_RT;
        break;
        case TW_EVENT_RT_CLOSE:
            t->callback.fn = dummy_close_rt_slot;
            t->callback.arg = arg;
        break;
        case TW_EVENT_SY_SCHEDULE:
            t->callback.fn = dummy_next_sy_slot;
            t->callback.arg = arg;
            atval -= EARLY_TIME_FOR_SY;
        break;
        case TW_EVENT_BEGIN_JSLEEP:
            t->callback.fn = dummy_next_sleep_event;
            t->callback.arg = arg;
        break;
        case TW_EVENT_REXEC_TIMEOUT:
            t->callback.fn = dummy_next_timeout_event;
            t->callback.arg = arg;
        break;
    }
    // add the timeout event to the wheel at the adjusted time
    pthread_mutex_lock(&tb->twmutex);
    timeouts_add(tb->twheel, t, atval);
    pthread_mutex_unlock(&tb->twmutex);

    return true;
}

bool twheel_delete_timeout(tboard_t *tb, long int *id)
{
    struct timeouts_it it = TIMEOUTS_IT_INITIALIZER(TIMEOUTS_PENDING);
    struct timeout *q;
    
    pthread_mutex_lock(&tb->twmutex);
    while ((q = timeouts_next(tb->twheel, &it)) != NULL) {
        if ((q->callback.fn == dummy_next_timeout_event) && (*(long int *)(q->callback.arg) == *id))
            break;
    }
    if (q != NULL) {
        timeouts_del(tb->twheel, q);
        pthread_mutex_unlock(&tb->twmutex);
        return true;
    }
    pthread_mutex_unlock(&tb->twmutex);
    return false;
}

struct timeout *twheel_get_next(tboard_t *tb)
{
    struct timeout *x = NULL;
    if (timeouts_expired(tb->twheel)) {
        pthread_mutex_lock(&tb->twmutex);
        x = timeouts_get(tb->twheel);
        pthread_mutex_unlock(&tb->twmutex);
    }
    return x;
}

void twheel_update_to_now(tboard_t *tb)
{
    pthread_mutex_lock(&tb->twmutex);
    timeouts_update(tb->twheel, (timeout_t)getcurtime());
    pthread_mutex_unlock(&tb->twmutex);
}



