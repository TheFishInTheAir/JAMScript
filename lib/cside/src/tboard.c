#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "tboard.h"
#include "command.h"
#include "mqtt_adapter.h"
#include "core.h"

#define MINICORO_IMPL
#define MINICORO_ASM

// Uncomment following to zero stack memory. Affects performance
//#define MCO_ZERO_MEMORY
// Uncomment following to run with valgrind properly (otherwise valgrind
// will be unable to access memory). Affects performance
//#define MCO_USE_VALGRIND

#include <minicoro.h>
#include "queue/queue.h"

////////////////////////////////////////////
//////////// TBOARD FUNCTIONS //////////////
////////////////////////////////////////////

tboard_t* tboard_create(void *core, int secondary_queues)
{
    // create tboard
    assert(secondary_queues <= MAX_SECONDARIES);

    tboard_t *tboard = (tboard_t *)calloc(1, sizeof(tboard_t)); // allocate memory for tboard
                                                                // free'd in tboard_destroy()

    // assert that remote_task_t and task_t are different sizes. If they are the same size,
    // then undefined behavior will occur when issuing blocking/remote tasks.
    assert(sizeof(remote_task_t) != sizeof(task_t));

    // initiate primary queue's mutex and condition variables
    assert(pthread_mutex_init(&(tboard->cmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->tmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->hmutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->emutex), NULL) == 0);
    assert(pthread_mutex_init(&(tboard->msg_mutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->tcond), NULL) == 0);
    assert(pthread_cond_init(&(tboard->msg_cond), NULL) == 0);

    // create and initialize primary queues
    assert(pthread_mutex_init(&(tboard->pmutex), NULL) == 0);
    assert(pthread_cond_init(&(tboard->pcond), NULL) == 0);

    tboard->core = core;
    tboard->pqueue = queue_create();

    queue_init(&(tboard->pqueue));

    // set number of secondaries tboard has
    tboard->sqs = secondary_queues;

    for (int i=0; i<secondary_queues; i++) {
        // create & initialize secondary i's mutex, cond, queues
        assert(pthread_mutex_init(&(tboard->smutex[i]), NULL)==0);
        assert(pthread_cond_init(&(tboard->scond[i]), NULL) == 0);

        tboard->squeue[i] = queue_create();

        queue_init(&(tboard->squeue[i]));
    }

    // initialize remote message queues
    tboard->msg_sent = queue_create();
    tboard->msg_recv = queue_create();
    queue_init(&(tboard->msg_sent));
    queue_init(&(tboard->msg_recv));

    tboard->status = 0; // indicate its been created but not started
    tboard->shutdown = 0;
    tboard->task_count = 0; // how many concurrent tasks are running
    tboard->exec_hist = NULL;

    tboard->task_table = NULL;

    return tboard; // return address of tboard in memory
}

void tboard_start(tboard_t *tboard)
{
    // we want to start the threads for tboard executor
    // for this we allocate argument sent to executor function so it knows what to do
    // then we create the thread
    if (tboard == NULL || tboard->status != 0)
        return; // only want to start an initialized tboard
    
    // create primary executor
    exec_t *primary = (exec_t *)calloc(1, sizeof(exec_t));
    primary->type = PRIMARY_EXEC;
    primary->num = 0;
    primary->tboard = tboard;
    pthread_create(&(tboard->primary), NULL, executor, primary);

    // save it incase we call kill so we can free memory
    tboard->pexect = primary;

    // create secondary executors
    for (int i=0; i<tboard->sqs; i++) {
        exec_t *secondary = (exec_t *)calloc(1, sizeof(exec_t));
        secondary->type = SECONDARY_EXEC;
        secondary->num = i;
        secondary->tboard = tboard;
        pthread_create(&(tboard->secondary[i]), NULL, executor, secondary);
        // save it incase we call kill so we can free memory
        tboard->sexect[i] = secondary;
    }

    tboard->status = 1; // started

}

void tboard_destroy(tboard_t *tboard)
{
    // wait for threads to terminate before destroying task board
    pthread_join(tboard->primary, NULL);
    for (int i=0; i<tboard->sqs; i++) {
        pthread_join(tboard->secondary[i], NULL);
    }
    // broadcast that threads have all terminated to any thread sleeping on condition variable
    pthread_mutex_lock(&(tboard->emutex)); // exit mutex
    pthread_cond_broadcast(&(tboard->tcond)); // incase multiple threads are waiting
    pthread_mutex_unlock(&(tboard->emutex));

    // lock tmutex. If we get lock, it means that user has taken all necessary data
    // from task board as it should be locked before tboard_kill() is run
    pthread_mutex_lock(&(tboard->tmutex));
    // destroy mutex and condition variables 
    pthread_mutex_destroy(&(tboard->cmutex));
    pthread_mutex_destroy(&(tboard->pmutex));
    pthread_cond_destroy(&(tboard->pcond)); // broadcasted in tboard_kill()
    for (int i=0; i<tboard->sqs; i++) {
        pthread_mutex_destroy(&(tboard->smutex[i]));
        pthread_cond_destroy(&(tboard->scond[i])); // broadcasted in tboard_kill();
    }
    pthread_cond_destroy(&(tboard->tcond));

    // empty task queues and destroy any persisting contexts
    for (int i=0; i<tboard->sqs; i++) {
        struct queue_entry *entry = queue_peek_front(&(tboard->squeue[i]));
        while (entry != NULL) {
            queue_pop_head(&(tboard->squeue[i]));
            task_destroy((task_t *)(entry->data)); // destroys task_t and coroutine
            free(entry);
            entry = queue_peek_front(&(tboard->squeue[i]));
        }
    }
    struct queue_entry *entry = queue_peek_front(&(tboard->pqueue));
    while (entry != NULL) {
        queue_pop_head(&(tboard->pqueue));
        task_destroy((task_t *)(entry->data)); // destroys task_t and coroutine
        free(entry);
        entry = queue_peek_front(&(tboard->pqueue));
    }

    // empty outgoing remote task message queues
    struct queue_entry *msg = queue_peek_front(&(tboard->msg_sent));
    while (msg != NULL) {
        queue_pop_head(&(tboard->msg_sent));
        remote_task_destroy((remote_task_t *)(msg->data)); // destroys remote_task_t
                                                           // and any parent task_t's
        free(msg);
        msg = queue_peek_front(&(tboard->msg_sent));
    }
    // empty incoming remote task message queues
    msg = queue_peek_front(&(tboard->msg_recv));
    while (msg != NULL) {
        queue_pop_head(&(tboard->msg_recv));
        remote_task_destroy((remote_task_t *)(msg->data)); // destroys remote_task_t
                                                           // and any parent task_t's
        free(msg);
        msg = queue_peek_front(&(tboard->msg_recv));
    }

    // unlock tmutex so we can destroy it
    pthread_mutex_unlock(&(tboard->tmutex));
    
    // broadcast MQTT msg_cond condition variable before destroying
    pthread_cond_broadcast(&(tboard->msg_cond)); // incase MQTT is waiting on this
    pthread_cond_destroy(&(tboard->msg_cond));

    // free executor arguments
    free(tboard->pexect);
    for (int i=0; i<tboard->sqs; i++) {
        free(tboard->sexect[i]);
    }
    
    // destroy history mutex
    history_destroy(tboard);
    // destroy the registry of functions
    destroy_func_registry(tboard);


    // destroy rest of task board mutexes
    pthread_mutex_destroy(&(tboard->hmutex));
    pthread_mutex_destroy(&(tboard->tmutex));
    pthread_mutex_destroy(&(tboard->emutex));
    pthread_mutex_destroy(&(tboard->msg_mutex));

    // free task board object
    free(tboard);
}

bool tboard_kill(tboard_t *t)
{
    printf("Status =========================== %d\n", t->status);
    if (t == NULL || t->status == 0)
        return false;
    
    // lock emutex to before queueing executor thread cancellation
    pthread_mutex_lock(&(t->emutex));
    // indicate to taskboard that shutdown is occuring
    t->shutdown = 1;

    // queue primary executor thread cancellation, signal condition variable
    pthread_mutex_lock(&(t->pmutex));
    pthread_cancel(t->primary);
    pthread_cond_signal(&(t->pcond));
    pthread_mutex_unlock(&(t->pmutex));

    for (int i=0; i<t->sqs; i++) {
        // queue secondary executor thread i cancellation, signal condition variable
        pthread_mutex_lock(&(t->smutex[i]));
        pthread_cancel(t->secondary[i]);
        pthread_cond_signal(&(t->scond[i]));
        pthread_mutex_unlock(&(t->smutex[i]));
    }
    
    // wait for executor threads to terminate fully
    pthread_cond_wait(&(t->tcond), &(t->emutex)); // will be signaled by tboard_destroy once threads exit
    // unlock emutex so it can be destroyed
    pthread_mutex_unlock(&(t->emutex));
    // task board has been killed so we return true
    printf("Kill done \n");
    return true;
}

void tboard_exit()
{
    pthread_exit(NULL);
}

int tboard_get_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    int ret = t->task_count;
    pthread_mutex_unlock(&(t->cmutex));
    return ret;
}

void tboard_inc_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    t->task_count++;
    pthread_mutex_unlock(&(t->cmutex));
}

void tboard_deinc_concurrent(tboard_t *t){
    pthread_mutex_lock(&(t->cmutex));
    t->task_count--;
    pthread_mutex_unlock(&(t->cmutex));
}

int tboard_add_concurrent(tboard_t *t) {
    // non-zero value indicates we can add a new task
    int ret = 0;
    pthread_mutex_lock(&(t->cmutex));
    if (DEBUG && t->task_count < 0)
        tboard_log("tboard_add_concurrent: Invalid task_count encountered: %d\n",t->task_count);

    if (t->task_count < MAX_TASKS)
        ret = ++(t->task_count);
    pthread_mutex_unlock(&(t->cmutex));
    return ret;
}

void tboard_register_func(tboard_t *t, function_t fn) {
    function_t *f = (function_t *)malloc(sizeof(function_t));
    f->fn = fn.fn;
    f->fn_name = strdup(fn.fn_name);
    f->fn_sig = strdup(fn.fn_sig);
    f->sideef = fn.sideef;
    HASH_ADD_KEYPTR(hh, t->registry, f->fn_name, strlen(f->fn_name), f);
}

function_t *tboard_find_func(tboard_t *t, char *fname) {
    function_t *f;
    HASH_FIND_STR(t->registry, fname, f);
    if (f)
        return f;
    else 
        return NULL;
}

void destroy_func_registry(tboard_t *t) {
    function_t *currf, *tmp;

    HASH_ITER(hh, t->registry, currf, tmp) {
        HASH_DEL(t->registry, currf);  /* delete; t->registry advances to next */
        free(currf->fn_name);
        free(currf->fn_sig);
        free(currf);
    }
}


///////////////////////////////////////////////////
/////////// Logging functionality /////////////////
///////////////////////////////////////////////////

int tboard_log(char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    printf("Logging: ");
    result = vprintf(format, args);
    fflush(stdout);
    va_end(args);
    return result;
}

int tboard_err(char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    fprintf(stderr, "Error: ");
    result = vfprintf(stderr, format, args);
    fflush(stderr);
    va_end(args);
    return result;
}