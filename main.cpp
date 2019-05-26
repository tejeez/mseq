#include "mbed.h"

template<typename T> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}

const double SECONDS_PER_TICK=0.001;
const double MIN_BPM=20.0,MAX_BPM=200.0;
const double MIN_SWING=0.1,MAX_SWING=0.9;
const int num_of_columns, num_of_channels=6, num_of_options=4;
const int num_of_beats=4; // used for everyother effect

double bpm=120.0;
double delta=0.0;
double pwmScale=1000.0,pwmWidth=500.0;
int curtick=0,curbeat=0;

// PREVIOUS == previous channel
Enum RowOption { SWING, EVERYOTHER, PREVIOUS, NEXT, INVERT, PWM, DOUBLETIME, HALFTIME };

// rowOption[i].read() & 0x1 == first channel nth option column
BusIn rowOptions[]={(),,...};
BusIn globalOptions(); // two left-most switches under the potentiometers

long long optionsCache[num_of_options], globalOptionsCache;
void readOptions() {
	globalOptionsCache=globalOptions;
	for(int i=0;i<num_of_options;i++) {
		optionsCache[i]=rowOption[i];
	}
}

boolean rowOptionOn(RowOption which, int channel) {
	return (globalOptionsCache&0x01&&channel<3&&which==SWING) // for enabling a certain option for certain channels as constant;
	||	(globalOptionsCache&0x02&&channel<3&&which==EVERYOTHER)
	||	(globalOptionsCache&0x03&&channel<2&&which==DOUBLETIME)
	||	(globalOptionsCache&0x04&&channel<2&&which==HALFTIME)
	||	(which==PREVIOUS&&optionsCache[2][channel]) // for enabling by right-most switch 
	||	(which==NEXT&&optionsCache[3][channel])
	||	(which==OFFSET&&optionsCache[0][channel])
	||	(which==INVERT&&optionsCache[1][channel]);
}

DigitalOut control_leds[]={LED1,LED2,LED3};
DigitalOut col_leds[]={}, chan_leds[]={};
BusOut column_multiplex_selector();
BusIn row_input_full(), row_input_half();
BusOut bnc_output(1,2,3,4,5,6);

AnalogIn tempo_potentiometer(), swing_potentiometer(), offset_potentiometer(), pwm_width_potentiometer();

Ticker main_ticker;

void global_tick_cb() {
	readOptions();

	double swing=Lerp(swing_potentiometer.read(),MIN_SWING,MAX_SWING);
	long long input_acc, switches_down, switches_up;

	input_acc=0;

	switches_down=row_input_half;
	switches_up=row_input_full;
	for(int i=0;i<num_of_channels;i++) {
		// prev/next switch for this channel
		int inputchan = i+rowOptionOn(PREVIOUS,i)?-1:rowOptionOn(NEXT,i)?1:0;
		// normalize channel number
		inputchan=inputchan<0?num_of_channels-1:inputchan>=num_of_channels?0:inputchan;

		bool play_channel=false;
		bool switch_down=switches_down&(1<<inputchan), switch_up=switches_up&(1<<inputchan);
		play_channel=switch_up||switch_down;

		if(rowOptionOn(SWING,i)) {
			if(delta>swing && switch_down) {
				play_channel=false;
			}
		}

		if(rowOptionOn(EVERYOTHER,i)) {
			play_channel=play_channel&&curbeat%2==1;
		}

		if(rowOptionOn(INVERT,i)) {
			play_channel=!play_channel;
		}

		if(rowOptionOn(PWM,i)&&(delta*pwmScale)>pwmWidth) {
			play_channel=false;
		}

		if(play_channel) {
			input_acc^=1<<i;
		}

		chan_leds[i]=play_channel?1:0;
	}
	bnc_output=input_acc;

	delta+=((4.0*bpm)/60.0)*SECONDS_PER_TICK;

	if(delta>=1.0) {
		delta-=1.0;
		col_leds[curtick]=0;
		curtick++;
		if(curtick>=num_of_columns) {
			curtick=0;
			curbeat++;
			if(curbeat>=num_of_beats) {
				curbeat=0;
			}
		}
		col_leds[curtick]=1;
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
