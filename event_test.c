
#include <stdio.h>
#include <time.h>
#include "event_poll.h"


int timer(void * arg){
	static int tt = 0;
	static int  i = 0;

	printf("entered %s :%d %d \n", (char *)arg,time(NULL) - tt, i++);

	if(i> 10)
		return 1;
	tt = time(NULL);
	return 0;
}



int timer2(void * arg){
	static int tt = 0;
	static int  i = 0;

	printf("entered %s :%d %d \n", (char *)arg,time(NULL) - tt, i++);

	if(i> 10)
		return 1;
	tt = time(NULL);
	return 0;
}



int timer3(void * arg){
	static int tt = 0;
	static int  i = 0;

	printf("entered %s :%d %d \n", (char *)arg,time(NULL) - tt, i++);

	if(i> 10)
		return 1;
	tt = time(NULL);
	return 0;
}



int timer4(void * arg){
	static int tt = 0;
	static int  i = 0;

	printf("entered %s :%d %d \n", (char *)arg,time(NULL) - tt, i++);

	tt = time(NULL);

	if(i> 10)
		return 1;
	return 0;
}


int timer5(void * arg){
	static int tt = 0;
	static int  i = 0;

	printf("entered %s :%d %d \n", (char *)arg,time(NULL) - tt, i++);
	if(i> 10)
		return 1;
	tt = time(NULL);
	return 0;
}

int main (void){
	event_poll_t * e;

	e =  event_init();

	event_add_timer(e,400,"400",timer );

	event_add_timer(e,3000,"3000",timer2 );
	event_add_timer(e,700,"700",timer3 );


	event_add_timer(e,4000,"4000",timer4 );
	event_main_loop(e);

	return 0;

}
