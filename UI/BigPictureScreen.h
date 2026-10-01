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
#include <vector>

#include "Common/File/Path.h"
#include "Common/File/PathBrowser.h"
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

// What the post-game stats card shows about the previous session.
struct BigPictureSessionStats {
	bool valid = false;
	std::string title;
	double playedSec = 0.0;     // in-game time, pauses excluded.
	double avgFps = 0.0;
	double worstFrameMs = 0.0;
	int longFrames = 0;         // frames that took over 50 ms.
};

const BigPictureSessionStats &BigPictureGetLastSession();

// Sampled once per rendered game frame while Big Picture Mode is on; the first
// in-game frame starts a session.
void BigPictureOnEmuFrame();
// Snapshots an in-progress session; called on every path back to the home screen.
void BigPictureFinalizeSession();

// Lifetime totals for one game, keyed by disc ID (path fallback), stored in
// BigPictureStats.ini next to ppsspp.ini.
struct BigPictureGameStats {
	bool valid = false;
	long long playedSec = 0;
	int sessions = 0;
	float avgFps = 0.0f;
	float worstFrameMs = 0.0f;
	long long lastPlayedUnix = 0;
};

bool BigPictureGetGameStats(const std::string &key, BigPictureGameStats *out);

// The home screen, honoring g_Config.bBigPictureMode. Every "back to menu"
// path goes through this so the mode behaves consistently.
Screen *CreateHomeScreen();

class BigPictureView;

// The hero-carousel home: games slide horizontally, the selection gets a
// pulsing comet ring, and a drop menu (press up/down) reaches Browse,
// Recent, Settings and Exit.
class BigPictureScreen : public UIBaseScreen {
public:
	const char *tag() const override { return "BigPicture"; }

	bool isTopLevel() const override { return true; }

	bool key(const KeyInput &key) override;
	void update() override;

	// Hooks for BigPictureView (drawn carousel + touch handling).
	int SelectedIndex() const { return sel_; }
	int GameCount() const { return (int)games_.size(); }
	const Path *GamePath(int i) const {
		return (i >= 0 && i < (int)games_.size()) ? &games_[i] : nullptr;
	}
	std::string StatsKeyForIndex(int i);
	bool MenuOpen() const { return menuOpen_; }
	int MenuIndex() const { return menuIdx_; }
	void Select(int i);
	void LaunchSelected();
	void ActivateMenu(int idx);
	void CloseMenu() { menuOpen_ = false; }
	static constexpr int kMenuCount = 4;

protected:
	void CreateViews() override;
	void DrawBackground(UIContext &ui) override;

private:
	void OnPlay(UI::EventParams &e);
	void OnOptions(UI::EventParams &e);
	void OnClear(UI::EventParams &e);
	void RebuildGames();
	void RefreshPreview();
	void StartDirListing();

	std::vector<Path> games_;
	std::vector<Path> dirGames_;
	PathBrowser dirBrowser_;
	bool dirStarted_ = false;
	bool dirReady_ = false;
	int sel_ = 0;
	bool menuOpen_ = false;
	int menuIdx_ = 0;
	bool recentsOnly_ = false;
	BigPictureView *carousel_ = nullptr;
	UI::TextView *titleText_ = nullptr;
	UI::TextView *counterText_ = nullptr;
	UI::TextView *statsText_ = nullptr;
	UI::TextView *clockText_ = nullptr;
	double nextRefresh_ = 0.0;
};

// The file browser Big Picture used before the carousel; reachable from the
// carousel menu. Pushed on top of the carousel, so back returns to it.
class BigPictureBrowseScreen : public UIBaseScreen {
public:
	const char *tag() const override { return "BigPictureBrowse"; }

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
