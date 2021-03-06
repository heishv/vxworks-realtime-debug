vxWorks realtime debug tools
============================
This repository used for debug realtime feature on vxWorks. vxWorks is a realtime operating system, \
support absolutely task preempt base on priority. But in most cases, fine debugging is needed to \
achieve the goal. Many debugging tools are based on statistics. In a large number of statistical \
data, special cases will be covered by the average value. However, in actual use, we hope there will \
be no violation. The main purpose of this tool is to capture the usage over a period of time, find \
out special cases, and provide materials for further analysis. \
In this repository, include two parts of features, one is task switch tracing, capture all task switch \
action, calculate task max cost and min cost, max interval and min interval between two continuous \
invoke. For a specific task, the consumption of each call should be about the same. If it is a periodic \
task, its call interval should also be the same. If the gap is too large, you need to find a reason. \
This tool will help you to find, this is the main purpose of this function. \
Another function is to record task preemption. WinDriver has a tool(SystemView) that can record all \
system events, but it is not easy to find a specific violation in a large number of events. This tool \
also uses same event recording mechanism of SystemView to record the target task for debugging. The \
debugging target can only be set to one, which can be the name or ID of the task. For tasks that are \
executed immediately after activation and then blocked, we think this is normal. There is no preemption, \
and such records will be erased from the records. If the target task is not executed immediately after \
activation, or there are other tasks or interrupted execution during execution, we think preemption has \
occurred and will be recorded. We only show preemption cases. The engineers can readjust the priority \
of tasks or the activation relationship between tasks according to the specific semaphores, tasks and \
other information of preemption.


Building, running, testing
==========================
This tool can only work with VIP project and cannot be used in RTP. Please download the repository and \
put it in the VIP project directory, then refresh the project and compile it with the project together.\
Call tr_start() and tr_stop() before and after the target software, and the debugging function will be\
automatically executed. Such as:
```shell
    tr_start();
    ...
    ...
    tr_stop();
```
Or
```shell
    pr_start();
    ...
    ...
    pr_stop
```
Or you can call tr_start on shell directly.
```shell
    -> tr_start
    value = 0 = 0x0
```
After calling tr_start()/pr_start, the information will be collected automatically. When tr_stop()/pr_stop \
is called, stop collecting and analyzing the collected information, and then print the results.\
The buffer has limitation, collecting also will stop if reach the end. You can change the buffer size\
in realtime_debug_config.h.

## Configuring
```shell
/* Default configuration for task_switch_trace */

/* Maximum core number, if real core number greater than it, initialization will be failed. */
/* If real core number less than it, record info by real core number */
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
#define TRACE_SAVE_FILE_ENABLE
#define TRACE_TARGET_FILE               "/sd0:1/trace"

/* Print all info after tr_stop() invoked */
#define TRACE_PRINT_ENABLE

/* Summarize all task info */
#define TRACE_AUTO_ANALYSIS_ENABLE

/* If system is not 100% busy, task will not switch out when there no other task is ready, */
/* so the result is not real. Create a task with 255 priority, it will occupy spare time. */
/* To let test result more accurate. */
/* For multi-core cpu, vxWorks will create idle task automatically, don't need enable it again. */
/*#define TRACE_IDLE_TASK_ENABLE*/



/* Default configuration for task_preemt_analysis */

/* If target task was preempted, save as a event. How many events are allowed to be save */
/* Reach this number, recording will be stopped automatically */
#define PREEMPT_EVENT_MAX_NUMBER        1000

/* If enabled, record all events related to the target task. Otherwise, only preemption events are recorded */
/*#define PREEMPT_SAVE_ALL_ENABLE*/

```

Tested environment and known problem
====================================

Tested environment:
    vxWorks6.9.4.12
    vxWorks7.0
    
Tested architecture:
    PPC
    ARM
    X86
    
known problem:
Because can't get detail timer info, it can't work on vxWorks6.9.4.12 simulator, and can't get detail \
timer counter value on vxWorks7.0 simulator.
Task preemption anslysis tool can't work in vxWorks6.9.
X86 system have two timer, if default configuration can't work, please change SYSCLK_TIMER_NAME to "Intel 8253 Timer"


Task switch trace
=================
The original data of task switching can be printed directly or recorded in a file. If it is a multi-core \
system, the task switching of each core will be recorded in a separate file. If you need to check the \
task switching between multiple cores, please find the data in the same time period between different \
file records. Blow is the record format:
```shell
-> current pos on core 0 is 2000, rollover is 825000.
     0  tick: 494-11699    old_task: 0150cae0 tShellRem26922888    -->    new_task: 01506a40 tStdioProxy1506760
     1  tick: 494-14172    old_task: 01506a40 tStdioProxy1506760   -->    new_task: 01451b10 tNet0
     2  tick: 494-17644    old_task: 01451b10 tNet0                -->    new_task: 01506760 ipcom_telnetspawn
     3  tick: 494-19572    old_task: 01506760 ipcom_telnetspawn    -->    new_task: 01451b10 tNet0
     4  tick: 494-20388    old_task: 01451b10 tNet0                -->    new_task: 01532340 tr_worker
     5  tick: 494-20516    old_task: 01532340 tr_worker            -->    new_task: 01532620 tr_idle
     6  tick: 494-20860    old_task: 01532620 tr_idle              -->    new_task: 01451b10 tNet0
     7  tick: 494-21235    old_task: 01451b10 tNet0                -->    new_task: 01532620 tr_idle
     8  tick: 497-148535   old_task: 01532620 tr_idle              -->    new_task: 01451b10 tNet0
     |         |     |                   |       |
     |         |     |                   |       |--------> task name
     |         |     |                   |----------------> task id
     |         |     |------------------------------------> timer counter value(it is a tick if reach to rollover, then start from 0 again)
     |         |------------------------------------------> tick
     |----------------------------------------------------> index
```
An automatic analysis will be done when stopted. It mainly analyzes the call times of each task, the time \
consumed in execution, and the interval between two calls. If the task is not locked on the same core, the \
call interval may be inaccurate, and this error is no plan to correct in the automatic analysis process.
```shell
       task_name    times      min_cost            max_cost         min_interval        max_interval       total_cost
       ---------    -----   --------------      --------------      ------------        ------------       ----------
      tIdleTask3       58   0.0005(3-52)       29.9988(3-75)        0.0015(0-53)       29.9999(3-76)      498.9716
            tTcf        2   0.0013(2-466)       0.0014(2-464)       0.0019(0-465)       0.0019(0-465)       0.0028
      tIdleTask2      659   0.0004(2-455)       4.9998(2-67)        0.0012(2-608)       5.0012(2-8)       657.2426
 tShellRem18446>       24   0.0009(1-47)        0.1917(2-604)       0.1067(0-633)     172.5077(1-549)       0.5052
 ipcom_telnetsp>      153   0.0007(2-624)       0.0026(2-546)       0.0012(3-625)     193.9230(2-327)       0.1841
      tIdleTask1      434   0.0005(1-327)      24.2442(1-26)        0.0016(1-484)      24.2466(1-27)      656.0825
      tTcfEvents        9   0.0013(2-452)       0.0067(2-456)       0.0017(0-455)       0.0284(0-869)       0.0209
    tr_test_give      503   0.0005(1-379)       0.0025(3-70)        0.9986(0-559)     165.9988(3-58)        0.6196
    tr_test_take      477   0.0004(0-387)       0.0018(3-3)         0.0468(3-393)      30.0000(3-75)        0.3080
   miiBusMonitor        5   0.0313(0-245)       0.0316(0-1507)    119.9999(0-1924)    120.0000(0-1506)      0.1576
       tr_worker       11   0.0004(1-1)         0.0006(0-486)      59.9998(0-1133)     60.0000(0-692)       0.0063
        tExcTask      654   0.0004(2-558)       0.0032(2-692)       0.9951(0-1838)     44.9998(2-36)        0.8661
            tTcf      121   0.0010(0-161)       0.0024(2-718)       4.9998(0-378)     114.9998(2-1109)      0.1457
 tStdioProxy202>       56   0.0006(2-88)        0.0169(1-324)       0.0021(2-511)     172.4797(1-291)       0.1414
      tIdleTask0      998   0.0005(0-388)       0.9996(0-48)        0.0009(0-387)       1.0302(0-245)     602.5604
           tNet0      141   0.0008(0-129)       0.3091(0-1084)      0.0018(0-857)      30.0000(0-1325)      1.1841
                       |           |   |
                       |           |   |-----------------> index number
                       |           |---------------------> core
                       |---------------------------------> how many times has the task been activated             
```


Task preemtion analysis
=======================
Only one task can be tracked at the same time. Setting multiple target tasks is not supported. If  \
PREEMPT_SAVE_ALL_ENABLE is defined, all events related to the target task will be recorded, otherwise \
only the preemption events will be recorded. Blow is the record format:
```shell
                         |----------------> total 21 events captured, each line is a event
                         |       |--------> 12 events is normal case, ignored
                         |       |
Get 9 preempt event(push 21, pop 12):

----546-929        (SEMGIVE)      task stub1 ready by semGive 20bbcf68
  | 546-1367       (TICKUNDELAY)  task stub2 ready by taskDelay timeout
  | 546-1793       (SEMGIVE)      task tExcTask ready by semGive 83b3e0
  | 546-2165       (INT_EXIT)     interrupt exit
  . 546-2550       (DISPATCH)     task tExcTask running
  . 546-2694       (SEMTAKE)      task pending by semTake 83b3e0
  . 546-3137       (DISPATCH)     task stub1 running
  . 546-3628       (SEMGIVE)      task stub3 ready by semGive 20b9f8c8
----546-88101      (SEMTAKE)      task pending by semTake 20bbcf68
  |  |   |             |
  |  |   |             |---------> event name
  |  |   |-----------------------> timer counter value(it is a tick if reach to rollover, then start from 0 again) 
  |  |---------------------------> tick
  |------------------------------> mark, '----' mark a round of task invoking, '|' means normal, '.' means preemption started

```

Contact us
==========
lijingrui@gmail.com
