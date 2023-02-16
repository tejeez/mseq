#include "mbed.h"

// ----------------------
// Some general functions
// ----------------------

template<typename T> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}


// ------------------------
// Configuration parameters
// ------------------------

const char *VERSIO_MERKKIJONO = "v0.0.3";

const float SECONDS_PER_TICK=0.001;
const float MIN_BPM=70.0,MAX_BPM=200.0;
const float MIN_SWING=0.0,MAX_SWING=0.7;
const float MIN_LENGTH=1.0f, MAX_LENGTH=16.4f;
const int num_of_columns=16, num_of_channels=6;
const int num_of_bars=2;


// ------------------------------
// Pinout and hardware interfaces
//-------------------------------

// Number of rows and columns in switch matrix
const int SWITCH_COLUMNS = 19, SWITCH_ROWS = 6;
// Switch matrix columns used for channel option switches
const int OPTION1_COLUMN = 16, OPTION2_COLUMN = 17;
// Switch matrix column used for global option switches
const int GLOBAL_OPTION_COLUMN = 18;
// Switch matrix rows used for global option switches
const int GLOBAL_OPTION1_ROW = 4, GLOBAL_OPTION2_ROW = 5;

DigitalOut led1(LED1), led2(LED2), led3(LED3);

#if 0
// channels leds not connected
DigitalOut chan_leds[num_of_channels]={
	PH_0, PH_1, PC_2,
	PC_3, PD_4, PD_5	
};
#endif

/* Channel output pins:
 * 1n - 1i -- PD_7 - PF_7
 * 2n - 2i -- PE_3 - PA_13
 * 3n - 3i -- PD_6 - PC_12
 * 4n - 4i -- PA_15 - PC_10
 * 5n - 5i -- PA_14 - PC_11
 * 6n - 6i -- PF_6 - PD_2
 */

DigitalOut bnc1_output[num_of_channels] = {
	PD_7, PE_3, PD_6,
	PA_15, PA_14, PF_6
};

DigitalOut bnc2_output[num_of_channels] = {
	PF_7, PA_13, PC_12,
	PC_10, PC_11, PD_2
};

AnalogIn tempo_potentiometer(PA_0), swing_potentiometer(PA_4);
//AnalogIn length_potentiometer(PA_1), option2_potentiometer(PC_0); // wrong pins

DigitalOut column_multiplex_selector[SWITCH_COLUMNS] = {
	PG_6,  PG_5,  PG_8,  PE_0,
	PF_11, PF_15, PF_3,  PE_11,
	PE_9,  PF_14, PD_15, PD_14,
	PB_2,  PF_10, PE_8,  PF_4,
	PF_5,  PB_15, PB_1
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


DigitalOut col_led_out(PC_8);


// -------------------------------
// States of switch and LED matrix
// -------------------------------

enum led_state {
	// LED is off
	LED_OFF = 0,
	// LED is on with reduced brightness
	LED_DIM = 1,
	// LED is on with full brightness
	LED_BRIGHT = 2,
};
// Column LED brightnesses
enum led_state col_leds[SWITCH_COLUMNS];

// Switch positions as bit fields read from switch_input bus
uint32_t switch_states[SWITCH_COLUMNS];

// Position of a single switch as an enum
enum switch_state { SWITCH_BROKEN=0, SWITCH_UP=1, SWITCH_DOWN=2, SWITCH_MID=3 };

// Get the position of a single switch
inline enum switch_state get_switch_state(unsigned row, unsigned col) {
	if(col >= SWITCH_COLUMNS || row >= SWITCH_ROWS)
		return SWITCH_MID;
	return (enum switch_state)((switch_states[col] >> (2*row)) & 3);
}



// ---------------
// Sequencer state
// ---------------

float bpm=125.0, swing=0.0;
int pattern_length=16;

volatile float delta_v = 0.0;
volatile int curtick_v = 0;
volatile int curbar_v = 0;


// ---------------
// Sequencer logic
// ---------------

enum RowOption {
	PREVIOUS,   // Play pattern from one channel up
	NEXT,       // Play pattern from one channel lower
	FIRSTHALF,  // Loop first half of the pattern
	SECONDHALF, // Loop second half of the pattern
	HALFSPEED,  // Play pattern at half the speed
	EVERYOTHER, // Play pattern every second time
};

bool rowOptionOn(RowOption which, int channel) {
	// Left channel option switch
	enum switch_state switch1 = get_switch_state(channel, OPTION1_COLUMN);
	// Right channel option switch
	enum switch_state switch2 = get_switch_state(channel, OPTION2_COLUMN);
	// Upper global option switch
	enum switch_state global1 = get_switch_state(GLOBAL_OPTION1_ROW, GLOBAL_OPTION_COLUMN);
	// Lower global option switch
	//enum switch_state global2 = get_switch_state(GLOBAL_OPTION2_ROW, GLOBAL_OPTION_COLUMN);

	switch(which) {
		case PREVIOUS:
			return switch2 == SWITCH_DOWN;
		case NEXT:
			return switch2 == SWITCH_UP;

		// Global option changes behaviour of the first channel option switch.
		// Mid position: firsthalf/secondhalf
		case FIRSTHALF:
			return global1 == SWITCH_MID  && switch1 == SWITCH_DOWN;
		case SECONDHALF:
			return global1 == SWITCH_MID  && switch1 == SWITCH_UP;
		// Up position: halfspeed/everyother
		case HALFSPEED:
			return global1 == SWITCH_UP   && switch1 == SWITCH_DOWN;
		case EVERYOTHER:
			return global1 == SWITCH_UP   && switch1 == SWITCH_UP;
		// Down position and the whole global2 switch
		// are available for additional options.

		default:
			return 0;
	}
}


void global_tick_cb() {
	// Copy volatiles to local variables to avoid weird surprises
	// if they are changed somewhere else during the function.
	int curtick = curtick_v, curbar = curbar_v;
	float delta = delta_v;

	// LED2 flashes on every fourth tick
	led2 = (curtick % 4) == 0;
	// LED3 turns on for every second bar
	led3 = (curbar % 2) == 0;

	for(int i=0;i<num_of_channels;i++) {
		// Switch row currently used for this channel
		int inputchan=i;
		// prev/next switch for this channel
		if(rowOptionOn(PREVIOUS,i))
			inputchan--;
		if(rowOptionOn(NEXT,i))
			inputchan++;
		if(inputchan<0)
			inputchan = num_of_channels-1;
		if(inputchan >= num_of_channels)
			inputchan = 0;

		// Switch column currently used for this channel
		int inputcol;
		// Should this channel be playing at all
		bool enable_channel = true;
		// Choose inputcol and enable_channel based on channel mode options
		if (rowOptionOn(FIRSTHALF, i)) {
			inputcol = curtick % 8;
		} else if(rowOptionOn(SECONDHALF, i)) {
			inputcol = (curtick % 8) + 8;
		} else if(rowOptionOn(HALFSPEED, i)) {
			inputcol = (curtick + (curbar%2) * pattern_length) / 2;
		} else if(rowOptionOn(EVERYOTHER, i)) {
			inputcol = curtick;
			if (curbar % 2 == 1)
				enable_channel = false;
		} else {
			// Default mode with no options enabled
			inputcol = curtick;
		}

		// Position of switch used for this channel at this tick
		enum switch_state sw;
		if (enable_channel) {
			sw = get_switch_state(inputchan, inputcol);
		} else {
			sw = SWITCH_MID;
		}

		if (1) { // there's just one output mode for now
			/* Switch down is short notes.
			 * Other BNC output is inverted. */

			bool play_channel = sw != SWITCH_MID;

			if(sw == SWITCH_DOWN) {
				if(delta >= 0.5) { // short notes
					play_channel = 0;
				}
			}

			bnc1_output[i] = !play_channel; // bnc buffers invert
			bnc2_output[i] = play_channel; // inverted because bnc buffers invert

			// LED to show first channel for testing purpose
			if(i==0) led1 = play_channel;
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
		curtick++;
		if(curtick>=num_of_columns || curtick >= pattern_length) {
			curtick=0;
			curbar++;
			if(curbar>=num_of_bars) {
				curbar=0;
			}
		}
	}

	// Write variables back to volatiles
	curtick_v = curtick;
	curbar_v = curbar;
	delta_v = delta;
}



// ----------
// Main loops
// ----------

void print_states() {
	int row, col;
	printf("\033[HSekvensseri %s\r\n", VERSIO_MERKKIJONO);
	for(col=0; col<SWITCH_COLUMNS; col++) {
		if(col%4==0) putchar('|');
		putchar(col_leds[col]>0 ? 'o' : ' ');
	}
	putchar('\r');
	putchar('\n');
	for(row=0; row<SWITCH_ROWS; row++) {
		for(col=0; col<SWITCH_COLUMNS; col++) {
			if(col%4==0) putchar('|');
			char c=' ';
			switch(get_switch_state(row, col)) {
				case SWITCH_UP:      c = '^'; break;
				case SWITCH_DOWN:    c = 'v'; break;
				case SWITCH_MID:     c = '-'; break;
				case SWITCH_BROKEN:  c = '?'; break;
			}
			putchar(c);
		}
		putchar('\r');
		putchar('\n');
	}
	printf("\r\n"
		"BPM:%6.1f  Swing: %4.2f  Length: %2d  \r\n"
		"Pos: %2d  Bar: %2d  \r\n"
		,bpm, swing, pattern_length
		,curtick_v, curbar_v);
}


float read_avg(AnalogIn &adc) {
	// this might improve potentiometer reading stability
	// by averaging each reading
	float a = 0;
	int i;
	for(i=0; i<20; i++)
		a += adc.read();
	return a / 20.0f;
}


void read_potentiometers() {
	bpm   = Lerp(read_avg(tempo_potentiometer), MIN_BPM, MAX_BPM);
	swing = Lerp(read_avg(swing_potentiometer), MIN_SWING, MAX_SWING);
	// TODO: check length potentiometer pin
	//pattern_length = round(Lerp(read_avg(length_potentiometer), MIN_LENGTH, MAX_LENGTH));
}


void set_leds(void)
{
	const int curtick = curtick_v;
	for (int i=0; i<SWITCH_COLUMNS; i++) {
		enum led_state ledstate = LED_OFF;
		// Show the last step with the current pattern length
		if (i == pattern_length - 1) {
			ledstate = LED_DIM;
		}
		if (i == curtick) {
			ledstate = LED_BRIGHT;
		}
		col_leds[i] = ledstate;
	}
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

		// keep LED on for 2 ms if brightness is 2,
		// otherwise only for 200 us
		if (col_leds[col] >= LED_BRIGHT) {
			col_led_out = 1;
			ThisThread::sleep_for(2ms);
		}

		col_led_out = col_leds[col] >= LED_DIM;
		wait_us(200);

		// read switch states after waiting so voltages have settled
		switch_states[col] = switch_input;

		// finally turn off the column
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}
}


void read_loop() {
	for(;;) {
		read_potentiometers();
		set_leds();
		read_matrix();
		ThisThread::sleep_for(2ms);
	}
}


Ticker main_ticker;
Thread read_thread;

int main() {
	switch_input.mode(PullUp);

	printf("\033[2J\033[HSekvensseri %s\r\n", VERSIO_MERKKIJONO);

	read_thread.start(read_loop);
	// TODO: use the value from SECONDS_PER_TICK. I'm not good enough
	// with C++ to convert it to the correct type for the new API.
	main_ticker.attach(&global_tick_cb, 1ms);

	while(1) {
		print_states();
		ThisThread::sleep_for(200ms);
	}

	return 0;
}
