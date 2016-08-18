#include <EEPROM.h>
#include <OctoWS2811.h>
#include <Wire.h>
#include <SPI.h>
#include <FlexCAN.h>
#include <XPT2046_Touchscreen.h>
#include <RamMonitor.h>

#include <ELUtilities.h>
#include <ELSunRiseAndSet.h>
#include <ELRealTime.h>
#include <ELOutput.h>
#include <ELModule.h>
#include <ELLuminositySensor.h>
#include <ELInternetDevice_ESP8266.h>
#include <ELInternet.h>
#include <ELDigitalIO.h>
#include <ELConfig.h>
#include <ELCommand.h>
#include <ELAssert.h>
#include <EL.h>

void
SetupIcicleModule(
	void);

void 
setup(
	void)
{
	//Serial.begin(115200);

	//WaitForSerialPort();

	Serial.printf("free=%lu\n", GetFreeMemory());

	CModule_SysMsgSerialHandler::Include();
	CModule_SerialCmdHandler::Include();
	CModule_SysMsgCmdHandler::Include();

	SetupIcicleModule();

	CModule::SetupAll("v0.1", false);

	Serial.printf("free=%lu\n", GetFreeMemory());
}

void
loop(
	void)
{
	CModule::LoopAll();
}
