/*
 * control.c
 *
 * Created: 15.04.2013 21:26:33
 *  Author: Avega
 */ 


#include <util/crc16.h>
#include "compilers.h"

#include "control.h"
#include "buttons.h"
#include "power_control.h"
#include "led_indic.h"
#include "leds.h"
#include "systimer.h"	
#include "adc.h"
#include "pid_controller.h"




// Global variables - main system control
// #ifdef USE_EEPROM_CRC
//   Default values must reside in the FLASH memory - for the case when EEPROM is not programmed at all
//   At the very first start after programming MCU default values from FLASH memory are copied to EEPROM and
//   both calibration and global params get protected by 8-bit CRC. EEPROM error message is displayed.
// #endif
EEMEM gParams_t eeGlobalParams = 
{
	.setup_temp_value 	= 50, 
	.rollCycleSet 		= 10,
	.sound_enable 		= 1,
	.power_off_timeout 	= 30,
};

EEMEM cParams_t eeCalibrationParams = 
{
	.cpoint1 			= 22,		
	.cpoint1_adc 		= 195,	
	.cpoint2 			= 120,
	.cpoint2_adc 		= 401
};

EEMEM uint8_t ee_gParamsCRC = 0xFF;		// Used only when USE_EEPROM_CRC is defined
EEMEM uint8_t ee_cParamsCRC = 0xFF;		// Always declared to avoid changes in EEPROM data map

#ifdef USE_EEPROM_CRC
const PROGMEM gParams_t pmGlobalDefaults =
{
	.setup_temp_value 	= 50,
	.rollCycleSet 		= 10,
	.sound_enable 		= 1,
	.power_off_timeout 	= 30
};

const PROGMEM cParams_t pmCalibrationDefaults = 
{
	.cpoint1 			= 22,	// Default Celsius for first point
	.cpoint1_adc 		= 195,	// Normalized ADC for first point
	.cpoint2 			= 120,
	.cpoint2_adc 		= 401
};
#endif

gParams_t p;		// Global params which are saved to and restored from EEPROM
					// The global parameters are saved only when device is disconnected from the AC line 
cParams_t cp;		// Calibration params are saved only after calibration of any of two calibration points



uint8_t heaterState = 0;				// Global heater flags
uint8_t autoPowerOffState = 0;			// Global flag, active when auto power off mode is active.
										// Flag is set and cleared in menu module.
static uint8_t setPoint_prev = MIN_SET_TEMP + 1;	// Used for monitoring temperature setup changes
													// Init with value that can never be set

//------- Debug --------//
uint8_t 	dbg_SetPointCelsius;	// Temperature setting, Celsius degree
uint16_t 	dbg_SetPointPID;		// Temperature setting, PID input
uint8_t 	dbg_RealTempCelsius;	// Real temperature, Celsius degree
uint16_t 	dbg_RealTempPID;		// Real temperature, PID input



// Function to control motor rotation
void processRollControl(void)
{	
	uint8_t beepState = 0;
	static uint8_t force_rotate = 0;
	
	// Process auto power off control
	if (autoPowerOffState & AUTO_POFF_ACTIVE)
	{
		stopCycleRolling(RESET_POINTS);	
		if ( (adc_status & (SENSOR_ERROR_NO_PRESENT | SENSOR_ERROR_SHORTED)) ||
			 (adc_celsius > (POFF_MOTOR_TRESHOLD + POFF_MOTOR_HYST)) )
		{
			// If there is any sensor error, or
			// if temperature is greater than (threshold + some hysteresis) 
			if (!(rollState & (ROLL_FWD | ROLL_REV)))
			{
				// If motor is stopped
				setMotorDirection(ROLL_FWD);		// Start rotating in order to prevent rollers damage
				force_rotate = 0;					// Do not start motor on power off exit
			}
		}
		else if (adc_celsius <= POFF_MOTOR_TRESHOLD)
		{
			if (rollState & (ROLL_FWD | ROLL_REV))
			{	
				// If temperature is below threshold and motor is rotating
				setMotorDirection(0);			// Stop the motor
				force_rotate = ROLL_FWD;		// Start motor on power-off mode exit
			}
		}
	}
	else
	{
		// Control direction by buttons
		if ((raw_button_state & (BD_ROTFWD | BD_ROTREV)) == (BD_ROTFWD | BD_ROTREV))
		{
			// Both Forward and Reverse buttons are pressed - stop
			// Attention - stopping motor when rollers are hot can possibly damage them
			setMotorDirection(0);
		}
		else if (button_action_down & BD_ROTFWD)
		{
			setMotorDirection(ROLL_FWD);	
			beepState |= 0x01;			// pressed FWD button
		}		
		else if (button_action_down & BD_ROTREV)
		{
			setMotorDirection(ROLL_REV);
			beepState |= 0x02;			// pressed REV button
		}		
		else if (button_action_long & BD_CYCLE)
		{
			stopCycleRolling(RESET_POINTS);		// Reset points and disable CYCLE mode (if was enabled)
			beepState |= 0x08;					// reset of points by long pressing of ROLL button
		}
		else if (force_rotate)
		{
			// Auto power off mode was exited by pressing some other button, not direction buttons
			// Start roll, but do not beep in this case
			setMotorDirection(force_rotate);
		}
		force_rotate = 0;		// First normal pass will clear 
			
		if (button_action_up_short & BD_CYCLE)
		{
			if (rollState & ROLL_CYCLE)
			{
				stopCycleRolling(DO_NOT_RESET_POINTS);
				beepState |= 0x20;		// stopped cycle
			}
			else if (startCycleRolling())
			{
				beepState |= 0x10;		// started cycle
			}
			else
			{
				beepState |= 0x40;		// failed to start cycle
			}			
		}		
		
		// ROLL_DIR_CHANGED is set only when direction is changed automatically,
		// not when changed by calling setMotorDirection() function
		if (rollState & ROLL_DIR_CHANGED)
		{
			rollState &= ~ROLL_DIR_CHANGED;
			beepState |= 0x04;	
		}
		if (rollState & CYCLE_ROLL_DONE)
		{
			rollState &= ~CYCLE_ROLL_DONE;
			beepState |= 0x80;	
		}		
			
		//-----------//
			
		if (beepState & 0x80)		// Roll cycle done
		{
			Sound_Play(m_siren4);	
		}		
		else if (beepState & 0x40)	// Roll cycle start fail
		{
			Sound_Play(m_beep_500Hz_40ms);	
		} 
		else if (beepState & 0x08)	// Reset points
		{
			Sound_Play(m_beep_800Hz_40ms);	
		}							// Other
		else if ( beepState & (0x01 | 0x02 | 0x10 | 0x20 | 0x04) )
		{
			Sound_Play(m_beep_1000Hz_40ms);	
		}			
			
	}

	//----- LED indication ------//
	clearExtraLeds(LED_ROTFWD | LED_ROTREV);
	if (rollState & ROLL_FWD)
		setExtraLeds(LED_ROTFWD);
	else if (rollState & ROLL_REV)
		setExtraLeds(LED_ROTREV);
}




void processHeaterControl(void)
{
	uint16_t set_value_adc;	
	uint16_t setPoint;
	uint16_t processValue;
	uint16_t pid_output = 0;
	
	// Process heater ON/OFF control by button
	if (button_state & BS_HEATCTRL)
	{
		heaterState ^= HEATER_ENABLED;
		// Force update heater power
		sys_timers.flags |= UPDATE_PID;		// Not very good approach if UPDATE_PID flag is used outside this function
	}
	
	// Process PID controller reset
	if (button_state & BL_HEATCTRL)
	{
		heaterState |= RESET_PID;
		// Force update heater power
		sys_timers.flags |= UPDATE_PID;
	}
	else
	{
		heaterState &= ~RESET_PID;
	}
	
	// Process auto power off control and sensor errors
	if ((autoPowerOffState & AUTO_POFF_ACTIVE) || (adc_status & (SENSOR_ERROR_NO_PRESENT | SENSOR_ERROR_SHORTED)))
	{
		heaterState &= ~HEATER_ENABLED;
	}	

	// Update integrator limits if setpoint is changed
	if (heaterState & SETPOINT_CHANGED)
	{
		setPIDIntegratorLimit(p.setup_temp_value);
		// Force update heater power
		sys_timers.flags |= UPDATE_PID;
	}

	
	// Check if heater control should be updated
	// PID call interval is a multiple of Celsius update interval. 
	if (sys_timers.flags & UPDATE_PID)
	{
		// Convert temperature setup to equal ADC value
		set_value_adc = conv_Celsius_to_ADC(p.setup_temp_value);					

		// PID input: 1 count ~ 0.125 Celsius degree (see adc.c)
		setPoint = set_value_adc * ADC_OVERSAMPLE_RATE;		
		processValue = adc_filtered;
		
		// Process PID
		// If heater is disabled, output will be 0
		pid_output = processPID(setPoint, processValue, heaterState);		
		
		// If unregulated mode is selected, override PID output 
		// This mode must be used with care for calibration only
		if ((heaterState & HEATER_ENABLED) && (p.setup_temp_value >= MAX_SET_TEMP))
			pid_output = HEATER_MAX_POWER;		
			
		// Set new heater power value	
		setHeaterPower(pid_output);	
		
		
		//------- Debug --------//		
		// PID input:
		dbg_SetPointCelsius = (heaterState & HEATER_ENABLED) ? p.setup_temp_value : 0;
		dbg_SetPointPID = (heaterState & HEATER_ENABLED) ? setPoint : 0;
		dbg_RealTempCelsius = adc_filtered;
		dbg_RealTempPID = processValue;
		// PID output:
		// updated in PID controller function
		
	}	
		
	
	//----- LED indication ------//
	if (heaterState & HEATER_ENABLED)
		setExtraLeds(LED_HEATER);
	else
		clearExtraLeds(LED_HEATER);
	
}


// Function to monitor heater events
void processHeaterEvents(void)
{
	// Generate temperature changed event
	if (setPoint_prev != p.setup_temp_value)
	{
		heaterState |= SETPOINT_CHANGED;
		setPoint_prev = p.setup_temp_value;
	}
	else
	{
		heaterState &= ~SETPOINT_CHANGED;
	}
}


// Function to process all heater alerts:
//	- sensor errors
//	- getting close to desired temperature
//	- continuous heating when disabled
void processHeaterAlerts(void)
{
	static uint8_t tempAlertRange = TEMP_ALERT_RANGE;
	static int16_t refCapturedTemp = INT16_MAX;
	int16_t currentTemperature = adc_celsius;
	
	// ADC sensor errors alert
	if (adc_status & (SENSOR_ERROR_NO_PRESENT | SENSOR_ERROR_SHORTED))
	{
		if (sys_timers.flags & EXPIRED_10SEC)
		{
			// Enable beeper output regardless of menu setting
			Sound_OverrideDisable();
			Sound_Play(m_siren3);
		}
		
		// No more alerts should be processed
		return;
	}
	
	
	// Indicate reaching of desired temperature
	if ( (currentTemperature > p.setup_temp_value - tempAlertRange) && (currentTemperature < p.setup_temp_value + tempAlertRange) )
	{
		if ((tempAlertRange == TEMP_ALERT_RANGE) && (heaterState & HEATER_ENABLED))
		{
			Sound_Play(m_siren1);
		}
		// Add some hysteresis
		tempAlertRange = TEMP_ALERT_RANGE + TEMP_ALERT_HYST;
	}			
	else
	{
		tempAlertRange = TEMP_ALERT_RANGE;
	}

	
	// Growing temperature with heater disabled alert 
	// This alert is done regardless of global sound enable
	// A false triggering may occur if ambient temperature grows.
	// To reset the warning in this case just turn on heater for at least one systimer tick (50ms)
	// If heater is enabled, it is implied that user controls heating process
	if (heaterState & (HEATER_ENABLED | CALIBRATION_ACTIVE))
	{
		// Heater enabled, just save current temperature as reference
		// Same if calibration in progress, even if heater is disabled
		refCapturedTemp = currentTemperature;
	}
	else if (sys_timers.flags & EXPIRED_10SEC)
	{
		// Heater disabled. If temperature is falling,
		if (currentTemperature < refCapturedTemp)
		{
			// save current temperature as reference
			refCapturedTemp = currentTemperature;
		}
		else
		{
			// Heater is disabled. If current temperature is higher than reference + some safe interval,
			// there might be a hardware failure - short circuit, etc
			// BEEP like a devil  }:-(
			if (currentTemperature >= (refCapturedTemp + SAFE_TEMP_INTERVAL))
			{
				// Enable beeper output regardless of menu setting
				Sound_OverrideDisable();
				Sound_Play(m_siren2);
			}
		}
	}
	

}


static uint8_t getDataCRC(void *p,uint8_t byte_count)
{
	uint8_t crc_byte = 0;
	while(byte_count--)
	{
		// Using ibutton CRC function for reason of 8-bit output CRC
		crc_byte = _crc_ibutton_update (crc_byte, *(uint8_t*)p++);	
	}
	return crc_byte;
}


uint8_t restoreGlobalParams(void)
{	
	uint8_t defaults_used = 0;
	
	// Restore global parameters - temperature setting, sound enable, etc.
	eeprom_read_block(&p,&eeGlobalParams,sizeof(gParams_t));
	// Restore ADC calibration parameters
	eeprom_read_block(&cp,&eeCalibrationParams,sizeof(cParams_t));
	
	#ifdef USE_EEPROM_CRC
	uint8_t crc_byte;
	uint8_t temp8u;
	
	//------- Check global params -------//
	crc_byte = getDataCRC(&p,sizeof(gParams_t));
	temp8u = eeprom_read_byte(&ee_gParamsCRC);
	// Restore global defaults if corrupted
	if (temp8u != crc_byte)
	{
		//PGM_read_block(&p,&pmGlobalDefaults,sizeof(gParams_t));
		memcpy_P(&p,&pmGlobalDefaults,sizeof(gParams_t));
		// Save restored default values with correct CRC
		saveGlobalParamsToEEPROM();
		defaults_used |= 0x01;
	}
	
	//----- Check calibration params -----//
	crc_byte = getDataCRC(&cp,sizeof(cParams_t));
	temp8u = eeprom_read_byte(&ee_cParamsCRC);
	// Restore calibration defaults if corrupted
	if (temp8u != crc_byte)
	{
		//PGM_read_block(&cp,&pmCalibrationDefaults,sizeof(cParams_t));
		memcpy_P(&cp,&pmCalibrationDefaults,sizeof(cParams_t));
		// Save restored default values with correct CRC
		saveCalibrationToEEPROM();
		defaults_used |= 0x02;	
	}
	#endif
	
	return defaults_used;
}


void saveCalibrationToEEPROM(void)
{
	// Calibration parameters normally are only saved after calibrating 
	eeprom_update_block(&cp,&eeCalibrationParams,sizeof(cParams_t));	
	#ifdef USE_EEPROM_CRC
	uint8_t new_crc_byte = getDataCRC(&cp,sizeof(cParams_t));
	eeprom_update_byte(&ee_cParamsCRC,new_crc_byte);
	#endif
}

void saveGlobalParamsToEEPROM(void)
{
	// Save global parameters to EEPROM
	// eeprom_update_block() updates only bytes that were changed
	eeprom_update_block(&p,&eeGlobalParams,sizeof(gParams_t));
	#ifdef USE_EEPROM_CRC
	uint8_t new_crc_byte = getDataCRC(&p,sizeof(gParams_t));
	eeprom_update_byte(&ee_gParamsCRC,new_crc_byte);
	#endif
}

void exitPowerOff(void)
{
	// Put all ports into HI-Z
	DDRB = 0x00;
	PORTB = 0x00;
	DDRC = 0x00;
	PORTC = 0x00;
	DDRD = 0x00;
	PORTD = 0x00;
	
	// Disable all interrupts
	cli();
	
	saveGlobalParamsToEEPROM();
	
	// DIE!
	while(1);
}



