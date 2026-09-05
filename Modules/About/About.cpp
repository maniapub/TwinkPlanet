#include "About.h"
#include <format>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

#define LinkOSS(name, url) Text(name); SameLine(); TextLinkOpenURL(url)

static const char* GetBuildName()
{
#if defined(BUILD_NATIONS)
	return "Nations";
#elif defined(BUILD_PREMIUM)
	return "Premium";
#elif defined(BUILD_TMMC)
	return "Modloader";
#else
	return "Unknown";
#endif
}

static const char* GetConfigurationName()
{
#if defined(BUILD_DEBUG)
	return "Debug";
#else
	return "Release";
#endif
}

void AboutModule::Render()
{
	using namespace ImGui;
	if (Begin(ICON_FK_INFO_CIRCLE " About TwinkPlanet", &Enabled, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
	{
		PushFont(GetFont(), GetStyle().FontSizeBase * 1.6f);
		Text("TwinkPlanet");
		PopFont();

		Text(std::format("Version {}", Versions.TwinkieVer).c_str());
		Text(std::format("Build {}", GetBuildName()).c_str());
		Text(std::format("Configuration {}", GetConfigurationName()).c_str());

		Separator();

		if (BeginTable("##AboutTwinkieInfo", 2))
		{
			TableNextColumn(); TextDisabled("Lua");
			TableNextColumn(); Text(Versions.LuaVer);

			TableNextColumn(); TextDisabled("Dear ImGui");
			TableNextColumn(); Text(IMGUI_VERSION);

			TableNextColumn(); TextDisabled("MSVC");
			TableNextColumn(); Text(std::to_string(_MSC_VER).c_str());

			TableNextColumn(); TextDisabled("Account type");
			TableNextColumn(); Text(Twinkie->GetPayingAccountType() == TM::AccountType::Disconnected ? "Disconnected" : (Twinkie->GetPayingAccountType() == TM::AccountType::Nations ? "Nations" : "United"));

			TableNextColumn(); TextDisabled("Game type");
			TableNextColumn(); Text(Twinkie->IsGameInstallUnited() ? "United" : "Nations");

			EndTable();
		}

		Separator();

		BeginDisabled();
		Button(ICON_FK_HEART " Donate");
		EndDisabled();
		if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			SetTooltip("No monetary donations available yet, star the Github repo instead!");
		}
		SameLine();
		if (Button(ICON_FK_GITHUB " Github"))
		{
			ShellExecuteA(NULL, "open", "https://github.com/maniapub/TwinkPlanet", NULL, NULL, SW_SHOWNORMAL);
		}
		SameLine();
		if (Button(ICON_FK_DISCORD_ALT " Discord"))
		{
			ShellExecuteA(NULL, "open", "https://discord.gg/HNqYNEgjm9", NULL, NULL, SW_SHOWNORMAL);
		}
		SameLine();
		if (Button(ICON_FK_HOME " Home"))
		{
			ShellExecuteA(NULL, "open", "https://github.com/TwinkieTweaks/Twinkie", NULL, NULL, SW_SHOWNORMAL);
		}

		Text("Made with love by maniapub. <3 / jailman :o (for original twinkie)");

#ifdef BUILD_DEBUG
		SeparatorText("Debug");
		Text("This is a debug copy, please report any bugs to the author, and do not share this with anyone.");
#endif

		SeparatorText("Credits");
		Text("The RMC module was inspired by BigBang1112's Randomizer TMF.");
		LinkOSS("Randomizer TMF by BigBang1112: ", "https://github.com/BigBang1112/randomizer-tmf");

		Text("The Discord Rich Presence module was adapted from the three below.");
		LinkOSS("TwinkieTweaks/TwinkieX: ", "https://github.com/TwinkieTweaks/TwinkieX");
		LinkOSS("ForeverRPC: ", "https://github.com/AroPix/ForeverRPC");
		LinkOSS("Discord GameSDK: ", "https://discord.com/developers/docs/developer-tools/game-sdk");

		Text("The No Distance Fog module was ported from a tool by Celyanito.");

		SeparatorText("OSS");
		Text("TwinkPlanet uses several open-source libraries. All of which are listed below.");
		LinkOSS("dear imgui: ", "https://github.com/ocornut/imgui");
		LinkOSS("mINI: ", "https://github.com/metayeti/mINI");
		LinkOSS("IconFontCppHeaders: ", "https://github.com/juliettef/IconFontCppHeaders");
	}
	End();
}
