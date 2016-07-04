/*
 * HM-Sensor-Test2.cpp
 *
 * Created: 27.12.2015 14:47:56
 * Author : Martin
 */ 

//- load library's --------------------------------------------------------------------------------------------------------
#include <Arduino.h>
#include <AS.h>																				// ask sin framework
#include <THSensor.h>
#include "register.h"																		// configuration sheet
#include "OneWire.h"
#include <avr/wdt.h>

#define SER_DBG

#define DHT_PWR		9																		// power for DHT22
#define OW_PIN		5																		// this pin DS18B20 is connected to

OneWire OW(OW_PIN);
waitTimer thTimer;
int16_t celsius;
uint8_t transmitDevTryMax;

void serialEvent();


//- arduino functions -----------------------------------------------------------------------------------------------------
void setup() {

	// - Hardware setup ---------------------------------------
	// - everything off ---------------------------------------
	
	wdt_disable();																			// clear WDRF to avoid endless resets after WDT reset
	MCUSR &= ~(1<<WDRF);																	// stop all WDT activities
	WDTCSR |= (1<<WDCE) | (1<<WDE);
	WDTCSR = 0x00;

	EIMSK = 0;																				// disable external interrupts
	ADCSRA = 0;																				// ADC off
	power_all_disable();																	// and everything else
	
	DDRB = DDRC = DDRD = 0x00;																// everything as input
	PORTB = PORTC = PORTD = 0x00;															// pullup's off

	// todo: timer0 and SPI should enable internally
	power_timer0_enable();
	power_spi_enable();																		// enable only needed functions


	// enable only what is really needed

	#ifdef SER_DBG																			// some debug
		dbgStart();																			// serial setup
		dbg << F("HB_UW_Sen_TH_Pn\n");
		dbg << F(LIB_VERSION_STRING);
		_delay_ms (10);																		// ...and some information
	#endif
	
	// - AskSin related ---------------------------------------
	hm.init();																				// init the asksin framework
	sei();																					// enable interrupts

	// - user related -----------------------------------------
	#ifdef SER_DBG
		dbg << F("HMID: ") << _HEX(HMID,3) << F(", MAID: ") << _HEX(MAID,3) << F("\n\n");	// some debug
	#endif
}


//- user functions --------------------------------------------------------------------------------------------------------
void initTH1() {																			// init the sensor
	
	pinMode(DHT_PWR, OUTPUT);
	pinMode(OW_PIN, INPUT_PULLUP);
	
	#ifdef SER_DBG
		dbg << "init th1\n";
	#endif
}

void cnl0Change(void) {
	
	// set lowBat threshold
	hm.bt.set(hm.ee.getRegAddr(0,0,0,REG_CHN0_LOW_BAT_LIMIT_TH)*10, BATTERY_MEAS_INTERVAL);

	// set OSCCAL frequency
	if (uint8_t oscCal = hm.ee.getRegAddr(0,0,0,REG_CHN0_OSCCAL)) {
		dbg << F("will set OSCCAL: old=") << OSCCAL << F(", new=") << oscCal << F("\n");
		// Attention: your controller my have other factory calibration !!
		// If you are unsure about the internal RC-frequency of your chip then comment out setting OSCCAL -
		// use factory default instead!
		// my chip: 1kHz - 8A=994Hz, 8B=998,4Hz, 8C=1001,6Hz, 8E=1010Hz
		// frequency measured with help of millis-ISR (toggling LED port and measuring frequency on it)

		OSCCAL = oscCal;
	} else {
		dbg << F("will set default OSCCAL: ") << getDefaultOSCCAL() << F("\n");
		OSCCAL = getDefaultOSCCAL();
	}
	calibrateWatchdog();

	// if burstRx is set ...
	if (hm.ee.getRegAddr(0,0,0,REG_CHN0_BURST_RX)) {
		dbg << F("PM=onradio\n");
		hm.pw.setMode(POWER_MODE_WAKEUP_ONRADIO);											// set mode to wakeup on burst
	} else {	// no burstRx wanted
		// if no peer registered ==> then 8sec sleep is possible
//		if (hm.ee.countFreeSlots(1) == hm.ee.getPeerSlots(1)) {
			dbg << F("PM=8000ms\n");
			hm.pw.setMode(POWER_MODE_WAKEUP_8000MS);										// set mode to awake every 8 secs
//		} else {
//			dbg << F("peers! setting burstRx=1\n");
//			uint8_t arr[] = {REG_CHN0_BURST_RX, 0x01};
//			hm.ee.setListArray(0,0,0,sizeof(arr),arr);										// set burstRx - is required when some peers are registered
//			hm.pw.setMode(POWER_MODE_WAKEUP_ONRADIO);										// set power management mode
//		}
	}

	// fetch transmitDevTryMax
	if ((transmitDevTryMax = hm.ee.getRegAddr(0,0,0,REG_CHN0_TRANS_DEV_TRY_MAX)) > 10)
		transmitDevTryMax = 10;
	else if (transmitDevTryMax < 1)
		transmitDevTryMax = 1;
}

// this is called when HM wants to send measured values to peers or master
// due to asynchronous measurement we simply can take the values very quick from variables
void measureTH1(THSensor::s_meas *ptr) {
	int16_t t;
	
	#ifdef SER_DBG
		//dbg << "msTH1 DS-t: " << celsius << ' ' << _TIME << '\n';
	#endif
	// take temp value from DS18B20
	t = celsius / 10;
	((uint8_t *)&(ptr->temp))[0] = ((t >> 8) & 0x7F);										// battery status is added later
	((uint8_t *)&(ptr->temp))[1] = t & 0xFF;
	
	
	// fetch battery voltage
	t = hm.bt.getVolts();
	((uint8_t *)&(ptr->bat))[0] = t >> 8;
	((uint8_t *)&(ptr->bat))[1] = t & 0xFF;
}

// this is called regularly - real measurement is done here
void measure() {
	enum mState {mInit, mWait, mPwrOn, mStartDS};
	static mState state = mWait;

	if (!thTimer.done())
		return;
		
	if (state == mInit) {																	// wait some time till next measurement
		thTimer.set(88000);																	// measurement every 90 secs
		state = mWait;
	}
	else if (state == mWait) {																// power on sensor and wait 1 sec
		thTimer.set(1000);
		digitalWrite(DHT_PWR, 1);															// power on here
		state = mPwrOn;
		#ifdef SER_DBG
			//dbg << "power on Sensor" << ' ' << _TIME << '\n';
		#endif
	}
	else if (state == mPwrOn) {																// now start measurement on DS18B20 and wait another second
		thTimer.set(1000);
		uint8_t rc = OW.reset();															// attention - OW device get ready to communicate!
		OW.skip();																			// skip rom selection - we have only one device attached
		OW.write(0x44);																		// start conversion
		#ifdef SER_DBG
			//dbg << "rc: " << rc << _TIME << '\n';
		#endif
		state = mStartDS;
	}
	else if (state == mStartDS)	{															// get results here and switch off sensor
		
		OW.reset();																			// attention - get ready to read result from DS18B20
		OW.skip();																			// no rom selection
		OW.write(0xBE);																		// read temp from scratchpad
		celsius = ((uint32_t)(OW.read() | (OW.read() << 8)) * 100) >> 4;					// we need only first two bytes from scratchpad
	
		#ifdef SER_DBG
			dbg << "DS-t: " << celsius << ' ' << _TIME << '\n';
		#endif

		digitalWrite(DHT_PWR, 0);															// power off DHT22
		#ifdef SER_DBG
			//dbg << "power off Sensor" << ' ' << _TIME << '\n';
			_delay_ms(10);
		#endif
		state = mInit;
	}
}


int main(void)
{
	// Initialize all functions and pins
	setup();
	
    /* Replace with your application code */
    while (1) 
    {
			// - AskSin related ---------------------------------------
			hm.poll();																		// poll the homematic main loop

			// - user related -----------------------------------------
			serialEvent();
			measure();
    }
}


//- predefined functions --------------------------------------------------------------------------------------------------
void serialEvent() {
	#ifdef SER_DBG
	
	static uint8_t i = 0;																	// it is a high byte next time
	while (Serial.available()) {
		uint8_t inChar = (uint8_t)Serial.read();											// read a byte
		if (inChar == '\n') {																// send to receive routine
			i = 0;
			hm.sn.active = 1;
		}
		
		if      ((inChar>96) && (inChar<103)) inChar-=87;									// a - f
		else if ((inChar>64) && (inChar<71))  inChar-=55;									// A - F
		else if ((inChar>47) && (inChar<58))  inChar-=48;									// 0 - 9
		else continue;
		
		if (i % 2 == 0) hm.sn.buf[i/2] = inChar << 4;										// high byte
		else hm.sn.buf[i/2] |= inChar;														// low byte
		
		i++;
	}
	#endif
}

