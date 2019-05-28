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
 * Most pins: 
 */

DigitalOut led1(LED1), led2(LED2), led3(LED3);

DigitalOut chan_leds[num_of_channels]={
		PC_13,PC_14,PC_15,
		PH_0,PH_1,PC_2
}; // placeholder pins, TODO

DigitalOut bnc_output[num_of_channels] = {
	PC_10,PC_12,PF_6,
	PF_7,PA_13,PA_14
};

BusIn globalOptions(); // two left-most switches under the potentiometers


// TODO choose potentiometer pins
AnalogIn tempo_potentiometer(), swing_potentiometer(), offset_potentiometer(), pwm_width_potentiometer();


/*
 * Matrix readout stuff:
 */

DigitalOut column_multiplex_selector[SWITCH_COLUMNS] = {
	PG_6,  PG_5,  PG_8,  PE_0,
	PF_11, PF_15, PF_3,  PE_11,
	PE_9,  PF_14, PD_15, PD_14,
	PE_7,  PF_10,  PE_8, PF_4,
	PF_5,  PB_1
};

/* Two consecutive bits in this BusIn indicate the state of each switch.
 * They are read straight to switch_states and the function get_switch_state
 * is used to extract the state of an individual switch from the resulting
 * array of bit fields. */
BusIn switch_input(
		PG_4,  PG_7,  PD_10, PG_14,
		PF_12, PF_13, PE_13, PE_15,
		PE_14, PE_12, PE_10, PD_11);

DigitalOut col_led_out(PE_7);


bool col_leds[SWITCH_COLUMNS];

uint32_t switch_states[SWITCH_COLUMNS];
enum switch_state { SWITCH_BROKEN=0, SWITCH_UP=1, SWITCH_DOWN=2, SWITCH_MID=3 };

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

	led3 = 1;
	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 0;
		col_led_out = col_leds[col];
		// do all waiting here so the LED has some time to be on
		wait(0.01f);
		switch_states[col] = switch_input;
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}
	led3 = 0;
}



/*
 * Sequencer logic:
 */




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

		play_channel = sw != SWITCH_MID;

		// short notes
		if(sw == SWITCH_DOWN && delta >= 0.5)
			play_channel = 0;
#if 0
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
#endif

		if(play_channel) {
			input_acc^=1<<i;
		}

		chan_leds[i]=play_channel?1:0;
		bnc_output[i] = play_channel;
	}
	delta += bpm*(4.0f/60.0f*SECONDS_PER_TICK);

	while(delta>=1.0) {
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
		// blink LEDs for testing purpose
		led1 = (1 & curtick) == 0;
		led2 = curtick == 0;
	}
}


void print_states() {
	int row, col;
	printf("\033[H\n");
	for(row=0; row<SWITCH_ROWS; row++) {
		for(col=0; col<SWITCH_COLUMNS; col++) {
			//printf("%d ", (int)get_switch_state(row, col));
			char c=' ';
			switch(get_switch_state(row, col)) {
				case SWITCH_UP:      c = '^'; break;
				case SWITCH_DOWN:    c = 'v'; break;
				case SWITCH_MID:     c = '-'; break;
				case SWITCH_BROKEN:  c = '?'; break;
			}
			putchar(c);
		}
		putchar('\n');
	}
}


int main() {
	switch_input.mode(PullUp);

	printf("\033[2J\033[HSekvensseri\n");

	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		// update potentiometer and switch states in non-timed thread
	
		// TODO: why is this: error: request for member 'read' in 'swing_potentiometer', which is of non-class type 'mbed::AnalogIn()'
		swing=Lerp(/*swing_potentiometer.read()*/ .5f,MIN_SWING,MAX_SWING);
		bpm=Lerp(/*tempo_potentiometer.read()*/ .5f,MIN_BPM,MAX_BPM);

		read_matrix();

		print_states();

	}

	return 0;
}
