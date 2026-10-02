// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "ppsspp_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "Common/Data/Format/IniFile.h"
#include "Common/Data/Text/I18n.h"
#include "Common/GPU/thin3d.h"
#include "Common/Render/DrawBuffer.h"
#include "Common/TimeUtil.h"
#include "Common/UI/Context.h"
#include "Common/UI/Screen.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"

#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Util/RecentFiles.h"

#include "GPU/GPU.h"
#include "GPU/Vulkan/GPU_Vulkan.h"
#include "GPU/Vulkan/PipelineManagerVulkan.h"

#include "UI/BigPictureScreen.h"
#include "UI/GameBrowser.h"
#include "UI/GameInfoCache.h"
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

// One session at a time, snapshotted on the way back to the home screen.
static BigPictureSessionStats g_lastSession;
static bool g_sessionRunning = false;
static double g_sessionLastFrame = 0.0;
static double g_sessionActiveSec = 0.0;
static double g_sessionWorstMs = 0.0;
static long long g_sessionFrames = 0;
static long long g_sessionLongFrames = 0;
static std::string g_sessionTitle;
static std::string g_sessionGameId;

const BigPictureSessionStats &BigPictureGetLastSession() {
	return g_lastSession;
}

void BigPictureOnEmuFrame() {
	if (!g_Config.bBigPictureMode)
		return;
	const bool inGame = GetUIState() == UISTATE_INGAME && PSP_IsInited();
	const double now = time_now_d();
	if (inGame && !g_sessionRunning) {
		g_sessionRunning = true;
		g_sessionLastFrame = now;
		g_sessionActiveSec = 0.0;
		g_sessionWorstMs = 0.0;
		g_sessionFrames = 0;
		g_sessionLongFrames = 0;
		g_sessionTitle.clear();
		g_sessionGameId.clear();
	}
	if (!inGame || !g_sessionRunning)
		return;
	const double dtMs = (now - g_sessionLastFrame) * 1000.0;
	g_sessionLastFrame = now;
	g_sessionFrames++;
	if (dtMs > 0.0 && dtMs < 2000.0) {  // skip pause-sized gaps
		g_sessionActiveSec += dtMs / 1000.0;
		if (dtMs > g_sessionWorstMs)
			g_sessionWorstMs = dtMs;
		if (dtMs > 50.0)
			g_sessionLongFrames++;
	}
	if (g_sessionTitle.empty()) {
		g_sessionTitle = g_paramSFO.GetValueString("TITLE");
		g_sessionGameId = g_paramSFO.GetValueString("DISC_ID");
	}
}

static Path BigPictureStatsPath() {
	return g_Config.memStickDirectory / "PSP/SYSTEM/BigPictureStats.ini";
}

static std::string SanitizeStatsKey(std::string key) {
	for (char &c : key) {
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
			|| c == '.' || c == '_' || c == '-';
		if (!ok)
			c = '_';
	}
	if (key.empty())
		key = "unknown";
	return key;
}

static IniFile g_statsIni;
static bool g_statsIniLoaded = false;
static bool g_statsIniDirty = true;

bool BigPictureGetGameStats(const std::string &key, BigPictureGameStats *out) {
	if (!out || key.empty())
		return false;
	if (!g_statsIniLoaded || g_statsIniDirty) {
		g_statsIni = IniFile();
		g_statsIniLoaded = g_statsIni.Load(BigPictureStatsPath());
		g_statsIniDirty = false;
	}
	Section *sec = g_statsIni.GetSection(SanitizeStatsKey(key));
	if (!sec)
		return false;
	out->valid = true;
	uint64_t played = 0;
	uint64_t last = 0;
	int sessions = 0;
	float avg = 0.0f;
	float worst = 0.0f;
	sec->Get("played", &played);
	sec->Get("sessions", &sessions);
	sec->Get("avg", &avg);
	sec->Get("worst", &worst);
	sec->Get("last", &last);
	out->playedSec = (long long)played;
	out->sessions = sessions;
	out->avgFps = avg;
	out->worstFrameMs = worst;
	out->lastPlayedUnix = (long long)last;
	return true;
}

static void BigPictureRecordSession(const std::string &key, const BigPictureSessionStats &s) {
	if (key.empty())
		return;
	Path path = BigPictureStatsPath();
	IniFile ini;
	ini.Load(path);
	Section *sec = ini.GetOrCreateSection(SanitizeStatsKey(key));
	uint64_t played = 0;
	uint64_t last = 0;
	int sessions = 0;
	float avg = 0.0f;
	float worst = 0.0f;
	sec->Get("played", &played);
	sec->Get("sessions", &sessions);
	sec->Get("avg", &avg);
	sec->Get("worst", &worst);
	sec->Get("last", &last);
	played += (uint64_t)(long long)s.playedSec;
	sessions += 1;
	avg = (float)s.avgFps;
	worst = (float)s.worstFrameMs;
	last = (uint64_t)time(nullptr);
	sec->Set("played", played);
	sec->Set("sessions", sessions);
	sec->Set("avg", avg);
	sec->Set("worst", worst);
	sec->Set("last", last);
	ini.Save(path);
	g_statsIniDirty = true;
}

void BigPictureFinalizeSession() {
	if (!g_sessionRunning)
		return;
	g_sessionRunning = false;
	if (g_sessionFrames < 1 || g_sessionActiveSec < 0.5)
		return;  // Too short to be a session.
	g_lastSession.valid = true;
	g_lastSession.title = g_sessionTitle.empty() ? "(unknown game)" : g_sessionTitle;
	g_lastSession.playedSec = g_sessionActiveSec;
	g_lastSession.avgFps = g_sessionFrames / g_sessionActiveSec;
	g_lastSession.worstFrameMs = g_sessionWorstMs;
	g_lastSession.longFrames = (int)g_sessionLongFrames;
	BigPictureRecordSession(g_sessionGameId.empty() ? g_sessionTitle : g_sessionGameId, g_lastSession);
	g_sessionGameId.clear();
}

Screen *CreateHomeScreen() {
	// Every return-to-menu path lands here, so the session closes exactly once.
	BigPictureFinalizeSession();
	if (g_Config.bBigPictureMode)
		return new BigPictureScreen();
	return new MainScreen();
}

static uint32_t RGB(int r, int g, int b, int a = 255) {
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

struct Point2F {
	float x;
	float y;
};

static Point2F RectPerimeterPoint(const Bounds &r, float u) {
	const float w = r.w;
	const float h = r.h;
	const float perim = 2.0f * (w + h);
	float s = u * perim;
	if (s < w)
		return Point2F{ r.x + s, r.y };
	s -= w;
	if (s < h)
		return Point2F{ r.x + w, r.y + s };
	s -= h;
	if (s < w)
		return Point2F{ r.x + w - s, r.y + h };
	s -= w;
	return Point2F{ r.x, r.y + h - s };
}

// Thin sweep around the selected card: one slow lap every 6 s over a faint
// steady outline, single ice-blue accent, gentle breathing pulse.
static void DrawCometRing(UIContext &dc, const Bounds &card) {
	const Bounds r(card.x - 2.0f, card.y - 2.0f, card.w + 4.0f, card.h + 4.0f);
	const float t = (float)time_now_d();
	const float thick = std::max(2.0f, card.w * 0.007f);
	const float pulse = 0.74f + 0.26f * (0.5f + 0.5f * std::cos(t * 1.0472f));
	const float head = std::fmod(t / 6.0f, 1.0f);
	const float tail = 0.32f;
	const int SEG = 72;
	Point2F prev = RectPerimeterPoint(r, 0.0f);
	for (int i = 1; i <= SEG; i++) {
		const float u0 = (float)(i - 1) / (float)SEG;
		const Point2F p = RectPerimeterPoint(r, (float)i / (float)SEG);
		float d = head - u0;
		if (d < 0.0f)
			d += 1.0f;
		float k = 0.0f;
		if (d <= tail)
			k = 1.0f - d / tail;
		const float wht = k * 0.55f;
		const int rr = (int)(201 + (255 - 201) * wht);
		const int gg = (int)(221 + (255 - 221) * wht);
		const int bb = (int)(240 + (255 - 240) * wht);
		float af = pulse * (0.10f + 0.78f * k * k);
		if (af > 1.0f)
			af = 1.0f;
		dc.Draw()->Line(dc.GetTheme().whiteImage, prev.x, prev.y, p.x, p.y, thick,
			RGB(rr, gg, bb, (int)(af * 255.0f)));
		prev = p;
	}
}

static std::string FormatPlayed(long long sec) {
	const long long h = sec / 3600;
	const long long m = (sec % 3600) / 60;
	char buf[64];
	if (h > 0)
		snprintf(buf, sizeof(buf), "%lld h %02lld m played", h, m);
	else
		snprintf(buf, sizeof(buf), "%lld m played", m);
	return buf;
}

static std::string FormatLastPlayed(long long unixTs) {
	if (unixTs <= 0)
		return "never";
	time_t t = (time_t)unixTs;
	struct tm when {};
	localtime_r(&t, &when);
	time_t nowT = time(nullptr);
	struct tm nowTm {};
	localtime_r(&nowT, &nowTm);
	char buf[64];
	if (when.tm_year == nowTm.tm_year && when.tm_yday == nowTm.tm_yday)
		snprintf(buf, sizeof(buf), "today %02d:%02d", when.tm_hour, when.tm_min);
	else if (when.tm_year == nowTm.tm_year && when.tm_yday == nowTm.tm_yday - 1)
		snprintf(buf, sizeof(buf), "yesterday %02d:%02d", when.tm_hour, when.tm_min);
	else
		strftime(buf, sizeof(buf), "%b %d %H:%M", &when);
	return buf;
}

class BigPictureView : public UI::View {
public:
	explicit BigPictureView(BigPictureScreen *screen, UI::LayoutParams *layoutParams)
		: UI::View(layoutParams), screen_(screen) {}

	void Draw(UIContext &dc) override;
	bool Touch(const TouchInput &input) override;

private:
	Bounds CardRect(int i) const;
	Bounds MenuPanel() const;
	Bounds MenuItem(int i) const;
	void DrawCard(UIContext &dc, const Bounds &card, int i, bool selected);
	void DrawMenu(UIContext &dc);

	BigPictureScreen *screen_;
	float scrollX_ = 0.0f;
	float selScale_ = 1.0f;
	double lastDrawT_ = -1.0;
	bool down_ = false;
	float downX_ = 0.0f;
	float downY_ = 0.0f;
};

Bounds BigPictureView::CardRect(int i) const {
	const Bounds &b = bounds_;
	const float gap = std::max(10.0f, b.h * 0.06f);
	const float cardW = std::min(b.h * 0.34f, b.w * 0.16f);
	const float cardH = cardW * 1.42f;
	const float scale = (i == screen_->SelectedIndex()) ? selScale_ : 1.0f;
	const float w = cardW * scale;
	const float h = cardH * scale;
	const float cx = b.x + i * (cardW + gap) + cardW * 0.5f - scrollX_;
	return Bounds(cx - w * 0.5f, b.y + (b.h - h) * 0.5f, w, h);
}

Bounds BigPictureView::MenuPanel() const {
	const Bounds &b = bounds_;
	const float itemH = std::max(40.0f, b.h * 0.11f);
	const float panelW = std::min(b.w * 0.5f, 440.0f);
	const float panelH = itemH * BigPictureScreen::kMenuCount + 16.0f;
	return Bounds(b.centerX() - panelW * 0.5f, b.y + b.h * 0.10f, panelW, panelH);
}

Bounds BigPictureView::MenuItem(int i) const {
	const Bounds p = MenuPanel();
	const float itemH = (p.h - 16.0f) / (float)BigPictureScreen::kMenuCount;
	return Bounds(p.x + 8.0f, p.y + 8.0f + i * itemH, p.w - 16.0f, itemH);
}

void BigPictureView::DrawCard(UIContext &dc, const Bounds &card, int i, bool selected) {
	const Path *path = screen_->GamePath(i);
	if (!path)
		return;
	auto info = g_gameInfoCache->GetInfo(dc.GetDrawContext(), *path,
		GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON | GameInfoFlags::PIC1);

	dc.DrawRectDropShadow(card, selected ? 14.0f : 8.0f, selected ? 0.7f : 0.4f);
	if (selected)
		dc.Draw()->RectVGradient(card.x, card.y, card.x2(), card.y2(), 0xFF232C3E, 0xFF0D131F);
	else
		dc.Draw()->RectVGradient(card.x, card.y, card.x2(), card.y2(), 0xA01A2231, 0x8C0B101A);

	Draw::Texture *tex = nullptr;
	if (info) {
		if (info->Ready(GameInfoFlags::ICON) && info->icon.texture)
			tex = info->icon.texture;
		else if (info->Ready(GameInfoFlags::PIC1) && info->pic1.texture)
			tex = info->pic1.texture;
	}
	if (tex) {
		// ICON0 is 144x80 (1.8:1); letterbox it into the card with a margin.
		float aw = card.w * 0.92f;
		float ah = aw / 1.8f;
		if (ah > card.h * 0.66f) {
			ah = card.h * 0.66f;
			aw = ah * 1.8f;
		}
		const float ix = card.centerX() - aw * 0.5f;
		const float iy = card.centerY() - ah * 0.5f;
		dc.Flush();
		dc.GetDrawContext()->BindTexture(0, tex);
		dc.Draw()->DrawTexRect(ix, iy, ix + aw, iy + ah, 0.0f, 0.0f, 1.0f, 1.0f,
			selected ? 0xFFFFFFFF : 0x9EFFFFFF);
		dc.Flush();
		dc.RebindTexture();
	}

	const float tagH = std::max(18.0f, card.h * 0.14f);
	dc.Draw()->Rect(card.x, card.y2() - tagH, card.w, tagH, selected ? 0xB0000000 : 0x74000000);
	const std::string title = info ? info->GetTitle() : std::string();
	dc.SetFontStyle(*UI::GetTextStyle(dc, UI::TextSize::Tiny));
	dc.DrawTextRectSqueeze(title, Bounds(card.x + 3.0f, card.y2() - tagH, card.w - 6.0f, tagH),
		selected ? 0xFFFFFFFF : 0xA0D8DEE8, ALIGN_CENTER);
	dc.Draw()->RectOutline(card.x, card.y, card.w, card.h,
		selected ? 0x50C9DDF0 : 0x26FFFFFF);
}

void BigPictureView::DrawMenu(UIContext &dc) {
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	const Bounds panel = MenuPanel();
	dc.Draw()->Rect(panel.x, panel.y, panel.w, panel.h, 0xF0141C2E);
	dc.Draw()->RectOutline(panel.x, panel.y, panel.w, panel.h, 0x50A0C0FF);

	const std::string labels[BigPictureScreen::kMenuCount] = {
		std::string(mm->T("BigPictureBrowse", "Browse games")),
		std::string(mm->T("BigPictureRecent", "Recent")),
		std::string(mm->T("Settings")),
		std::string(mm->T("BigPictureExit", "Exit Big Picture")),
	};
	dc.SetFontStyle(*UI::GetTextStyle(dc, UI::TextSize::Small));
	for (int i = 0; i < BigPictureScreen::kMenuCount; i++) {
		const Bounds item = MenuItem(i);
		if (i == screen_->MenuIndex()) {
			dc.Draw()->Rect(item.x, item.y, item.w, item.h, 0x3060A0FF);
			dc.Draw()->RectOutline(item.x, item.y, item.w, item.h, 0x80A0C8FF);
		}
		dc.DrawTextRectSqueeze(labels[i], Bounds(item.x + 16.0f, item.y, item.w - 32.0f, item.h),
			0xFFE6ECF6, ALIGN_VCENTER);
	}
}

void BigPictureView::Draw(UIContext &dc) {
	const Bounds &b = bounds_;
	const double now = time_now_d();
	float dt = lastDrawT_ < 0.0 ? 0.016f : (float)(now - lastDrawT_);
	lastDrawT_ = now;
	if (dt > 0.1f)
		dt = 0.1f;

	const int count = screen_->GameCount();
	const int sel = screen_->SelectedIndex();
	const float gap = std::max(10.0f, b.h * 0.06f);
	const float cardW = std::min(b.h * 0.34f, b.w * 0.16f);

	if (count > 0) {
		const float target = sel * (cardW + gap) + cardW * 0.5f - b.w * 0.5f;
		scrollX_ += (target - scrollX_) * std::min(1.0f, dt * 10.0f);
		selScale_ += (1.95f - selScale_) * std::min(1.0f, dt * 9.0f);
	}

	for (int i = 0; i < count; i++) {
		const Bounds card = CardRect(i);
		if (card.x > b.x2() || card.x2() < b.x)
			continue;
		DrawCard(dc, card, i, i == sel);
	}

	if (count > 0)
		DrawCometRing(dc, CardRect(sel));

	if (count == 0) {
		auto mm = GetI18NCategory(I18NCat::MAINMENU);
		dc.SetFontStyle(*UI::GetTextStyle(dc, UI::TextSize::Small));
		dc.DrawTextRectSqueeze(mm->T("BigPictureEmpty", "No games found. Press Down for the menu, then Browse games."),
			Bounds(b.x + 20.0f, b.y + b.h * 0.42f, b.w - 40.0f, 60.0f), 0xFFB0B8C8, ALIGN_HCENTER);
	}

	if (screen_->MenuOpen())
		DrawMenu(dc);
}

bool BigPictureView::Touch(const TouchInput &input) {
	if (input.flags & TouchInputFlags::DOWN) {
		down_ = true;
		downX_ = input.x;
		downY_ = input.y;
		return true;
	}
	if (input.flags & TouchInputFlags::UP) {
		if (!down_)
			return true;
		down_ = false;
		const float dx = input.x - downX_;
		const float dy = input.y - downY_;

		if (screen_->MenuOpen()) {
			for (int i = 0; i < BigPictureScreen::kMenuCount; i++) {
				if (MenuItem(i).Contains(input.x, input.y)) {
					screen_->ActivateMenu(i);
					return true;
				}
			}
			screen_->CloseMenu();
			return true;
		}
		if (std::fabs(dx) > 40.0f && std::fabs(dy) < 60.0f) {
			if (screen_->GameCount() > 0)
				screen_->Select(screen_->SelectedIndex() + (dx < 0.0f ? 1 : -1));
			return true;
		}
		for (int i = 0; i < screen_->GameCount(); i++) {
			if (CardRect(i).Contains(input.x, input.y)) {
				if (i == screen_->SelectedIndex())
					screen_->LaunchSelected();
				else
					screen_->Select(i);
				return true;
			}
		}
		return true;
	}
	return true;
}

void BigPictureScreen::StartDirListing() {
	if (dirStarted_)
		return;
	dirStarted_ = true;
	dirBrowser_.SetPath(g_Config.currentDirectory);
	dirBrowser_.Refresh();
}

void BigPictureScreen::RebuildGames() {
	games_.clear();
	auto add = [&](const Path &p) {
		const std::string s = p.ToString();
		for (const Path &g : games_) {
			if (g.ToString() == s)
				return;
		}
		games_.push_back(p);
	};
	if (recentsOnly_) {
		for (const std::string &s : g_recentFiles.GetRecentFiles())
			add(Path(s));
	} else {
		for (const std::string &s : g_recentFiles.GetRecentFiles())
			add(Path(s));
		for (const Path &p : dirGames_)
			add(p);
	}
	if (games_.empty())
		sel_ = 0;
	else if (sel_ >= (int)games_.size())
		sel_ = (int)games_.size() - 1;
}

void BigPictureScreen::RefreshPreview() {
	if (!titleText_)
		return;
	auto mm = GetI18NCategory(I18NCat::MAINMENU);
	const Path *path = GamePath(sel_);
	if (!path) {
		titleText_->SetText(mm->T("BigPictureNoGames", "No games yet"));
		if (counterText_)
			counterText_->SetText("0 / 0");
		if (statsText_)
			statsText_->SetText(mm->T("BigPictureBrowseHint", "Press Down, then Browse games"));
		return;
	}
	if (counterText_) {
		char buf[64];
		snprintf(buf, sizeof(buf), "%d / %d", sel_ + 1, (int)games_.size());
		counterText_->SetText(buf);
	}

	auto info = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), *path, GameInfoFlags::PARAM_SFO);
	const std::string title = info ? info->GetTitle() : std::string();
	titleText_->SetText(title);

	BigPictureGameStats st;
	const bool hasStats = BigPictureGetGameStats(StatsKeyForIndex(sel_), &st);
	if (!statsText_)
		return;
	if (!hasStats) {
		statsText_->SetText(mm->T("BigPictureNoSessions", "No sessions recorded yet - press PLAY"));
	} else {
		char buf[256];
		snprintf(buf, sizeof(buf), "%s | %d sessions | last %s",
			FormatPlayed(st.playedSec).c_str(), st.sessions, FormatLastPlayed(st.lastPlayedUnix).c_str());
		std::string s(buf);
		if (st.avgFps > 0.0f) {
			snprintf(buf, sizeof(buf), "\nLast session: %.1f FPS avg | %.0f ms worst", st.avgFps, st.worstFrameMs);
			s += buf;
		}
		statsText_->SetText(s);
	}
}

std::string BigPictureScreen::StatsKeyForIndex(int i) {
	const Path *path = GamePath(i);
	if (!path)
		return std::string();
	auto info = g_gameInfoCache->GetInfo(screenManager()->getDrawContext(), *path, GameInfoFlags::PARAM_SFO);
	if (info && !info->id.empty())
		return info->id;
	return SanitizeStatsKey(path->ToString());
}

void BigPictureScreen::CreateViews() {
	using namespace UI;

	root_ = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(FILL_PARENT, FILL_PARENT));

	auto mm = GetI18NCategory(I18NCat::MAINMENU);

	// Top bar: player, mode, clock.
	LinearLayout *header = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(20, 12, 20, 4)));
	const std::string nick = g_Config.sNickName.empty() ? "Player" : g_Config.sNickName;
	header->Add(new TextView(nick, ALIGN_LEFT, false));
	header->Add(new Spacer(new LinearLayoutParams(1.0f)));
	header->Add(new TextView(mm->T("BigPictureTitle", "PPSSPP BIG PICTURE"), ALIGN_HCENTER, false));
	header->Add(new Spacer(new LinearLayoutParams(1.0f)));
	clockText_ = new TextView("--:--", ALIGN_RIGHT, false);
	clockText_->SetTextSize(TextSize::Small);
	header->Add(clockText_);
	root_->Add(header);

	// The carousel itself draws cards, ring and menu; touch lives there too.
	carousel_ = new BigPictureView(this, new LinearLayoutParams(FILL_PARENT, FILL_PARENT, 1.0f));
	root_->Add(carousel_);

	// Preview block under the carousel.
	LinearLayout *titleRow = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 10, 24, 0)));
	titleText_ = new TextView("-", ALIGN_LEFT, false, new LinearLayoutParams(1.0f));
	titleText_->SetTextSize(TextSize::Big);
	titleText_->SetShadow(true);
	titleRow->Add(titleText_);
	counterText_ = new TextView("", ALIGN_RIGHT, false);
	counterText_->SetTextSize(TextSize::Small);
	titleRow->Add(counterText_);
	root_->Add(titleRow);

	statsText_ = new TextView("", ALIGN_LEFT, false, new LinearLayoutParams(FILL_PARENT, 84.0f, Margins(24, 2, 24, 2)));
	statsText_->SetTextSize(TextSize::Small);
	statsText_->SetShadow(true);
	root_->Add(statsText_);

	// Actions.
	LinearLayout *buttons = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 6, 24, 4)));
	buttons->Add(new Button(mm->T("BigPicturePlay", "PLAY")))->OnClick.Handle(this, &BigPictureScreen::OnPlay);
	buttons->Add(new Button(mm->T("BigPictureOptions", "OPTIONS")))->OnClick.Handle(this, &BigPictureScreen::OnOptions);
	buttons->Add(new Button(mm->T("BigPictureClear", "CLEAR")))->OnClick.Handle(this, &BigPictureScreen::OnClear);
	root_->Add(buttons);

	root_->Add(new TextView(mm->T("BigPictureHints", "Back exits to the standard menu"), ALIGN_LEFT, false,
		new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 0, 24, 12))));

	StartDirListing();
	RebuildGames();
	RefreshPreview();
}

void BigPictureScreen::DrawBackground(UIContext &ui) {
	const Bounds &b = ui.GetBounds();
	ui.Draw()->RectVGradient(b.x, b.y, b.x2(), b.y2(), 0xFF090D14, 0xFF141C2A);
}

void BigPictureScreen::update() {
	UIBaseScreen::update();

	if (dirStarted_ && !dirReady_ && dirBrowser_.IsListingReady()) {
		std::vector<File::FileInfo> listing;
		dirBrowser_.GetListing(listing, "iso:cso:chd:pbp:elf:prx:ppdmp:");
		for (const File::FileInfo &fi : listing) {
			if (!fi.isDirectory)
				dirGames_.push_back(fi.fullName);
		}
		dirReady_ = true;
		RebuildGames();
		RefreshPreview();
	}

	const double now = time_now_d();
	if (now >= nextRefresh_) {
		nextRefresh_ = now + 1.0;
		if (clockText_) {
			time_t tt = time(nullptr);
			struct tm tmv {};
			localtime_r(&tt, &tmv);
			char buf[8];
			snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
			clockText_->SetText(buf);
		}
		RefreshPreview();
	}
}

void BigPictureScreen::Select(int i) {
	if (games_.empty())
		return;
	const int n = (int)games_.size();
	sel_ = ((i % n) + n) % n;
	RefreshPreview();
}

void BigPictureScreen::LaunchSelected() {
	const Path *path = GamePath(sel_);
	if (path)
		LaunchFile(screenManager(), this, *path);
}

void BigPictureScreen::ActivateMenu(int idx) {
	menuOpen_ = false;
	switch (idx) {
	case 0:
		screenManager()->push(new BigPictureBrowseScreen());
		break;
	case 1:
		recentsOnly_ = !recentsOnly_;
		RebuildGames();
		RefreshPreview();
		break;
	case 2:
		screenManager()->push(new GameSettingsScreen(Path()));
		break;
	case 3:
		screenManager()->switchScreen(new MainScreen());
		break;
	default:
		break;
	}
}

void BigPictureScreen::OnPlay(UI::EventParams &e) {
	LaunchSelected();
}

void BigPictureScreen::OnOptions(UI::EventParams &e) {
	const Path *path = GamePath(sel_);
	screenManager()->push(new GameSettingsScreen(path ? *path : Path()));
}

void BigPictureScreen::OnClear(UI::EventParams &e) {
	if (games_.empty())
		return;
	games_.erase(games_.begin() + sel_);
	if (games_.empty())
		sel_ = 0;
	else if (sel_ >= (int)games_.size())
		sel_ = (int)games_.size() - 1;
	RefreshPreview();
}

bool BigPictureScreen::key(const KeyInput &key) {
	if (!(key.flags & KeyInputFlags::DOWN))
		return UIBaseScreen::key(key);

	if (menuOpen_) {
		switch (key.keyCode) {
		case NKCODE_DPAD_DOWN:
			menuIdx_ = (menuIdx_ + 1) % kMenuCount;
			return true;
		case NKCODE_DPAD_UP:
			menuIdx_ = (menuIdx_ + kMenuCount - 1) % kMenuCount;
			return true;
		default:
			break;
		}
		if (UI::IsAcceptKey(key)) {
			ActivateMenu(menuIdx_);
			return true;
		}
		if (UI::IsEscapeKey(key)) {
			menuOpen_ = false;
			return true;
		}
		return true;  // Swallow everything else while the menu is up.
	}

	switch (key.keyCode) {
	case NKCODE_DPAD_RIGHT:
		if (!games_.empty())
			Select(sel_ + 1);
		return true;
	case NKCODE_DPAD_LEFT:
		if (!games_.empty())
			Select(sel_ - 1);
		return true;
	case NKCODE_DPAD_UP:
	case NKCODE_DPAD_DOWN:
		menuOpen_ = true;
		menuIdx_ = 0;
		return true;
	default:
		break;
	}

	if (UI::IsAcceptKey(key)) {
		LaunchSelected();
		return true;
	}
	if (UI::IsEscapeKey(key)) {
		screenManager()->switchScreen(new MainScreen());
		return true;
	}
	return UIBaseScreen::key(key);
}

void BigPictureBrowseScreen::CreateViews() {
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
	browser_->OnChoice.Handle(this, &BigPictureBrowseScreen::OnGameSelected);
	root_->Add(browser_);

	// Footer: hint on the left, actions on the right.
	LinearLayout *footer = new LinearLayout(ORIENT_HORIZONTAL, new LinearLayoutParams(FILL_PARENT, WRAP_CONTENT, Margins(24, 8, 24, 16)));
	footer->Add(new TextView(mm->T("BigPictureHints", "Back returns to Big Picture"), ALIGN_LEFT, false));
	footer->Add(new Spacer(new LinearLayoutParams(1.0f)));
	footer->Add(new Button(mm->T("Settings")))->OnClick.Handle(this, &BigPictureBrowseScreen::OnSettings);
	footer->Add(new Button(mm->T("BigPictureExit", "Exit Big Picture")))->OnClick.Handle(this, &BigPictureBrowseScreen::OnExit);
	root_->Add(footer);
}

void BigPictureBrowseScreen::OnGameSelected(UI::EventParams &e) {
	LaunchFile(screenManager(), nullptr, Path(e.s));
}

void BigPictureBrowseScreen::OnSettings(UI::EventParams &e) {
	// Not passing a game ID, changing the global settings.
	screenManager()->push(new GameSettingsScreen(Path()));
}

void BigPictureBrowseScreen::OnExit(UI::EventParams &e) {
	// Back to the carousel home rather than the standard menu.
	TriggerFinish(DR_BACK);
}

bool BigPictureBrowseScreen::key(const KeyInput &key) {
	bool result = UIBaseScreen::key(key);
	if (!result && (key.flags & KeyInputFlags::DOWN) && UI::IsEscapeKey(key)) {
		TriggerFinish(DR_BACK);
		return true;
	}
	return result;
}
