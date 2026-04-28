#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_H_
#define SYS_PARAM_SRV_OSAL_FREERTOS_H_

#ifdef __cplusplus
    extern "C" {
#endif

/*================================================================[INCLUDE]=================================================*/

#include "sys_param_srv_osal.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* FreeRTOS core (tskIDLE_PRIORITY, etc.) */
#include "FreeRTOS.h"

/*===========================================================[MACRO DEFINITIONS]============================================*/

/**
 * \brief SysParamSrv FreeRTOS-specific infinity timeout value.
 */
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_INFINITY_TIMEOUT
    #define SYS_PARAM_SRV_OSAL_FREERTOS_INFINITY_TIMEOUT    UINT32_MAX
#endif

/**
 * \brief SysParamSrv FreeRTOS-specific priority levels.
 */
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_LOW
    #define SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_LOW       (tskIDLE_PRIORITY + 1)
#endif
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_MIDDLE
    #define SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_MIDDLE    (tskIDLE_PRIORITY + 2)
#endif
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_HIGH
    #define SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_HIGH      (tskIDLE_PRIORITY + 3)
#endif
#ifndef SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_ULTRA
    #define SYS_PARAM_SRV_OSAL_FREERTOS_THREAD_PRIO_ULTRA     (tskIDLE_PRIORITY + 4)
#endif

/*========================================================[DATA TYPES DEFINITIONS]==========================================*/

/**
 * \brief   FreeRTOS parameters structure.
 * \details Optional user-defined parameters for initializing the FreeRTOS-specific OSAL instance.
 */
typedef struct
{
    void *handle;   /*!< Optional user-defined handle (integration/extension). */
} SysParamSrv_osalFreertosParam_s;

/**
 * \struct  SysParamSrv_osalFreertos_s
 * \brief   SysParamSrv FreeRTOS OSAL structure.
 * \details FreeRTOS-specific extension of the SysParamSrv OSAL. The base must be the first field.
 */
typedef struct
{
    SysParamSrv_osal_s              base;       /*!< Base OSAL object.                        */
    SysParamSrv_osalFreertosParam_s param;      /*!< FreeRTOS-specific configuration (opt.).  */
    bool                            validFlag;  /*!< Validation flag.                         */
} SysParamSrv_osalFreertos_s;

/*===========================================================[PUBLIC INTERFACE]=============================================*/

/**
 * \brief   Initialize the SysParamSrv FreeRTOS OSAL instance.
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance (must not be NULL).
 * \param   name          Optional name string (may be NULL).
 * \param   parent        Optional parent object pointer (may be NULL).
 * \param   param         Optional FreeRTOS parameter structure (may be NULL).
 * \return  SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalFreertosInit(SysParamSrv_osalFreertos_s *const osalFreertos,
                                                   const char *name,
                                                   void *const parent,
                                                   const SysParamSrv_osalFreertosParam_s *const param);

/**
 * \brief   Deinitialize the SysParamSrv FreeRTOS OSAL instance.
 * \param   osalFreertos  Pointer to the FreeRTOS-specific OSAL instance.
 * \return  SysParamSrv_osalErr_e error code, non-zero indicates error.
 */
SysParamSrv_osalErr_e sysParamSrv_osalFreertosDeinit(SysParamSrv_osalFreertos_s *const osalFreertos);

#ifdef __cplusplus
    }
#endif

#endif /* SYS_PARAM_SRV_OSAL_FREERTOS_H_ */
