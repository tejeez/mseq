#include "mbed.h"

#define VERSIO_MERKKIJONO "v0.0.2"

template<typename T> T Lerp(T val,T min, T max) {
	return val*(max-min)+min;
}

const float SECONDS_PER_TICK=0.001;
const float MIN_BPM=70.0,MAX_BPM=200.0;
const float MIN_SWING=0.0,MAX_SWING=0.7;
const int num_of_columns=16, num_of_channels=6;
const int num_of_beats=2;
const int SWITCH_COLUMNS = 19, SWITCH_ROWS = 6;
const int OPTION1_COLUMN = 16, OPTION2_COLUMN = 17, GLOBAL_OPTION_COLUMN=18;

float bpm=125.0, swing=0.0;
float delta=0.0;
float pwmScale=1000.0,pwmWidth=500.0;
volatile int curtick=0;
int curbeat=0;
int pattern_length=16;

// PREVIOUS == previous channel
enum RowOption { EVERYOTHER, PREVIOUS, NEXT, PWM, DOUBLETIME, HALFTIME, MELODYOUTPUT };

/*
 * Most pins: 
 */

DigitalOut led1(LED1), led2(LED2), led3(LED3);

#if 0
// channels leds not connected
DigitalOut chan_leds[num_of_channels]={
	PH_0, PH_1, PC_2,
	PC_3, PD_4, PD_5	
};
#endif

/*
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

//class AveragedPotentioMeter {
//	AnalogIn pot;
//	float buf[100];
//};

AnalogIn tempo_potentiometer(PA_0), swing_potentiometer(PA_4);
//AnalogIn option1_potentiometer(PA_1), option2_potentiometer(PC_0); // wrong pins

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

// global option things
const uint32_t OPTION_SYNC = 1<<8, OPTION_ASDF = 1<<9, OPTION_QWER = 1<<10, OPTION_ZXCV = 1<<11;

DigitalOut col_led_out(PC_8);


/*
 * State storage:
 */
char col_leds[SWITCH_COLUMNS];
uint32_t globalOptionsCache;
uint32_t switch_states[SWITCH_COLUMNS];

enum switch_state { SWITCH_BROKEN=0, SWITCH_UP=1, SWITCH_DOWN=2, SWITCH_MID=3 };

enum switch_state get_switch_state(unsigned row, unsigned col) {
	if(col >= SWITCH_COLUMNS || row >= SWITCH_ROWS)
		return SWITCH_DOWN;
	return (enum switch_state)((switch_states[col] >> (2*row)) & 3);
}


/*
 * Sequencer logic:
 */


inline bool rowOptionOn(RowOption which, int channel) {
	enum switch_state switch1, switch2;
	switch1 = get_switch_state(channel, OPTION1_COLUMN);
	switch2 = get_switch_state(channel, OPTION2_COLUMN);
	switch(which) {
		case EVERYOTHER:
			// allow on all channels because DOUBLE/HALFTIME are not working
			return /*channel>=3 && */switch1 == SWITCH_UP;
		case MELODYOUTPUT:
			return /*channel>=3 && */switch1 == SWITCH_DOWN;
		case PREVIOUS:
			return switch2 == SWITCH_DOWN;
		case NEXT:
			return switch2 == SWITCH_UP;
		case DOUBLETIME:
			return channel<3 && switch1 == SWITCH_UP;
		case HALFTIME:
			return channel<3 && switch1 == SWITCH_DOWN;
		default:
			return 0;
	}
}


void global_tick_cb() {
	led2 = (curtick & 3) == 0;
	led3 = curtick == 0;
	for(int i=0;i<num_of_channels;i++) {
		// prev/next switch for this channel
		int inputchan=i;
		if(rowOptionOn(PREVIOUS,i))
			inputchan--;
		if(rowOptionOn(NEXT,i))
			inputchan++;
		if(inputchan<0)
			inputchan = num_of_channels-1;
		if(inputchan >= num_of_channels)
			inputchan = 0;


		enum switch_state sw;
		/*
		if(rowOptionOn(HALFTIME,i)) {
	       		sw = get_switch_state(inputchan,curtick/4+curbeat*8);
		} else if(rowOptionOn(DOUBLETIME,i)) {
		*/
			sw = get_switch_state(inputchan,curtick);
			/*
		} else {
			sw = get_switch_state(inputchan,curtick/2);
		}
		*/

		if(rowOptionOn(MELODYOUTPUT,i)) {
			/* One output is switch up, other is switch down */
			//chan_leds[i]   = sw == SWITCH_UP || sw == SWITCH_DOWN;
			bnc1_output[i] = sw == SWITCH_UP;
			bnc2_output[i] = sw == SWITCH_DOWN;
		} else {
			/* Switch down is short notes.
			 * Other BNC output is inverted. */

			bool play_channel = sw != SWITCH_MID;

			if(sw == SWITCH_DOWN) {
				if(rowOptionOn(EVERYOTHER,i)) {
					play_channel=play_channel&&curbeat%2==1;
				} else if(delta >= 0.5) { // short notes
					play_channel = 0;
				}
			}

			//chan_leds[i]   =  play_channel;
			bnc1_output[i] =  !play_channel; // bnc buffers invert
		/*	
			if(i==0) {
				bnc2_output[i] = curtick%2==1; // 16-beat
			} else if(i==1) {
				bnc2_output[i] = curtick%4>1; // 8-beat
			} else if(i==2) {
				bnc2_output[i] = curbeat%2==1; // 1-beat
			} else {
			*/
				bnc2_output[i] = play_channel; // inverted because bnc buffers invert
			//}

			// led3 to show first sequence for testing purpose
			if(i==0) led1 = play_channel;
		}
	}

	//delta += bpm*(8.0f/60.0f*SECONDS_PER_TICK);
	delta += bpm*(4.0f/60.0f*SECONDS_PER_TICK);

	float delta_max = 1.0f;
	// delta nominally wraps at 1, but swing is implemented by changing where it wraps
	//if(curtick % 4 >= 2)
	if(curtick & 1)
		delta_max -= swing;
	else
		delta_max += swing;

	while(delta>=delta_max) {
		delta-=delta_max;
		col_leds[curtick]=0;
		curtick++;
		if(curtick>=num_of_columns || curtick >= pattern_length) {
			curtick=0;
			curbeat++;
			if(curbeat>=num_of_beats) {
				curbeat=0;
			}
		}
		col_leds[curtick]=2;
	}
}



/*
 * Main loops:
 */

void print_states() {
	int row, col;
	printf("\033[HSekvensseri " VERSIO_MERKKIJONO "\n");
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
		"BPM:%6.1f  Swing: %4.2f   \r\n"
		"Pos: %2d  \r\n"
		,bpm, swing, curtick);
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
		bpm   = Lerp(read_avg(tempo_potentiometer) ,MIN_BPM,MAX_BPM);
		swing = Lerp(read_avg(swing_potentiometer) ,MIN_SWING,MAX_SWING);
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
			if(col_leds[col] >= 2) {
			col_led_out = 1;
			ThisThread::sleep_for(2);
		}

		col_led_out = col_leds[col] >= 1;
		wait(0.0002f);

		// read switch states after waiting so voltages have settled
		switch_states[col] = switch_input;

		// finally turn off the column
		col_led_out = 0;
		column_multiplex_selector[col] = 1;
	}

	globalOptionsCache = ~switch_states[GLOBAL_OPTION_COLUMN];
}


void read_loop() {
	for(;;) {
		read_potentiometers();
		read_matrix();
		ThisThread::sleep_for(2);
	}
}


Ticker main_ticker;
Thread read_thread;

int main() {
	switch_input.mode(PullUp);
/*
	for(int i=0;i<num_of_channels;i++) {
		bnc1_output[i]=1;
		bnc2_output[i]=1;
	}

	int testattava=5;
	while(1) {
		printf("on\r\n");
		bnc2_output[testattava]=1;
		ThisThread::sleep_for(10);
		printf("off\r\n");
		bnc2_output[testattava]=0;
		ThisThread::sleep_for(10);
	}

	return 0;
*/

	printf("\033[2J\033[HSekvensseri\n");

	read_thread.start(read_loop);
	main_ticker.attach(&global_tick_cb,SECONDS_PER_TICK);

	while(1) {
		print_states();
		ThisThread::sleep_for(200);
	}

	return 0;
}
