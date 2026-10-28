/*
 * FreeRTOS Kernel V11.3.1 - application configuration
 * Target : STM32F767IGT6 (Cortex-M7, 216MHz, HSE 25MHz)
 * Port   : portable/GCC/ARM_CM7/r0p1 (AC6 / armclang)
 * Timebase: HAL uses TIM7 (1ms), FreeRTOS uses SysTick (1ms)
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*---------------------------------------------------------------------------*/
/* Hardware / clock                                                          */
/*---------------------------------------------------------------------------*/
#define configCPU_CLOCK_HZ                        ( 216000000UL )
#define configTICK_RATE_HZ                        ( 1000 )
#define configTICK_TYPE_WIDTH_IN_BITS             TICK_TYPE_WIDTH_32_BITS

/*---------------------------------------------------------------------------*/
/* Scheduler                                                                 */
/*---------------------------------------------------------------------------*/
#define configUSE_PREEMPTION                      1
#define configUSE_TIME_SLICING                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TICKLESS_IDLE                   0
#define configMAX_PRIORITIES                      32
#define configMINIMAL_STACK_SIZE                  128
#define configMAX_TASK_NAME_LEN                   16
#define configIDLE_SHOULD_YIELD                   1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1
#define configQUEUE_REGISTRY_SIZE                 0
#define configENABLE_BACKWARD_COMPATIBILITY       0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   0
#define configUSE_MINI_LIST_ITEM                  1
#define configSTACK_DEPTH_TYPE                    size_t
#define configMESSAGE_BUFFER_LENGTH_TYPE          size_t
#define configUSE_NEWLIB_REENTRANT                0

/*---------------------------------------------------------------------------*/
/* Memory allocation                                                         */
/*---------------------------------------------------------------------------*/
#define configSUPPORT_STATIC_ALLOCATION           0
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configTOTAL_HEAP_SIZE                     ( 64 * 1024 )
#define configAPPLICATION_ALLOCATED_HEAP          0
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0
#define configENABLE_HEAP_PROTECTOR               0

/*---------------------------------------------------------------------------*/
/* Interrupt priority (Cortex-M7, 4 priority bits)                           */
/*---------------------------------------------------------------------------*/
#define configPRIO_BITS                           4
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5
#define configMAX_SYSCALL_INTERRUPT_PRIORITY      \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY   configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#define configKERNEL_INTERRUPT_PRIORITY           \
    ( configLIBRARY_KERNEL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_API_CALL_INTERRUPT_PRIORITY     configMAX_SYSCALL_INTERRUPT_PRIORITY

/*---------------------------------------------------------------------------*/
/* Software timers                                                           */
/*---------------------------------------------------------------------------*/
#define configUSE_TIMERS                          1
#define configTIMER_TASK_PRIORITY                 5
#define configTIMER_TASK_STACK_DEPTH              ( 2 * configMINIMAL_STACK_SIZE )
#define configTIMER_QUEUE_LENGTH                  10

/*---------------------------------------------------------------------------*/
/* Optional features                                                         */
/*---------------------------------------------------------------------------*/
#define configUSE_EVENT_GROUPS                    1
#define configUSE_STREAM_BUFFERS                  1
#define configUSE_TASK_NOTIFICATIONS              1
#define configUSE_MUTEXES                         1
#define configUSE_RECURSIVE_MUTEXES               1
#define configUSE_COUNTING_SEMAPHORES             1
#define configUSE_QUEUE_SETS                      0
#define configUSE_APPLICATION_TASK_TAG            0
#define configUSE_POSIX_ERRNO                     0
#define configUSE_CO_ROUTINES                     0
#define configMAX_CO_ROUTINE_PRIORITIES           1

/*---------------------------------------------------------------------------*/
/* Hooks / callbacks                                                         */
/*---------------------------------------------------------------------------*/
#define configUSE_IDLE_HOOK                       0
#define configUSE_TICK_HOOK                       0
#define configUSE_MALLOC_FAILED_HOOK              0
#define configUSE_DAEMON_TASK_STARTUP_HOOK        0
#define configUSE_SB_COMPLETED_CALLBACK           0
#define configCHECK_FOR_STACK_OVERFLOW            2

/*---------------------------------------------------------------------------*/
/* Run time stats                                                            */
/*---------------------------------------------------------------------------*/
#define configGENERATE_RUN_TIME_STATS             0
#define configUSE_TRACE_FACILITY                  0
#define configUSE_STATS_FORMATTING_FUNCTIONS      0

/*---------------------------------------------------------------------------*/
/* Port related (indirect ISR routing in stm32f7xx_it.c)                     */
/*---------------------------------------------------------------------------*/
#define configCHECK_HANDLER_INSTALLATION          0
#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS 0

/*---------------------------------------------------------------------------*/
/* Assert                                                                    */
/*---------------------------------------------------------------------------*/
#define configASSERT( x )                         \
    if( ( x ) == 0 )                              \
    {                                             \
        taskDISABLE_INTERRUPTS();                 \
        for( ; ; )                                \
        {                                         \
        }                                         \
    }

/*---------------------------------------------------------------------------*/
/* API inclusion                                                             */
/*---------------------------------------------------------------------------*/
#define INCLUDE_vTaskPrioritySet                  1
#define INCLUDE_uxTaskPriorityGet                 1
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      1
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_xTaskGetIdleTaskHandle            1
#define INCLUDE_eTaskGetState                     1
#define INCLUDE_xTimerPendFunctionCall            1
#define INCLUDE_xTaskAbortDelay                   1
#define INCLUDE_xTaskGetHandle                    1
#define INCLUDE_xTaskResumeFromISR                1

#endif /* FREERTOS_CONFIG_H */
