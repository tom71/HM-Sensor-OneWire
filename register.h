#ifndef _REGISTER_h
	#define _REGISTER_h

	//- load libraries -------------------------------------------------------------------------------------------------------
	#include <AS.h>                                                         // the asksin framework
	#include "hardware.h"                                                   // hardware definition
	#include <THSensor.h>
	#include "hmkey.h"

	// Register in Channel 0
	#define	REG_CHN0_BURST_RX					1							// Register 1.0 / 1.0: 0=burstRx off, otherwise on
	#define	REG_CHN0_LED_MODE					5							// Register 5.6 / 0.1: bit 6 LED mode (on/off)
	#define	REG_CHN0_PAIR_CENTRAL				10							// Register 10.0 / 3.0: 3 byte HMID
	#define	REG_CHN0_LOW_BAT_LIMIT_TH			18							// Register 18.0 / 1.0: in 1/10 volts
	#define	REG_CHN0_TRANS_DEV_TRY_MAX			20							// Register 20.0 / 1.0: 1-10, maxRetryCount
	#define	REG_CHN0_OSCCAL						35							// Register 35.0 / 1.0: 0=default, other=ATMEL:OSCCAL


	//- stage modules --------------------------------------------------------------------------------------------------------
	AS hm;                                                                  // asksin framework
	THSensor thsens;                                                        // create instance of channel module

	// some forward declarations
	extern void initTH1();
	extern void measureTH1(THSensor::s_meas *);

	/*
	 * HMID, Serial number, HM-Default-Key, Key-Index
	 */
	const uint8_t HMSerialData[] PROGMEM = {
		/* HMID */            0x58, 0x23, 0xFF,
		/* Serial number */   'X', 'M', 'S', '1', '2', '3', '4', '5', '6', '7',
		/* Default-Key */     HM_DEVICE_AES_KEY,
		/* Key-Index */       HM_DEVICE_AES_KEY_INDEX,
	};

	/*
	 * Settings of HM device
	 * firmwareVersion: The firmware version reported by the device
	 *                  Sometimes this value is important for select the related device-XML-File
	 *
	 * modelID:         Important for identification of the device.
	 *                  @See Device-XML-File /device/supported_types/type/parameter/const_value
	 *
	 * subType:         Identifier if device is a switch or a blind or a remote
	 * DevInfo:         Sometimes HM-Config-Files are referring on byte 23 for the amount of channels.
	 *                  Other bytes not known.
	 *                  23:0 0.4, means first four bit of byte 23 reflecting the amount of channels.
	 */
	const uint8_t devIdnt[] PROGMEM = {
		/* firmwareVersion 1 byte */  0x10,
		/* modelID         2 byte */  0xF2,0x01,
		/* subTypeID       1 byte */  0x70,
		/* deviceInfo      3 byte */  0x01, 0x01, 0x00,
	};

	/*
	 * Register definitions
	 * The values are addresses in relation to the start address defines in cnlTbl
	 * Register values can found in related Device-XML-File.

	 * Special register list 0: 0x0A, 0x0B, 0x0C
	 * Special register list 1: 0x08
	 *
	 * @See Defines.h
	 *
	 * @See: cnlTbl
	 */
	const uint8_t cnlAddr[] PROGMEM = {
		// List0-Register
		0x01,0x05,0x0a,0x0b,0x0c,0x12,0x14,0x23,
		// List4-Register
		0x01,0x02,
	};  // 6 byte
	// List 0: 0x01, 0x05, 0x0A, 0x0B, 0x0C, 0x12, 0x14, 0x23
	//uint8_t burstRx;         // 0x01,             startBit:0, bits:8
	//uint8_t             :6;  // 0x05              startBit:0, bits:6
	//uint8_t ledMode     :2;  // 0x05,             startBit:6, bits:2
	//uint8_t pairCentral[3];  // 0x0A, 0x0B, 0x0C, startBit:0, bits:8 (3 mal)
	//uint8_t lowBatLimit;     // 0x12,             startBit:0, bits:8
	//uint8_t transmDevTryMax; // 0x14,             startBit:0, bits:8
	//uint8_t osccal;          // 0x23              startBit:0, bits:8

	// List 4: 0x01,0x02,
	//uint8_t  peerNeedsBurst:1; // 0x01, s:0, e:1
	//uint8_t  useDHTTemp    :1; // 0x01, s:1, e:1
	//uint8_t                :6; //
	//uint8_t  tempCorr          // 0x02, max +/- 12,7°C (1/10°C)


	/*
	* Channel - List translation table
	* channel, list, startIndex, start address in EEprom, hidden
	*/
	EE::s_cnlTbl cnlTbl[] = {
		// cnl, lst, sIdx,  sLen, pAddr,  hidden
		{ 0, 0, 0x00,  8, 0x001f, 0, },
		{ 1, 4, 0x08,  2, 0x0027, 0, },		// 2 reg * 8 peers = 16 byte
	};  // 14 byte

	/*
	* Peer-Device-List-Table
	* channel, maximum allowed peers, start address in EEprom
	*/
	EE::s_peerTbl peerTbl[] = {
		// cnl, peerMax, pAddr;
		{ 1, 6, 0x0037, },					// 8 * 4 = 32 byte
	};	// 4 Byte

	/*
	* handover to AskSin lib
	*
	* TODO: Describe
	*/
	EE::s_devDef devDef = {
		1, 2, devIdnt, cnlAddr,
	};	// 6 Byte

	/*
	* module registrar
	*
	* TODO: Describe
	*/
	RG::s_modTable modTbl[1];


	/**
	* @brief First time and regular start functions
	*/
	void everyTimeStart(void) {
		/*
		* Place here everything which should be done on each start or reset of the device.
		* Typical use case are loading default values or user class configurations.
		*/

		// init the homematic framework
		hm.confButton.config(1);											// configure the config button mode
		hm.ld.set(welcome);                                                 // show something
		cnl0Change();														// initialize with values from eeprom

	    thsens.regInHM(1, 4, &hm);											// register sensor module on channel 1, with a list4 and introduce asksin instance
	    thsens.config(&initTH1, &measureTH1);								// configure the user class and handover addresses to respective functions and variables
	}

	/**
	* TODO: maybe we can delete this?
	*/
	void firstTimeStart(void) {
		/*
		* place here everything which should be done on the first start or after a complete reset of the sketch
		* typical usecase are default values which should be written into the register or peer database
		*/

		const uint8_t cnl0lst0[] = {
			0x00,0x40,0x00,0x00,0x00,0x15,0x03,0x00,
		};
		hm.ee.setList(0, 0, 0, (uint8_t *)cnl0lst0);
		
		//const uint8_t cnl1lst4[] = {
		//	0x00,0x03,0x64,0x00,0x0a,0x00,0x10,0x27,0x00,
		//};

	}
#endif

