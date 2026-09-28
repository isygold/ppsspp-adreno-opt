#pragma once

#include <string>
#include <vector>

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "UI/TabbedDialogScreen.h"

class SystemInfoScreen : public UITabbedBaseDialogScreen {
public:
	SystemInfoScreen(const Path &filename) : UITabbedBaseDialogScreen(filename) {}

	const char *tag() const override { return "SystemInfo"; }

	void CreateTabs() override;
	void update() override;
	void resized() override { RecreateViews(); }

protected:
	bool ShowSearchControls() const override { return false; }

private:
	void CreateDeviceInfoTab(UI::LinearLayout *deviceInfo);
	void CreateStorageTab(UI::LinearLayout *storage);
	void CreateBuildConfigTab(UI::LinearLayout *storage);
	void CreateCPUExtensionsTab(UI::LinearLayout *storage);
	void CreateAudioInfoTab(UI::LinearLayout *storage);
	void CreateDriverBugsTab(UI::LinearLayout *storage);
	void CreateOpenGLExtsTab(UI::LinearLayout *gpuExtensions);
	void CreateVulkanExtsTab(UI::LinearLayout *gpuExtensions);
	void CreatePipelineLogTab(UI::LinearLayout *pipelineLog);
	void SavePipelineLogEntries();

	// Pipeline log tab state.
	UI::TextView *pipelineLogEntries_ = nullptr;
	int pipelineLogEntriesShown_ = 0;
	bool pipelineLogWasRecording_ = false;
	bool pipelineLogInit_ = false;
};
