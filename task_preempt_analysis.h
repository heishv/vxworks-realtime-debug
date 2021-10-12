/* Copyright (c) 2019, heishv
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache License 2.0
 */

#ifndef __TASK_PREEMPT_ANALYSIS_H__
#define __TASK_PREEMPT_ANALYSIS_H__

#define PR_MONITOR_STATUS_IDLE              0x00
#define PR_MONITOR_STATUS_ACTIVE            0x01
#define PR_MONITOR_STATUS_RUNNING           0x02
#define PR_MONITOR_STATUS_EXIT              0x03

#define PR_FREE_CORE_MARK                   0xFF

#define PR_EVENT_MAX_PAYLOAD_SIZE           (64)

//#define PR_ENABLE_DEBUG

#ifdef PR_ENABLE_DEBUG
#define PR_MAX_TRACE_TASK_NUM               256
#define PR_DBG_BUF_SIZE                     0x100000

char pr_dbg_buf[PR_DBG_BUF_SIZE];
uint32_t pr_dbg_pos;

#define PR_DBG_PRINT(a,b,c)                 \
if (pr_dbg_pos < PR_DBG_BUF_SIZE - 0x200)   \
    pr_dbg_pos += sprintf(pr_dbg_buf + pr_dbg_pos, a, b, c);
#else
#define PR_DBG_PRINT(a,b,c)                 \
    do { \
       } while((0))
#endif


#define PR_EVENT_GET_STATUS(evtBase)        (((PR_EVENT_NODE *)evtBase)->status)
#define PR_EVENT_CUR_BUF()                  (&pr_event_mng[pr_event_mng_cur])
#define PR_EVENT_LAST_BUF()                 \
    ((pr_event_mng_cur >= 1) ? (&pr_event_mng[pr_event_mng_cur - 1]) : 0)

typedef void (PR_EVENT_COLLECTOR_FUNC_T)(DATA_COLLECTOR_ID data_collector, const void * args, size_t size);

typedef struct tag_PR_EVENT_MAP_INFO {
    event_t     evtId;
    PR_EVENT_COLLECTOR_FUNC_T *data_collector;
}PR_EVENT_MAP_INFO;

typedef struct tag_PR_EVENT_NODE {
    int16_t         event;
    uint8_t         core;
    uint8_t         status;
    uint32_t        counter;

    uint32_t        flag;
    uint32_t        tick;

    _Vx_usr_arg_t   data[2];
}PR_EVENT_NODE;

STATUS pr_start(long taskNameOrId);
STATUS pr_stop(void);

#endif

