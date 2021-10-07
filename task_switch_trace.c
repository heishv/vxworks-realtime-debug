/* Copyright (c) 2019, heishv
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache License 2.0
 */

#include <vxWorks.h>
#include <errnoLib.h>
#include <hookLib.h>
#include <taskHookLib.h>
#include <private/taskLibP.h>
#include <private/kernelLibP.h>
#if (_WRS_VXWORKS_MAJOR==6)
#include <vxbTimerLib.h>
#elif (_WRS_VXWORKS_MAJOR==7)
#include <subsys/timer/vxbTimerLib.h>
#endif
#include <private/timerLibP.h>
#include <usrLib.h>
#include <strlib.h>

#include "realtime_debug_config.h"
#include "task_switch_trace.h"

TRACE_LOG_STRUCT *tr_log_buf[TRACE_CORE_NUM] = {0};
unsigned int tr_log_pos[TRACE_CORE_NUM] = {0};
unsigned int tr_working = 0;
#ifdef TRACE_IDLE_TASK_ENABLE
TASK_ID tr_idle_taskid;
#endif

extern _Vx_ticks_t tickGet (void);
#if (_WRS_VXWORKS_MAJOR==6)
extern void *pClkCookie;
#elif (_WRS_VXWORKS_MAJOR==7)
static void *pClkCookie;
#endif

static unsigned int tr_diff(unsigned int a1, unsigned int a2, unsigned int b1, unsigned int b2, unsigned int roll)
{
    unsigned int dec_cost, diff, tick_cost;

    if (a2 < b2) {
        if (a1 - 1 < b1) {
            dec_cost = a2 + roll - b2;
            diff = dec_cost;
        }
        else {
            tick_cost  = a1 - 1 - b1;
            dec_cost   = a2 + roll - b2;
            diff = roll * tick_cost + dec_cost;
        }
    }
    else {
        tick_cost  = a1 - b1;
        dec_cost   = a2 - b2;
        diff = roll * tick_cost + dec_cost;
    }
    return diff;
}

static int tr_sysclkcount_get(unsigned int *clkCnt)
{
    timerHandle_t clk_timer;

    clk_timer = sysClkHandleGet();

    if ((clkCnt == NULL)
      ||(clk_timer == NULL)
      ||(clk_timer->timerCountGet == NULL)) {
        return ERROR;
    }

    /* Get time stamp register number */
    clk_timer->timerCountGet(pClkCookie, clkCnt);

    return OK;
}

static void tr_switch_hook(WIND_TCB *old_tcb, WIND_TCB *new_tcb)
{
    int core_idx;

    core_idx = vxCpuIndexGet();

    /* If reach to end, check TRACE_LOOP_LAYOVER */
    if (tr_log_pos[core_idx] >= TRACE_RECORD_NUM) {
#ifdef TRACE_LAYOVER_ENABLE
        /* Again */
        tr_log_pos[core_idx] = 0;
#else
        /* Exit */
        tr_working = 0;
        return;
#endif
    }

    /* Save task switch info */
    tr_log_buf[core_idx][tr_log_pos[core_idx]].value1   = old_tcb;
    tr_log_buf[core_idx][tr_log_pos[core_idx]].value2   = new_tcb;
    tr_log_buf[core_idx][tr_log_pos[core_idx]].cur_tick = tickGet();
    tr_sysclkcount_get(&tr_log_buf[core_idx][tr_log_pos[core_idx]].cur_dec);

    tr_log_pos[core_idx]++;
}

static int tr_save(int core_idx, FILE *file)
{
    char sz_str[TRACE_MAX_STR_LEN];
    char tmp_buf[64];
    size_t write_cnt, str_len;
    unsigned int loop_idx, max_idx;
    unsigned int roll_over, iloop, num_len, pos;
    size_t buf_len;
    char *old_name, *new_name;
    FAST WIND_TCB *tmp_tcb;
    timerHandle_t clk_timer;

    clk_timer = sysClkHandleGet();

    /* Get rollover */
    if (clk_timer != NULL) {
        clk_timer->timerRolloverGet(pClkCookie, &roll_over);
    }
    else {
        return ERROR;
    }

    /* Output header */
    str_len = sprintf(sz_str, "current pos on core %d is %d, rollover is %d. \r\n",
        core_idx, tr_log_pos[core_idx], roll_over);
    write_cnt = fwrite(sz_str, 1, str_len, file);
    if (write_cnt != str_len) {
        printf("write to file failed.\r\n");
        return ERROR;
    }
    /* Output record */
#ifndef TRACE_LAYOVER_ENABLE
    max_idx = tr_log_pos[core_idx];
#else
    max_idx = TRACE_RECORD_NUM;
#endif
    for (loop_idx = 0; loop_idx < max_idx; loop_idx++) {
        if (taskIdVerify ((TASK_ID)tr_log_buf[core_idx][loop_idx].value1) == OK) {
            tmp_tcb  = (WIND_TCB *)tr_log_buf[core_idx][loop_idx].value1;
            old_name = taskName((TASK_ID)tmp_tcb);
        }
        else {
            old_name = "";
        }

        if (taskIdVerify ((TASK_ID)tr_log_buf[core_idx][loop_idx].value2) == OK) {
            tmp_tcb  = (WIND_TCB *)tr_log_buf[core_idx][loop_idx].value2;
            new_name = taskName((TASK_ID)tmp_tcb);
        }
        else {
            new_name = "";
        }

        pos = sprintf(tmp_buf, "%d-%d",
            tr_log_buf[core_idx][loop_idx].cur_tick,
            tr_log_buf[core_idx][loop_idx].cur_dec);
        buf_len = strlen(tmp_buf);

        num_len = 12;
        if (buf_len < num_len) {
            for (iloop = 0; iloop < num_len - buf_len; iloop++) {
                strcat(tmp_buf, " ");
            }
        }

        str_len = sprintf(sz_str,
            "%6d  " \
            "tick: %s " \
            "old_task: %08x %-18s   -->    "\
            "new_task: %08x %-18s\r\n",
            loop_idx,
            tmp_buf,
            tr_log_buf[core_idx][loop_idx].value1, old_name,
            tr_log_buf[core_idx][loop_idx].value2, new_name);

        write_cnt = fwrite(sz_str, 1, str_len, file);
        if (write_cnt != str_len) {
            printf("write to file failed.\r\n");
            break;
        }
    }

    return OK;
}

#ifdef TRACE_SAVE_FILE_ENABLE
static void tr_save_to_file(int core_idx, char *file_name)
{
    FILE *file;

    /* Open file */
    file = fopen(file_name, "a+");
    if (!file) {
        printf ("Could not open file %s.\r\n", file_name);
        return;
    }

    /* Save to file */
    tr_save(core_idx, file);

    /* Close file */
    if (file) {
        fclose (file);
    }
}
#endif

#ifdef TRACE_AUTO_ANALYSIS_ENABLE
static TRACE_TASK_SUMMARY *tr_get_task_desc(void *head, void *tcb)
{
    TRACE_TASK_SUMMARY *task_desc;
    TRACE_TASK_SUMMARY *tmp_head;

    /* Get head */
    tmp_head = head;

    /* To match tcb */
    task_desc = tmp_head;
    while(task_desc) {
        if (task_desc->tcb == tcb) {
            break;
        }
        task_desc = task_desc->next;
    }

    /* If no match, alloc */
    if (task_desc == NULL) {
        task_desc = malloc(sizeof(TRACE_TASK_SUMMARY));
        if (!task_desc) {
            printf("Alloc memory failed.\r\n");
            return NULL;
        }
        memset(task_desc, 0, sizeof(TRACE_TASK_SUMMARY));

        /* Set tcb */
        task_desc->tcb  = tcb;

        /* Link to list */
        task_desc->next = tmp_head->next;
        tmp_head->next = task_desc;
    }

    return task_desc;
}

static void tr_print_num(unsigned int n1, unsigned int n2, unsigned int idx, unsigned char core)
{
    char tmp_buf[64];
    int iloop, num_len = 9, idx_len=11;
    size_t buf_len;

    sprintf(tmp_buf, "%d.%04d", n1, n2);
    buf_len = strlen(tmp_buf);

    if (buf_len < num_len) {
        for (iloop = 0; iloop < num_len - buf_len; iloop++) {
            printf(" ");
        }
    }
    printf(tmp_buf);

    if (idx) {
        sprintf(tmp_buf, "(%d-%d)", core, idx);
        buf_len = strlen(tmp_buf);
        printf(tmp_buf);
    }
    else {
        buf_len = 0;
    }

    if (buf_len < idx_len) {
        for (iloop = 0; iloop < idx_len - buf_len; iloop++) {
            printf(" ");
        }
    }

    return;
}

static void tr_print_task_desc(TRACE_TASK_SUMMARY *head)
{
    TRACE_TASK_SUMMARY *task_desc;
    FAST WIND_TCB *tmp_tcb;
    unsigned int roll_over;
    char print_name[16];
    timerHandle_t clk_timer;

    clk_timer = sysClkHandleGet();

    if (clk_timer != NULL) {
        clk_timer->timerRolloverGet(pClkCookie, &roll_over);
    }
    else {
        return;
    }

    task_desc = head;
    printf("\r\n       task_name    times      min_cost            max_cost      "\
        "   min_interval        max_interval       total_cost");
    printf("\r\n       ---------    -----   --------------      --------------   "\
        "   ------------        ------------       ----------");
    while(task_desc) {
        tmp_tcb = task_desc->tcb;
        if (taskIdVerify((TASK_ID)tmp_tcb) != ERROR) {
            print_name[0]  = 0;
            print_name[14] = 0;
            strncpy(print_name, taskName((TASK_ID)tmp_tcb), 16);
            print_name[15] = 0;
            if (print_name[14] != 0) {
                print_name[14] = '>';
            }
        }
        else {
            task_desc = task_desc->next;
            continue;
        }

        printf("\r\n%16s   %6d", print_name, task_desc->times);
        tr_print_num(task_desc->cost_min_dec/roll_over, (task_desc->cost_min_dec%roll_over)*10000/roll_over,
            task_desc->when_cost_min, task_desc->cost_min_core);
        tr_print_num(task_desc->cost_max_dec/roll_over, (task_desc->cost_max_dec%roll_over)*10000/roll_over,
            task_desc->when_cost_max, task_desc->cost_max_core);
        tr_print_num(task_desc->intv_min_dec/roll_over, (task_desc->intv_min_dec%roll_over)*10000/roll_over,
            task_desc->when_intv_min, task_desc->intv_min_core);
        tr_print_num(task_desc->intv_max_dec/roll_over, (task_desc->intv_max_dec%roll_over)*10000/roll_over,
            task_desc->when_intv_max, task_desc->intv_max_core);
        tr_print_num(task_desc->total_tick + task_desc->total_dec/roll_over,
            (task_desc->total_dec%roll_over)*10000/roll_over, 0, 0);
        task_desc = task_desc->next;
    }

    printf("\r\n");
}
#endif

#ifdef TRACE_AUTO_ANALYSIS_ENABLE
void tr_summary()
{
    unsigned int loop_idx;
    TRACE_TASK_SUMMARY *task_old, *task_new;
    TRACE_TASK_SUMMARY head;
    unsigned int roll_over;
    unsigned int diff;
    unsigned int max_idx;
    unsigned int core_num;
    unsigned char core_idx;
    timerHandle_t clk_timer;

    clk_timer = sysClkHandleGet();

    /* Get rollover */
    if (clk_timer != NULL) {
        clk_timer->timerRolloverGet(pClkCookie, &roll_over);
    }
    else {
        return;
    }

    /* Init */
    head.next = NULL;

    /* Get core number */
    core_num = vxCpuConfiguredGet();
    for (core_idx = 0; core_idx < core_num; core_idx++) {

#ifndef TRACE_LAYOVER_ENABLE
        max_idx = tr_log_pos[core_idx];
#else
        max_idx = TRACE_RECORD_NUM;
#endif
        for (loop_idx = 0; loop_idx < max_idx; loop_idx++) {

            /* 1. Save old task info */
            task_old = tr_get_task_desc(&head, tr_log_buf[core_idx][loop_idx].value1);
            if (!task_old) {
                printf("unknown error, exit.\r\n");
                return;
            }
            if ((tr_log_buf[core_idx][loop_idx].cur_dec == 0)&&(tr_log_buf[core_idx][loop_idx].cur_tick == 0)) {
                return;
            }

            /* Skip first */
            if ((task_old->enter_tick != 0)
              &&(task_old->enter_dec != 0)) {
                /* Calculate cost */
                diff = tr_diff(tr_log_buf[core_idx][loop_idx].cur_tick, tr_log_buf[core_idx][loop_idx].cur_dec,
                    task_old->enter_tick, task_old->enter_dec, roll_over);

                /* Update total cost */
                task_old->total_dec  += diff%roll_over;
                task_old->total_tick += diff/roll_over;

                /* Update max cost */
                if (diff > task_old->cost_max_dec) {
                    task_old->cost_max_dec  = diff;
                    task_old->when_cost_max = loop_idx;
                    task_old->cost_max_core = core_idx;
                }

                /* Update min cost */
                if ((diff < task_old->cost_min_dec)
                  ||(task_old->cost_min_dec == 0)){
                    task_old->cost_min_dec  = diff;
                    task_old->when_cost_min = loop_idx;
                    task_old->cost_min_core = core_idx;
                }
            }

            /* 2. Save new task info */
            task_new = tr_get_task_desc(&head, tr_log_buf[core_idx][loop_idx].value2);
            if (!task_new) {
                printf("unknown error, exit.\r\n");
                return;
            }

            if ((task_new->enter_tick != 0)
              &&(task_new->enter_dec != 0)) {
                /* Calculate interval */
                diff = tr_diff(tr_log_buf[core_idx][loop_idx].cur_tick, tr_log_buf[core_idx][loop_idx].cur_dec,
                    task_new->enter_tick, task_new->enter_dec, roll_over);

                /* Update max interval */
                if (diff > task_new->intv_max_dec) {
                    task_new->intv_max_dec  = diff;
                    task_new->when_intv_max = loop_idx;
                    task_old->intv_max_core = core_idx;
                }

                /* Update min interval */
                if ((diff < task_new->intv_min_dec)
                  ||(task_new->intv_min_dec == 0)){
                    task_new->intv_min_dec  = diff;
                    task_new->when_intv_min = loop_idx;
                    task_old->intv_min_core = core_idx;
                }
            }

            task_new->times++;
            task_new->enter_tick = tr_log_buf[core_idx][loop_idx].cur_tick;
            task_new->enter_dec  = tr_log_buf[core_idx][loop_idx].cur_dec;
        }

        /* Switch core, reset enter time */
        task_new = head.next;
        while(task_new) {
            task_new->enter_tick = 0;
            task_new->enter_dec  = 0;
            task_new = task_new->next;
        }
    }

    /* Print summary */
    tr_print_task_desc(&head);

    /* Free buffer */
    task_new = head.next;
    while(task_new) {
        task_old = task_new;
        task_new = task_new->next;
        free(task_old);
    }
}
#endif

static int tr_remove()
{
    int core_idx, core_num;
#ifdef TRACE_SAVE_FILE_ENABLE
    char file_name[128];
#endif

    /* Get core number */
    core_num = vxCpuConfiguredGet();

    /* Remove task switch hook */
    taskSwitchHookDelete((FUNCPTR)tr_switch_hook);

    for (core_idx = 0; core_idx < core_num; core_idx++) {
#ifdef TRACE_SAVE_FILE_ENABLE
        sprintf(file_name, "%s-%d.txt", TRACE_TARGET_FILE, core_idx);
        tr_save_to_file(core_idx, file_name);
#endif

#ifdef TRACE_PRINT_ENABLE
        tr_save(core_idx, stdout);
#endif
    }

#ifdef TRACE_AUTO_ANALYSIS_ENABLE
    /* Summarize */
    tr_summary();
#endif

#ifdef TRACE_IDLE_TASK_ENABLE
    taskDelete(tr_idle_taskid);
#endif

    return OK;
}

static int tr_daemon()
{
    while (1) {
        if (tr_working == 0) {
            tr_remove();
            break;
        }
        taskDelay(sysClkRateGet());
    }
    return OK;
}

#ifdef TRACE_IDLE_TASK_ENABLE
static int tr_idle()
{
    while(1);

    return OK;
}
#endif

int tr_start()
{
    int core_idx, core_num;

#if (_WRS_VXWORKS_MAJOR==7)
    if (!pClkCookie) {
        pClkCookie = sysClkHandleGet();
    }
#endif

    /* If pointer is not zero, means test is working */
    if (tr_working != 0) {
        printf("Test is working(%d/%d), please end current test then restart it again.\r\n",
            tr_log_pos[0], TRACE_RECORD_NUM);
        return ERROR;
    }
    else {
        /* Get core number */
        core_num = vxCpuConfiguredGet();
        if (core_num > TRACE_CORE_NUM) {
            printf("Configured core number(%d) less than real core number(%d), please reset, exit!\r\n",
                core_num, TRACE_CORE_NUM);
            return ERROR;
        }

        /* Alloc buffer for each core */
        for (core_idx = 0; core_idx < core_num; core_idx++) {
            tr_log_buf[core_idx] = malloc(sizeof(TRACE_LOG_STRUCT) * TRACE_RECORD_NUM);

            /* Set buffer to zero */
            memset(tr_log_buf[core_idx], 0, sizeof(TRACE_LOG_STRUCT) * TRACE_RECORD_NUM);

            /* Reset pointer */
            tr_log_pos[core_idx] = 0;
        }
    }

    /* Start to work */
    tr_working = 1;

    /* Set flag and spawn a daemon */
    taskSpawn("tr_worker", TRACE_WORKER_TASK_PRIORITY, 0, 200000, tr_daemon, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);

#ifdef TRACE_IDLE_TASK_ENABLE
    /* Set flag and spawn a daemon */
    tr_idle_taskid = taskSpawn("tr_idle", 255, 0, 200000, tr_idle, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
#endif

    /* Add task switch hook */
    taskSwitchHookAdd((FUNCPTR)tr_switch_hook);

    return 0;
}

int tr_stop()
{
    /* Remove hook and summarize */
    tr_remove();

    return 0;
}


