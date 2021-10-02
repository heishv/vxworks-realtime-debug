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
#include "task_preempt_analysis.h"
#include <ipcom_cstyle.h>
#include <ipcom_sock.h>
#include <in.h>

/* In order to check the address of the semaphore, define them as a global */
SEM_ID  pr_test_sem_bin;
SEM_ID  pr_test_sem_mux;
SEM_ID  pr_test_sem_bin2;
WDOG_ID pr_test_wd = NULL;

/*******************************************************************************
*
* pr_test_stub1 -
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_test_stub1()
{
    volatile int32_t loopNum;
    while(1) {
        /* Waiting for resource ready */
        semTake(pr_test_sem_bin, -1);

        /* Activate stub3 */
        semGive(pr_test_sem_bin2);

        loopNum = 700000;

        /* Mutex */
        semTake(pr_test_sem_mux, -1);
        while(loopNum--);
        semGive(pr_test_sem_mux);

    }

    return OK;
}

/*******************************************************************************
*
* pr_test_stub2 -
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_test_stub2()
{
    while(1) {
        taskDelay(100);

        /* Activate stub1 */
        semGive(pr_test_sem_bin);
    }

    return OK;
}

/*******************************************************************************
*
* pr_test_stub3 -
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_test_stub3()
{
    volatile int32_t loopNum;

    while(1) {
        loopNum = 500000;

        /* Waiting for resource ready */
        semTake(pr_test_sem_bin2, -1);

        /* Mutex */
        semTake(pr_test_sem_mux, -1);
        while(loopNum--);
        semGive(pr_test_sem_mux);
    }

    return OK;
}

/*******************************************************************************
*
* pr_test_stub4 -
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_test_stub4()
{
    struct ifreq req;
    int netfd;
    int ret;
    char ethname[] = "cpsw0";
    uint8_t tmpbuf[1600];

    /* Setup socket */
    netfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (netfd < 0){
        printf("socket error(%d)\r\n", netfd);
        return ERROR;
    }

    /* Get options */
    strncpy(req.ifr_name, ethname, IFNAMSIZ);
    ret = ioctl(netfd, SIOCGIFFLAGS, &req);
    if (ret != 0) {
        printf("set SIOCGIFFLAGS error(%d)\r\n", ret);
        close(netfd);
        return ERROR;
    }

    while(1) {
        ret = recvfrom(netfd, tmpbuf, 1600, 0, NULL, NULL);
        if (ret < 0) {
            continue;
        }
    }

    return OK;
}

/*******************************************************************************
*
* pr_test_stub5 -
*
* RETURNS: OK or ERROR
*
*
*/
STATUS pr_test_stub5()
{
    /* Activate stub1 */
    semGive(pr_test_sem_bin);

    /* Timer */
    wdStart(pr_test_wd, 100, pr_test_stub4, 0);

    /* Use it to confirm watchdog is working */
    /*logMsg("Hi\n",0,0,0,0,0,0);*/

    return OK;
}


/*******************************************************************************
*
* pr_test_info - Print debug info
*
* RETURNS: N/A
*
* \NOMANUAL
*/
void pr_test_dbg_info()
{
#ifdef PR_DEBUG_ENABLE
    printf(pr_dbg_buf);
#endif
}


/*******************************************************************************
*
* pr_test - test function entry
*
* RETURNS: N/A
*
* \NOMANUAL
*/
void pr_test(int index)
{
    int32_t tidTarget1, tidTarget2, tidTarget3, tidTarget4, tidTarget;
    extern void td(long taskNameOrId);

    if (pr_test_sem_bin == NULL) {
        pr_test_sem_bin  = semBCreate(0, 0);
    }
    if (pr_test_sem_mux == NULL) {
        pr_test_sem_mux  = semMCreate(0);
    }
    if (pr_test_sem_bin2 == NULL) {
        pr_test_sem_bin2 = semBCreate(0, 0);
    }
    if (pr_test_wd == NULL) {
        pr_test_wd = wdCreate();
        if (pr_test_wd == NULL) {
            printf("initialize watchdog failed.\n");
            return;
        }
    }

    if (wdStart(pr_test_wd, 100, pr_test_stub5, 0) != OK) {
        printf("start watchdog failed.\n");
        return;
    }

    tidTarget1 = taskSpawn("stub1", 100, 0, 10000, pr_test_stub1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    tidTarget2 = taskSpawn("stub2", 102, 0, 10000, pr_test_stub2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    tidTarget3 = taskSpawn("stub3", 101, 0, 10000, pr_test_stub3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    tidTarget4 = taskSpawn("stub4", 100, 0, 10000, pr_test_stub4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

    switch(index) {
        case 1:
            tidTarget = tidTarget1;
            break;
        case 2:
            tidTarget = tidTarget2;
            break;
        case 3:
            tidTarget = tidTarget3;
            break;
        case 4:
            tidTarget = tidTarget4;
            break;
        default:
            tidTarget = tidTarget1;
            break;
    }

    pr_start(tidTarget);
    taskDelay(500);
    pr_stop();

    td(tidTarget1);
    td(tidTarget2);
    td(tidTarget3);
    td(tidTarget4);
    wdCancel(pr_test_wd);
}

