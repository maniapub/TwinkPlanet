#pragma once

#include "SettingMgr/SettingMgr.h"
#include "TwinkTrackmania/TwinkTrackmania.h"
#include "TwinkLogs.h"
#include "Utils.h"
#include "imgui-dx9/imgui.h"
#include "GlyphTable/IconsForkAwesome.h"
#include "GlyphTable/IconsKenney.h"
#include <vector>

// this isn't *technically* an interface but it's the class all modules inherit from
class __declspec(dllexport) IModule
{
public:
	bool Enabled = false;
	const bool* UiRenderEnabled = nullptr;
	std::string Name = "";
	std::string FancyName = "";
	// Which group this shows up under in the "Modules" menu (TwinkUi.cpp) - purely a display
	// grouping, doesn't affect Custom/Debug routing. Left empty falls into an "Other" bucket
	// rather than disappearing, so nothing silently drops out of the menu.
	std::string Category = "";
	TwinkTrackmania* Twinkie = nullptr;
	TwinkLogs* Logger = nullptr;

	IModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
	{
		this->UiRenderEnabled = UiRenderEnabled;
		this->Twinkie = &Twinkie;
		this->Logger = &Logger;
	}

	IModule() = default;

	virtual ~IModule() = default;

	virtual void Render() {}
	virtual void RenderAnyways() {}
	virtual void RenderInactive() {}
	virtual void RenderSettings() {}
	virtual void RenderMenuItem() {}

	virtual void SettingsInit(SettingMgr& Settings) {}
	virtual void SettingsSave(SettingMgr& Settings) {}

	virtual bool IsDebug() { return false; }
	virtual bool HasSettings() { return true; }
	// False for a module whose RenderMenuItem() is a deliberate no-op (nothing to toggle from a
	// menu - e.g. it's always-on, or only configurable from its Settings tab). Lets menu-grouping
	// code (TwinkUi.cpp's "Modules" menu) skip it entirely instead of printing a category header
	// with nothing visible underneath it.
	virtual bool HasMenuEntry() { return true; }
};