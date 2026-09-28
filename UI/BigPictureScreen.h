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

#pragma once

#include <string>

#include "Common/File/Path.h"
#include "Common/UI/ViewGroup.h"
#include "UI/BaseScreens.h"

namespace Draw {
class DrawContext;
}

class GameBrowser;

namespace UI {
class TextView;
}

// The facts the library header shows off: which driver ran, and whether the
// extended dynamic state paths were enabled at device creation.
struct BigPictureGPUStatus {
	std::string device;
	std::string driver;
	bool eds1 = false;
	bool eds3 = false;
};

BigPictureGPUStatus GetBigPictureGPUStatus(Draw::DrawContext *draw);

// Live count of compiled pipeline variants (0 outside a Vulkan game).
int GetBigPicturePipelineCount();

// The home screen, honoring g_Config.bBigPictureMode. Every "back to menu"
// path goes through this so the mode behaves consistently.
Screen *CreateHomeScreen();

class BigPictureScreen : public UIBaseScreen {
public:
	const char *tag() const override { return "BigPicture"; }

	bool isTopLevel() const override { return true; }

	bool key(const KeyInput &key) override;

protected:
	void CreateViews() override;

	void OnGameSelected(UI::EventParams &e);
	void OnSettings(UI::EventParams &e);
	void OnExit(UI::EventParams &e);

private:
	GameBrowser *browser_ = nullptr;
	UI::TextView *statusText_ = nullptr;
};
