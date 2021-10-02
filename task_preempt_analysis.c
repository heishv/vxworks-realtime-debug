/* Copyright (c) 2019, heishv
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache License 2.0
 */

#include <vxWorks.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <semLib.h>
#include <intLib.h>
#include <errno.h>
#include <taskLib.h>
#include <dataCollectorLib.h>
#include "private/eventdefsP.h"
#include <private/windLibP.h>
#include <private/msgQLibP.h>
#include "realtime_debug_config.h"
#include "task_preempt_analysis.h"
#include <subsys/timer/vxbTimerLib.h>
#include <private/timerLibP.h>

extern _Vx_ticks_t tickGet (void);

LOCAL void pr_collector_int_enter(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_int_exit_k(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_int_exit(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_sem_give(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_sem_take(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_msg_recv(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_msg_send(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_dispatch_pi(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_dispatch(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_sig_kill(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_tick_undelay(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
LOCAL void pr_collector_tick_timeout(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
/*LOCAL void pr_collector_tick_announce_tmr_wd(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);*/
LOCAL void pr_collector_tick_delay(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);
/*LOCAL void pr_collector_wd_start(DATA_COLLECTOR_ID data_collector, const void *args, size_t size);*/
LOCAL char *pr_get_action_name(event_t action);
LOCAL STATUS pr_event_push(event_t action, const void *addr, size_t nbytes);
#ifndef PREEMPT_SAVE_ALL_ENABLE
LOCAL STATUS pr_event_pop(void);
#endif

LOCAL int32_t pr_collector_init;
LOCAL int32_t pr_target_task;
LOCAL _Vx_usr_arg_t pr_target_ent_event;
LOCAL int8_t  pr_monitor_status = PR_MONITOR_STATUS_IDLE;
LOCAL int8_t  pr_preempt_flag;
LOCAL int32_t pr_event_mng[PREEMPT_EVENT_MAX_NUMBER];
LOCAL int32_t pr_event_mng_cur;
LOCAL uint8_t *pr_event_buf;

LOCAL int32_t pr_cnt_push;
LOCAL int32_t pr_cnt_pop;


/* list of data collectors for each event. */
LOCAL PR_EVENT_MAP_INFO pr_event_map[] =
{
    {EVENT_INT_ENTER,             pr_collector_int_enter},
    {EVENT_INT_EXIT_K,            pr_collector_int_exit_k},
    {EVENT_INT_EXIT,              pr_collector_int_exit},
    {EVENT_OBJ_SEMGIVE,           pr_collector_sem_give},
    {EVENT_OBJ_SEMTAKE,           pr_collector_sem_take},
    {EVENT_OBJ_MSGRECEIVE,        pr_collector_msg_recv},
    {EVENT_OBJ_MSGSEND,           pr_collector_msg_send},
    {EVENT_OBJ_SIGKILL,           pr_collector_sig_kill},
    {EVENT_WIND_EXIT_DISPATCH,    pr_collector_dispatch},
    {EVENT_WIND_EXIT_DISPATCH_PI, pr_collector_dispatch_pi},
    {EVENT_WINDTICKUNDELAY,       pr_collector_tick_undelay},
    {EVENT_WINDTICKTIMEOUT,       pr_collector_tick_timeout},
    {EVENT_WINDDELAY,             pr_collector_tick_delay},
};

static UINT32 pr_sysclkcount_get()
{
    timerHandle_t clk_timer;
    UINT32  clkCnt;

    clk_timer = sysClkHandleGet();

    if ((clk_timer == NULL)
      ||(clk_timer->timerCountGet == NULL)) {
        return ERROR;
    }

    /* Get time stamp register number */
    clk_timer->timerCountGet(clk_timer, &clkCnt);

    return clkCnt;
}

/*******************************************************************************
*
* pr_event_push - push a event to event stack
*
* This function push event to stack, blow info will be recorded
*    event id    (int)
*   timestamp    (int)
*        data    (real length)
*
* The length is assumed to be compatible with logging constraints
* (e.g. alignment)
*
* RETURNS: OK, or ERROR if unable to log event
* SEE ALSO:
* \NOMANUAL
*
*/
STATUS pr_event_push(event_t action, const void *addr, size_t nbytes)
{
    PR_EVENT_NODE      *eventBase;
    int32_t             newTask;
    int32_t             newEvent;
    size_t              event_size;
    EVT_TASK_1_T        *taskEvt;
    /*struct msg_q        *msgQId;*/
    EVENT_WIND_EXIT_DISPATCH_T *ctxInfo;

    /* If no target task, don't save anything */
    if (!pr_target_task) {
        return (ERROR);
    }

    /* If no buffer, exit */
    if (nbytes > PR_EVENT_MAX_PAYLOAD_SIZE) {
        return (ERROR);
    }

    /* If no buffer, exit */
    if (pr_event_mng_cur >= PREEMPT_EVENT_MAX_NUMBER - 1) {
        return (ERROR);
    }

    switch(action) {
        /* Handle the events that activate a task */
        case EVENT_OBJ_SEMGIVE:
        case EVENT_OBJ_MSGSEND:
        case EVENT_OBJ_SIGKILL:
        case EVENT_WINDTICKUNDELAY:
        case EVENT_WINDTICKTIMEOUT:

            if (EVENT_OBJ_SEMGIVE == action) {
                taskEvt = (EVT_TASK_1_T *)addr;
                /* Get actived task */
                newTask = (int32_t)SEM_OWNER((SEM_ID)(taskEvt->args[0]));
            }
            /*else if (EVENT_OBJ_MSGSEND == action) {
                taskEvt = (EVT_TASK_1_T *)addr;
                msgQId  = (struct msg_q *)(taskEvt->args[0]);
                newTask = msgQId->msgQ;
            }*/
            else if ((EVENT_OBJ_SIGKILL == action)
                   ||(EVENT_WINDTICKUNDELAY == action)
                   ||(EVENT_WINDTICKTIMEOUT == action)){
                taskEvt = (EVT_TASK_1_T *)addr;
                newTask = taskEvt->args[0];
            }
            else {
                /* Should not come here */
                return ERROR;
            }

            /* Status machine */
            switch (pr_monitor_status) {
                case PR_MONITOR_STATUS_IDLE:
                    /* This task is activated for the first time, put it in monitor channel */
                    if (newTask == pr_target_task) {
                        pr_monitor_status  = PR_MONITOR_STATUS_ACTIVE;
                        pr_preempt_flag    = FALSE;

                        /* Don't touch it, need to keep same structure with pending event */
                        pr_target_ent_event = taskEvt->args[0];
                    }
                    else {
                        /* Other task, don't care */
                    }
                    break;

                case PR_MONITOR_STATUS_ACTIVE:
                case PR_MONITOR_STATUS_RUNNING:
                    /* Just activate, no dispatch, not a preempt, do nothing here */
                    break;

                case PR_MONITOR_STATUS_EXIT:
                default:
                    /* Unknown status, reset to default */
                    PR_DBG_PRINT("Unknown status %d, line %d", pr_monitor_status, __LINE__);
                    pr_monitor_status = PR_MONITOR_STATUS_IDLE;
                    break;
            }

            PR_DBG_PRINT("%s: to activate %s\r\n", pr_get_action_name(action), taskName((TASK_ID)newTask));
            PR_DBG_PRINT("  current status is %d, preempt flag is %d\r\n", pr_monitor_status, pr_preempt_flag);
            break;

        /* Handle dispatch, it means that a task start to run */
        case EVENT_WIND_EXIT_DISPATCH:
        case EVENT_WIND_EXIT_DISPATCH_PI:
            ctxInfo = (EVENT_WIND_EXIT_DISPATCH_T *)addr;

            newTask = (int32_t)ctxInfo->taskIdNew;

            /* Status machine */
            switch (pr_monitor_status) {
                case PR_MONITOR_STATUS_IDLE:
                    /* Wrong status, no activate, but directly running */
                    if (newTask == pr_target_task) {
                        /* Wrong status, no activate, but directly running */
                        PR_DBG_PRINT("Get target task running, but no activate(%d)\r\n", __LINE__, 0);
                    }
                    else {
                        /* Target task is not actived, other task gets to run, do nothing */
                    }
                    break;

                case PR_MONITOR_STATUS_ACTIVE:
                    /* Normal process, enter next status */
                    if (newTask == pr_target_task) {
                        pr_monitor_status = PR_MONITOR_STATUS_RUNNING;
                    }
                    else {
                        /* Target task was preempted, recored it */
                        pr_preempt_flag = TRUE;
                    }
                    break;

                case PR_MONITOR_STATUS_RUNNING:
                    /* Wrong status, get running again */
                    if (newTask == pr_target_task) {
                        /* Wrong status, no activate, but directly running */
                        PR_DBG_PRINT("Get target task new status is running, but it's already running(%d)",
                            __LINE__, 0);
                    }
                    else {
                        /* Target task is running, get other task to run */
                        /* It means that target task was preempted */
                        pr_preempt_flag = TRUE;
                    }
                    break;

                case PR_MONITOR_STATUS_EXIT:
                    /* PR_MONITOR_STATUS_EXIT means all normal process ended */
                    /* Do nothing here */
                    break;

                default:
                    /* Unknown status, reset to default */
                    PR_DBG_PRINT("Unknown status %d, line %d", pr_monitor_status, __LINE__);
                    pr_monitor_status = PR_MONITOR_STATUS_IDLE;
                    break;
            }

            PR_DBG_PRINT("%s: to schedule %s\r\n", pr_get_action_name(action), taskName((TASK_ID)newTask));
            PR_DBG_PRINT("  current status is %d, preempt flag is %d\r\n", pr_monitor_status, pr_preempt_flag);
            break;

        case EVENT_INT_ENTER:
        case EVENT_INT_EXIT_K:
        case EVENT_INT_EXIT:
            /* Status machine */
            switch (pr_monitor_status) {
                case PR_MONITOR_STATUS_IDLE:
                case PR_MONITOR_STATUS_EXIT:
                    /* Do nothing */
                    break;

                case PR_MONITOR_STATUS_ACTIVE:
                case PR_MONITOR_STATUS_RUNNING:
                    if (action == EVENT_INT_ENTER) {
                        /* Target task was preempted, save it */
                        pr_preempt_flag = TRUE;
                    }
                    else {
                        /* Don't consider INT_EXIT as preempt */
                        /* Maybe the target task was actavited in interrupt */
                        /* So exit interrupt is normal process */
                    }
                    break;

                default:
                    /* Unknown status, reset to default */
                    PR_DBG_PRINT("Unknown status %d, line %d", pr_monitor_status, __LINE__);
                    pr_monitor_status = PR_MONITOR_STATUS_IDLE;
                    break;
            }

            if (action == EVENT_INT_ENTER) {
                newEvent = *(int32_t *)addr;
                PR_DBG_PRINT("%s: interrupt index %x.\r\n", pr_get_action_name(action), newEvent);
            }
            else {
                PR_DBG_PRINT("%s: -\r\n", pr_get_action_name(action), 0);
            }
            PR_DBG_PRINT("  current status is %d, preempt flag is %d\r\n", pr_monitor_status, pr_preempt_flag);
            break;

        case EVENT_OBJ_SEMTAKE:
        case EVENT_OBJ_MSGRECEIVE:
        case EVENT_WINDDELAY:
            taskEvt  = (EVT_TASK_1_T *)addr;
            newEvent = taskEvt->args[0];

            /* Status machine */
            switch (pr_monitor_status) {
                case PR_MONITOR_STATUS_IDLE:
                case PR_MONITOR_STATUS_ACTIVE:
                case PR_MONITOR_STATUS_EXIT:
                    if (newEvent == pr_target_ent_event) {
                        /* Wrong status */
                        PR_DBG_PRINT("Get target task new status is running, but it is already running(%d)",
                            __LINE__, 0);

                        /* But set to activate status, for next time */
                        pr_monitor_status = PR_MONITOR_STATUS_ACTIVE;
                    }
                    else {
                        /* Do nothing */
                    }
                    break;

                case PR_MONITOR_STATUS_RUNNING:
                    /* Finish this cycle */
                    if (newEvent == pr_target_ent_event) {

                        /* If preempted, save all actions */
                        if (pr_preempt_flag == TRUE) {
                            pr_monitor_status = PR_MONITOR_STATUS_EXIT;
                        }
                        else {
                    #ifdef PREEMPT_SAVE_ALL_ENABLE
                            pr_monitor_status = PR_MONITOR_STATUS_EXIT;
                    #else
                            /* No preempt, pop saved actions, recover status */
                            while (1) {
                                /* Pop all activities, until to last exit status */
                                eventBase = PR_EVENT_LAST_BUF();
                                if (!eventBase) {
                                    PR_DBG_PRINT("current eventBase %p, can't pop again\r\n", eventBase,0);
                                    break;
                                }
                                else {

                                    PR_DBG_PRINT("pop an event, status %d, pr_event_mng_cur = %d\r\n",
                                        PR_EVENT_GET_STATUS(eventBase),pr_event_mng_cur);
                                    /* Pop stack action, until last exit event */
                                    if (PR_EVENT_GET_STATUS(eventBase) != PR_MONITOR_STATUS_EXIT) {
                                        pr_event_pop();
                                    }
                                    else {
                                        break;
                                    }
                                }
                            }
                            pr_monitor_status = PR_MONITOR_STATUS_IDLE;
                    #endif
                            pr_preempt_flag = FALSE;
                        }
                    }
                    else {
                        /* Do nothing */
                    }

                    PR_DBG_PRINT("%s:\r\n", pr_get_action_name(action), 0);
                    PR_DBG_PRINT("  current status is %d, preempt flag is %d\r\n",
                        pr_sysclkcount_get(), pr_preempt_flag);
                    break;

                default:
                    /* Unknown status, reset to default */
                    PR_DBG_PRINT("Unknown status %d, line %d", pr_monitor_status, __LINE__);
                    pr_monitor_status = PR_MONITOR_STATUS_IDLE;
                    pr_preempt_flag = FALSE;
                    break;
            }
            break;

        default:
            if (pr_monitor_status != PR_MONITOR_STATUS_IDLE) {
                /* Save scenario */
                pr_preempt_flag = TRUE;
            }
            break;
    }

    /* If not start, ignore it */
    if (pr_monitor_status == PR_MONITOR_STATUS_IDLE) {
        return (ERROR);
    }

    /* 3 means status, timestamp and action */
    event_size = nbytes + PR_EVENT_NODE_HEAD;

    /* If multicore, use it to lock */
    PR_EVENT_BUF_LOCK();

    /* Save action and timestamp to buffer */
    eventBase = PR_EVENT_CUR_BUF();
    eventBase->event   = action;
    eventBase->flag    = pr_preempt_flag;
    eventBase->status  = pr_monitor_status;
    eventBase->counter = pr_sysclkcount_get();
    eventBase->tick    = tickGet();

    /* Save event to buffer */
    if ((nbytes != 0) && (addr != NULL)) {
        memcpy ((char *)eventBase->data, addr, nbytes);
    }

    /* Increase stack */
    if (pr_event_mng_cur + 1 < PREEMPT_EVENT_MAX_NUMBER) {
        pr_event_mng[pr_event_mng_cur + 1] = pr_event_mng[pr_event_mng_cur] + event_size;
        pr_event_mng_cur++;
    }

    PR_EVENT_BUF_UNLOCK();

    /* Status switch */
    if (pr_monitor_status == PR_MONITOR_STATUS_EXIT) {
        pr_monitor_status = PR_MONITOR_STATUS_IDLE;
    }
    pr_cnt_push++;

    return (OK);
}

#ifndef PREEMPT_SAVE_ALL_ENABLE
/*******************************************************************************
*
* pr_event_pop - pop last event from event stack
*
* RETURNS: OK, or ERROR if unable to log event
* SEE ALSO:
* \NOMANUAL
*
*/
STATUS pr_event_pop()
{
    if (!pr_event_mng_cur) {
        return (ERROR);
    }

    /* If multicore, use it to lock */
    PR_EVENT_BUF_LOCK();

    /* Decrease stack */
    pr_event_mng_cur--;

    PR_EVENT_BUF_UNLOCK();

    pr_cnt_pop++;

    return (OK);
}
#endif

/*******************************************************************************
*
* pr_collector_int_enter - write EVENT_INT_ENTER event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_int_enter(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_INT_ENTER, args, size);
}

/*******************************************************************************
*
* pr_collector_int_exit_k - write EVENT_INT_EXIT_K event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_int_exit_k(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_INT_EXIT_K, args, size);
}

/*******************************************************************************
*
* pr_collector_int_exit - write EVENT_INT_EXIT event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_int_exit(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_INT_EXIT, args, size);
}

/*******************************************************************************
*
* pr_collector_sem_give - write EVENT_OBJ_SEMGIVE event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_sem_give(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_OBJ_SEMGIVE, args, size);
}

/*******************************************************************************
*
* pr_collector_sem_take - write EVENT_OBJ_SEMTAKE event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_sem_take(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_OBJ_SEMTAKE, args, size);
}

/*******************************************************************************
*
* pr_collector_msg_recv - write EVENT_OBJ_MSGRECEIVE event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_msg_recv(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_OBJ_MSGRECEIVE, args, size);
}

/*******************************************************************************
*
* pr_collector_msg_send - write EVENT_OBJ_MSGSEND event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_msg_send(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_OBJ_MSGSEND, args, size);
}

/*******************************************************************************
*
* pr_collector_dispatch - write EVENT_WIND_EXIT_DISPATCH event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_dispatch(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WIND_EXIT_DISPATCH, args, size);
}

/*******************************************************************************
*
* pr_collector_dispatch_pi - write EVENT_WIND_EXIT_DISPATCH_PI event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_dispatch_pi(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WIND_EXIT_DISPATCH_PI, args, size);
}

/*******************************************************************************
*
* pr_collector_sig_kill - write EVENT_OBJ_SIGKILL event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_sig_kill(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_OBJ_SIGKILL, args, size);
}

/*******************************************************************************
*
* pr_collector_tick_undelay - write EVENT_WINDTICKUNDELAY event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_tick_undelay(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WINDTICKUNDELAY, args, size);
}

/*******************************************************************************
*
* pr_collector_tick_undelay - write EVENT_WINDTICKTIMEOUT event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_tick_timeout(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WINDTICKTIMEOUT, args, size);
}

/*******************************************************************************
*
* pr_collector_tick_announce_tmr_wd - write EVENT_WINDTICKANNOUNCETMRWD event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
/*LOCAL void pr_collector_tick_announce_tmr_wd(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WINDTICKANNOUNCETMRWD, args, size);
}*/

/*******************************************************************************
*
* pr_collector_tick_delay - write EVENT_WINDDELAY event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
LOCAL void pr_collector_tick_delay(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    EVT_TASK_1_T evt;
    evt.args[0] = taskIdSelf();

    pr_event_push(EVENT_WINDDELAY, &evt, size);
}

/*******************************************************************************
*
* pr_collector_wd_start - write EVENT_WINDWDSTART event to event stack
*
* Write an event to the event stack. A timestamp is included. The
* event is written as an eventId, with no parameters. The input event has
* the interrupt ident as a parameter, the output has it encoded into the
* eventId.
*
* RETURNS: N/A
*
*
* \NOMANUAL
*/
/*LOCAL void pr_collector_wd_start(DATA_COLLECTOR_ID data_collector, const void *args, size_t size)
{
    pr_event_push(EVENT_WINDWDSTART, args, size);
}*/

/*******************************************************************************
*
* pr_collector_register - install data handlers for RealTimeAnalyser events
*
* This routine installs dataCollectors for RealTimeAnalyser events at the fixed
* logging level.
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_collector_register()
{
    DATA_COLLECTOR_ID   pCollector;
    unsigned int        i;
    int                 result = OK;

    for (i = 0; i < NELEMENTS(pr_event_map); i++)
    {
        pCollector = dataCollectorCreate(pr_event_map[i].evtId, pr_event_map[i].data_collector,
            0, NULL, NULL, 1, "RealTimeAnalyser");
        if (pCollector == NULL) {
            result = ERROR;
            break;
        }

        if (dataCollectorRegister (pCollector) != OK) {
            dataCollectorDelete (pCollector);
            result = ERROR;
            break;
        }
    }

    return (result);
}

/*******************************************************************************
*
* pr_get_action_name - get action name
*
* RETURNS:
*
*
*/
char *pr_get_action_name(event_t action)
{
    switch(action) {
        /* Handle the events that activate a task */
        case EVENT_OBJ_SEMGIVE:
            return "SemGive";
        case EVENT_OBJ_MSGSEND:
            return "MsgSend";
        case EVENT_OBJ_SIGKILL:
            return "SigKill";
        case EVENT_WINDTICKUNDELAY:
            return "TickUndelay";
        case EVENT_WINDTICKTIMEOUT:
            return "TickTimeout";
        case EVENT_WINDTICKANNOUNCETMRWD:
            return "WdTimeout";

        case EVENT_INT_ENTER:
            return "IntEnter";
        case EVENT_INT_EXIT_K:
            return "IntExitK";
        case EVENT_INT_EXIT:
            return "IntExit";

        case EVENT_OBJ_SEMTAKE:
            return "SemTake";
        case EVENT_OBJ_MSGRECEIVE:
            return "MsgReceive";
        case EVENT_WINDDELAY:
            return "TickDelay";
        case EVENT_WINDWDSTART:
            return "WdStart";

        case EVENT_WIND_EXIT_DISPATCH:
            return "Dispatch";
        case EVENT_WIND_EXIT_DISPATCH_PI:
            return "DispatchPI";
        default:
            return "Unknown";
    }
}

/*******************************************************************************
*
* pr_get_string - get string
*
* RETURNS:
*
*
*/
void pr_get_string(char *str, event_t action, uint32_t tick, uint32_t counter, TASK_ID taskId)
{
    char timestr[32];
    int  padlen;

    sprintf(timestr, "%u-%u", tick, counter);

    padlen = 14 - strlen(timestr);
    if (padlen > 0) {
        while(padlen) {
            timestr[14 - padlen] = ' ';
            padlen--;
        }
        timestr[14] = 0;
    }

    switch(action) {
        /* Handle the events that activate a task */
        case EVENT_OBJ_SEMGIVE:
            sprintf(str, "%s (SEMGIVE)      task %s ready by semGive", timestr, taskName(taskId));
            break;
        case EVENT_OBJ_MSGSEND:
            sprintf(str, "%s (MSGSEND)      task %s ready by msgSend", timestr, taskName(taskId));
            break;
        case EVENT_OBJ_SIGKILL:
            sprintf(str, "%s (SIGKILL)      task %s ready by sigKill", timestr, taskName(taskId));
            break;
        case EVENT_WINDTICKUNDELAY:
            sprintf(str, "%s (TICKUNDELAY)  task %s ready by taskDelay timeout", timestr, taskName(taskId));
            break;
        case EVENT_WINDTICKTIMEOUT:
            sprintf(str, "%s (TICKTIMEOUT)  task %s ready by taskDelay timeout", timestr, taskName(taskId));
            break;
        case EVENT_WINDTICKANNOUNCETMRWD:
            sprintf(str, "%s (WDTIMEOUT)    watchdog %x check(not timeout)", timestr, taskId);
            break;

        case EVENT_INT_ENTER:
            sprintf(str, "%s (INT_ENTER)    interrupt enter, id(%d)", timestr, taskId);
            break;
        case EVENT_INT_EXIT_K:
            sprintf(str, "%s (INT_EXIT_K)   interrupt exit", timestr);
            break;
        case EVENT_INT_EXIT:
            sprintf(str, "%s (INT_EXIT)     interrupt exit", timestr);
            break;

        case EVENT_OBJ_SEMTAKE:
            sprintf(str, "%s (SEMTAKE)      task pending by semTake", timestr);
            break;
        case EVENT_OBJ_MSGRECEIVE:
            sprintf(str, "%s (MSGRECEIVE)   task pending by msgReceive", timestr);
            break;
        case EVENT_WINDDELAY:
            sprintf(str, "%s (TICKDELAY)    task delay by taskDelay", timestr);
            break;
        case EVENT_WINDWDSTART:
            sprintf(str, "%s (WDSTART)      start watchdog", timestr);
            break;

        case EVENT_WIND_EXIT_DISPATCH:
            sprintf(str, "%s (DISPATCH)     task %s running", timestr, taskName(taskId));
            break;
        case EVENT_WIND_EXIT_DISPATCH_PI:
            sprintf(str, "%s (DISPATCH)     task %s running", timestr, taskName(taskId));
            break;
        default:
            sprintf(str, "%s Unknown        action %d", timestr, action);
            break;
    }
}

/*******************************************************************************
*
* pr_print - print event
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_print()
{
    int32_t         eventNum;
    int32_t         loopNo;
    int32_t         targetTask = 0;
    int16_t         action;
    uint32_t        counter;
    uint32_t        tick;
    TASK_ID         taskId;
    int32_t         status;
    uint8_t         preempt;
    int32_t         semId;
    EVT_TASK_1_T    *taskEvt;
    /*struct msg_q    *msgQId;*/
    char            str[256];
    char            headstr[8];
    PR_EVENT_NODE  *eventBase;
    EVENT_INT_ENTER_T *intInfo;
    EVENT_WIND_EXIT_DISPATCH_T *ctxInfo;

    eventNum = pr_event_mng_cur;
    status   = PR_MONITOR_STATUS_IDLE;

    printf("Get %d preempt event(push %d, pop %d):\r\n", eventNum, pr_cnt_push, pr_cnt_pop);

    for (loopNo = 0; loopNo < eventNum; loopNo++) {

        eventBase = (PR_EVENT_NODE *)(pr_event_buf + pr_event_mng[loopNo]);
        preempt   = eventBase->flag;
        counter   = eventBase->counter;
        action    = eventBase->event;
        tick      = eventBase->tick;

        if (((status == PR_MONITOR_STATUS_EXIT)
           ||(status == PR_MONITOR_STATUS_IDLE))
           &&(eventBase->status != PR_MONITOR_STATUS_IDLE)) {
            sprintf(headstr, "\r\n----");
        }
        else if ((status != PR_MONITOR_STATUS_IDLE)
           &&((eventBase->status == PR_MONITOR_STATUS_EXIT)
            ||(eventBase->status == PR_MONITOR_STATUS_IDLE))) {
            sprintf(headstr, "----");
        }
        else {
            if (preempt) {
                sprintf(headstr, "  . ");
            }
            else {
                sprintf(headstr, "  | ");
            }
        }

        status    = eventBase->status;

        switch(action) {
            /* Handle the events that activate a task */
            case EVENT_OBJ_SEMGIVE:
                taskEvt = (EVT_TASK_1_T *)eventBase->data;

                /* Get actived task */
                semId  = (taskEvt->args[0]);
                taskId = (int32_t)SEM_OWNER((SEM_ID)(taskEvt->args[0]));
                if (!targetTask) {
                    targetTask = taskId;
                }
                pr_get_string(str, action, tick, counter, taskId);
                printf("%s%s %x\r\n", headstr, str, semId);
                break;

            /*case EVENT_OBJ_MSGSEND:
                taskEvt = (EVT_OBJ_1_T *)bufPointer;

                msgQId = (struct msg_q *)(taskEvt->args[0]);

                taskId = msgQId->msgQ.pendQ;
                if (!targetTask) {
                    targetTask = taskId;
                }
                pr_get_string(str, action, counter, taskId);
                printf("%s%s\r\n", headstr, str);
                break;*/

            case EVENT_OBJ_SIGKILL:
            case EVENT_WINDTICKUNDELAY:
            case EVENT_WINDTICKTIMEOUT:
                taskEvt = (EVT_TASK_1_T *)eventBase->data;
                taskId  = taskEvt->args[0];
                if (!targetTask) {
                    targetTask = taskId;
                }
                pr_get_string(str, action, tick, counter, taskId);
                printf("%s%s\r\n", headstr, str);
                break;

            case EVENT_OBJ_SEMTAKE:
                taskEvt = (EVT_TASK_1_T *)eventBase->data;
                semId   = taskEvt->args[0];
                pr_get_string(str, action, tick, counter, semId);
                printf("%s%s %x\r\n", headstr, str, semId);
                break;

            case EVENT_OBJ_MSGRECEIVE:
            case EVENT_WINDDELAY:
            case EVENT_WINDWDSTART:
                pr_get_string(str, action, tick, counter, 0);
                printf("%s%s\r\n", headstr, str);
                break;

            case EVENT_WIND_EXIT_DISPATCH:
            case EVENT_WIND_EXIT_DISPATCH_PI:
                ctxInfo = (EVENT_WIND_EXIT_DISPATCH_T *)eventBase->data;
                taskId  = (int32_t)ctxInfo->taskIdNew;

                pr_get_string(str, action, tick, counter, taskId);
                printf("%s%s\r\n", headstr, str);
                break;

            case EVENT_INT_ENTER:
            case EVENT_INT_EXIT_K:
            case EVENT_INT_EXIT:
                intInfo = (EVENT_INT_ENTER_T *)eventBase->data;
                pr_get_string(str, action, tick, counter, intInfo->interruptId);
                printf("%s%s\r\n", headstr, str);
                break;

            default:
                break;
        }
    }

    return OK;
}

/*******************************************************************************
*
* pr_start - set task name or task ID for RealTimeAnalyser events and register collector
*
* Every core can set a task name as monitor target, we can monitor all of the
* related event with this task. If it was activated then run, we will ignore this
* event, else we will record who preemt it, include interrput and higher priority
* tasks.
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_start(long taskNameOrId)
{
    extern TASK_ID taskIdFigure(long taskNameOrId);
    TASK_ID tid = taskIdFigure(taskNameOrId);

    if (taskNameOrId == 0) {
        printf("No target task.\r\n");
        return ERROR;
    }

    /* Keep original value, it maybe a watchdog ID */
    if (tid == TASK_ID_ERROR){
        pr_target_task = taskNameOrId;
    }
    else {
        pr_target_task = tid;
    }

    /* Alloc buffer */
    if (!pr_event_buf) {
        pr_event_buf = malloc(PREEMPT_EVENT_MAX_NUMBER * PR_EVENT_NODE_MAX_SIZE);
    }

    /* Init parameters */
    memset(pr_event_mng, 0, sizeof(pr_event_mng));
    pr_event_mng_cur    = 0;
    pr_monitor_status   = PR_MONITOR_STATUS_IDLE;
    pr_preempt_flag     = FALSE;
    pr_target_ent_event = 0;
    pr_cnt_push         = 0;
    pr_cnt_pop          = 0;

    /* For first time, initialize collector */
    if (!pr_collector_init) {
        if (pr_collector_register() == ERROR) {
            printf("Collector register failed.\r\n");
            return ERROR;
        }
        pr_collector_init = TRUE;

        /* Start to collect */
        dataCollectorOn();
    }

    return OK;
}

/*******************************************************************************
*
* pr_stop - stop recording
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_stop()
{
    /* Don't record */
    pr_target_task = 0;

    /* Show result */
    pr_print();

    /* Free buffer */
    if (pr_event_buf) {
        free(pr_event_buf);
        pr_event_buf = NULL;
    }

    return OK;
}

