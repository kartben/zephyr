/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/


/**************************************************************************/
/**************************************************************************/
/**                                                                       */
/** GUIX Component                                                        */
/**                                                                       */
/**   System Management (System)                                          */
/**                                                                       */
/**************************************************************************/

//#define GX_SOURCE_CODE

/* Include necessary system files.  */

#include "gx_api.h"
#include "gx_system.h"
#include "gx_system_rtos_bind.h"

#include <zephyr/kernel.h>

/**************************************************************************/
/*                                                                        */
/*  FUNCTION                                               RELEASE        */
/*                                                                        */
/*    _gx_system_rtos_bind                                PORTABLE C      */
/*                                                           6.1          */
/*  AUTHOR                                                                */
/*                                                                        */
/*    Kenneth Maxwell, Microsoft Corporation                              */
/*                                                                        */
/*  DESCRIPTION                                                           */
/*                                                                        */
/*    This file contains a small set of shell functions used to bind GUIX */
/*    to a generic RTOS. GUIX is by default configured to run with the    */
/*    ThreadX RTOS, but you can port to another RTOS by defining the      */
/*    pre-processor directive GX_DISABLE_THREADX_BINDING and completing   */
/*    the implementation of the functions found in this file.             */
/*    Refer to the GUIX User Guide for more information.                  */
/*                                                                        */
/*  INPUT                                                                 */
/*                                                                        */
/*    Refer to GUIX User Guide                                            */
/*                                                                        */
/*  OUTPUT                                                                */
/*                                                                        */
/*    Refer to GUIX User Guide                                            */
/*                                                                        */
/*  CALLS                                                                 */
/*                                                                        */
/*    Refer to GUIX User Guide                                            */
/*                                                                        */
/*  CALLED BY                                                             */
/*                                                                        */
/*    GUIX system serives                                                 */
/*                                                                        */
/*  RELEASE HISTORY                                                       */
/*                                                                        */
/*    DATE              NAME                      DESCRIPTION             */
/*                                                                        */
/*  05-19-2020     Kenneth Maxwell          Initial Version 6.0           */
/*  09-30-2020     Kenneth Maxwell          Modified comment(s),          */
/*                                            resulting in version 6.1    */
/*                                                                        */
/**************************************************************************/

#ifndef GX_THREADX_BINDING

/* This is an example of a generic RTOS binding. We have #ifdefed this code out
   of the GUIX library build, so that the user attempting to port to a new RTOS
   will intentionally observe a series of linker errors. These functions need to
   be implemented in a way that works correctly with the target RTOS.
*/

#if defined(__ZEPHYR__)

#include <zephyr/kernel.h>

#ifndef OS_CFG_TICK_RATE_HZ
#define OS_CFG_TICK_RATE_HZ 100
#endif

#define GX_TIMER_TASK_PRIORITY   (GX_SYSTEM_THREAD_PRIORITY + 1)
#define GX_TIMER_TASK_STACK_SIZE 512
void guix_timer_handler(struct k_timer *timer_id);

/* a few global status variables */
GX_BOOL guix_timer_event_pending;

/* define stack space for GUIX task */
K_THREAD_STACK_DEFINE(guix_task_stack, GX_THREAD_STACK_SIZE);
struct k_thread guix_thread;

/* define stack space for timer task */
// CPU_STK guix_timer_task_stack[GX_TIMER_TASK_STACK_SIZE];

/* define semaphore for lock/unlock macros */
struct k_mutex guix_system_lock_mutex;

// OS_ERR uc_error;
// OS_TCB guix_timer_tcb;

struct k_fifo guix_event_queue_fifo;
struct guix_event_item_t {
    GX_EVENT event_data;
};

static K_TIMER_DEFINE(guix_timer, guix_timer_handler, NULL);

/* rtos initialize: perform any setup that needs to be done before the GUIX task runs here */
VOID   gx_generic_rtos_initialize(VOID)
{
    guix_timer_event_pending = GX_FALSE;
    k_mutex_init(&guix_system_lock_mutex);
    k_fifo_init(&guix_event_queue_fifo);
}

VOID (*gx_system_thread_entry)(ULONG);

/* thread_start: start the GUIX thread running. */
UINT   gx_generic_thread_start(VOID(*guix_thread_entry)(ULONG))
{
    /* save the GUIX system thread entry function pointer */
    gx_system_thread_entry = guix_thread_entry;

    /* create the main GUIX thread */
    k_thread_create(&guix_thread, guix_task_stack, GX_THREAD_STACK_SIZE,
                    (k_thread_entry_t) gx_system_thread_entry,
                    NULL, NULL, NULL,
                    GX_SYSTEM_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&guix_thread, "guix");

    // /* create a simple task to generate timer events into GUIX */
    // OSTaskCreate(&guix_timer_tcb, "guix_timer_task",
    //              guix_timer_task_entry,
    //              GX_NULL,
    //              GX_TIMER_TASK_PRIORITY,
    //              &guix_timer_task_stack[0],
    //              GX_TIMER_TASK_STACK_SIZE -1,
    //              GX_TIMER_TASK_STACK_SIZE,
    //              0, 0, GX_NULL, OS_OPT_TASK_NONE, &uc_error);

    // /* suspend the timer task until needed */
    // OSTaskSuspend(&guix_timer_tcb, &uc_error);
    // return GX_SUCCESS;
}

/* event_post: push an event into the fifo event queue */
UINT   gx_generic_event_post(GX_EVENT *event_ptr)
{
    struct guix_event_item_t item;

    /* copy the event data into this slot */
    item.event_data = *event_ptr;

    /* push the event into the fifo queue */
    k_fifo_put(&guix_event_queue_fifo, &item);

    return GX_SUCCESS;
}



/* event_fold: update existing matching event, otherwise post new event */
UINT   gx_generic_event_fold(GX_EVENT *event_ptr)
{
    return gx_generic_event_post(event_ptr);
}


/* event_pop: pop oldest event from fifo queue, block if wait and no events exist */
UINT   gx_generic_event_pop(GX_EVENT *put_event, GX_BOOL wait)
{
    struct guix_event_item_t *item;

    if(wait) {
        item = k_fifo_get(&guix_event_queue_fifo, K_FOREVER);
    } else {
        item = k_fifo_get(&guix_event_queue_fifo, K_NO_WAIT);
    }

    if(item != NULL) {
        *put_event = item->event_data;
        return GX_SUCCESS;
    } else {
        return GX_FAILURE;
    }
}

/* event_purge: delete events targetted to particular widget */
VOID   gx_generic_event_purge(GX_WIDGET *target)
{
    // todo
}

/* start the RTOS timer */
VOID   gx_generic_timer_start(VOID)
{
//    k_timer_start(&guix_timer, 100, 100);
}

/* stop the RTOS timer */
VOID   gx_generic_timer_stop(VOID)
{
//    k_timer_stop(&guix_timer);
}

/* lock the system protection mutex */
VOID   gx_generic_system_mutex_lock(VOID)
{
    k_mutex_lock(&guix_system_lock_mutex, K_FOREVER);
}

/* unlock system protection mutex */
VOID   gx_generic_system_mutex_unlock(VOID)
{
    k_mutex_unlock(&guix_system_lock_mutex);
}

/* return number of low-level system timer ticks. Used for pen speed caculations */
ULONG  gx_generic_system_time_get(VOID)
{
    return(k_cycle_get_64());
}

/* thread_identify: return current thread identifier, cast as VOID * */
VOID  *gx_generic_thread_identify(VOID)
{
    return((VOID *) k_current_get());
}

/* a simple task to drive the GUIX timer mechanism */
void guix_timer_handler(struct k_timer *timer_id)
{
    // TODO
}

VOID gx_generic_time_delay(int ticks)
{
    // TODO review

    int ms_per_tick = 1000 / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    int sleep_time_ms = ticks * ms_per_tick;

    k_sleep(K_MSEC(sleep_time_ms));

}


#endif  /* Binding for Zephyr enabled */

#endif  /* if !ThreadX */

