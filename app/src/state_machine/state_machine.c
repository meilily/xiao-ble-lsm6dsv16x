#include "state_machine.h"
#include <zephyr/smf.h>
#include <zephyr/logging/log.h>

#include <app/lib/lsm6dsv16x.h>

LOG_MODULE_REGISTER(state_machine, CONFIG_APP_LOG_LEVEL);

/* User defined object */
struct s_object {
    /* This must be first */
    struct smf_ctx ctx;

    /* Events */
    struct k_event smf_event;
    int32_t events;

    /* Other state specific data add here */
} s_obj;

/* Forward declaration of state table */
static const struct smf_state xiao_states[];
static xiao_state_t current_state;

/* State IDLE */
static void idle_entry(void *o)
{
    LOG_INF("Entering IDLE state.");
    current_state = IDLE;
}

static void idle_run(void *o)
{
    struct s_object *s = (struct s_object *)o;

    /* Change states on Button Press Event */
    if (s->events & XIAO_EVENT_START_RECORDING) {
        smf_set_state(SMF_CTX(&s_obj), &xiao_states[RECORDING]);
    } else {
        LOG_WRN("Unhandled event in IDLE state.");
    }
}

/* State RECORDING */
static void recording_entry(void *o)
{
    LOG_INF("Entering RECORDING state.");
    current_state = RECORDING;
    lsm6dsv16x_start_acquisition();
}

static void recording_run(void *o)
{
    struct s_object *s = (struct s_object *)o;

    /* Change states on Button Press Event */
    if (s->events & XIAO_EVENT_STOP_RECORDING) {
        smf_set_state(SMF_CTX(&s_obj), &xiao_states[IDLE]);
    } else {
        LOG_WRN("Unhandled event in RECORDING state.");
    }
}

static void recording_exit(void *o)
{
    lsm6dsv16x_stop_acquisition();
}

xiao_state_t state_machine_current_state(void) {
    return current_state;
}

/* Populate state table */
static const struct smf_state xiao_states[] = {
    [IDLE] = SMF_CREATE_STATE(idle_entry, idle_run, NULL, NULL, NULL),
    [RECORDING] = SMF_CREATE_STATE(recording_entry, recording_run, recording_exit, NULL, NULL),
};

int state_machine_post_event(xiao_event_t event)
{
    /* Post the event */
    k_event_post(&s_obj.smf_event, event);
    return 0;
}

/* Initialize the state machine */
int state_machine_init(void)
{
    /* Initialize the event */
    k_event_init(&s_obj.smf_event);

    /* Set initial state */
    smf_set_initial(SMF_CTX(&s_obj), &xiao_states[IDLE]);

    return 0;
}

/* Run the state machine */
int state_machine_run(void)
{
    int ret;

    /* Run the state machine */
    while((s_obj.events = k_event_wait(&s_obj.smf_event, 0xFFFFFFFF, true, K_FOREVER))) {
        /* State machine terminates if a non-zero value is returned */
        ret = smf_run_state(SMF_CTX(&s_obj));
        if (ret) {
                /* handle return code and terminate state machine */
                break;
        }
    }
    return 0;
}