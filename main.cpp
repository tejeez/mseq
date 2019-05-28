#include "mbed.h"

template<typename T> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}

const float SECONDS_PER_TICK=0.001;
const float MIN_BPM=20.0,MAX_BPM=200.0;
const float MIN_SWING=0.0,MAX_SWING=0.9;
const int num_of_columns=8, num_of_channels=6, num_of_options=4;
const int num_of_beats=4; // used for everyother effect
const int SWITCH_COLUMNS = 18, SWITCH_ROWS = 6;
const int OPTION1_COLUMN = 16, OPTION2_COLUMN = 17;

float bpm=125.0, swing=0.0;
float delta=0.0;
float pwmScale=1000.0,pwmWidth=500.0;
int curtick=0,curbeat=0,curtick2=0;
int pattern_length_1=16, pattern_length_2=16;

// PREVIOUS == previous channel
enum RowOption { EVERYOTHER, PREVIOUS, NEXT, PWM, DOUBLETIME, HALFTIME, MELODYOUTPUT };

/*
 * Most pins: 
 */

DigitalOut led1(LED1), led2(LED2), led3(LED3);

DigitalOut chan_leds[num_of_channels]={
	PH_0, PH_1, PC_2,
	PC_3, PD_4, PD_5	
};

DigitalOut bnc1_output[num_of_channels] = {
	PC_11, PD_2, PC_12,
	PC_10, PF_6, PF_7
};

DigitalOut bnc2_output[num_of_channels] = {
	PA_13, PA_14, PA_15,
	PC_13, PC_14, PC_15
};

BusIn options_input(PG_0, PE_1, PG_9, PG_12); // two left-most switches under the potentiometers. placeholder pins

AnalogIn tempo_potentiometer(PA_0), swing_potentiometer(PA_4), length1_potentiometer(PB_0), length2_potentiometer(PC_0);

const uint32_t OPTION_SYNC = 1;

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
		PD_11, PE_10, PE_12, PE_14,
		PE_15, PE_13, PF_13, PF_12,
		PG_14, PD_10, PG_7,  PG_4
);

DigitalOut col_led_out(PE_7);

/* State storage: */
bool col_leds[SWITCH_COLUMNS];
uint32_t globalOptionsCache;
uint32_t switch_states[SWITCH_COLUMNS];

/* Matrix functions: */
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

	for(col=0; col<SWITCH_COLUMNS; col++) {
		column_multiplex_selector[col] = 0;
		col_led_out = col_leds[col];
		// do all waiting here so the LED has some time to be on
		wait(0.001f);
		switch_states[col] = switch_input;
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}

	globalOptionsCache = ~(uint32_t)options_input;
}


/*
 * Sequencer logic:
 */


bool rowOptionOn(RowOption which, int channel) {
	enum switch_state switch1, switch2;
	switch1 = get_switch_state(channel, OPTION1_COLUMN);
	switch2 = get_switch_state(channel, OPTION2_COLUMN);
	switch(which) {
		case EVERYOTHER:
			return switch1 == SWITCH_UP;
		case MELODYOUTPUT:
			return switch1 == SWITCH_DOWN;
		case PREVIOUS:
			return switch2 == SWITCH_UP;
		case NEXT:
			return switch2 == SWITCH_DOWN;
		default:
			return 0;
	}
}


void global_tick_cb() {
	for(int i=0;i<num_of_channels;i++) {
		// prev/next switch for this channel
		int inputchan = i+rowOptionOn(PREVIOUS,i)?-1:rowOptionOn(NEXT,i)?1:0;
		// normalize channel number
		inputchan=inputchan<0?num_of_channels-1:inputchan>=num_of_channels?0:inputchan;

		// two lowest rows run from the secondary sequencer
		enum switch_state sw = get_switch_state(inputchan, i < 4 ? curtick : curtick2);

		if(rowOptionOn(MELODYOUTPUT,i)) {
			/* One output is switch up, other is switch down */
			chan_leds[i]   = sw == SWITCH_UP || sw == SWITCH_DOWN;
			bnc1_output[i] = sw == SWITCH_UP;
			bnc2_output[i] = sw == SWITCH_DOWN;

		} else {
			/* Switch down is short notes.
			 * Other BNC output is inverted. */

			bool play_channel = sw != SWITCH_MID;

			// short notes
			if(sw == SWITCH_DOWN && delta >= 0.5)
				play_channel = 0;

			if(rowOptionOn(EVERYOTHER,i)) {
				play_channel=play_channel&&curbeat%2==1;
			}

			chan_leds[i]   =  play_channel;
			bnc1_output[i] =  play_channel;
			bnc2_output[i] = !play_channel; // inverted

			// led3 to show first sequence for testing purpose
			if(i==0) led3 = play_channel;
		}
	}

	delta += bpm*(4.0f/60.0f*SECONDS_PER_TICK);

	float delta_max = 1.0f;
	// delta nominally wraps at 1, but swing is implemented by changing where it wraps
	if(curtick & 1)
		delta_max -= swing;
	else
		delta_max += swing;

	while(delta>=delta_max) {
		delta-=delta_max;
		col_leds[curtick]=0;
		curtick++;
		if(curtick>=num_of_columns || curtick >= pattern_length_1) {
			curtick=0;
			curbeat++;
			if(curbeat>=num_of_beats) {
				curbeat=0;
			}
		}
		col_leds[curtick]=1;

		// secondary sequencer
		col_leds[curtick2]=0;
		curtick2++;
		if(curtick2>=num_of_columns || curtick2 >= pattern_length_2) {
			curtick2=0;
		}
		col_leds[curtick2]=1;

		// synchronize secondary sequencer at beginning of pattern if desired
		if((globalOptionsCache & OPTION_SYNC) && curtick == 0)
			curtick2 = 0;

		// blink LEDs for testing purpose
		led1 = curtick == 0;
		led2 = curtick2 == 0;
	}
}



/*
 * Main loop:
 */

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


Ticker main_ticker;

int main() {
	switch_input.mode(PullUp);
	options_input.mode(PullUp);

	printf("\033[2J\033[HSekvensseri\n");

	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		// update potentiometer and switch states in non-timed thread
	
		swing=Lerp(swing_potentiometer.read(),MIN_SWING,MAX_SWING);
		bpm=Lerp(tempo_potentiometer.read(),MIN_BPM,MAX_BPM);
		pattern_length_1 = (int)Lerp(length1_potentiometer.read(),0.5f,16.5f);
		pattern_length_2 = (int)Lerp(length2_potentiometer.read(),0.5f,16.5f);

		read_matrix();

		print_states();

	}

	return 0;
}
