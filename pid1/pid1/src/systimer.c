/*
 * systimer.c
 *
 * Created: 24.03.2013 14:58:22
 *  Author: Avega
 */ 

#include "compilers.h"
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include "soft_timer.h"
#include "systimer.h"
#include "led_indic.h"
#include "adc.h"
#include "control.h"

static inline void Sound_Process(void);

SoftTimer8b_t menuUpdateTimer = {
	.Enabled = 1,
	.RunOnce = 0,
	.CompA = 0,
	.FOvfl = 0,
	.FA_TGL = 0,
	.Timer = 0,
	.Top = MENU_UPDATE_INTERVAL
};	


sys_timers_t sys_timers = {
	.celsius_upd_counter = 1,					// To force Celsius update  - timer is decremental
	.log_counter = 1,							// To force log message		- timer is decremental
	.counter_10sec = COUNTER_10SEC_INTERVAL, 	//							- timer is decremental
	.counter_1min = COUNTER_1MIN_INTERVAL,		//							- timer is decremental
	.poff_counter = 0,							//							- timer is incremental
	.pid_update_counter = PID_UPDATE_INTERVAL	//							- timer is decremental
};


static uint16_t beep_cnt = 0;
static uint8_t enableOverride = 0;


void processSystemTimers(void)
{
	sys_timers.flags = 0x00;
	
	// Process Celsius counter
	if (--sys_timers.celsius_upd_counter == 0)
	{
		sys_timers.celsius_upd_counter = CELSIUS_UDPATE_INTERVAL;
		sys_timers.flags |= EXPIRED_CELSIUS;
		
		// Process PID update counter
		if (--sys_timers.pid_update_counter == 0)
		{
			sys_timers.pid_update_counter = PID_UPDATE_INTERVAL;
			sys_timers.flags |= UPDATE_PID;
		}
	}
	
	// Process log counter
	if (--sys_timers.log_counter == 0)
	{
		sys_timers.log_counter = LOG_INTERVAL;
		sys_timers.flags |= EXPIRED_LOG;
	}
	
	// Process 10 seconds counter
	if (--sys_timers.counter_10sec == 0)
	{
		sys_timers.counter_10sec = COUNTER_10SEC_INTERVAL;
		sys_timers.flags |= EXPIRED_10SEC;
		
		// Process 1 minute counter
		if (--sys_timers.counter_1min == 0)
		{
			sys_timers.counter_1min = COUNTER_1MIN_INTERVAL;
			sys_timers.flags |= EXPIRED_1MIN;
			
			// Process auto power off counter
			if (sys_timers.poff_counter != MAX_POWEROFF_TIMEOUT - 1)
				sys_timers.poff_counter++;
			if (sys_timers.poff_counter == p.power_off_timeout - 1)
				sys_timers.flags |= AUTOPOFF_SOON;
			if (sys_timers.poff_counter == p.power_off_timeout)
				sys_timers.flags |= AUTOPOFF_EXPIRED;			
		}
	}	
}


void resetAutoPowerOffCounter(void)
{
	sys_timers.poff_counter = 0;
}


// ----------------------- //
// ----------------------- //

/*

// Enable / disable beeper output
inline void SetBeepOutput(uint8_t val)
{
	if (val)
	// Toggle OCR1A on compare match
	TCCR1A |= (1<<COM1A0);
	else
	// Disable OCR1A output
	TCCR1A &= ~(1<<COM1A0 | 1<<COM1A1);
}

// Setup beeper period
void SetBeeperPeriod(uint16_t new_period_us)
{
	// Timer1 runs at 250kHz, T = 4us
	// Output toggles on every compare match
	if (new_period_us & 0xFFF8)
	OCR1A = (new_period_us>>3) - 1;
	else
	OCR1A = 0x1;
	TCNT1 = 0;
}

// Setup beeper frequency (Hz)
void SetBeeperFreq(uint16_t freq_hz)
{
	uint16_t period_us = 1000000 / freq_hz;
	if (period_us & 0xFFF8)
	OCR1A = (period_us>>3) - 1;
	else
	OCR1A = 0x1;
	TCNT1 = 0;
}

// Beep for some time in ms
void StartBeep(uint16_t time_ms)
{
	if ( (p.sound_enable) || (enableOverride) )
	{
		beep_cnt = time_ms;
		SetBeepOutput(1);
	}
	enableOverride = 0;
}

void OverrideSoundDisable(void)
{
	enableOverride = 1;
}

// Stop beeper
void StopBeep()
{
	beep_cnt = 0;
	SetBeepOutput(0);
}

*/

void SetBeeperFreq(uint16_t freq_hz)
{
}
	
void StartBeep(uint16_t time_ms)
{
}		

void OverrideSoundDisable(void)
{
}

// Period is 1ms @ 16MHz
ISR(TIMER2_COMP_vect)
{	
	
	// Manage beeper
/*	if (beep_cnt)
		beep_cnt--;
	else
		SetBeepOutput(0);	// done
*/	
	// Manage LED indicator
	processLedIndicator();
	
	// Do sound stuff
	Sound_Process();
	
	// Process menu update timer
	processSoftTimer8b(&menuUpdateTimer);	
	
	// Start ADC conversion 
	ADCSRA |= (1<<ADSC);
	
}

//==================================================//
//				Sound driver						//
//==================================================//




//---------------------------------------------//

/*
static uint8_t sound_state = SOUND_OFF;
static const EEMEM tone_t* current_melody;
static uint8_t SoundEnable_override = 0;


void Sound_Play(const EEMEM tone_t* p_melody)
{
	if ((p.sound_enable) || (SoundEnable_override))
	{
		// Disable interrupts from timer2 - the possible source of calling Sound_Process()
		TIMSK &= ~(1<<OCIE2);				
		current_melody = p_melody;
		sound_state = SOUND_SET_TONE;
		SoundEnable_override = 0;
		// Reenable interrupts
		TIMSK |= (1<<OCIE2);
	}
}

void Sound_Stop(void)
{
	// Disable interrupts from timer2 - the possible source of calling Sound_Process()
	TIMSK &= ~(1<<OCIE2);				
	sound_state = SOUND_OFF;
	// Reenable interrupts
	TIMSK |= (1<<OCIE2);
}

void Sound_OverrideDisable(void)
{
	SoundEnable_override = 1;
}

static inline void GetNextTone(const EEMEM tone_t* p_melody, tone_t* tone)
{
	eeprom_read_block(&tone,&p_melody,sizeof(tone_t));	
}

static inline void Sound_Process(void)
{
	static uint16_t note_time_counter;
	tone_t tone;
	
	switch (sound_state)
	{
		case SOUND_PLAY:
			if (--note_time_counter != 0)
				break;
			// If current tone time is over, we fall through to state SOUND_APPLY_TONE 
		case SOUND_APPLY_TONE:
			tone = GetNextTone(*current_melody++);
			if (tone.duration == 0)
			{
				// Last tone will be longer for one Sound_Process call period.
				// If it is a problem, add disabling timer output here.
				sound_state = SOUND_OFF;	
			}
			else
			{
				// Setup period
				if (tone.tone_period != 0)
				{
					// Timer runs at 250kHz (T = 4us), tone_period is set in units of 8us
					// Output toggles on compare match
					OCR1A = tone.tone_period - 1;
					TCNT1 = 0;
					// Toggle OCR1A on compare match
					TCCR1A |= (1<<COM1A0);
				}
				else
				{
					// Disable OCR1A output
					TCCR1A &= ~(1<<COM1A0 | 1<<COM1A1);
				}
				note_time_counter = tone.duration * TONE_DURATION_SCALE;
				sound_state = SOUND_PLAY;
			}
			break;
		default:
			// Disable OCR1A output
			TCCR1A &= ~(1<<COM1A0 | 1<<COM1A1);
			break;
	}
}

*/


const EEMEM tone_t m_beep_1000Hz_100ms[] = {
	{ FREQ(1000),	LAST(2500) },
	{ FREQ(1100),	LAST(2500) },
	{0,	0}	
};

const EEMEM tone_t m_siren1[] = {
	{ FREQ(800),	LAST(200) },
	{ FREQ(900),	LAST(200) },
	{ FREQ(1000),	LAST(200) },
	{ FREQ(1100),	LAST(200) },
	{ FREQ(1200),	LAST(200) },
	{ FREQ(1100),	LAST(200) },
	{ FREQ(1000),	LAST(200) },
	{ FREQ(900),	LAST(200) },
	{ FREQ(800),	LAST(200) },
	{ FREQ(900),	LAST(200) },
	{ FREQ(1000),	LAST(200) },
	{ FREQ(1100),	LAST(200) },
	{ FREQ(1200),	LAST(200) },
	{ FREQ(1100),	LAST(200) },
	{ FREQ(1000),	LAST(200) },
	{ FREQ(900),	LAST(200) },
	{ FREQ(800),	LAST(200) },
	{0,	0}	
};

const EEMEM tone_t m_siren2[] = {
	{ FREQ(700),	LAST(250) },
	{ FREQ(900),	LAST(250) },
	{ FREQ(800),	LAST(250) },
	{ FREQ(1000),	LAST(250) },
	{ 0,			LAST(250) },
	{ FREQ(900),	LAST(250) },
	{ FREQ(1000),	LAST(500) },
	{0,	0}	
};



//---------------------------------------------//


static uint8_t sound_state = SOUND_OFF;
static const tone_t* new_melody;
static uint8_t SoundEnable_override = 0;
static uint8_t beep_duration;
static uint8_t beep_tone_period;

#ifdef USE_BEEP_FUNCTION
void Sound_Beep(uint16_t freq_hz, uint16_t time_ms)
{
	if ((p.sound_enable) || (SoundEnable_override))
	{
		beep_duration = LAST(time_ms);		// Maximum is 2550ms
		beep_tone_period = FREQ(freq_hz);	// Lowest is 500Hz
		sound_state = SOUND_DO_BEEP;		// No need to disable interrupts - atomic operation
		SoundEnable_override = 0;
	}
}
#endif

void Sound_Play(const tone_t* p_melody)
{
	if ((p.sound_enable) || (SoundEnable_override))
	{
		new_melody =  p_melody;
		sound_state = SOUND_START_NEW;		// No need to disable interrupts - atomic operation
		SoundEnable_override = 0;
	}
}

void Sound_Stop(void)
{
	sound_state = SOUND_OFF;
}

void Sound_OverrideDisable(void)
{
	SoundEnable_override = 1;
}

// Main sound driver function. Called with T = 1ms from system timer ISR.
// For the reason of speed, this code is pipelined, possibly with some code size overhead.
static inline void Sound_Process(void)
{
	static uint16_t note_time_counter;
	static tone_t tone;
	static const tone_t* p_melody;
	uint8_t new_state = sound_state;
	
	switch (sound_state)
	{
		case SOUND_START_NEW:
			p_melody = new_melody;
			new_state = SOUND_GET_NEXT_TONE;
			break;
		#ifdef USE_BEEP_FUNCTION
		case SOUND_DO_BEEP:
			tone.duration = beep_duration;
			tone.tone_period = beep_tone_period;
			new_state = SOUND_APPLY_TONE;
			p_melody = NULL;				// Beeper mode
			break; 
		#endif
		case SOUND_PLAY:
			if (--note_time_counter == 0)
				new_state = SOUND_GET_NEXT_TONE;
			break;
		case SOUND_GET_NEXT_TONE:
			#ifdef USE_BEEP_FUNCTION
			if (p_melody != NULL)			// If driver is playing melody, not beeping
			{
			#endif
				eeprom_read_block(&tone,p_melody++,sizeof(tone_t));	
				new_state = SOUND_APPLY_TONE;
			#ifdef USE_BEEP_FUNCTION
			}
			else
			{
				new_state = SOUND_OFF;
			}
			#endif
			break;
		case SOUND_APPLY_TONE:
			if (tone.duration == 0)
			{
				// Finished
				new_state = SOUND_OFF;
			}
			else
			{
				// Setup period
				if (tone.tone_period != 0)
				{
					// Timer runs at 250kHz (T = 4us), tone_period is set in units of 8us
					// Output toggles on compare match
					OCR1A = tone.tone_period - 1;
					TCNT1 = 0;
					// Toggle OCR1A on compare match
					TCCR1A |= (1<<COM1A0);
				}
				else
				{
					// Disable OCR1A output
					TCCR1A &= ~(1<<COM1A0 | 1<<COM1A1);
				}
				note_time_counter = (uint16_t)tone.duration * TONE_DURATION_SCALE - 2;
				new_state = SOUND_PLAY;
			}
			break;
		default:
			// Disable OCR1A output
			TCCR1A &= ~(1<<COM1A0 | 1<<COM1A1);
			break;
	}
	sound_state = new_state;
}





























