#include "mbed.h"

template<typename T> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}

const float SECONDS_PER_TICK=0.001;
const float MIN_BPM=20.0,MAX_BPM=200.0;
const float MIN_SWING=0.1,MAX_SWING=0.9;
const int num_of_columns=16, num_of_channels=6, num_of_options=4;
const int num_of_beats=4; // used for everyother effect
const int SWITCH_COLUMNS = 18, SWITCH_ROWS = 6;

float bpm=120.0, swing=0.0;
float delta=0.0;
float pwmScale=1000.0,pwmWidth=500.0;
int curtick=0,curbeat=0;

// PREVIOUS == previous channel
enum RowOption { SWING, EVERYOTHER, PREVIOUS, NEXT, INVERT, PWM, DOUBLETIME, HALFTIME };






/*
 * Matrix readout stuff:
 */

DigitalOut column_multiplex_selector[SWITCH_COLUMNS] = {
	// placeholder pins, TODO
	PG_12,PG_12,PG_12,PG_12,
	PG_12,PG_12,PG_12,PG_12,
	PG_12,PG_12,PG_12,PG_12,
	PG_12,PG_12,PG_12,PG_12,
	PG_12,PG_12
};

/* Two consecutive bits in this BusIn indicate the state of each switch.
 * They are read straight to switch_states and the function get_switch_state
 * is used to extract the state of an individual switch from the resulting
 * array of bit fields. */
BusIn switch_input(PE_8,PE_9, PE_10,PE_11, PE_12,PE_13, PE_14,PE_15, PF_12,PF_13, PF_14,PF_15);
DigitalOut col_led_out(PE_7);


bool col_leds[SWITCH_COLUMNS];

uint32_t switch_states[SWITCH_COLUMNS];
enum switch_state { SWITCH_UP=1, SWITCH_DOWN=2, SWITCH_MID=3 };

enum switch_state get_switch_state(unsigned row, unsigned col) {
	if(col >= SWITCH_COLUMNS || row >= SWITCH_ROWS)
		return SWITCH_DOWN;
	return (enum switch_state)((switch_states[col] >> (2*row)) & 3);
}

void read_matrix() {
	int col;
	/* Column selector is active low.
	 * First ensure all column selectors are high
	 * and then take one of them down at a time */
	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 1;
	}

	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 0;
		col_led_out = col_leds[col];
		// do all waiting here so the LED has some time to be on
		wait(0.001f);
		switch_states[col] = switch_input;
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}
}



/*
 * something else:
 */


BusIn globalOptions(); // two left-most switches under the potentiometers


long long optionsCache[num_of_options], globalOptionsCache;
#if 0
/* TODO: fix this all to make use of cached switch states instead.
 * Also find out how to do all these row options with just two 3-position switches...
 * (invert could be just another output though, so no switch actually needed for that) */
void readOptions() {
	globalOptionsCache=globalOptions;
	for(int i=0;i<num_of_options;i++) {
		optionsCache[i]=rowOption[i];
	}
}
#endif

bool rowOptionOn(RowOption which, int channel) {
	return 0; // TODO
#if 0
	return (globalOptionsCache&0x01&&channel<3&&which==SWING) // for enabling a certain option for certain channels as constant;
	||	(globalOptionsCache&0x02&&channel<3&&which==EVERYOTHER)
	||	(globalOptionsCache&0x03&&channel<2&&which==DOUBLETIME)
	||	(globalOptionsCache&0x04&&channel<2&&which==HALFTIME)
	||	(which==PREVIOUS&&optionsCache[2][channel]) // for enabling by right-most switch 
	||	(which==NEXT&&optionsCache[3][channel])
	||	(which==OFFSET&&optionsCache[0][channel])
	||	(which==INVERT&&optionsCache[1][channel]);
#endif
}


DigitalOut control_leds[]={LED1,LED2,LED3};
DigitalOut chan_leds[num_of_channels]={LED1,LED2,LED3,LED1,LED2,LED3}; // placeholder pins, TODO

//BusIn row_input_full(), row_input_half();
BusOut bnc_output(PF_9, PF_8, PF_2, PE_5, PE_4, PE_2); // placeholder pins, TODO



// TODO choose potentiometer pins
AnalogIn tempo_potentiometer(), swing_potentiometer(), offset_potentiometer(), pwm_width_potentiometer();

Ticker main_ticker;

void global_tick_cb() {
	uint32_t input_acc=0;

	for(int i=0;i<num_of_channels;i++) {
		// prev/next switch for this channel
		int inputchan = i+rowOptionOn(PREVIOUS,i)?-1:rowOptionOn(NEXT,i)?1:0;
		// normalize channel number
		inputchan=inputchan<0?num_of_channels-1:inputchan>=num_of_channels?0:inputchan;

		bool play_channel=false;
		
		enum switch_state sw = get_switch_state(inputchan, curtick);

		play_channel = sw == SWITCH_UP || sw == SWITCH_MID;

		if(rowOptionOn(SWING,i)) {
			if(sw == SWITCH_MID && delta>swing) {
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

	delta += bpm*(4.0f/60.0f*SECONDS_PER_TICK);

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
}


int main() {
	switch_input.mode(PullUp);

	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		// update potentiometer and switch states in non-timed thread
	
		// TODO: why is this: error: request for member 'read' in 'swing_potentiometer', which is of non-class type 'mbed::AnalogIn()'
		swing=Lerp(/*swing_potentiometer.read()*/ .0f,MIN_SWING,MAX_SWING);
		bpm=Lerp(/*tempo_potentiometer.read()*/ .0f,MIN_BPM,MAX_BPM);

		read_matrix();

		//wait(0.01f);
	}

	return 0;
}

#if 0
/* Two consecutive bits in this BusIn indicate the state of each switch.
 * They are read straight to switch_states and the function get_switch_state
 * is used to extract the state of an individual switch from the resulting
 * array of bit fields. */
BusIn switch_input(PE_8,PE_9, PE_10,PE_11, PE_12,PE_13, PE_14,PE_15, PF_12,PF_13, PF_14,PF_15);
DigitalOut col_led_out(PE_7);


bool col_leds[SWITCH_COLUMNS];

uint32_t switch_states[SWITCH_COLUMNS];
enum switch_state { SWITCH_UP=1, SWITCH_DOWN=2, SWITCH_MID=3 };

enum switch_state get_switch_state(unsigned row, unsigned col) {
	if(col >= SWITCH_COLUMNS || row >= SWITCH_ROWS)
		return SWITCH_DOWN;
	return (enum switch_state)((switch_states[col] >> (2*row)) & 3);
}

void read_matrix() {
	int col;
	/* Column selector is active low.
	 * First ensure all column selectors are high
	 * and then take one of them down at a time */
	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 1;
	}

	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 0;
		col_led_out = col_leds[col];
		// do all waiting here so the LED has some time to be on
		wait(0.001f);
		switch_states[col] = switch_input;
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}
}



/*
 * something else:
 */


BusIn globalOptions(); // two left-most switches under the potentiometers


long long optionsCache[num_of_options], globalOptionsCache;
#if 0
/* TODO: fix this all to make use of cached switch states instead.
 * Also find out how to do all these row options with just two 3-position switches...
 * (invert could be just another output though, so no switch actually needed for that) */
void readOptions() {
	globalOptionsCache=globalOptions;
	for(int i=0;i<num_of_options;i++) {
		optionsCache[i]=rowOption[i];
	}
}
#endif

bool rowOptionOn(RowOption which, int channel) {
	return 0; // TODO
#if 0
	return (globalOptionsCache&0x01&&channel<3&&which==SWING) // for enabling a certain option for certain channels as constant;
	||	(globalOptionsCache&0x02&&channel<3&&which==EVERYOTHER)
	||	(globalOptionsCache&0x03&&channel<2&&which==DOUBLETIME)
	||	(globalOptionsCache&0x04&&channel<2&&which==HALFTIME)
	||	(which==PREVIOUS&&optionsCache[2][channel]) // for enabling by right-most switch 
	||	(which==NEXT&&optionsCache[3][channel])
	||	(which==OFFSET&&optionsCache[0][channel])
	||	(which==INVERT&&optionsCache[1][channel]);
#endif
}


DigitalOut control_leds[]={LED1,LED2,LED3};
DigitalOut chan_leds[]={};

//BusIn row_input_full(), row_input_half();
BusOut bnc_output(PF_9, PF_8, PF_2, PE_5, PE_4, PE_2); // placeholder pins, TODO



// TODO choose potentiometer pins
AnalogIn tempo_potentiometer(), swing_potentiometer(), offset_potentiometer(), pwm_width_potentiometer();

Ticker main_ticker;

void global_tick_cb() {
	uint32_t input_acc=0;

	for(int i=0;i<num_of_channels;i++) {
		// prev/next switch for this channel
		int inputchan = i+rowOptionOn(PREVIOUS,i)?-1:rowOptionOn(NEXT,i)?1:0;
		// normalize channel number
		inputchan=inputchan<0?num_of_channels-1:inputchan>=num_of_channels?0:inputchan;

		bool play_channel=false;
		
		enum switch_state sw = get_switch_state(inputchan, curtick);

		play_channel = sw == SWITCH_UP || sw == SWITCH_MID;

		if(rowOptionOn(SWING,i)) {
			if(sw == SWITCH_MID && delta>swing) {
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
		if(play_channel)
			input_acc |= 1<<i;
	}
	bnc_output=input_acc;

	delta += bpm*(4.0f/60.0f*SECONDS_PER_TICK);

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
}


int main() {
	switch_input.mode(PullUp);

	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		// update potentiometer and switch states in non-timed thread
	
		// TODO: why is this: error: request for member 'read' in 'swing_potentiometer', which is of non-class type 'mbed::AnalogIn()'
		swing=Lerp(/*swing_potentiometer.read()*/ .0f,MIN_SWING,MAX_SWING);
		bpm=Lerp(/*tempo_potentiometer.read()*/ .0f,MIN_BPM,MAX_BPM);

		read_matrix();

		//wait(0.01f);
	}

	return 0;
}
#endif
