/**
 * \file      sys_param_srv_osal_freertos.c
 * \brief     FreeRTOS OSAL port for SysParamSrv.
 * \details   Portable table implementation over FreeRTOS: queues, mutexes,
 *            threads, time, and memory. Registered in sysParamSrv_osalFreertosInit().
 * \author    Dzmitry Stryzhevich (d.strizhevich@unic-lab.by)
 * \copyright Copyright (c) 2025 UnicLab LTD.
 */

/*=============================================================================[ INCLUDE ]=============================================================================*/

#include "sys_param_srv_osal_freertos.h"
#include "sys_param_srv_osal.h"

#include "FreeRTOS.h"
#include <task.h>
#include <queue.h>
#include <semphr.h>

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*================================================================[ INTERNAL MACRO DEFINITIONS ]======================================================================*/

/**
 * \def   SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT
 * \brief Redirect to the subsystem's main assert if available; otherwise fallback to <assert.h>.
 */
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT
    #if defined(SYS_PARAM_SRV_ASSERT)
        #define SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(cond)    SYS_PARAM_SRV_ASSERT(cond)
    #else
        #include <assert.h>
        #define SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(cond)    assert(cond)
    #endif
#endif

/*===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]======================================================*/

/*-------------------------------- Queues ---------------------------------*/

/**
 * \brief Create a queue.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueCreate(void * const osalFreertos,
                                                                 const size_t queueItemSize,
                                                                 const size_t queueDepth,
                                                                 SysParamSrv_osalQueueHandle_t * const queueHandle);

/**
 * \brief Delete a queue.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueDelete(void * const osalFreertos,
                                                                 const SysParamSrv_osalQueueHandle_t queueHandle);

/**
 * \brief Enqueue an item.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueItemPut(void * const osalFreertos,
                                                                  const SysParamSrv_osalQueueHandle_t queueHandle,
                                                                  const void * const queueItemPtr);

/**
 * \brief Dequeue an item with timeout.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueItemPend(void * const osalFreertos,
                                                                   const SysParamSrv_osalQueueHandle_t queueHandle,
                                                                   void * const queueItemPtr,
                                                                   const SysParamSrv_osalTimeMs_t timeoutMs);

/**
 * \brief Reset a queue (discard all items).
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueReset(void * const osalFreertos,
                                                                const SysParamSrv_osalQueueHandle_t queueHandle);

/**
 * \brief Find a queue handle in the registry.
 * \return 1-based index when found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindQueueHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                             const SysParamSrv_osalQueueHandle_t queueHandle);

/*-------------------------------- Locks ----------------------------------*/

/**
 * \brief Create a lock object (recursive mutex).
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLockObjCreate(void * const osalFreertos,
                                                                   SysParamSrv_osalLockObjHandle_t * const lockObjHandle);

/**
 * \brief Delete a lock object.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLockObjDelete(void * const osalFreertos,
                                                                   const SysParamSrv_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Acquire a lock (blocking, infinite).
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLock(void * const osalFreertos,
                                                          const SysParamSrv_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Release a previously acquired lock.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosUnlock(void * const osalFreertos,
                                                            const SysParamSrv_osalLockObjHandle_t lockObjHandle);

/**
 * \brief Find a lock object handle in the registry.
 * \return 1-based index when found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindLockObjHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                               const SysParamSrv_osalLockObjHandle_t lockObjHandle);

/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a thread.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadCreate(void * const osalFreertos,
                                                                  SysParamSrv_osalThreadHandle_t * const threadHandle,
                                                                  SysParamSrv_osalThreadCfg_s threadCfg);

/**
 * \brief Delete a thread.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadDelete(void * const osalFreertos,
                                                                  const SysParamSrv_osalThreadHandle_t threadHandle);

/**
 * \brief Suspend a thread.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadSuspend(void * const osalFreertos,
                                                                   const SysParamSrv_osalThreadHandle_t threadHandle);

/**
 * \brief Resume a suspended thread.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadResume(void * const osalFreertos,
                                                                  const SysParamSrv_osalThreadHandle_t threadHandle);

/**
 * \brief Delay the execution of the current thread.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadDelay(void * const osalFreertos,
                                                                 const SysParamSrv_osalTimeMs_t delayMs);

/**
 * \brief Terminate the calling thread (self).
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadExit(void * const osalFreertos);

/**
 * \brief Find a thread handle in the registry.
 * \return 1-based index when found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindThreadHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                              const SysParamSrv_osalThreadHandle_t threadHandle);

/**
 * \brief Validate thread parameters (stack/priority/worker).
 */
static bool sysParamSrv_osalFreertosThreadParamCheck(const SysParamSrv_osalThreadCfg_s * const threadCfg);

/*--------------------------------- Time ----------------------------------*/

/**
 * \brief Retrieve the current system time in milliseconds.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosTimeMsGet(void * const osalFreertos,
                                                               SysParamSrv_osalTimeMs_t * const osTimeMs);

/**
 * \brief Convert milliseconds to FreeRTOS ticks (with infinity handling).
 */
static TickType_t sysParamSrv_osalFreertosTimeMsToTicksConvert(const SysParamSrv_osalTimeMs_t timeMs);

/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory via FreeRTOS heap.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosMemAlloc(void * const osalFreertos,
                                                              const size_t size,
                                                              void ** const outPtr);

/**
 * \brief Free memory via FreeRTOS heap.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosMemFree(void * const osalFreertos,
                                                             void * const ptr);

/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the portable OSAL backend and its binding.
 */
static bool sysParamSrv_osalFreertosIsValid(const void * const osal);

/*------------------------------ Look-up tables ---------------------------*/

/**
 * \brief FreeRTOS priority levels LUT.
 */
static const UBaseType_t SysParamSrv_osalFreertosThreadPriority[SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_THE_LAST_ONE] =
{
    SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_LOW,
    SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_MIDDLE,
    SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_HIGH,
    SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_ULTRA
};

/**
 * \brief SysParamSrv FreeRTOS OSAL vtable.
 */
static const SysParamSrv_osalVtable_s sysParamSrv_osalFreeRtosVtable =
{
    /* Queues */
    .queueCreate   = sysParamSrv_osalFreertosQueueCreate,
    .queueDelete   = sysParamSrv_osalFreertosQueueDelete,
    .queueItemPut  = sysParamSrv_osalFreertosQueueItemPut,
    .queueItemPend = sysParamSrv_osalFreertosQueueItemPend,
    .queueReset    = sysParamSrv_osalFreertosQueueReset,

    /* Locks */
    .lockObjCreate = sysParamSrv_osalFreertosLockObjCreate,
    .lockObjDelete = sysParamSrv_osalFreertosLockObjDelete,
    .lock          = sysParamSrv_osalFreertosLock,
    .unlock        = sysParamSrv_osalFreertosUnlock,

    /* Threads */
    .threadCreate  = sysParamSrv_osalFreertosThreadCreate,
    .threadDelete  = sysParamSrv_osalFreertosThreadDelete,
    .threadSuspend = sysParamSrv_osalFreertosThreadSuspend,
    .threadResume  = sysParamSrv_osalFreertosThreadResume,
    .threadDelay   = sysParamSrv_osalFreertosThreadDelay,
    .threadExit    = sysParamSrv_osalFreertosThreadExit,

    /* Time */
    .timeMsGet = sysParamSrv_osalFreertosTimeMsGet,

    /* Memory */
    .memAlloc = sysParamSrv_osalFreertosMemAlloc,
    .memFree  = sysParamSrv_osalFreertosMemFree,

    /* Predicate */
    .isValid = sysParamSrv_osalFreertosIsValid
};

/*=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]================================================================*/

/**
 * \brief   Initialize the SysParamSrv FreeRTOS OSAL instance.
 *
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance (must not be NULL).
 * \param   name          Optional name string (may be NULL).
 * \param   parent        Optional parent object pointer (may be NULL).
 * \param   param         Optional FreeRTOS parameter structure (may be NULL).
 *
 * \return  SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalFreertosInit(SysParamSrv_osalFreertos_s *const osalFreertos,
                                                   const char *name,
                                                   void *const parent,
                                                   const SysParamSrv_osalFreertosParam_s *const param)
{
    /* Validate required parameter */
    if (osalFreertos == NULL)
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Forbid ISR context */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Clear local valid flag */
    osalFreertos->validFlag = false;

    /* Apply parameters if provided */
    if (param != NULL)
    {
        osalFreertos->param = *param;
    }
    else
    {
        osalFreertos->param.handle = NULL;
    }

    /* Initialize base OSAL */
    SysParamSrv_osalErr_e initStatus = sysParamSrv_osalInit(&osalFreertos->base, name, parent);
    if (initStatus != SYS_PARAM_SRV_OSAL_NO_ERR)
    {
        return initStatus;
    }

    /* Bind vtable */
    osalFreertos->base.vtable = &sysParamSrv_osalFreeRtosVtable;

    /* Set local valid flag */
    osalFreertos->validFlag = true;

    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief   Deinitialize the SysParamSrv FreeRTOS OSAL instance.
 * \details Stops and deletes known threads/queues/locks, clears base OSAL.
 *
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 * \return  SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalFreertosDeinit(SysParamSrv_osalFreertos_s *const osalFreertos)
{
    if (osalFreertos == NULL)
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    osalFreertos->validFlag = false;

    /* Suspend and delete threads */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.threadObjHandle[i].handle != NULL)
        {
            vTaskSuspend((TaskHandle_t)osalFreertos->base.threadObjHandle[i].handle);
            vTaskDelete((TaskHandle_t)osalFreertos->base.threadObjHandle[i].handle);

            osalFreertos->base.threadObjHandle[i].handle        = NULL;
            osalFreertos->base.threadObjHandle[i].cfg.name      = NULL;
            osalFreertos->base.threadObjHandle[i].cfg.worker    = NULL;
            osalFreertos->base.threadObjHandle[i].cfg.stackSize = 0u;
            osalFreertos->base.threadObjHandle[i].cfg.param     = NULL;
            osalFreertos->base.threadObjHandle[i].cfg.prio      = SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_LOW;
        }
    }

    /* Delete queues */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.queueObjHandle[i] != NULL)
        {
            vQueueDelete((QueueHandle_t)osalFreertos->base.queueObjHandle[i]);
            osalFreertos->base.queueObjHandle[i] = NULL;
        }
    }

    /* Delete lock objects */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osalFreertos->base.lockObjHandle[i] != NULL)
        {
            vSemaphoreDelete((SemaphoreHandle_t)osalFreertos->base.lockObjHandle[i]);
            osalFreertos->base.lockObjHandle[i] = NULL;
        }
    }

    /* Base deinit */
    return sysParamSrv_osalDeinit(&osalFreertos->base);
}


//============================================================================[ PRIVATE FUNCTIONS ]==================================================================================

/**
 * \brief Create a queue.
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param queueItemSize  Size of a single queue item in bytes.
 * \param queueDepth     Maximum number of items the queue can hold.
 * \param queueHandle    Output: created queue handle (must not be NULL).
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueCreate(void * const osalFreertos,
                                                                 const size_t queueItemSize,
                                                                 const size_t queueDepth,
                                                                 SysParamSrv_osalQueueHandle_t * const queueHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Check the level at which the function is called */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;  // Exit: Error: Call from an ISR context
    }

    /* Upcast to base OSAL */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    /* Clear the output handle */
    *queueHandle = NULL;

    /* Find a free slot and create a FreeRTOS queue */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == NULL)
        {
            /* Create the queue in the free slot */
            osal->queueObjHandle[i] = xQueueCreate((UBaseType_t)queueDepth, (UBaseType_t)queueItemSize);
            if (osal->queueObjHandle[i] == NULL)
            {
                return SYS_PARAM_SRV_OSAL_QUEUE_MEM_ALLOCATION_ERR;  // Exit: Error: Memory allocation error
            }

            *queueHandle = osal->queueObjHandle[i];
            break;
        }
    }

    /* Verify that a queue was successfully created */
    if (*queueHandle == NULL)
    {
        return SYS_PARAM_SRV_OSAL_QUEUE_CREATE_ERR;
    }

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief Delete a queue.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param queueHandle   Queue handle to delete.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueDelete(void * const osalFreertos,
                                                                 const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Check the level at which the function is called */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Upcast to base OSAL */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    /* Verify the existence of the queue handle in the registry */
    const size_t qid = sysParamSrv_osalFreertosFindQueueHandle(port, queueHandle);
    if ((qid == 0u) ||
        (qid > SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM))
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Delete the queue and clear the slot */
    vQueueDelete((QueueHandle_t)queueHandle);
    osal->queueObjHandle[qid - 1u] = NULL;   /* Adjust index: find() returns index + 1 */

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Enqueue an item.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param queueHandle   Target queue handle.
 * \param queueItemPtr  Pointer to item to be enqueued.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueItemPut(void * const osalFreertos,
                                                                  const SysParamSrv_osalQueueHandle_t queueHandle,
                                                                  const void * const queueItemPtr)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Verify the existence of the queue handle in the registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t qid                       = sysParamSrv_osalFreertosFindQueueHandle(port, queueHandle);
    if ((qid == 0u) ||
        (qid > SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM))
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    BaseType_t ok = pdFALSE;

    /* Use ISR-safe or task context API accordingly */
    if (xPortIsInsideInterrupt())
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        /* Add the item to the queue from an ISR */
        ok = xQueueSendFromISR((QueueHandle_t)queueHandle, queueItemPtr, &xHigherPriorityTaskWoken);

        /* Yield if a higher-priority task was woken */
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    else
    {
        /* Add the item to the queue (no wait) */
        ok = xQueueSend((QueueHandle_t)queueHandle, queueItemPtr, 0);
    }

    if (ok != pdTRUE)
    {
        /* Queue is full */
        return SYS_PARAM_SRV_OSAL_QUEUE_IS_FULL_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Dequeue an item with timeout.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param queueHandle   Queue handle to read from.
 * \param queueItemPtr  Destination buffer for the item.
 * \param timeoutMs     Timeout in milliseconds.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueItemPend(void * const osalFreertos,
                                                                   const SysParamSrv_osalQueueHandle_t queueHandle,
                                                                   void * const queueItemPtr,
                                                                   const SysParamSrv_osalTimeMs_t timeoutMs)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueItemPtr != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Check the level at which the function is called */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Verify the existence of the queue handle in the registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t qid                       = sysParamSrv_osalFreertosFindQueueHandle(port, queueHandle);
    if ((qid == 0u) ||
        (qid > SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Convert timeout in milliseconds to FreeRTOS ticks */
    const TickType_t tout = sysParamSrv_osalFreertosTimeMsToTicksConvert(timeoutMs);

    /* Attempt to receive an item from the queue */
    const BaseType_t ok = xQueueReceive((QueueHandle_t)queueHandle, queueItemPtr, tout);
    if (ok != pdTRUE)
    {
        /* Queue is empty / timeout expired */
        return SYS_PARAM_SRV_OSAL_QUEUE_IS_EMPTY_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Reset a queue (discard all items).
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param queueHandle   Queue handle to reset.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosQueueReset(void * const osalFreertos,
                                                                const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Check the level at which the function is called */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Verify the existence of the queue handle in the registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t qid                       = sysParamSrv_osalFreertosFindQueueHandle(port, queueHandle);
    if ((qid == 0u) ||
        (qid > SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Reset the FreeRTOS queue (non-blocking) */
    const BaseType_t ok = xQueueReset((QueueHandle_t)queueHandle);
    if (ok != pdTRUE)
    {
        /* Reset failed due to internal port specifics (should be rare) */
        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Find a queue handle in the registry.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param queueHandle   Queue handle to search.
 * \return size_t       Queue ID (index + 1) if found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindQueueHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                             const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(queueHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Upcast to base OSAL */
    SysParamSrv_osal_s *const osal = &osalFreertos->base;

    /* Search for the matching handle */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == queueHandle)
        {
            return i + 1u; /* Return 1-based ID */
        }
    }

    /* 0 = invalid ID (not found) */
    return 0u;
}


/*-------------------------------- Locks ----------------------------------*/

/**
 * \brief Create a lock object (recursive mutex).
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param lockObjHandle  Output: created lock handle.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLockObjCreate(void * const osalFreertos,
                                                                   SysParamSrv_osalLockObjHandle_t * const lockObjHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage for blocking-capable primitives */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Upcast to base OSAL */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    /* Clear the output handle */
    *lockObjHandle = NULL;

    /* Find a free slot and create a recursive mutex */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == NULL)
        {
            osal->lockObjHandle[i] = xSemaphoreCreateRecursiveMutex();
            if (osal->lockObjHandle[i] == NULL)
            {
                /* Memory allocation failure inside FreeRTOS */
                return SYS_PARAM_SRV_OSAL_LOCK_OBJ_MEM_ALLOCATION_ERR;
            }

            *lockObjHandle = osal->lockObjHandle[i];
            break;
        }
    }

    /* Verify that the lock object was created */
    if (*lockObjHandle == NULL)
    {
        /* No free slots in registry */
        return SYS_PARAM_SRV_OSAL_LOCK_OBJ_CREATE_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Delete a lock object.
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param lockObjHandle  Lock handle to delete.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLockObjDelete(void * const osalFreertos,
                                                                   const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Upcast and validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    const size_t lid = sysParamSrv_osalFreertosFindLockObjHandle(port, lockObjHandle);
    if ((lid == 0u) ||
        (lid > SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Delete the lock object and clear the slot */
    vSemaphoreDelete((SemaphoreHandle_t)lockObjHandle);
    osal->lockObjHandle[lid - 1u] = NULL;  /* Adjust index: find() returns index + 1 */

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Acquire a lock (blocking, infinite).
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param lockObjHandle  Lock handle to acquire.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosLock(void * const osalFreertos,
                                                          const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t lid                       = sysParamSrv_osalFreertosFindLockObjHandle(port, lockObjHandle);
    if ((lid == 0u) ||
        (lid > SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Take the recursive mutex with indefinite timeout */
    const BaseType_t ok = xSemaphoreTakeRecursive((SemaphoreHandle_t)lockObjHandle, portMAX_DELAY);
    if (ok != pdTRUE)
    {
        /* Unexpected port-specific failure */
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Release a previously acquired lock.
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param lockObjHandle  Lock handle to release.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosUnlock(void * const osalFreertos,
                                                            const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t lid                       = sysParamSrv_osalFreertosFindLockObjHandle(port, lockObjHandle);
    if ((lid == 0u) ||
        (lid > SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Give the recursive mutex */
    const BaseType_t ok = xSemaphoreGiveRecursive((SemaphoreHandle_t)lockObjHandle);
    if (ok != pdTRUE)
    {
        /* Unexpected port-specific failure */
        SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(0);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Find a lock object handle in the registry.
 * \param osalFreertos   Pointer to FreeRTOS OSAL instance.
 * \param lockObjHandle  Lock handle to search.
 * \return size_t        Lock ID (index + 1) if found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindLockObjHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                               const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(lockObjHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Upcast to base OSAL */
    SysParamSrv_osal_s *const osal = &osalFreertos->base;

    /* Try to find the handle */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == lockObjHandle)
        {
            return i + 1u; /* 1-based ID */
        }
    }

    /* 0 = invalid ID (not found) */
    return 0u;
}


/*-------------------------------- Threads --------------------------------*/

/**
 * \brief Create a thread.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param threadHandle  Output: created thread handle.
 * \param threadCfg     Thread configuration.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadCreate(void * const osalFreertos,
                                                                  SysParamSrv_osalThreadHandle_t * const threadHandle,
                                                                  SysParamSrv_osalThreadCfg_s threadCfg)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Perform thread parameters validation procedure */
    if (!sysParamSrv_osalFreertosThreadParamCheck(&threadCfg))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Upcast to base OSAL */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    /* Clear output handle */
    *threadHandle = NULL;

    /* Try to create a FreeRTOS thread by finding an available slot */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == NULL)
        {
            /* Convert priority via LUT and stack from bytes to words */
            const UBaseType_t prio                  = SysParamSrv_osalFreertosThreadPriority[threadCfg.prio];
            const configSTACK_DEPTH_TYPE stackWords = (configSTACK_DEPTH_TYPE)(threadCfg.stackSize / sizeof(StackType_t));

            /* Create the task */
            const BaseType_t rc = xTaskCreate(threadCfg.worker,
                                              threadCfg.name,
                                              stackWords,
                                              threadCfg.param,
                                              prio,
                                              (TaskHandle_t *)&osal->threadObjHandle[i].handle);
            if (rc != pdPASS)
            {
                /* FreeRTOS failed to allocate TCB/stack */
                return SYS_PARAM_SRV_OSAL_THREAD_MEM_ALLOCATION_ERR;
            }

            /* Store configuration snapshot and return handle */
            osal->threadObjHandle[i].cfg = threadCfg;
            *threadHandle                = osal->threadObjHandle[i].handle;
            break;
        }
    }

    /* Verify the thread was created */
    if (*threadHandle == NULL)
    {
        /* No free OSAL thread slots */
        return SYS_PARAM_SRV_OSAL_THREAD_CREATE_ERR;
    }

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Delete a thread.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to delete.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadDelete(void * const osalFreertos,
                                                                  const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Upcast and validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;

    const size_t tid = sysParamSrv_osalFreertosFindThreadHandle(port, threadHandle);
    if ((tid == 0u) ||
        (tid > SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Suspend and delete the task */
    vTaskSuspend((TaskHandle_t)threadHandle);
    vTaskDelete((TaskHandle_t)threadHandle);

    /* Clear the registry slot */
    osal->threadObjHandle[tid - 1u].handle        = NULL;
    osal->threadObjHandle[tid - 1u].cfg.worker    = NULL;
    osal->threadObjHandle[tid - 1u].cfg.name      = NULL;
    osal->threadObjHandle[tid - 1u].cfg.stackSize = 0u;
    osal->threadObjHandle[tid - 1u].cfg.param     = NULL;
    osal->threadObjHandle[tid - 1u].cfg.prio      = SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_LOW;

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Suspend a thread.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to suspend.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadSuspend(void * const osalFreertos,
                                                                   const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t tid                       = sysParamSrv_osalFreertosFindThreadHandle(port, threadHandle);
    if ((tid == 0u) ||
        (tid > SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Suspend the thread */
    vTaskSuspend((TaskHandle_t)threadHandle);

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Resume a suspended thread.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to resume.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadResume(void * const osalFreertos,
                                                                  const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Validate presence in registry */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    const size_t tid                       = sysParamSrv_osalFreertosFindThreadHandle(port, threadHandle);
    if ((tid == 0u) ||
        (tid > SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM))
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Resume the thread */
    vTaskResume((TaskHandle_t)threadHandle);

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Delay the execution of the current thread.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param delayMs       Delay duration in milliseconds.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadDelay(void * const osalFreertos,
                                                                 const SysParamSrv_osalTimeMs_t delayMs)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Convert to RTOS ticks and delay */
    const TickType_t ticks = sysParamSrv_osalFreertosTimeMsToTicksConvert(delayMs);
    vTaskDelay(ticks);

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Terminate the calling thread (self).
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 *
 * \note This call does not return in FreeRTOS (vTaskDelete(NULL)).
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosThreadExit(void * const osalFreertos)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Prevent ISR usage */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Upcast and get current task handle */
    SysParamSrv_osalFreertos_s *const port = (SysParamSrv_osalFreertos_s *)osalFreertos;
    SysParamSrv_osal_s *const osal         = &port->base;
    const TaskHandle_t self                = xTaskGetCurrentTaskHandle();
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(self != NULL);

    /* Remove self from registry before deletion */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == self)
        {
            osal->threadObjHandle[i].handle        = NULL;
            osal->threadObjHandle[i].cfg.worker    = NULL;
            osal->threadObjHandle[i].cfg.name      = NULL;
            osal->threadObjHandle[i].cfg.stackSize = 0u;
            osal->threadObjHandle[i].cfg.param     = NULL;
            osal->threadObjHandle[i].cfg.prio      = SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_LOW;
            break;
        }
    }

    /* Terminate current task (never returns) */
    vTaskDelete(NULL);

    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Find a thread handle in the registry.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param threadHandle  Thread handle to search.
 * \return size_t       Thread ID (index + 1) if found, 0 otherwise.
 */
static inline size_t sysParamSrv_osalFreertosFindThreadHandle(SysParamSrv_osalFreertos_s * const osalFreertos,
                                                              const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadHandle != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Upcast to base OSAL and search */
    SysParamSrv_osal_s *const osal = &osalFreertos->base;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == threadHandle)
        {
            return i + 1u; /* 1-based ID */
        }
    }

    /* 0 = invalid ID (not found) */
    return 0u;
}


/**
 * \brief Validate thread parameters (stack/priority/worker).
 * \param threadCfg  Pointer to thread configuration.
 * \return true if valid; false otherwise.
 */
static bool sysParamSrv_osalFreertosThreadParamCheck(const SysParamSrv_osalThreadCfg_s * const threadCfg)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(threadCfg != NULL);

    /* Validate stack size (converted from bytes to words) */
    const configSTACK_DEPTH_TYPE stackWords =
        (configSTACK_DEPTH_TYPE)(threadCfg->stackSize / sizeof(StackType_t));
    if (stackWords < configMINIMAL_STACK_SIZE)
    {
        return false; /* Stack size too small */
    }

    /* Validate thread priority against supported levels */
    switch (threadCfg->prio)
    {
        case SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_LOW:
        case SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_MIDDLE:
        case SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_HIGH:
        case SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_ULTRA:
        {
            break;
        }
        default:
        {
            return false; /* Invalid priority */
        }
    }

    /* Worker function must be provided */
    if (threadCfg->worker == NULL)
    {
        return false;
    }

    return true; /* All parameters valid */
}


/*--------------------------------- Time ----------------------------------*/

/**
 * \brief Retrieve the current system time in milliseconds.
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param osTimeMs      Output: current OS time in milliseconds.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosTimeMsGet(void * const osalFreertos,
                                                               SysParamSrv_osalTimeMs_t * const osTimeMs)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osTimeMs != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Get the current OS tick count (ISR-safe variant if needed) */
    TickType_t ticks = 0;
    if (xPortIsInsideInterrupt())
    {
        ticks = xTaskGetTickCountFromISR();
    }
    else
    {
        ticks = xTaskGetTickCount();
    }

    /* Convert ticks to milliseconds */
    *osTimeMs = (SysParamSrv_osalTimeMs_t)(ticks * portTICK_PERIOD_MS);

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Convert milliseconds to FreeRTOS ticks (with infinity handling).
 * \param timeMs  Timeout in milliseconds (SYS_PARAM_SRV_OSAL_INFINITY_TOUT for infinite).
 * \return TickType_t number of ticks.
 */
static TickType_t sysParamSrv_osalFreertosTimeMsToTicksConvert(const SysParamSrv_osalTimeMs_t timeMs)
{
    /* Map OSAL infinity to FreeRTOS portMAX_DELAY */
    if (timeMs == SYS_PARAM_SRV_OSAL_INFINITY_TOUT)
    {
        return portMAX_DELAY;
    }

    /* Safe conversion via FreeRTOS helper macro */
    return pdMS_TO_TICKS(timeMs);
}


/*-------------------------------- Memory ---------------------------------*/

/**
 * \brief Allocate memory via FreeRTOS heap (pvPortMalloc).
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param size          Allocation size in bytes.
 * \param outPtr        Output: allocated pointer (must not be NULL).
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosMemAlloc(void * const osalFreertos,
                                                              const size_t size,
                                                              void ** const outPtr)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(outPtr != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Allocation from ISR is not allowed/safe for most heap schemes */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Validate size */
    if (size == 0u)
    {
        *outPtr = NULL;

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Allocate from the FreeRTOS heap */
    void *p = pvPortMalloc(size);
    if (p == NULL)
    {
        *outPtr = NULL;

        /* Use the closest error code available for memory allocation failure */
        return SYS_PARAM_SRV_OSAL_LOCK_OBJ_MEM_ALLOCATION_ERR;
    }

    /* Return allocated pointer */
    *outPtr = p;

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/**
 * \brief Free memory via FreeRTOS heap (vPortFree).
 * \param osalFreertos  Pointer to FreeRTOS OSAL instance.
 * \param ptr           Pointer to memory to free.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
static SysParamSrv_osalErr_e sysParamSrv_osalFreertosMemFree(void * const osalFreertos,
                                                             void * const ptr)
{
    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osalFreertos != NULL);
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(sysParamSrv_osalFreertosIsValid(osalFreertos));

    /* Validate pointer */
    if (ptr == NULL)
    {
        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;
    }

    /* Freeing from ISR is not safe */
    if (xPortIsInsideInterrupt())
    {
        return SYS_PARAM_SRV_OSAL_CALL_FROM_ISR_ERR;
    }

    /* Free the memory */
    vPortFree(ptr);

    /* Exit: no errors */
    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/*------------------------------- Predicate -------------------------------*/

/**
 * \brief Validate the portable OSAL and its binding.
 * \param osal  Opaque pointer to SysParamSrv_osalFreertos_s.
 * \return true if vtable is bound and the instance is valid; false otherwise.
 */
static bool sysParamSrv_osalFreertosIsValid(const void * const osal)
{
    /* Validate input parameters (caller must ensure this) */
    SYS_PARAM_SRV_OSAL_FREERTOS_ASSERT(osal != NULL);

    /* Downcast and check vtable binding */
    const SysParamSrv_osalFreertos_s *const port = (const SysParamSrv_osalFreertos_s *)osal;
    if (port->base.vtable != &sysParamSrv_osalFreeRtosVtable)
    {
        return false;
    }

    /* Return the port's own validity flag */
    return port->validFlag;
}
