/* Copyright (c) 2019, heishv
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache License 2.0
 */

#ifndef __TASK_SWITCH_TRACE_H__
#define __TASK_SWITCH_TRACE_H__

#define TRACE_MAX_STR_LEN           256

typedef struct tag_TRACE_LOG_STRUCT {
    unsigned int cur_tick;          /* tick number when sample */
    unsigned int cur_dec;           /* time stamp when sample */
    void         *value1;           /* last tcb */
    void         *value2;           /* next tcb */
}TRACE_LOG_STRUCT;

typedef struct tag_TRACE_TASK_SUMMARY {
    void         *next;
    void         *tcb;              /* tcb */
    unsigned int times;             /* how many times it was switched into */

    unsigned int enter_tick;        /* enter tick */
    unsigned int enter_dec;         /* enter time stamp */

    unsigned int total_tick;        /* total cost ticks */
    unsigned int total_dec;         /* total cost time stamp */

    unsigned int cost_max_dec;      /* max cost */
    unsigned int cost_min_dec;      /* min cost */
    unsigned int when_cost_max;     /* recored number, when it reach max */
    unsigned int when_cost_min;     /* recored number, when it reach min */

    unsigned char cost_max_core;    /* core index */
    unsigned char cost_min_core;    /* core index */
    unsigned char intv_max_core;    /* core index */
    unsigned char intv_min_core;    /* core index */

    unsigned int intv_max_dec;      /* min interval between two switch in, it is useful for period task */
    unsigned int intv_min_dec;      /* max interval between two switch in, it is useful for period task */
    unsigned int when_intv_max;     /* recored number, when it reach max */
    unsigned int when_intv_min;     /* recored number, when it reach min */
}TRACE_TASK_SUMMARY;

#endif

