#include "mbed.h"

const double SECONDS_PER_TICK=0.001;
double bpm=10.0;
double delta=0.0;
int curtick=0;

DigitalOut leds[]={LED1,LED2,LED3};
//BusIn row_input();
//BusOut bnc_output(1,2,3,4,5,6);

Ticker main_ticker;

const int seq1[]={3,3,3,3},
      seq2[]={1,0,1,0},
      seq3[]={0,1,0,1};

void global_tick_cb() {
	if(delta<0.5) {
		leds[0]=seq1[curtick]&1;
		leds[1]=seq2[curtick]&1;
		leds[2]=seq3[curtick]&1;
	} else {
		leds[0]=seq1[curtick]==1?1:0;
		leds[1]=seq2[curtick]==1?1:0;
		leds[2]=seq3[curtick]==1?1:0;
	}

	delta+=((4.0*bpm)/60.0)*SECONDS_PER_TICK;

	if(delta>=1.0) {
		delta-=1.0;
		curtick++;
		if(curtick>=4) curtick=0;
	}
}

int main() {
	leds[0]=1;
	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		;
	}

	return 0;
}
