/**
 * \file      sys_param_srv_osal.c
 * \brief     OSAL layer interface implementation for SysParamSrv.
 * \details   The concrete OSAL port is supplied via the vtable vtable in the RTOS layer.
 * \author    Dzmitry Stryzhevich (d.strizhevich@unic-lab.by)
 * \copyright Copyright (c) 2025 UnicLab LTD. All rights reserved.
 */

/*=============================================================================[ INCLUDE ]=============================================================================*/

#include "sys_param_srv_osal.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include config file if it is defined at compilation time */
#ifdef SYS_PARAM_SRV_CONFIG_FILE
    #include SYS_PARAM_SRV_OSAL_STR(SYS_PARAM_SRV_CONFIG_FILE)
#endif


/*================================================================[ INTERNAL MACRO DEFINITIONS ]======================================================================*/

/**
 * \def   SYS_PARAM_SRV_OSAL_ASSERT
 * \brief Redirect to the subsystem's main assert if available; otherwise fallback to <assert.h>.
 */
#ifndef SYS_PARAM_SRV_OSAL_ASSERT
    #if defined(SYS_PARAM_SRV_ASSERT)
        #define SYS_PARAM_SRV_OSAL_ASSERT(cond)    SYS_PARAM_SRV_ASSERT(cond)
    #else
        #include <assert.h>
        #define SYS_PARAM_SRV_OSAL_ASSERT(cond)    assert(cond)
    #endif
#endif

/**
 * \def   SYS_PARAM_SRV_OSAL_TRACE
 * \brief Redirect to the subsystem's main trace if available; otherwise no-op.
 */
#ifndef SYS_PARAM_SRV_OSAL_TRACE
    #if defined(SYS_PARAM_SRV_TRACE)
        #define SYS_PARAM_SRV_OSAL_TRACE(...)    SYS_PARAM_SRV_TRACE(__VA_ARGS__)
    #else
        #define SYS_PARAM_SRV_OSAL_TRACE(...)    ((void)0)
    #endif
#endif


/*===============================================================[ INTERNAL FUNCTIONS AND OBJECTS DECLARATION ]======================================================*/

/**
 * \brief  Reset OSAL objects such as queues, lock objects, threads and memory slots.
 */
static void sysParamSrv_osalResetObjects(SysParamSrv_osal_s *const osal);

/**
 * \brief  Find a free queue slot.
 */
static size_t sysParamSrv_osalRegQueueFreeSlotFind(void * const osalPort);

/**
 * \brief  Find queue handle.
 */
static size_t sysParamSrv_osalRegQueueHandleFind(void * const osalPort,
                                                 const SysParamSrv_osalQueueHandle_t queueHandle);

/**
 * \brief  Find a free lock object slot.
 */
static size_t sysParamSrv_osalRegLockFreeSlotFind(void * const osalPort);

/**
 * \brief  Find lock object handle.
 */
static size_t sysParamSrv_osalRegLockHandleFind(void * const osalPort,
                                                const SysParamSrv_osalLockObjHandle_t lockObjHandle);

/**
 * \brief  Find a free thread slot.
 */
static size_t sysParamSrv_osalRegThreadFreeSlotFind(void * const osalPort);

/**
 * \brief  Find thread handle.
 */
static size_t sysParamSrv_osalRegThreadHandleFind(void * const osalPort,
                                                  const SysParamSrv_osalThreadHandle_t threadHandle);

/**
 * \brief  Find a free memory slot.
 */
static size_t sysParamSrv_osalRegMemFreeSlotFind(void * const osalPort);

/**
 * \brief  Find pointer in memory registry.
 */
static size_t sysParamSrv_osalRegMemHandleFind(void * const osalPort,
                                               const void * const ptr);

/**
 * \brief   Protected registry helpers vtable (for backend ports).
 * \details Provides unified helpers for slot search and handle lookup for queues, locks, threads, and memory.
 *          IDs are 1-based (index + 1). Zero value indicates "not found"/"no free slot".
 */
static const SysParamSrv_osalPtable_s sysParamSrv_osalPtable =
{
    /*------------------------------- Queues --------------------------------*/
    .queueFreeSlotFind = sysParamSrv_osalRegQueueFreeSlotFind,
    .queueHandleFind   = sysParamSrv_osalRegQueueHandleFind,

    /*-------------------------------- Locks --------------------------------*/
    .lockObjFreeSlotFind = sysParamSrv_osalRegLockFreeSlotFind,
    .lockObjHandleFind   = sysParamSrv_osalRegLockHandleFind,

    /*------------------------------- Threads -------------------------------*/
    .threadFreeSlotFind = sysParamSrv_osalRegThreadFreeSlotFind,
    .threadHandleFind   = sysParamSrv_osalRegThreadHandleFind,

    /*-------------------------------- Memory -------------------------------*/
    .memFreeSlotFind = sysParamSrv_osalRegMemFreeSlotFind,
    .memHandleFind   = sysParamSrv_osalRegMemHandleFind
};


/*=======================================================================[ PUBLIC INTERFACE FUNCTIONS ]================================================================*/

/*---------------------------------- Lifecycle --------------------------------------*/

/**
 * \brief  Initialize SysParamSrv OSAL instance; set name/parent and clear internal objects.
 * \param  osal    OSAL instance pointer.
 * \param  name    Optional instance name (may be NULL).
 * \param  parent  Optional parent pointer (opaque; may be NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalInit(SysParamSrv_osal_s *const osal,
                                           const char *name,
                                           void *const parent)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalInit(%p, %s, %p)",
                             (void *)osal, (name ? name : "(null)"), parent);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalInit -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Init instance */
    osal->validFlag = false;
    osal->name      = name;
    osal->parent    = parent;

    /* Reset internals */
    sysParamSrv_osalResetObjects(osal);

    /* Reset methods table before use */
    osal->vtable = NULL;

    /* Assign ptable methods table */
    osal->ptable = &sysParamSrv_osalPtable;

    /* Mark as valid */
    osal->validFlag = true;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalInit -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: Error: Error: Success
}


/**
 * \brief  Deinitialize SysParamSrv OSAL instance.
 * \param  osal  OSAL instance pointer.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalDeinit(SysParamSrv_osal_s *const osal)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalDeinit(%p)", (void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalDeinit -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalDeinit -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Clear instance */
    osal->validFlag = false;
    osal->name      = NULL;
    osal->parent    = NULL;
    osal->vtable    = NULL;

    /* Reset internals */
    sysParamSrv_osalResetObjects(osal);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalDeinit -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: Error: success
}


/**
 * \brief  Validate OSAL instance (including vtable layer).
 * \param  osal  OSAL instance pointer (const).
 * \return true if valid/initialized; false otherwise.
 */
bool sysParamSrv_osalIsValid(const SysParamSrv_osal_s *const osal)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid(%p)", (const void *)osal);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid -> %d", 0);

        return false;  // Exit: Error: invalid args
    }

    /* Validate local state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid -> %d", 0);

        return false;  // Exit: Error: Error: not initialized
    }

    /* Check vtable table presence */
    if (osal->vtable == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid -> %d", 0);

        return false;  // Exit: Error: Error: No vtable table
    }

    /* Check vtable predicate presence */
    if (osal->vtable->isValid == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid -> %d", 0);

        return false;  // Exit: Error: Error: missing backend predicate
    }

    /* Delegate to backend */
    const bool ok = osal->vtable->isValid(osal);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalIsValid -> %d", (int)ok);

    return ok;  // Exit:  return the summary validation status
}


/*---------------------------------- Metadata -----------------------------------------*/

/**
 * \brief  Get pointer to a parent of the given OSAL object.
 * \param  osal     OSAL instance pointer.
 * \param  parent   Output: pointer to parent (must not be NULL; may be set to NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalParentGet(SysParamSrv_osal_s *const osal,
                                                void **const parent)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentGet(%p, %p)", (void *)osal, (void *)parent);

    /* Validate args */
    if ((osal == NULL) ||
        (parent == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Get parent */
    *parent = (void *)osal->parent;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Set the parent object for the given OSAL instance.
 * \param  osal    OSAL instance pointer.
 * \param  parent  Parent pointer to be set (may be NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalParentSet(SysParamSrv_osal_s *const osal,
                                                const void *const parent)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentSet(%p, %p)", (void *)osal, parent);

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentSet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentSet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Set parent */
    osal->parent = parent;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalParentSet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Get pointer to the name field of the given OSAL instance.
 * \param  osal  OSAL instance pointer.
 * \param  name  Output: pointer to name (must not be NULL; may be set to NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalNameGet(SysParamSrv_osal_s *const osal,
                                              const char **const name)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameGet(%p, %p)", (void *)osal, (void *)name);

    /* Validate args */
    if ((osal == NULL) ||
        (name == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Get name */
    *name = osal->name;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief  Set name for the given OSAL instance.
 * \param  osal  OSAL instance pointer.
 * \param  name  Pointer to name string to set (may be NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalNameSet(SysParamSrv_osal_s *const osal,
                                              const char *const name)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameSet(%p, %s)", (void *)osal, (name ? name : "(null)"));

    /* Validate args */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameSet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if (osal->validFlag != true)
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameSet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Set name */
    osal->name = name;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalNameSet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/*----------------------------------- Queue -------------------------------------------*/

/**
 * \brief  Create a message queue.
 * \param  osal           OSAL instance pointer.
 * \param  queueItemSize  Size of a single queue item in bytes.
 * \param  queueDepth     Maximum number of items the queue can hold.
 * \param  queueHandle    Output: created queue handle (must not be NULL).
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueCreate(SysParamSrv_osal_s *const osal,
                                                  const size_t queueItemSize,
                                                  const size_t queueDepth,
                                                  SysParamSrv_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueCreate(%p, %zu, %zu, %p)",
                             (void *)osal, queueItemSize, queueDepth, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueCreate -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueCreate -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if ((osal->vtable == NULL) ||
        (osal->vtable->queueCreate == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueCreate -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->queueCreate(osal, queueItemSize, queueDepth, queueHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueCreate -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Delete a message queue.
 * \param  osal         OSAL instance pointer.
 * \param  queueHandle  Queue handle to delete.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueDelete(SysParamSrv_osal_s *const osal,
                                                  const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueDelete(%p, %p)", (void *)osal, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueDelete -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueDelete -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueDelete == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueDelete -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->queueDelete(osal, queueHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueDelete -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief  Put an item into a queue.
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Pointer to the item to enqueue.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueItemPut(SysParamSrv_osal_s *const osal,
                                                   const SysParamSrv_osalQueueHandle_t queueHandle,
                                                   const void *const queueItemPtr)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPut(%p, %p, %p)",
                             (void *)osal, (void *)queueHandle, (void *)queueItemPtr);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPut -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPut -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPut == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPut -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->queueItemPut(osal, queueHandle, queueItemPtr);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPut -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Get an item from a queue (blocking with timeout).
 * \param  osal          OSAL instance pointer.
 * \param  queueHandle   Queue handle.
 * \param  queueItemPtr  Destination buffer for the item.
 * \param  timeoutMs     Timeout in milliseconds.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueItemPend(SysParamSrv_osal_s *const osal,
                                                    const SysParamSrv_osalQueueHandle_t queueHandle,
                                                    void *const queueItemPtr,
                                                    const SysParamSrv_osalTimeMs_t timeoutMs)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPend(%p, %p, %p, %u)",
                             (void *)osal, (void *)queueHandle, (void *)queueItemPtr,
                             (unsigned)timeoutMs);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (queueItemPtr == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPend -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPend -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueItemPend == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPend -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->queueItemPend(osal, queueHandle, queueItemPtr, timeoutMs);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueItemPend -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Reset a queue (discard all items).
 * \param  osal         OSAL instance pointer.
 * \param  queueHandle  Queue handle to reset.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueReset(SysParamSrv_osal_s *const osal,
                                                 const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueReset(%p, %p)", (void *)osal, (void *)queueHandle);

    /* Validate args */
    if ((osal == NULL) ||
        (queueHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueReset -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: invalid args
    }

    /* Validate instance state */
    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueReset -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Check vtable methods table */
    if (osal->vtable->queueReset == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueReset -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: port-specific error
    }

    /* Delegate to underlying OS backend */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->queueReset(osal, queueHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueReset -> %d", retStatus);

    return retStatus;  // Exit: Error: backend status
}


/**
 * \brief  Get a queue handle of the given OSAL object.
 * \param  osal           Pointer to OSAL instance.
 * \param  queueSlotInd   Index of queue slot.
 * \param  queueHandle    Pointer to the current queue handle.
 * \return SysParamSrv_osalErr_e, zero value = success, otherwise an error has occurred.
 */
SysParamSrv_osalErr_e sysParamSrv_osalQueueHandleGet(SysParamSrv_osal_s *const osal,
                                                     const size_t queueSlotInd,
                                                     SysParamSrv_osalQueueHandle_t *const queueHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueHandleGet(%p, %zu, %p)",
                             (void *)osal, queueSlotInd, (void *)queueHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (queueHandle == NULL) ||
        (SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM <= queueSlotInd))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueHandleGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueHandleGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit
    }

    /* Retrieve the queue handle from the specified slot */
    *queueHandle = osal->queueObjHandle[queueSlotInd];

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalQueueHandleGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/*------------------------------------- Locks --------------------------------*/

/**
 * \brief Create a lock object.
 * \param  osal          Pointer to the OSAL instance.
 * \param  lockObjHandle Pointer to the location where the lock object handle will be created.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalLockObjCreate(SysParamSrv_osal_s *const osal,
                                                    SysParamSrv_osalLockObjHandle_t *const lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjCreate(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjCreate -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjCreate -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;
    }

    if (osal->vtable->lockObjCreate == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjCreate -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Create the lock object */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->lockObjCreate(osal, lockObjHandle);

    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjCreate -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delete a lock object.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object to delete.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalLockObjDelete(SysParamSrv_osal_s *const osal,
                                                    const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjDelete(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate  parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjDelete -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjDelete -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->lockObjDelete == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjDelete -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Delete the lock object */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->lockObjDelete(osal, lockObjHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjDelete -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Lock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalLock(SysParamSrv_osal_s *const osal,
                                           const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLock(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLock -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLock -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->lock == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLock -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Lock access to the resource */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->lock(osal, lockObjHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLock -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Unlock access to the resource.
 * \param  osal           Pointer to the OSAL instance.
 * \param  lockObjHandle  Handle of the lock object.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalUnlock(SysParamSrv_osal_s *const osal,
                                             const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalUnlock(%p, %p)", (void *)osal, (void *)lockObjHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalUnlock -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalUnlock -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->unlock == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalUnlock -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Unlock access to the resource */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->unlock(osal, lockObjHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalUnlock -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Get a lock object handle of the given OSAL object.
 * \param  osal            Pointer to the OSAL instance.
 * \param  lockObjSlotInd  Index of the lock object slots.
 * \param  lockObjHandle   Pointer where the lock object handle will be copied.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalLockObjHandleGet(SysParamSrv_osal_s *const osal,
                                                       const size_t lockObjSlotInd,
                                                       SysParamSrv_osalLockObjHandle_t *const lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjHandleGet(%p, %zu, %p)",
                             (void *)osal, lockObjSlotInd, (void *)lockObjHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (lockObjHandle == NULL) ||
        (SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM <= lockObjSlotInd))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjHandleGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjHandleGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    /* Retrieve the lock object handle */
    *lockObjHandle = osal->lockObjHandle[lockObjSlotInd];

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalLockObjHandleGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;
}


/*------------------------------------ Threads -------------------------------*/

/**
 * \brief Create a new thread.
 * \param osal         Pointer to the OSAL instance.
 * \param threadHandle Pointer to store the handle of the created thread.
 * \param threadCfg    Configuration parameters for the thread.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadCreate(SysParamSrv_osal_s *const osal,
                                                   SysParamSrv_osalThreadHandle_t *const threadHandle,
                                                   SysParamSrv_osalThreadCfg_s threadCfg)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadCreate(%p, %p, {%p,%s,%zu,%p,%d})",
                             (void *)osal, (void *)threadHandle,
                             (void *)threadCfg.worker,
                             (threadCfg.name ? threadCfg.name : "(null)"),
                             threadCfg.stackSize, threadCfg.args, (int)threadCfg.prio);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadCreate -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadCreate -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if (osal->vtable->threadCreate == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadCreate -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to create the thread and get the status */
    const SysParamSrv_osalErr_e retStatus =
        osal->vtable->threadCreate(osal, threadHandle, threadCfg);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadCreate -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delete the thread.
 * \note The operation must be stopped before deleting the thread to avoid system damage.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread being deleted.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadDelete(SysParamSrv_osal_s *const osal,
                                                   const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelete(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelete -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelete -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: not initialized
    }

    if ((osal->vtable == NULL) ||
        (osal->vtable->threadDelete == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelete -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to delete the thread */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->threadDelete(osal, threadHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelete -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Suspend the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to suspend.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadSuspend(SysParamSrv_osal_s *const osal,
                                                    const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadSuspend(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate input parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadSuspend -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadSuspend -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if ((osal->vtable == NULL) ||
        (osal->vtable->threadSuspend == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadSuspend -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to suspend the thread */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->threadSuspend(osal, threadHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadSuspend -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Resume the thread.
 * \param osal          Pointer to OSAL instance.
 * \param threadHandle  Handle of the thread to resume.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadResume(SysParamSrv_osal_s *const osal,
                                                   const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadResume(%p, %p)", (void *)osal, (void *)threadHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadResume -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadResume -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->threadResume == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadResume -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Attempt to resume the thread */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->threadResume(osal, threadHandle);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadResume -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Delay the execution of the current thread.
 * \param osal     Pointer to OSAL instance.
 * \param delayMs  Delay duration in milliseconds.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadDelay(SysParamSrv_osal_s *const osal,
                                                  const SysParamSrv_osalTimeMs_t delayMs)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelay(%p, %u)", (void *)osal, (unsigned)delayMs);

    /* Validate parameters */
    if (osal == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelay -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelay -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->threadDelay == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelay -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;
    }

    /* Delay the current thread */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->threadDelay(osal, delayMs);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadDelay -> %d", retStatus);

    return retStatus;
}


/**
 * \brief Terminate the calling thread (does not return).
 * \param osal  Pointer to OSAL instance (must be valid).
 * \note  This function never returns control to the caller.
 */
void sysParamSrv_osalThreadExit(SysParamSrv_osal_s *const osal)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadExit(%p)", (void *)osal);

    /* Validate invariants */
    SYS_PARAM_SRV_OSAL_ASSERT(osal != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(osal->validFlag == true);
    SYS_PARAM_SRV_OSAL_ASSERT(osal->vtable != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(osal->vtable->threadExit != NULL);

    /* Terminate the current thread (must not return) */
    osal->vtable->threadExit(osal);

    /* Should never reach here */
    SYS_PARAM_SRV_OSAL_ASSERT(0);

    /* Placeholder to prevent formatter collapsing and to satisfy analyzers */
    while (1)
    {
        /* no-return placeholder */
        (void)0;
    }
}


/**
 * \brief Get a thread handle of the given OSAL object.
 * \param osal           Pointer to OSAL instance.
 * \param threadSlotInd  Index of thread slots.
 * \param threadHandle   Pointer where the thread handle will be copied.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalThreadHandleGet(SysParamSrv_osal_s *const osal,
                                                      const size_t threadSlotInd,
                                                      SysParamSrv_osalThreadHandle_t *const threadHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadHandleGet(%p, %zu, %p)",
                             (void *)osal, threadSlotInd, (void *)threadHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (threadHandle == NULL) ||
        (SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM <= threadSlotInd))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadHandleGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadHandleGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    /* Copy the thread handle from the specified slot */
    *threadHandle = osal->threadObjHandle[threadSlotInd].handle;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalThreadHandleGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: return the summary status
}


/*-------------------------------------- Time --------------------------------*/

/**
 * \brief Retrieve the current system time in milliseconds.
 * \param osal       Pointer to OSAL instance.
 * \param osTimeMs   Pointer to store the current time in milliseconds.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalTimeMsGet(SysParamSrv_osal_s *const osal,
                                                SysParamSrv_osalTimeMs_t *const osTimeMs)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalTimeMsGet(%p, %p)", (void *)osal, (void *)osTimeMs);

    /* Validate parameters */
    if ((osal == NULL) ||
        (osTimeMs == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalTimeMsGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalTimeMsGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;
    }

    if (osal->vtable->timeMsGet == NULL)
    {
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalTimeMsGet -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Get the current OS time */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->timeMsGet(osal, osTimeMs);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalTimeMsGet -> %d", retStatus);

    return retStatus;  // Exit: return the summary status
}


/*------------------------------------- Memory --------------------------------*/

/**
 * \brief Allocate memory via the OSAL backend and register the pointer internally.
 * \param osal    Pointer to OSAL instance.
 * \param size    Allocation size in bytes.
 * \param outPtr  Pointer to the allocated memory (must not be NULL).
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalMalloc(SysParamSrv_osal_s *const osal,
                                             const size_t size,
                                             void **const outPtr)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc(%p, %zu, %p)", (void *)osal, size, (void *)outPtr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (outPtr == NULL) ||
        (size == 0u))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  /// Exit: Error: Not initialized
    }

    if (osal->vtable->memAlloc == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Delegate allocation (only now) */
    void *ptr                       = NULL;
    SysParamSrv_osalErr_e retStatus = osal->vtable->memAlloc(osal, size, &ptr);
    if ((retStatus != SYS_PARAM_SRV_OSAL_NO_ERR) ||
        (ptr == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc -> %d",
                                 (retStatus != SYS_PARAM_SRV_OSAL_NO_ERR) ? retStatus :
                                 SYS_PARAM_SRV_OSAL_MEM_ALLOCATION_ERR);

        return (retStatus != SYS_PARAM_SRV_OSAL_NO_ERR) ? retStatus
                                                        : SYS_PARAM_SRV_OSAL_MEM_ALLOCATION_ERR;  // Exit: Error: backend failed or returned NULL
    }

    *outPtr = ptr;

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMalloc -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/**
 * \brief Free memory via the OSAL backend and unregister the pointer internally.
 * \param osal  Pointer to OSAL instance.
 * \param ptr   Pointer to the memory block to free (must not be NULL and must be registered).
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalFree(SysParamSrv_osal_s *const osal,
                                           void *const ptr)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalFree(%p, %p)", (void *)osal, ptr);

    /* Validate parameters */
    if ((osal == NULL) ||
        (ptr == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalFree -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if ((osal->validFlag != true) ||
        (osal->vtable == NULL))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalFree -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    if (osal->vtable->memFree == NULL)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalFree -> %d", SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR);

        return SYS_PARAM_SRV_OSAL_PORT_SPECIFIC_ERR;  // Exit: Error: Port (backend) specific error
    }

    /* Delegate free to backend */
    const SysParamSrv_osalErr_e retStatus = osal->vtable->memFree(osal, ptr);

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalFree -> %d", retStatus);

    return retStatus;  // Exit: backend status
}


/**
 * \brief Get a memory handle of the given OSAL object.
 * \param osal          Pointer to OSAL instance.
 * \param memSlotInd    Index of memory slot.
 * \param memHandle     Pointer where the memory handle will be copied.
 * \return SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalMemHandleGet(SysParamSrv_osal_s *const osal,
                                                   const size_t memSlotInd,
                                                   SysParamSrv_osalMemHandle_t *const memHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMemHandleGet(%p, %zu, %p)",
                             (void *)osal, memSlotInd, (void *)memHandle);

    /* Validate parameters */
    if ((osal == NULL) ||
        (memHandle == NULL) ||
        (SYS_PARAM_SRV_OSAL_MEM_SLOTS_NUM <= memSlotInd))
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMemHandleGet -> %d", SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR);

        return SYS_PARAM_SRV_OSAL_INVALID_ARGS_ERR;  // Exit: Error: Invalid args
    }

    if (osal->validFlag != true)
    {
        /* Trace: returned value */
        SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMemHandleGet -> %d", SYS_PARAM_SRV_OSAL_NOT_INIT_ERR);

        return SYS_PARAM_SRV_OSAL_NOT_INIT_ERR;  // Exit: Error: Not initialized
    }

    *memHandle = osal->memObjHandle[memSlotInd];

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalMemHandleGet -> %d", SYS_PARAM_SRV_OSAL_NO_ERR);

    return SYS_PARAM_SRV_OSAL_NO_ERR;  // Exit: success
}


/*============================================================================[ PRIVATE FUNCTIONS ]============================================================================*/

/**
 * \brief  Reset OSAL objects such as queues, lock objects, threads and memory slots.
 * \param  osal  OSAL instance pointer.
 * \return None.
 */
static void sysParamSrv_osalResetObjects(SysParamSrv_osal_s *const osal)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalResetObjects(%p)", (void *)osal);

    /* Validate by caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osal != NULL);

    /* Reset queue slots */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        osal->queueObjHandle[i] = NULL;
    }

    /* Reset lock object slots */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        osal->lockObjHandle[i] = NULL;
    }

    /* Reset thread slots */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        osal->threadObjHandle[i].cfg.worker    = NULL;
        osal->threadObjHandle[i].cfg.name      = NULL;
        osal->threadObjHandle[i].cfg.stackSize = 0u;
        osal->threadObjHandle[i].cfg.args      = NULL;
        osal->threadObjHandle[i].cfg.prio      = SYS_PARAM_SRV_OSAL_THREAD_PRIORITY_LOW;
        osal->threadObjHandle[i].handle        = NULL;
    }

    /* Reset memory registry slots */
    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_MEM_SLOTS_NUM; ++i)
    {
        osal->memObjHandle[i] = NULL;
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalResetObjects -> ok");
}


/*-------------------------------- Registry: Queues -------------------------------*/

/**
 * \brief  Find a free queue slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t sysParamSrv_osalRegQueueFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find queue handle.
 * \param  osalPort     Derived OSAL pointer.
 * \param  queueHandle  Handle to search.
 * \return Queue ID (index + 1) or 0 if not found.
 */
static size_t sysParamSrv_osalRegQueueHandleFind(void * const osalPort,
                                                 const SysParamSrv_osalQueueHandle_t queueHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueHandleFind(%p, %p)", osalPort, (void *)queueHandle);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(queueHandle != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_QUEUE_SLOTS_NUM; ++i)
    {
        if (osal->queueObjHandle[i] == queueHandle)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegQueueHandleFind -> %d", 0);

    return 0u;
}


/*-------------------------------- Registry: Locks --------------------------------*/

/**
 * \brief  Find a free lock object slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t sysParamSrv_osalRegLockFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find lock object handle.
 * \param  osalPort      Derived OSAL pointer.
 * \param  lockObjHandle Handle to search.
 * \return Lock ID (index + 1) or 0 if not found.
 */
static size_t sysParamSrv_osalRegLockHandleFind(void * const osalPort,
                                                const SysParamSrv_osalLockObjHandle_t lockObjHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockHandleFind(%p, %p)", osalPort, (void *)lockObjHandle);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(lockObjHandle != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_LOCK_OBJ_SLOTS_NUM; ++i)
    {
        if (osal->lockObjHandle[i] == lockObjHandle)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegLockHandleFind -> %d", 0);

    return 0u;
}


/*------------------------------- Registry: Threads -------------------------------*/

/**
 * \brief  Find a free thread slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t sysParamSrv_osalRegThreadFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == NULL)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find thread handle.
 * \param  osalPort     Derived OSAL pointer.
 * \param  threadHandle Handle to search.
 * \return Thread ID (index + 1) or 0 if not found.
 */
static size_t sysParamSrv_osalRegThreadHandleFind(void * const osalPort,
                                                  const SysParamSrv_osalThreadHandle_t threadHandle)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadHandleFind(%p, %p)", osalPort, (void *)threadHandle);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(threadHandle != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_THREAD_SLOTS_NUM; ++i)
    {
        if (osal->threadObjHandle[i].handle == threadHandle)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegThreadHandleFind -> %d", 0);

    return 0u;
}


/*-------------------------------- Registry: Memory --------------------------------*/

/**
 * \brief  Find a free memory slot.
 * \param  osalPort  Derived OSAL pointer.
 * \return Slot ID (index + 1) or 0 if none.
 */
static size_t sysParamSrv_osalRegMemFreeSlotFind(void * const osalPort)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemFreeSlotFind(%p)", osalPort);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memObjHandle[i] == NULL)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemFreeSlotFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemFreeSlotFind -> %d", 0);

    return 0u;
}


/**
 * \brief  Find pointer in memory registry.
 * \param  osalPort  Derived OSAL pointer.
 * \param  ptr       Pointer to search.
 * \return Memory ID (index + 1) or 0 if not found.
 */
static size_t sysParamSrv_osalRegMemHandleFind(void * const osalPort,
                                               const void * const ptr)
{
    /* Trace input args */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemHandleFind(%p, %p)", osalPort, ptr);

    /* Must be validated by the caller */
    SYS_PARAM_SRV_OSAL_ASSERT(osalPort != NULL);
    SYS_PARAM_SRV_OSAL_ASSERT(ptr != NULL);
    SysParamSrv_osal_s *const osal = (SysParamSrv_osal_s *)osalPort;

    for (size_t i = 0; i < SYS_PARAM_SRV_OSAL_MEM_SLOTS_NUM; ++i)
    {
        if (osal->memObjHandle[i] == ptr)
        {
            /* Trace: returned value */
            SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemHandleFind -> %zu", i + 1u);

            return i + 1u;
        }
    }

    /* Trace: returned value */
    SYS_PARAM_SRV_OSAL_TRACE("sysParamSrv_osalRegMemHandleFind -> %d", 0);

    return 0u;
}
