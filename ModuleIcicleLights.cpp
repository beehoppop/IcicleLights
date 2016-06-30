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
		IInternetDevice*		internetDevice = CModule_ESP8266::Include(&Serial1, eESP8266ResetPint);
		IRealTimeDataProvider*	ds3234Provider = CreateDS3234Provider(10);
		CModule_Loggly*			loggly = CModule_Loggly::Include("pergola", "logs-01.loggly.com", "/inputs/568b321d-0d6f-47d3-ac34-4a36f4125612");
		
		CModule_RealTime::Include();
		CModule_Internet::Include();
		CModule_Command::Include();
		CModule_OutdoorLightingControl::Include(this, eMotionSensorPin, eTransformerRelayPin, eToggleButtonPin, NULL);
		
		AddSysMsgHandler(loggly);
		gInternetModule->Configure(internetDevice);
		gRealTime->Configure(ds3234Provider, 24 * 60 * 60);
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
		MCommandRegister("growcolor_set", CModule_Icicle::GrowDownColorSet, "[r] [g] [b]: Set the grow down color");
		MCommandRegister("recedecolor_set", CModule_Icicle::RecedeUpColorSet, "[r] [g] [b]: Set the recede up color");

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
		inOutput->printf("<tr><td>DripRate PostIcicleEnd</td><td>%2.2f</td></tr>", settings.waterDripRatePostLEDsPerSec);

		// add grow down color
		inOutput->printf("<tr><td>GrowDown Color</td><td>r:%02.02f g:%02.02f b:%02.02f</td></tr>", settings.growDownColorR, settings.growDownColorG, settings.growDownColorB);

		// add recede up color
		inOutput->printf("<tr><td>RecedeUp Color</td><td>r:%02.02f g:%02.02f b:%02.02f</td></tr>", settings.recedeUpColorR, settings.recedeUpColorG, settings.recedeUpColorB);

		// add water drip color
		inOutput->printf("<tr><td>WaterDrip Color</td><td>r:%02.02f g:%02.02f b:%02.02f</td></tr>", settings.waterDripR, settings.waterDripG, settings.waterDripB);

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

	uint8_t PeekDepthLifeDistributionSet(
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
		settings.waterDripRatePostLEDsPerSec = (float)atof(inArgV[2]);

		EEPROMSave();

		return eCmd_Succeeded;
	}

	uint8_t
	GrowDownColorSet(
		IOutputDirector*	inOutput,
		int					inArgC,
		char const*			inArgV[])
	{
		MReturnOnError(inArgC != 3, eCmd_Failed);
		
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
		MReturnOnError(inArgC != 3, eCmd_Failed);
		
		settings.recedeUpColorR = (uint8_t)(atof(inArgV[1]) * 255.0);
		settings.recedeUpColorG = (uint8_t)(atof(inArgV[2]) * 255.0);
		settings.recedeUpColorB = (uint8_t)(atof(inArgV[3]) * 255.0);

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
		settings.waterDripRatePostLEDsPerSec = 4.0f;
		settings.growDownColorR = 64;
		settings.growDownColorG = 64;
		settings.growDownColorB = 250;
		settings.recedeUpColorR = 250;
		settings.recedeUpColorG = 128;
		settings.recedeUpColorB = 200;
		settings.waterDripR = 0;
		settings.waterDripG = 0;
		settings.waterDripB = 0xFF;
	}

	virtual void
	Update(
		uint32_t	inDeltaUS)
	{
		UpdateModel(inDeltaUS);
		Render();
	}

	void
	UpdateModel(
		uint32_t	inDeltaUS)
	{
		for(int i = 0; i < eIcicleTotal; ++i)
		{
			icicles[i].UpdateIcicleState(inDeltaUS, this);
		}
	}

	void
	Render(
		void)
	{
		SIcicleState*	curState = icicles;

		for(int i = 0; i < eIcicleTotal; ++i, ++curState)
		{
			int	ledIndex;

			int		curDepthInt = (int)curState->curDepth;
			float	curDepthFrac = curState->curDepth - (float)curDepthInt;
			float	maxDepthTransition = float(curState->curMaxDepthLifeTimeMS) / float(curState->maxDepthLifeTimeMS);

			int	dripLEDA = -1;
			int	dripLEDB = -1;
			float	dripLEDAFrac = 0.0f;
			float	dripLEDBFrac = 0.0f;

			if(curState->waterDripLoc > 0.0f)
			{
				// This icicle is dripping water

				if(curState->waterDripLoc > 0.5f)
				{
					// Since the drip location is more than half way down an LED we can alias it across two LEDs for a smoother effect.

					// Treat the water drip location as the center with half of its LED effect before the location and half after the location
					dripLEDAFrac = curState->waterDripLoc - 0.5f;
					dripLEDA = int(dripLEDAFrac);
					dripLEDAFrac = 1.0f - (dripLEDAFrac - float(dripLEDA));

					dripLEDBFrac = curState->waterDripLoc + 0.5f;
					dripLEDB = int(dripLEDBFrac);
					dripLEDBFrac -= float(dripLEDB);
				}
				else
				{
					// The drip location is less then half way down the first LED so only consider its effect on that LED
					dripLEDA = int(curState->waterDripLoc);
					dripLEDAFrac = curState->waterDripLoc - float(dripLEDA);
				}
			}

			for(int j = 0; j < eLEDsPerIcicle; ++j)
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

				float	r, g, b;

				if(j <= curDepthInt)
				{
					// j is within the icicle

					if(curState->curMaxDepthLifeTimeMS > 0)
					{
						// We are transitioning from grow down to recede up
						r = float(settings.growDownColorR) * (1.0f - maxDepthTransition) + float(settings.recedeUpColorR) * maxDepthTransition;
						g = float(settings.growDownColorG) * (1.0f - maxDepthTransition) + float(settings.recedeUpColorG) * maxDepthTransition;
						b = float(settings.growDownColorB) * (1.0f - maxDepthTransition) + float(settings.recedeUpColorB) * maxDepthTransition;
					}
					else
					{
						// the icicle is either growing down or receding up
						if(curState->growthRateLEDsPerSec > 0.0f)
						{
							r = (float)settings.growDownColorR;
							g = (float)settings.growDownColorG;
							b = (float)settings.growDownColorB;
						}
						else
						{
							r = (float)settings.recedeUpColorR;
							g = (float)settings.recedeUpColorG;
							b = (float)settings.recedeUpColorB;
						}
					}
						
					if(j == curDepthInt)
					{
						r *= curDepthFrac;
						g *= curDepthFrac;
						b *= curDepthFrac;
					}

					if(j == dripLEDA)
					{
						r = r * (1.0f - dripLEDAFrac) + settings.waterDripR * dripLEDAFrac;
						g = g * (1.0f - dripLEDAFrac) + settings.waterDripG * dripLEDAFrac;
						b = b * (1.0f - dripLEDAFrac) + settings.waterDripB * dripLEDAFrac;
					}
					else if(j == dripLEDB)
					{
						r = r * (1.0f - dripLEDBFrac) + settings.waterDripR * dripLEDBFrac;
						g = g * (1.0f - dripLEDBFrac) + settings.waterDripG * dripLEDBFrac;
						b = b * (1.0f - dripLEDBFrac) + settings.waterDripB * dripLEDBFrac;
					}
				}
				else
				{
					// j is past the end of the icicle

					if(j == dripLEDA)
					{
						r = 0;
						g = 0;
						b = 255.0f * dripLEDAFrac;
					}
					else if(j == dripLEDB)
					{
						r = 0;
						g = 0;
						b = 255.0f * dripLEDBFrac;
					}
					else
					{
						r = g = b = 0.0f;
					}
				}

				if(r > 255.0f) r = 255.0f;
				if(g > 255.0f) g = 255.0f;
				if(b > 255.0f) b = 255.0f;

				leds.setPixel(ledIndex, uint8_t(r), uint8_t(g), uint8_t(b));
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
		float	waterDripRatePostLEDsPerSec;

		// This is the base color for icicles growing down
		uint8_t	growDownColorR, growDownColorG, growDownColorB;

		// This is the base color for icicles growing up
		uint8_t	recedeUpColorR, recedeUpColorG, recedeUpColorB;

		// This is the color for water drops
		uint8_t	waterDripR, waterDripG, waterDripB;
	};

	struct SIcicleState
	{
		void
		SetInitialState(
			CModule_Icicle*	inParent)
		{
			SetNewState(inParent);
			SetNextDripTime(inParent);
			curDepth = GetRandomFloat(1.0f, maxDepth);
		}

		void
		UpdateIcicleState(
			uint32_t		inDeltaUS,
			CModule_Icicle*	inParent)
		{
			uint16_t	deltaTimeMS = (uint16_t)((inDeltaUS + 500) / 1000);

			if(curMaxDepthLifeTimeMS > 0)
			{
				if(curMaxDepthLifeTimeMS + deltaTimeMS < maxDepthLifeTimeMS)
				{
					curMaxDepthLifeTimeMS += deltaTimeMS;
				}

				// We are done staying at the max depth so start receding
				else
				{
					growthRateLEDsPerSec = -growthRateLEDsPerSec;
					curMaxDepthLifeTimeMS = 0;
				}
			}
			else
			{
				curDepth += growthRateLEDsPerSec * (float)inDeltaUS / 1e6f;

				if(growthRateLEDsPerSec > 0.0f)
				{
					if(curDepth >= maxDepth)
					{
						curMaxDepthLifeTimeMS = 1;
						curDepth = maxDepth;
					}
				}
				else
				{
					if(curDepth <= 0.0f)
					{
						SetNewState(inParent);
					}
				}
			}

			// Check if we are dripping water
			if(waterDripLoc > 0.0f)
			{
				if(waterDripLoc < curDepth)
				{
					waterDripLoc += inParent->settings.waterDripRatePreLEDsPerSec * (float)inDeltaUS / 1e6f;
				}
				else
				{
					waterDripLoc += inParent->settings.waterDripRatePostLEDsPerSec * (float)inDeltaUS / 1e6f;
				}

				if(waterDripLoc >= (float)eLEDsPerIcicle)
				{
					// time to reset
					SetNextDripTime(inParent);
				}
			}

			// We are not dripping water, deduct from the next time a water drip should start
			else if(deltaTimeMS <= nextDripTimeMS)
			{
				nextDripTimeMS -= deltaTimeMS;
			}

			// time to start a water drop
			else
			{
				// start a drip
				waterDripLoc = 0.001f;
			}
		}

		void
		SetNewState(
			CModule_Icicle*	inParent)
		{
			curDepth = 0.0f;
			maxDepth = GetRandomFloat(1.0f, eLEDsPerIcicle);

			growthRateLEDsPerSec = GetRandomFloatGuassian(inParent->settings.meanGrowRateLEDsPerSec, inParent->settings.stdGrowRateLEDsPerSec);
			if(growthRateLEDsPerSec < 0.01f)
			{
				growthRateLEDsPerSec = 0.01f;
			}
			else if(growthRateLEDsPerSec > 1.0f)
			{
				growthRateLEDsPerSec = 1.0f;
			}

			maxDepth = GetRandomFloatGuassian(inParent->settings.meanPeekDepth, inParent->settings.stdPeekDepth);
			if(maxDepth < 1.5f)
			{
				maxDepth = 1.5f;
			}
			else if(maxDepth > (float)eLEDsPerIcicle)
			{
				maxDepth = (float)eLEDsPerIcicle;
			}

			float	maxDepthLifeTimeFloat = GetRandomFloatGuassian(inParent->settings.meanPeekDepthLifetimeSec, inParent->settings.stdPeekDepthLifetimeSec);
			if(maxDepthLifeTimeFloat < 1.0f)
			{
				maxDepthLifeTimeFloat = 1.0f;
			}
			else if(maxDepthLifeTimeFloat > 64.0)
			{
				maxDepthLifeTimeFloat = 64.0;
			}

			maxDepthLifeTimeMS = (uint16_t)(maxDepthLifeTimeFloat * 1e3f);
			curMaxDepthLifeTimeMS = 0;
		}

		void
		SetNextDripTime(
			CModule_Icicle*	inParent)
		{
			waterDripLoc = 0;
			nextDripTimeMS = (uint16_t)(GetRandomFloatGuassian(inParent->settings.meanIcicleStartDripTime, inParent->settings.stdIcicleStartDripTime) * 1e3);
		}

		// This is the current depth in fractional LEDs, 0 is at the top and eLEDsPerIcicle is at the bottom
		float	curDepth;

		// The grow rate in fractional LEDs
		float	growthRateLEDsPerSec;

		// The max depth of this icicle before it starts to recede
		float	maxDepth;

		// The current water drip location in fractional LEDs
		float	waterDripLoc;

		// The lifetime of the icicle at the maximum depth in MS
		uint16_t	maxDepthLifeTimeMS;

		// The current lifetime remaining at the maximum depth in MS
		uint16_t	curMaxDepthLifeTimeMS;

		// The next water drip time in MS
		uint16_t	nextDripTimeMS;
	};

	OctoWS2811		leds;
	SIcicleState	icicles[eIcicleTotal];
	SSettings		settings;

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
