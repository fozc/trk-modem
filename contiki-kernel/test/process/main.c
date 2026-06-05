/*
 * test.c
 *
 *  Created on: Jan 17, 2023
 *      Author: fatih.ozcan
 */
#include <stdio.h>
#include "contiki.h"
#include "timer.h"


PROCESS(task_printhello, "Print Hello task");
AUTOSTART_PROCESSES(&task_printhello);


static process_event_t evt_print_hello;
 
void tick_timer()
{
	if(etimer_pending()) {
		etimer_request_poll();
	}

	clock_tick();

}


PROCESS_THREAD(task_printhello, event, data)
{
	static struct etimer print_timer;

	PROCESS_BEGIN();

	etimer_set(&print_timer, CLOCK_SECOND);
	while (1)
	{
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&print_timer) || event == evt_print_hello);
		etimer_reset(&print_timer);
		printf("Task[%s]", task_printhello.name);

		if(event == evt_print_hello){
			printf("event-> evt_print_hello");
		}
		printf("\n");
	}

	PROCESS_END();
}

void bsp_init()
{
	  if(start_timer(1, &tick_timer)) // 1 msec
	  {
	    printf("\n timer error\n");
	  }
}

int main(void){

	printf("Hello from Contiki!\n");
	setvbuf(stdout, NULL, _IONBF, 0);
 
	process_init();
	process_start(&etimer_process, NULL);
	autostart_start(autostart_processes);

	//process_start(&task_printhello, NULL);

	etimer_request_poll();
 
	bsp_init();

	clock_time_t t = clock_time();

	while(1)
	{
		while(process_nevents()) /* Consume all events needs to be processed or a task needs to be executed. */
		{
			(void)process_run();
		}

		if(clock_time() - t > 2000)
		{
			process_post(PROCESS_BROADCAST, evt_print_hello, 0);
			t = clock_time();
		}
	}
 
	return 0;
}
