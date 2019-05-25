#include "mbed.h"

template<T t> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}

const double SECONDS_PER_TICK=0.001;
const double MIN_BPM=20.0,MAX_BPM=200.0;
const double MIN_SWING=0.1,MAX_SWING=0.9;
const int num_of_columns;

double bpm=10.0;
double delta=0.0;
int curtick=0;

DigitalOut leds[]={LED1,LED2,LED3};
BusOut column_multiplex_selector();;
BusIn row_input_full(), row_input_half();
BusOut bnc_output(1,2,3,4,5,6);

AnalogIn tempo_potentiometer(), swing_potentiometer();

Ticker main_ticker;

void global_tick_cb() {
	double swing=Lerp(swing_potentiometer.read(),MIN_SWING,MAX_SWING);
	bnc_output=row_input_full;
	if(delta<swing) {
		bnc_output|=row_input_half;
	}

	delta+=((4.0*bpm)/60.0)*SECONDS_PER_TICK;

	if(delta>=1.0) {
		delta-=1.0;
		curtick++;
		if(curtick>=num_of_columns) curtick=0;
	}

	// set this early for next cb
	column_multiplex_selector=curtick;
}

int main() {
	column_multiplex_selector=0;

	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		// update bpm in non-timed thread
		bpm=Lerp(tempo_potentiometer.read(),MIN_BPM,MAX_BPM);

		wait(0.01f);
	}

	return 0;
}
