/*
	Author: Brent Pease (embeddedlibraryfeedback@gmail.com)

	The MIT License (MIT)

	Copyright (c) 2015-FOREVER Brent Pease

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in all
	copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
	SOFTWARE.
*/

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
	Serial.begin(115200);

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
