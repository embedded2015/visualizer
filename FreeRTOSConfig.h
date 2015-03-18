#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler
#define vPortSVCHandler SVC_Handler



/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html.
 *----------------------------------------------------------*/

#define configUSE_PREEMPTION		1
#define configUSE_IDLE_HOOK		1
#define configUSE_TICK_HOOK		1
#define configCPU_CLOCK_HZ		( ( unsigned long ) 72000000 )
#define configTICK_RATE_HZ		( ( portTickType ) 100 )
#define configMAX_PRIORITIES		( ( unsigned portBASE_TYPE ) 5 )
#define configMINIMAL_STACK_SIZE	( ( unsigned short ) 128 )
#define configTOTAL_HEAP_SIZE		( ( size_t ) ( 17 * 1024 ) )
#define configMAX_TASK_NAME_LEN		( 16 )
#define configUSE_TRACE_FACILITY	0
#define configUSE_16_BIT_TICKS		0
#define configIDLE_SHOULD_YIELD		1
#define configUSE_MUTEXES		1

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 		0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */

#define INCLUDE_vTaskPrioritySet	1
#define INCLUDE_uxTaskPriorityGet	1
#define INCLUDE_vTaskDelete		1
#define INCLUDE_vTaskCleanUpResources	0
#define INCLUDE_vTaskSuspend		1
#define INCLUDE_vTaskDelayUntil		1
#define INCLUDE_vTaskDelay		1

/* This is the raw value as per the Cortex-M3 NVIC.  Values can be 255
(lowest) to 0 (1?) (highest). */
#define configKERNEL_INTERRUPT_PRIORITY 		127 //Needs to be below 240 (0xf0) to work with QEMU, since this is the priority mask used
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	191 /* equivalent to 0xb0, or priority 11. */


/* This is the value being used as per the ST library which permits 16
priority values, 0 to 15.  This must correspond to the
configKERNEL_INTERRUPT_PRIORITY setting.  Here 15 corresponds to the lowest
NVIC value of 255. */
#define configLIBRARY_KERNEL_INTERRUPT_PRIORITY	15

/* Trace functions */
void trace_task_create(void *task,
                       const char *task_name,
                       unsigned int priority);
void trace_task_switch(void *prev_task,
                       unsigned int prev_tick,
                       void *curr_task);
void trace_create_mutex(void *task);

void trace_queue_create(void *queue,
                        int queue_type,
                        unsigned int queue_size);
void trace_queue_send(void *task,
                      void *queue);
void trace_queue_recv(void *task,
                      void *queue);
void trace_queue_block(void *task,
                       void *queue);
void trace_interrupt_in();

void trace_interrupt_out();

int  get_current_interrupt_number
() __attribute__((naked));

#define traceTASK_CREATE( pxNewTCB ) \
	trace_task_create((pxNewTCB), \
	                  (pxNewTCB)->pcTaskName, \
	                  (pxNewTCB)->uxPriority);

#define traceTASK_SWITCHED_OUT() \
	tskTCB *pxPreviousTCB = pxCurrentTCB; \
	unsigned previous_systick_current = *(unsigned *) 0xE000E018;

#define traceTASK_SWITCHED_IN() \
	if (pxPreviousTCB != pxCurrentTCB) { \
		trace_task_switch(pxPreviousTCB, \
		                  previous_systick_current, \
		                  pxCurrentTCB); \
	}

#define traceCREATE_MUTEX( pxNewQueue ) trace_create_mutex(pxNewQueue);

#define traceQUEUE_CREATE( pxNewQueue ) \
	trace_queue_create(pxNewQueue, ucQueueType, uxQueueLength);

#define traceQUEUE_SEND( pxQueue ) \
	trace_queue_send(xTaskGetCurrentTaskHandle(), pxQueue);

#define traceQUEUE_SEND_FROM_ISR( pxQueue ) \
	trace_queue_send((void *) get_current_interrupt_number(), pxQueue);

#define traceQUEUE_RECEIVE( pxQueue ) \
	trace_queue_recv(xTaskGetCurrentTaskHandle(), pxQueue);

#define traceQUEUE_RECEIVE_FROM_ISR( pxQueue ) \
	trace_queue_recv((void *) get_current_interrupt_number(), pxQueue);

#define traceBLOCKING_ON_QUEUE_RECEIVE( pxQueue ) trace_queue_block(xTaskGetCurrentTaskHandle(), pxQueue);

#endif /* FREERTOS_CONFIG_H */
