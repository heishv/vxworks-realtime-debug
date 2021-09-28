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
#include <private/timerLibP.h>		/* _func_clkTimer* */
#include <usrLib.h>
#include <strlib.h>
#include "realtime_debug_config.h"
#include "task_switch_trace.h"

SEM_ID  tr_test_semId;

int tr_test_task1()
{
    while(1) {
        taskDelay(1);
        semGive(tr_test_semId);
    }

    return OK;
}

int tr_test_task2()
{
    while(1) {
        semTake(tr_test_semId, WAIT_FOREVER);
    }

    return OK;
}

int tr_test()
{
    tr_test_semId = semBCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    taskSpawn("tr_test_give", 150, 0, 200000, tr_test_task1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    taskSpawn("tr_test_take", 150, 0, 200000, tr_test_task2, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);

    return OK;
}

