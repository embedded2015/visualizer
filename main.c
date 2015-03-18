#define USE_STDPERIPH_DRIVER
#include "stm32f10x.h"

/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <stdarg.h>

#define NVIC_INTERRUPTx_PRIORITY ( ( volatile unsigned char *) 0xE000E400 )

int logfile = 0;

int syscall(int number, ...) __attribute__((naked));

int open(const char *pathname, int flags);
int close(int fildes);
size_t write(int fildes, const void *buf, size_t nbyte);

static void setup_hardware();

volatile xQueueHandle serial_str_queue = NULL;
volatile xSemaphoreHandle serial_tx_wait_sem = NULL;
volatile xQueueHandle serial_rx_queue = NULL;

/* Queue structure used for passing messages. */
typedef struct {
	char str[100];
} serial_str_msg;

/* Queue structure used for passing characters. */
typedef struct {
	char ch;
} serial_ch_msg;

/* IRQ handler to handle USART2 interruptss (both transmit and
 * receive interrupts). */
void USART2_IRQHandler()
{
	static signed portBASE_TYPE xHigherPriorityTaskWoken;
	serial_ch_msg rx_msg;

	/* If this interrupt is for a transmit... */
	if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET) {
		/* "give" the serial_tx_wait_sem semaphore to notfiy processes
		 * that the buffer has a spot free for the next byte.
		 */
		xSemaphoreGiveFromISR(serial_tx_wait_sem,
		                      &xHigherPriorityTaskWoken);

		/* Diables the transmit interrupt. */
		USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
		/* If this interrupt is for a receive... */
	} else if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
		/* Receive the byte from the buffer. */
		rx_msg.ch = USART_ReceiveData(USART2);

		/* Queue the received byte. */
		if (!xQueueSendToBackFromISR(serial_rx_queue, &rx_msg,
		                             &xHigherPriorityTaskWoken)) {
			/* If there was an error queueing the received byte,
			 * freeze. */
			while (1);
		}
	} else {
		/* Only transmit and receive interrupts should be enabled.
		 * If this is another type of interrupt, freeze.
		 */
		while (1);
	}

	if (xHigherPriorityTaskWoken)
		taskYIELD();
}

void send_byte(char ch)
{
	/* Wait until the RS232 port can receive another byte (this semaphore
	 * is "given" by the RS232 port interrupt when the buffer has room for
	 * another byte.
	 */
	while (!xSemaphoreTake(serial_tx_wait_sem, portMAX_DELAY));

	/* Send the byte and enable the transmit interrupt (it is disabled by
	 * the interrupt).
	 */
	USART_SendData(USART2, ch);
	USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

char receive_byte()
{
	serial_ch_msg msg;

	/* Wait for a byte to be queued by the receive interrupts handler. */
	while (!xQueueReceive(serial_rx_queue, &msg, portMAX_DELAY));

	return msg.ch;
}

void led_flash_task(void *pvParameters)
{
	while (1) {
		/* Toggle the LED. */
		GPIOC->ODR = GPIOC->ODR ^ 0x00001000;

		/* Wait one second. */
		vTaskDelay(100);
	}
}

void rs232_xmit_msg_task(void *pvParameters)
{
	serial_str_msg msg;
	int curr_char;

	while (1) {
		/* Read from the queue.  Keep trying until a message is
		 * received.  This will block for a period of time (specified
		 * by portMAX_DELAY). */
		while (!xQueueReceive(serial_str_queue, &msg, portMAX_DELAY));

		/* Write each character of the message to the RS232 port. */
		curr_char = 0;
		while (msg.str[curr_char] != '\0') {
			send_byte(msg.str[curr_char]);
			curr_char++;
		}
	}
}

/* Repeatedly queues a string to be sent to the RS232.
 *   delay - the time to wait between sending messages.
 *   A delay of 1 means wait 1/100th of a second.
 */
void queue_str_task(const char *str, int delay)
{
	serial_str_msg msg;

	/* Prepare the message to be queued. */
	strcpy(msg.str, str);

	while (1) {
		/* Post the message.  Keep on trying until it is successful. */
		while (!xQueueSendToBack(serial_str_queue, &msg,
		                         portMAX_DELAY));

		/* Wait. */
		vTaskDelay(delay);
	}
}

void queue_str_task1(void *pvParameters)
{
	queue_str_task("Hello 1\n\r", 200);
}

void queue_str_task2(void *pvParameters)
{
	queue_str_task("Hello 2\n\r", 50);
}

void serial_readwrite_task(void *pvParameters)
{
	serial_str_msg msg;
	char ch;
	int curr_char;
	int done;

	/* Prepare the response message to be queued. */
	strcpy(msg.str, "Got:");

	while (1) {
		curr_char = 4;
		done = 0;
		do {
			/* Receive a byte from the RS232 port (this call will
			 * block). */
			ch = receive_byte();

			/* If the byte is an end-of-line type character, then
			 * finish the string and inidcate we are done.
			 */
			if ((ch == '\r') || (ch == '\n')) {
				msg.str[curr_char] = '\n';
				msg.str[curr_char + 1] = '\0';
				done = -1;
				/* Otherwise, add the character to the
				 * response string. */
			} else {
				msg.str[curr_char++] = ch;
			}
		} while (!done);

		/* Once we are done building the response string, queue the
		 * response to be sent to the RS232 port.
		 */
		while (!xQueueSendToBack(serial_str_queue, &msg,
		                         portMAX_DELAY));
	}
}

int main()
{
	logfile = open("log", 4);

	init_led();

	init_button();
	enable_button_interrupts();

	init_rs232();
	enable_rs232_interrupts();
	enable_rs232();

	/* Create the queue used by the serial task.  Messages for write to
	 * the RS232. */
	serial_str_queue = xQueueCreate(10, sizeof(serial_str_msg));
	vSemaphoreCreateBinary(serial_tx_wait_sem);
	serial_rx_queue = xQueueCreate(1, sizeof(serial_ch_msg));

	/* Create a task to flash the LED. */
	xTaskCreate(led_flash_task,
	            (signed portCHAR *) "LED Flash",
	            512 /* stack size */, NULL,
	            tskIDLE_PRIORITY + 5, NULL);

	/* Create tasks to queue a string to be written to the RS232 port. */
	xTaskCreate(queue_str_task1,
	            (signed portCHAR *) "Serial Write 1",
	            512 /* stack size */, NULL,
	            tskIDLE_PRIORITY + 10, NULL);
	xTaskCreate(queue_str_task2,
	            (signed portCHAR *) "Serial Write 2",
	            512 /* stack size */,
	            NULL, tskIDLE_PRIORITY + 10, NULL);

	/* Create a task to write messages from the queue to the RS232 port. */
	xTaskCreate(rs232_xmit_msg_task,
	            (signed portCHAR *) "Serial Xmit Str",
	            512 /* stack size */, NULL, tskIDLE_PRIORITY + 2, NULL);

	/* Create a task to receive characters from the RS232 port and echo
	 * them back to the RS232 port. */
	xTaskCreate(serial_readwrite_task,
	            (signed portCHAR *) "Serial Read/Write",
	            512 /* stack size */, NULL,
	            tskIDLE_PRIORITY + 10, NULL);

	/* Start running the tasks. */
	vTaskStartScheduler();

	return 0;
}

void vApplicationTickHook()
{
}

void vApplicationIdleHook(void)
{
	send_byte('i');
	send_byte('d');
	send_byte('l');
	send_byte('e');
	send_byte('\n');
	send_byte('\r');
}

int _snprintf_int(int num, char *buf, int buf_size)
{
	int len = 1;
	char *p;
	int i = num < 0 ? -num : num;

	for (; i >= 10; i /= 10, len++);

	if (num < 0)
		len++;

	i = num;
	p = buf + len - 1;
	do {
		if (p < buf + buf_size)
			*p-- = '0' + i % 10;
		i /= 10;
	} while (i != 0);

	if (num < 0)
		*p = '-';

	return len < buf_size ? len : buf_size;
}

unsigned int get_reload()
{
	return *(uint32_t *) 0xE000E014;
}

unsigned int get_current()
{
	return *(uint32_t *) 0xE000E018;
}

unsigned int get_time()
{
	static unsigned int const *reload = (void *) 0xE000E014;
	static unsigned int const *current = (void *) 0xE000E018;
	static const unsigned int scale = 1000000 / configTICK_RATE_HZ;
					/* microsecond */

	return xTaskGetTickCount() * scale +
	       (*reload - *current) / (*reload / scale);
}

int get_interrupt_priority(int interrupt)
{
	if (interrupt < 240)
		return NVIC_INTERRUPTx_PRIORITY[interrupt];
	return -1;
}

int snprintf(char *buf, size_t size, const char *format, ...)
{
	va_list ap;
	char *dest = buf;
	char *last = buf + size;
	char ch;

	va_start(ap, format);
	for (ch = *format++; dest < last && ch; ch = *format++) {
		if (ch == '%') {
			ch = *format++;
			switch (ch) {
			case 's' : {
					char *str = va_arg(ap, char*);
					/* strncpy */
					while (dest < last) {
						if ((*dest = *str++))
							dest++;
						else
							break;
					}
				}
				break;
			case 'd' : {
					int num = va_arg(ap, int);
					dest += _snprintf_int(num, dest,
					                      last - dest);
				}
				break;
			case '%' :
				*dest++ = ch;
				break;
			default :
				return -1;
			}
		} else {
			*dest++ = ch;
		}
	}
	va_end(ap);

	if (dest < last)
		*dest = 0;
	else
		*--dest = 0;

	return dest - buf;
}

void trace_task_create(void *task,
                       const char *task_name,
                       unsigned int priority)
{
	char buf[128];
	int len = snprintf(buf, 128, "task %d %d %s\n", task, priority,
	                   task_name);
	write(logfile, buf, len);
}

void trace_task_switch(void *prev_task,
                       unsigned int prev_tick,
                       void *curr_task)
{
	char buf[128];
	int len = snprintf(buf, 128, "switch %d %d %d %d %d %d\n",
	                   prev_task, curr_task,
	                   xTaskGetTickCount(), get_reload(),
	                   prev_tick, get_current());
	write(logfile, buf, len);
}

void trace_create_mutex(void *mutex)
{
	char buf[128];
	int len = snprintf(buf, 128, "mutex %d %d\n", get_time(), mutex);
	write(logfile, buf, len);
}

void trace_queue_create(void *queue,
                        int queue_type,
                        unsigned int queue_size)
{
	char buf[128];
	int len = snprintf(buf, 128, "queue create %d %d %d %d\n",
	                   get_time(), queue, queue_type, queue_size);
	write(logfile, buf, len);
}

void trace_queue_send(void *task,
                      void *queue)
{
	char buf[128];
	int len = snprintf(buf, 128, "queue send %d %d %d\n",
	                   get_time(), task, queue);
	write(logfile, buf, len);
}

void trace_queue_recv(void *task,
                      void *queue)
{
	char  buf[128];
	int len = snprintf(buf, 128, "queue recv %d %d %d\n",
	                   get_time(), task, queue);
	write(logfile, buf, len);
}

void trace_queue_block(void *task,
                       void *queue)
{
	char buf[128];
	int len = snprintf(buf, 128, "queue block %d %d %d\n",
	                   get_time(), task, queue);
	write(logfile, buf, len);
}

void trace_interrupt_in()
{
	char buf[128];
	int number = get_current_interrupt_number();
	int len = snprintf(buf, 128, "interrupt in %d %d %d\n", get_time(),
	                   number, get_interrupt_priority(number));
	write(logfile, buf, len);
}

void trace_interrupt_out()
{
	char buf[128];
	int number = get_current_interrupt_number();
	int len = snprintf(buf, 128, "interrupt out %d %d\n",
	                   get_time(), number);
	write(logfile, buf, len);
}

int syscall(int number, ...)
{
	asm(
	    "bkpt 0xAB \n"
	    "bx   lr   \n"
	);
}

int open(const char *pathname, int flags)
{
	int argv[] = { (int) pathname, flags, strlen(pathname) };
	return syscall(0x01, argv);
}

int close(int fildes)
{
	return syscall(0x02, &fildes);
}

size_t write(int fildes, const void *buf, size_t nbyte)
{
	int argv[] = { fildes, (int) buf, nbyte };
	return nbyte - syscall(0x05, argv);
}

int get_current_interrupt_number()
{
	asm(
	    "mrs r0, ipsr\n"
	    "bx  lr     \n"
	);
}
