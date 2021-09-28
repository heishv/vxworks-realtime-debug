/* Copyright (c) 2019, heishv
 * All rights reserved.
 *
 * SPDX-License-Identifier: Apache License 2.0
 */

/* Default configuration for task_switch_trace */

/* Maximum core number */
#define TRACE_CORE_NUM                  32

/* Maximum number of records that can be recorded for each core */
#define TRACE_RECORD_NUM                2000

/* Worker task priority, should higher than idle task, but lower than realtime task */
#define TRACE_WORKER_TASK_PRIORITY      200

/* If defined, loop layover, layover from start when reach max record number */
/* Or finish when reach max record number. */
/*#define TRACE_LAYOVER_ENABLE*/

/* If define TRACE_SAVE_FILE_ENABLE, need enable dosfs related components */
/* TRACE_TARGET_FILE should include full path and file name, suchu as "/sd0:1/trace" or "host:/trace" */
/*#define TRACE_SAVE_FILE_ENABLE*/
#define TRACE_TARGET_FILE               "host:/trace"

/* Print all info after tr_stop() invoked */
#define TRACE_PRINT_ENABLE

/* Summarize all task info */
#define TRACE_AUTO_ANALYSIS_ENABLE

/* If system is not 100% busy, task will not switch out when there no other task is ready, */
/* so the result is not real. Create a task with 255 priority, it will occupy spare time. */
/* To let test result more accurate. */
/* For multi-core cpu, vxWorks will create idle task automatically, don't need enable it again. */
/*#define TRACE_IDLE_TASK_ENABLE*/



/* Default configuration for preemt_analysis */



