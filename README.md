vxWorks realtime debug tools
============================
This repository used for debug realtime feature on vxWorks. vxWorks is a realtime operating system,
support absolutely task preempt base on priority. But in most cases, fine debugging is needed to 
achieve the goal. Many debugging tools are based on statistics. In a large number of statistical 
data, individual special cases will be covered by the average value. However, in actual use, we hope 
there will be no violation. The main purpose of this tool is to capture the usage over a period of 
time, find out special cases, and provide materials for further analysis.

Building, running, testing
==========================
This tool can only work with VIP project and cannot be used in RTP. Please download the repository and 
put it in the VIP project directory, then refresh the project and compile it with the project together.
Call tr_start() and tr_stop() before and after the target software, and the debugging function will be
automatically executed. Such as:
    tr_start();
    ...
    ...
    tr_stop();
Or you can call tr_start on shell directly.
    -> tr_start
    value = 0 = 0x0
After calling tr_start(), the information will be collected automatically. When tr_stop() is called,
stop collecting and analyzing the collected information, and then print the results.
The buffer has limitation, collecting also will stop if reach the end. You can change the buffer size
in realtime_debug_config.h.

Configuring
===========
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

Tested environment and known problem
====================================
Tested environment:
    vxWorks6.9.4.12
    vxWorks7.0
known problem:
Because can't get detail timer info, it can't work on vxWorks6.9.4.12 simulator, and can't get detail 
timer counter value on vxWorks7.0 simulator.

