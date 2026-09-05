#pragma once

#include "../../IModule.h"
#include "../../Version.h"

class AboutModule : public IModule
{
public:

	Versioning Versions;

	AboutModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
	{
		this->UiRenderEnabled = UiRenderEnabled;
		this->Twinkie = &Twinkie;
		this->Logger = &Logger;
		this->Name = "About";
		this->FancyName = "About";
	}

	virtual void Render();
	virtual void RenderAnyways() {}
	virtual void RenderSettings() {}

	virtual void SettingsInit(SettingMgr& Settings) {}
	virtual void SettingsSave(SettingMgr& Settings) {}

	virtual bool HasSettings() { return false; }
	virtual bool IsDebug() { return false; }
	virtual bool HasMenuEntry() { return false; } // reached via its own entry in the Help menu instead
};