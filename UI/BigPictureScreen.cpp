// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#include "Common/Data/Text/I18n.h"
#include "Common/GPU/thin3d.h"
#include "Common/UI/Screen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Config.h"
#include "Core/System.h"

#include "GPU/GPU.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"

#include "UI/BigPictureScreen.h"
#include "UI/GameBrowser.h"
#include "UI/GameSettingsScreen.h"
#include "UI/MainScreen.h"
#include "UI/MiscViews.h"

#if PPSSPP_PLATFORM(IOS)
constexpr std::string_view bigPictureGetGamesUri = "https://www.ppsspp.org/getgames_ios";
#else
constexpr std::string_view bigPictureGetGamesUri = "https://www.ppsspp.org/getgames";
#endif

BigPictureGPUStatus GetBigPictureGPUStatus(Draw::DrawContext *draw) {
	BigPictureGPUStatus status;
	if (!draw)
		return status;
	status.device = draw->GetInfoString(Draw::InfoField::VENDORSTRING);
	status.driver = draw->GetInfoString(Draw::InfoField::DRIVER);
	// Only the enabled device extensions are listed, so this reflects what the
	// backend actually turned on. EDS1 reachable as Vulkan 1.3 core without the
	// extension name won't show here; no known driver does that yet.
	const std::vector<std::string> extensions = draw->GetExtensionList(true, true);
	for (const std::string &extension : extensions) {
		if (extension == "VK_EXT_extended_dynamic_state")
			status.eds1 = true;
		else if (extension == "VK_EXT_extended_dynamic_state3")
			status.eds3 = true;
	}
	return status;
}

int GetBigPicturePipelineCount() {
	if (!PSP_IsInited() || !gpu)
		return 0;
	GPU_Vulkan *vgpu = dynamic_cast<GPU_Vulkan *>(gpu);
	if (!vgpu)
		return 0;
	const PipelineManagerVulkan *pipelineManager = vgpu->GetPipelineManager();
	return pipelineManager ? pipelineManager->GetNumPipelines() : 0;
}

Screen *CreateHomeScreen() {
	if (g_Config.bBigPictureMode)
		return new BigPictureScreen();
	return new MainScreen();
}

void BigPictureScreen::CreateViews() {
	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	// Header: mode title on the left, the driver facts on the right.
	LinearLayout *header = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 16, 24, 8)));
	header->Add(new TextView(mm->T("BigPictureTitle", "PPSSPP BIG PICTURE"), ALIGN_LEFT, false));
	header->Add(new Spacer(new LinearLayoutParams(1.0f)));

	const BigPictureGPUStatus status = GetBigPictureGPUStatus(screenManager()->getDrawContext());
	std::string statusLine = status.device;
	if (!status.driver.empty())
		statusLine += " | " + status.driver;
	if (status.eds1 && status.eds3)
		statusLine += " | EDS1+EDS3";
	else if (status.eds1)
		statusLine += " | EDS1";
	else if (status.eds3)
		statusLine += " | EDS3";
	else
		statusLine += " | no EDS";
	statusText_ = new TextView(statusLine, ALIGN_RIGHT, false);
	header->Add(statusText_);
	root_->Add(header);

	SearchBar *search = new SearchBar(new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(8, 0, 8, 4)));
	root_->Add(search);

	const bool portrait = GetDeviceOrientation() == DeviceOrientation::Portrait;
	browser_ = new GameBrowser(GetRequesterToken(), Path(g_Config.currentDirectory), BrowseFlags::STANDARD, portrait, &g_Config.bGridView2, screenManager(), mm->T("How to get games"), bigPictureGetGamesUri, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	browser_->SetSearchBar(search);
	browser_->OnChoice.Handle(this, &BigPictureScreen::OnGameSelected);
	root_->Add(browser_);

	// Footer: hint on the left, actions on the right.
	LinearLayout *footer = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 8, 24, 16)));
	footer->Add(new TextView(mm->T("BigPictureHints", "Back exits to the standard menu"), ALIGN_LEFT, false));
	footer->Add(new Spacer(new LinearLayoutParams(1.0f)));
	footer->Add(new Button(mm->T("Settings")))->OnClick.Handle(this, &BigPictureScreen::OnSettings);
	footer->Add(new Button(mm->T("BigPictureExit", "Exit Big Picture")))->OnClick.Handle(this, &BigPictureScreen::OnExit);
	root_->Add(footer);
}

void BigPictureScreen::OnGameSelected(UI::EventParams &e) {
	LaunchFile(screenManager(), nullptr, Path(e.s));
}

void BigPictureScreen::OnSettings(UI::EventParams &e) {
	// Not passing a game ID, changing the global settings.
	screenManager()->push(new GameSettingsScreen(Path()));
}

void BigPictureScreen::OnExit(UI::EventParams &e) {
	screenManager()->switchScreen(new MainScreen());
}

bool BigPictureScreen::key(const KeyInput &key) {
	bool result = UIBaseScreen::key(key);
	if (!result && (key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		// Back leaves the big picture for the standard menu rather than exiting the app.
		screenManager()->switchScreen(new MainScreen());
		return true;
	}
	return result;
}
