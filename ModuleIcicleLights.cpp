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

/*
	ABOUT

*/

#if !defined(WIN32)
	#include <OctoWS2811.h>
#endif

#include <EL.h>
#include <ELAssert.h>
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
#include <ELOutdoorLightingControl.h>
#include <ELRemoteLogging.h>

enum
{
	eIciclesPerStrip = 108,
	eLEDsPerIcicle = 5,
	eLEDsPerStrip = eIciclesPerStrip * eLEDsPerIcicle,
	eIcicleTotal = eIciclesPerStrip * 8,

	eToggleButtonPin = 9,
	eTransformerRelayPin = 17,
	eMotionSensorPin = 22,
	eESP8266ResetPint = 23,

	eUpdateTimeUS = 30000,

	eRenderMode_Static = 0,
	eRenderMode_Dynamic = 1,
	eRenderMode_Rain = 2,
};

DMAMEM int		gIcicleLEDDisplayMemory[eLEDsPerStrip * 6];

class CModule_Icicle : public CModule, public ICmdHandler, public IOutdoorLightingInterface, public IInternetHandler
{
public:
	
	MModule_Declaration(CModule_Icicle)

private:
	
	CModule_Icicle(
		)
	:
		CModule(
			sizeof(settings),
			3,
			&settings,
			eUpdateTimeUS),
		leds(eLEDsPerStrip, gIcicleLEDDisplayMemory, NULL, WS2811_RGB)
	{
		IInternetDevice*		internetDevice = CModule_ESP8266::Include(3, &Serial1, eESP8266ResetPint);
		IRealTimeDataProvider*	ds3234Provider = CreateDS3234Provider(10);
		CModule_Loggly*			loggly = CModule_Loggly::Include("pergola", "logs-01.loggly.com", "/inputs/568b321d-0d6f-47d3-ac34-4a36f4125612");
		
		CModule_RealTime::Include();
		CModule_Internet::Include();
		CModule_Command::Include();
		CModule_OutdoorLightingControl::Include(this, eMotionSensorPin, eTransformerRelayPin, eToggleButtonPin, NULL);
		
		AddSysMsgHandler(loggly);
		gInternetModule->Configure(internetDevice);
		gRealTime->Configure(ds3234Provider, 24 * 60 * 60);

		updateCumulatorUS = 0;
		ledsOn = false;
	}

	virtual void
	Setup(
		void)
	{
		// Instantiate the wireless networking device and configure it to server pages
		gInternetModule->CommandServer_Start(8080);
		MInternetRegisterFrontPage(CModule_Icicle::CommandHomePageHandler);

		for(int i = 0; i < eIcicleTotal; ++i)
		{
			icicles[i].SetInitialState(this);
		}

		memset(gIcicleLEDDisplayMemory, 0, sizeof(gIcicleLEDDisplayMemory));

		MCommandRegister("grow_set", CModule_Icicle::GrowDistributionSet, "[mean] [std dev]: Set grow rate distribution");
		MCommandRegister("depth_set", CModule_Icicle::PeekDepthDistributionSet, "[mean] [std dev]: Set peek depth distribution");
		MCommandRegister("peekduration_set", CModule_Icicle::PeekDepthLifeDistributionSet, "[mean] [std dev]: Set the duration distribution at the peek depth");
		MCommandRegister("driptime_set", CModule_Icicle::DripStartTimeDistributionSet, "[mean] [std dev]: Set the drip start distribution");
		MCommandRegister("driprate_set", CModule_Icicle::DripRateSet, "[pre rate] [post rate]: Set the drip rate");
		MCommandRegister("growcolor_set", CModule_Icicle::GrowDownColorSet, "[r] [g] [b]: Set the grow down color range 0.0 -> 1.0");
		MCommandRegister("recedecolor_set", CModule_Icicle::RecedeUpColorSet, "[r] [g] [b]: Set the recede up color range 0.0 -> 1.0");
		MCommandRegister("staticcolor_set", CModule_Icicle::StaticColorSet, "[r] [g] [b]: Set the static color range 0.0 -> 1.0");
		MCommandRegister("staticintensity_set", CModule_Icicle::StaticIntensitySet, "[intensity]: Set the static intensity 0.0 -> 1.0");
		MCommandRegister("rendermode_set", CModule_Icicle::RenderModeSet, "[dynamic | static]: Set the render mode");

		leds.begin();

		for(int i = 0; i < eLEDsPerStrip * 8; ++i)
		{
			leds.setPixel(i, 0, 0, 0);
		}

		leds.show();
	}

	void
	CommandHomePageHandler(
		IOutputDirector*	inOutput)
	{
		// Send html via in Output to add to the command server home page served to clients

		inOutput->printf("<table border=\"1\">");
		inOutput->printf("<tr><th>Parameter</th><th>Value</th></tr>");

		// add grow rate
		inOutput->printf("<tr><td>GrowRate</td><td>%2.2f %2.2f</td></tr>", settings.meanGrowRateLEDsPerSec, settings.stdGrowRateLEDsPerSec);

		// add peek depth
		inOutput->printf("<tr><td>PeekDepth</td><td>%2.2f %2.2f</td></tr>", settings.meanPeekDepth, settings.stdPeekDepth);

		// add peek depth lifetime
		inOutput->printf("<tr><td>PeekDepthLifetime</td><td>%2.2f %2.2f</td></tr>", settings.meanPeekDepthLifetimeSec, settings.stdPeekDepthLifetimeSec);

		// add drip time
		inOutput->printf("<tr><td>DripTime</td><td>%2.2f %2.2f</td></tr>", settings.meanIcicleStartDripTime, settings.stdIcicleStartDripTime);

		// add drip rate PreIcicleEnd
		inOutput->printf("<tr><td>DripRate PreIcicleEnd</td><td>%2.2f</td></tr>", settings.waterDripRatePreLEDsPerSec);

		// add drip rate PostIcicleEnd
		inOutput->printf("<tr><td>DripRate PostIcicleEnd</td><td>%2.2f</td></tr>", settings.waterDripRatePostLEDsPerTick);

		// add grow down color
		inOutput->printf("<tr><td>GrowDown Color</td><td>r:%02d g:%02d b:%02d</td></tr>", settings.growDownColorR, settings.growDownColorG, settings.growDownColorB);

		// add recede up color
		inOutput->printf("<tr><td>RecedeUp Color</td><td>r:%02d g:%02d b:%02d</td></tr>", settings.recedeUpColorR, settings.recedeUpColorG, settings.recedeUpColorB);

		// add water drip color
		inOutput->printf("<tr><td>WaterDrip Color</td><td>r:%02d g:%02d b:%02d</td></tr>", settings.waterDripR, settings.waterDripG, settings.waterDripB);

		// add static color
		inOutput->printf("<tr><td>Static Color</td><td>r:%02d g:%02d b:%02d</td></tr>", settings.staticR, settings.staticG, settings.staticB);

		// add static intensity
		inOutput->printf("<tr><td>Static Intensity</td><td>%1.2f</td></tr>", settings.staticIntensity);

		inOutput->printf("</table>");
	}

	virtual void
	LEDStateChange(
		bool	inLEDsOn)
	{
		ledsOn = inLEDsOn;
	}

	virtual void
	MotionSensorStateChange(
		bool	inMotionSensorTriggered)
	{

	}

	virtual void
	PushButtonStateChange(
		int	inToggleCount)
	{

	}

	virtual void
	TimeOfDayChange(
		int	inTimeOfDay)
	{

	}

	uint8_t
	GrowDistributionSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);

		settings.meanGrowRateLEDsPerSec = (float)atof(inArgV[1]);
		settings.stdGrowRateLEDsPerSec = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	PeekDepthDistributionSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);

		settings.meanPeekDepth = (float)atof(inArgV[1]);
		settings.stdPeekDepth = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t 
	PeekDepthLifeDistributionSet(
		IOutputDirector* inOutput,
		int inArgC,
		char const* inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);

		settings.meanPeekDepthLifetimeSec = (float)atof(inArgV[1]);
		settings.stdPeekDepthLifetimeSec = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	DripStartTimeDistributionSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);

		settings.meanIcicleStartDripTime = (float)atof(inArgV[1]);
		settings.stdIcicleStartDripTime = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	DripRateSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);

		settings.waterDripRatePreLEDsPerSec = (float)atof(inArgV[1]);
		settings.waterDripRatePostLEDsPerTick = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	GrowDownColorSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 4, eCmd_Failed);
		
		settings.growDownColorR = (uint8_t)(atof(inArgV[1]) * 255.0);
		settings.growDownColorG = (uint8_t)(atof(inArgV[2]) * 255.0);
		settings.growDownColorB = (uint8_t)(atof(inArgV[3]) * 255.0);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	RecedeUpColorSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 4, eCmd_Failed);
		
		settings.recedeUpColorR = (uint8_t)(atof(inArgV[1]) * 255.0);
		settings.recedeUpColorG = (uint8_t)(atof(inArgV[2]) * 255.0);
		settings.recedeUpColorB = (uint8_t)(atof(inArgV[3]) * 255.0);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	StaticColorSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 4, eCmd_Failed);
		
		settings.staticR = (uint8_t)(atof(inArgV[1]) * 255.0);
		settings.staticG = (uint8_t)(atof(inArgV[2]) * 255.0);
		settings.staticB = (uint8_t)(atof(inArgV[3]) * 255.0);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	StaticIntensitySet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 2, eCmd_Failed);
		
		settings.staticIntensity = (float)atof(inArgV[1]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	RenderModeSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 2, eCmd_Failed);

		if(strcmp(inArgV[1], "dynamic") == 0)
		{
			settings.renderMode = eRenderMode_Dynamic;
		}
		else if(strcmp(inArgV[1], "static") == 0)
		{
			settings.renderMode = eRenderMode_Static;
		}
		else if(strcmp(inArgV[1], "rain") == 0)
		{
			settings.renderMode = eRenderMode_Rain;
		}

		EEPROMSave();

		return eCmd_Succeeded;
	}

	virtual void
	EEPROMInitialize(
		void)
	{
		settings.meanGrowRateLEDsPerSec = 0.05f;
		settings.stdGrowRateLEDsPerSec = 0.025f;
		settings.meanPeekDepth = 4.0f;
		settings.stdPeekDepth = 4.0f;
		settings.meanPeekDepthLifetimeSec = 10.0f;
		settings.stdPeekDepthLifetimeSec = 2.0f;
		settings.meanIcicleStartDripTime = 60.0f * 5.0f;
		settings.stdIcicleStartDripTime = 60.0f * 2.0f;
		settings.waterDripRatePreLEDsPerSec = 2.0f;
		settings.waterDripRatePostLEDsPerTick = 4.0f;
		settings.staticIntensity = 1.0f;
		settings.growDownColorR = 64;
		settings.growDownColorG = 64;
		settings.growDownColorB = 250;
		settings.recedeUpColorR = 250;
		settings.recedeUpColorG = 128;
		settings.recedeUpColorB = 200;
		settings.waterDripR = 0;
		settings.waterDripG = 0;
		settings.waterDripB = 0xFF;
		settings.staticR = 0xFF;
		settings.staticG = 0xFF;
		settings.staticB = 0x80;
		settings.renderMode = eRenderMode_Dynamic;
	}

	virtual void
	Update(
		uint32_t	inDeltaUS)
	{
		if(ledsOn == false)
		{
			for(int i = 0; i < eLEDsPerStrip * 8; ++i)
			{
				leds.setPixel(i, 0, 0, 0);
			}
			leds.show();
		}
		else
		{
			if(settings.renderMode == eRenderMode_Dynamic)
			{
				UpdateModel(inDeltaUS);
				RenderDynamic();
			}
			else if(settings.renderMode == eRenderMode_Static)
			{
				RenderStatic();
			}
		}
	}

	void
	UpdateModel(
		uint32_t	inDeltaUS)
	{
		updateCumulatorUS += inDeltaUS;

		uint16_t	updateSec4dot12 = uint16_t(updateCumulatorUS * (1 << 12) / 1000000);
		if(updateSec4dot12 >= (1 << 6))
		{
			for(int i = 0; i < eIcicleTotal; ++i)
			{
				icicles[i].UpdateIcicleState(updateSec4dot12, this);
			}

			updateCumulatorUS -= updateSec4dot12 * 1000000 / (1 << 12);
		}
	}

	void
	RenderStatic(
		void)
	{
		uint8_t	staticR = (uint8_t)((float)settings.staticR * settings.staticIntensity);
		uint8_t	staticG = (uint8_t)((float)settings.staticG * settings.staticIntensity);
		uint8_t	staticB = (uint8_t)((float)settings.staticB * settings.staticIntensity);

		for(int i = 0; i < eIcicleTotal; ++i)
		{
			int	ledIndex;

			uint8_t	depth;
			
			static uint8_t	gTable[] = {2, 3, 4, 2, 3};
			depth = gTable[i % (sizeof(gTable) / sizeof(gTable[0]))];

			for(uint32_t j = 0; j < eLEDsPerIcicle; ++j)
			{
				if((i & 1) == 1)
				{
					// odd icicles have reverse ordering
					ledIndex = (i + 1) * eLEDsPerIcicle - j - 1;
				}
				else
				{
					ledIndex = i * eLEDsPerIcicle + j;
				}

				MAssert(ledIndex < eLEDsPerStrip * 8);

				uint8_t	r, g, b;

				if(j <= depth)
				{
					r = staticR;
					g = staticG;
					b = staticB;
				}
				else
				{
					r = 0; g = 0; b = 0;
				}

				leds.setPixel(ledIndex, r, g, b);
			}
		}

		leds.show();
	}

	void
	RenderDynamic(
		void)
	{
		SIcicleState*	curState = icicles;

		for(int i = 0; i < eIcicleTotal; ++i, ++curState)
		{
			int	ledIndex;

			uint32_t	curDepthMag = curState->curDepth4dot12 >> 12;
			uint32_t	curDepthFrac8 = (curState->curDepth4dot12 >> 4) & 0xFF;

			uint32_t	dripLEDAMag = 0xFFFF;
			uint32_t	dripLEDBMag = 0xFFFF;
			uint32_t	dripLEDAFrac8 = 0;
			uint32_t	dripLEDBFrac8 = 0;

			if(curState->waterDripLoc4dot12 > 0)
			{
				// This icicle is dripping water
				uint16_t	waterDripLoc4dot8 = curState->waterDripLoc4dot12 >> 4;

				if(waterDripLoc4dot8 > 0x7F)
				{
					// Since the drip location is more than half way down an LED we can alias it across two LEDs for a smoother effect.

					// Treat the water drip location as the center with half of its LED effect before the location and half after the location
					dripLEDAFrac8 = waterDripLoc4dot8 - 0x7F;
					dripLEDAMag = dripLEDAFrac8 >> 8;
					dripLEDAFrac8 = 0x100 - (dripLEDAFrac8 & 0xFF);

					dripLEDBFrac8 = waterDripLoc4dot8 + 0x7F;
					dripLEDBMag = dripLEDBFrac8 >> 8;
					dripLEDBFrac8 &= 0xFF;
				}
				else
				{
					// The drip location is less then half way down the first LED so only consider its effect on that LED
					dripLEDAMag = 0;
					dripLEDAFrac8 = waterDripLoc4dot8 & 0xFF;
				}
			}

			for(uint32_t j = 0; j < eLEDsPerIcicle; ++j)
			{
				if((i & 1) == 1)
				{
					// odd icicles have reverse ordering
					ledIndex = (i + 1) * eLEDsPerIcicle - j - 1;
				}
				else
				{
					ledIndex = i * eLEDsPerIcicle + j;
				}

				uint32_t	r8dot8, g8dot8, b8dot8;

				if(j <= curDepthMag)
				{
					// j is within the icicle

					if(curState->curMaxDepthLifeTime4dot12 > 0)
					{
						uint32_t	maxDepthTransition8dot8 = (uint32_t(curState->curMaxDepthLifeTime4dot12) << 8) / uint32_t(curState->maxDepthLifeTime4dot12);
						// We are transitioning from grow down to recede up
						r8dot8 = settings.growDownColorR * (0x100 - maxDepthTransition8dot8) + settings.recedeUpColorR * maxDepthTransition8dot8;
						g8dot8 = settings.growDownColorG * (0x100 - maxDepthTransition8dot8) + settings.recedeUpColorG * maxDepthTransition8dot8;
						b8dot8 = settings.growDownColorB * (0x100 - maxDepthTransition8dot8) + settings.recedeUpColorB * maxDepthTransition8dot8;
					}
					else
					{
						// The icicle is either growing down or receding up
						if(!(curState->growthRateLEDsPerSec4dot12 & 0x8000))
						{
							r8dot8 = settings.growDownColorR << 8;
							g8dot8 = settings.growDownColorG << 8;
							b8dot8 = settings.growDownColorB << 8;
						}
						else
						{
							r8dot8 = settings.recedeUpColorR << 8;
							g8dot8 = settings.recedeUpColorG << 8;
							b8dot8 = settings.recedeUpColorB << 8;
						}
					}
						
					if(j == curDepthMag)
					{
						r8dot8 = (r8dot8 * curDepthFrac8) >> 8;
						g8dot8 = (g8dot8 * curDepthFrac8) >> 8;
						b8dot8 = (b8dot8 * curDepthFrac8) >> 8;
					}

					if(j == dripLEDAMag)
					{
						r8dot8 = ((r8dot8 * (0x100 - dripLEDAFrac8)) >> 8) + (settings.waterDripR * dripLEDAFrac8);
						g8dot8 = ((g8dot8 * (0x100 - dripLEDAFrac8)) >> 8) + (settings.waterDripG * dripLEDAFrac8);
						b8dot8 = ((b8dot8 * (0x100 - dripLEDAFrac8)) >> 8) + (settings.waterDripB * dripLEDAFrac8);
					}
					else if(j == dripLEDBMag)
					{
						r8dot8 = ((r8dot8 * (0x100 - dripLEDBFrac8)) >> 8) + (settings.waterDripR * dripLEDBFrac8);
						g8dot8 = ((g8dot8 * (0x100 - dripLEDBFrac8)) >> 8) + (settings.waterDripG * dripLEDBFrac8);
						b8dot8 = ((b8dot8 * (0x100 - dripLEDBFrac8)) >> 8) + (settings.waterDripB * dripLEDBFrac8);
					}
				}
				else
				{
					// j is past the end of the icicle
					r8dot8 = g8dot8 = b8dot8 = 0;
				}

				if(r8dot8 > 0xFFFF) r8dot8 = 0xFFFF;
				if(g8dot8 > 0xFFFF) g8dot8 = 0xFFFF;
				if(b8dot8 > 0xFFFF) b8dot8 = 0xFFFF;

				MAssert(ledIndex < eLEDsPerStrip * 8);
				leds.setPixel(ledIndex, uint8_t(r8dot8 >> 8), uint8_t(g8dot8 >> 8), uint8_t(b8dot8 >> 8));
			}
		}

		leds.show();
	}

	struct SSettings
	{
		// These two fields control the gaussian distribution of icicle growth rate in LEDs per second
		float	meanGrowRateLEDsPerSec;
		float	stdGrowRateLEDsPerSec;

		// These two fields control gaussian distribution of max depth before receding
		float	meanPeekDepth;
		float	stdPeekDepth;

		// These two fields control gaussian distribution of how long an icicle stays at the max depth before receding
		float	meanPeekDepthLifetimeSec;
		float	stdPeekDepthLifetimeSec;

		// These two two fields control the gaussian distribution of time(secs) between water droplets
		float	meanIcicleStartDripTime;
		float	stdIcicleStartDripTime;

		// This is how fast water drips before reaching the icicle depth
		float	waterDripRatePreLEDsPerSec;

		// This is how fast water drips after reaching the icicle depth
		float	waterDripRatePostLEDsPerTick;

		// This is the intensity of the static color render mode
		float	staticIntensity;

		// This is the base color for icicles growing down
		uint8_t	growDownColorR, growDownColorG, growDownColorB;

		// This is the base color for icicles growing up
		uint8_t	recedeUpColorR, recedeUpColorG, recedeUpColorB;

		// This is the color for water drops
		uint8_t	waterDripR, waterDripG, waterDripB;

		// This is the color for static display
		uint8_t	staticR, staticG, staticB;

		uint8_t	renderMode;
	};

	struct SIcicleState
	{
		void
		SetInitialState(
			CModule_Icicle*	inParent)
		{
			SetNewState(inParent);
			SetNextDripTime(inParent);
			curDepth4dot12 = (uint16_t)GetRandomFloat(1.0f, maxDepth4dot12);
			if(GetRandomInt(0, 2) == 0)
			{
				growthRateLEDsPerSec4dot12 = -growthRateLEDsPerSec4dot12;
			}
		}

		void
		UpdateIcicleState(
			int16_t			inUpdateSecs4dot12,
			CModule_Icicle*	inParent)
		{
			if(curMaxDepthLifeTime4dot12 > 0)
			{
				if(curMaxDepthLifeTime4dot12 + inUpdateSecs4dot12 < maxDepthLifeTime4dot12)
				{
					curMaxDepthLifeTime4dot12 += inUpdateSecs4dot12;
				}

				// We are done staying at the max depth so start receding
				else
				{
					growthRateLEDsPerSec4dot12 = -growthRateLEDsPerSec4dot12;
					curMaxDepthLifeTime4dot12 = 0;
				}
			}
			else
			{
				curDepth4dot12 += uint16_t((int32_t(growthRateLEDsPerSec4dot12) * int32_t(inUpdateSecs4dot12)) >> 12);

				if(!(growthRateLEDsPerSec4dot12 & 0x8000))
				{
					if(curDepth4dot12 >= maxDepth4dot12)
					{
						curMaxDepthLifeTime4dot12 = 1;
						curDepth4dot12 = maxDepth4dot12;
					}
				}
				else
				{
					if(curDepth4dot12 & 0x8000)
					{
						SetNewState(inParent);
					}
				}
			}

			// Check if we are dripping water
			if(waterDripLoc4dot12 > 0)
			{
				if(waterDripLoc4dot12 < curDepth4dot12)
				{
					waterDripLoc4dot12 += uint16_t(inParent->settings.waterDripRatePreLEDsPerSec * inUpdateSecs4dot12);
				}
				else
				{
					waterDripLoc4dot12 += uint16_t(inParent->settings.waterDripRatePostLEDsPerTick * inUpdateSecs4dot12);
				}

				if((waterDripLoc4dot12 >> 12) >= eLEDsPerIcicle)
				{
					// time to reset
					SetNextDripTime(inParent);
				}
			}

			// We are not dripping water, deduct from the next time a water drip should start
			else if((inUpdateSecs4dot12 >> 4) <= nextDripTime8dot8)
			{
				nextDripTime8dot8 -= (inUpdateSecs4dot12 >> 4);
			}

			// time to start a water drop
			else
			{
				// start a drip
				waterDripLoc4dot12 = 1;
			}
		}

		void
		SetNewState(
			CModule_Icicle*	inParent)
		{
			curDepth4dot12 = 0;
			maxDepth4dot12 = uint16_t(GetRandomFloat(1.0f, eLEDsPerIcicle) * float(1 << 12));

			growthRateLEDsPerSec4dot12 = (uint16_t)(GetRandomFloatGuassian(inParent->settings.meanGrowRateLEDsPerSec, inParent->settings.stdGrowRateLEDsPerSec) * float(1 << 12));
			if(growthRateLEDsPerSec4dot12 < 1)
			{
				growthRateLEDsPerSec4dot12 = 1;
			}
			else if(growthRateLEDsPerSec4dot12 > 0x1000)
			{
				growthRateLEDsPerSec4dot12 = 0x1000;
			}
			//growthRateLEDsPerSec4dot12 = -growthRateLEDsPerSec4dot12;

			maxDepth4dot12 = (uint16_t)(GetRandomFloatGuassian(inParent->settings.meanPeekDepth, inParent->settings.stdPeekDepth) * float(1 << 12));
			if(maxDepth4dot12 < 0x17FF)
			{
				maxDepth4dot12 = 0x17FF;
			}
			else if(maxDepth4dot12 > eLEDsPerIcicle << 12)
			{
				maxDepth4dot12 = eLEDsPerIcicle << 12;
			}

			maxDepthLifeTime4dot12 = (uint16_t)(GetRandomFloatGuassian(inParent->settings.meanPeekDepthLifetimeSec, inParent->settings.stdPeekDepthLifetimeSec) * float(1 << 12));
			if(maxDepthLifeTime4dot12 < 0x1000)
			{
				maxDepthLifeTime4dot12 = 0x1000;
			}
			else if(maxDepthLifeTime4dot12 > 0x7000)
			{
				maxDepthLifeTime4dot12 = 0x7000;
			}

			curMaxDepthLifeTime4dot12 = 0;
		}

		void
		SetNextDripTime(
			CModule_Icicle*	inParent)
		{
			waterDripLoc4dot12 = 0;
			nextDripTime8dot8 = (uint16_t)(GetRandomFloatGuassian(inParent->settings.meanIcicleStartDripTime, inParent->settings.stdIcicleStartDripTime) * float(1 << 8));
		}

		// This is the current depth in fractional LEDs, 0 is at the top and eLEDsPerIcicle is at the bottom
		int16_t	curDepth4dot12;

		// The grow rate in fractional LEDs
		int16_t	growthRateLEDsPerSec4dot12;

		// The max depth of this icicle before it starts to recede
		int16_t	maxDepth4dot12;

		// The current water drip location in fractional LEDs
		int16_t	waterDripLoc4dot12;

		// The lifetime of the icicle at the maximum depth in ticks
		uint16_t	maxDepthLifeTime4dot12;

		// The current lifetime remaining at the maximum depth in ticks
		uint16_t	curMaxDepthLifeTime4dot12;

		// The next water drip time in MS
		uint16_t	nextDripTime8dot8;
	};

	OctoWS2811		leds;
	SIcicleState	icicles[eIcicleTotal];
	SSettings		settings;

	uint32_t		updateCumulatorUS;

	uint16_t	icicleIndex;
	uint8_t		renderOrStateUpdate;

	bool	ledsOn;
};

MModuleImplementation_Start(CModule_Icicle);
MModuleImplementation_Finish(CModule_Icicle);

void
SetupIcicleModule(
	void)
{
	CModule_Icicle::Include();
}
