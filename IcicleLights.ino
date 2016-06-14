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

RamMonitor	gRamMonitor;

void
SetupIcicleModule(
	void);

void 
setup(
	void)
{
	Serial.begin(115200);

	//WaitForSerialPort();

	new CModule_Internet();
	new CModule_RealTime();
	new CModule_SerialCmdHandler();
	new CModule_SysMsgCmdHandler();

	SetupIcicleModule();

	Serial.printf("free=%lu\n", gRamMonitor.unallocated());

	CModule::SetupAll("v0.1", false);
}

void
loop(
	void)
{
	CModule::LoopAll();
}
