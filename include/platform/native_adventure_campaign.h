#ifndef PLATFORM_NATIVE_ADVENTURE_CAMPAIGN_H
#define PLATFORM_NATIVE_ADVENTURE_CAMPAIGN_H

#include <platform/native_memcard.h>

struct AdvProgress;

enum NativeAdventureCampaignResult
{
	NATIVE_ADVENTURE_CAMPAIGN_OK = 0,
	NATIVE_ADVENTURE_CAMPAIGN_NOT_FOUND,
	NATIVE_ADVENTURE_CAMPAIGN_INVALID,
	NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR,
};

enum NativeAdventureCampaignResult NativeAdventureCampaign_Load(struct AdvProgress *progress);
enum NativeAdventureCampaignResult NativeAdventureCampaign_Save(const struct AdvProgress *progress);
enum NativeAdventureCampaignResult NativeAdventureCampaign_Delete(void);
int NativeAdventureCampaign_Exists(void);
void NativeAdventureCampaign_BeginCreation(void);
void NativeAdventureCampaign_CancelInput(void);
int NativeAdventureCampaign_NameEntered(const char *name);
int NativeAdventureCampaign_IsCreating(void);
int NativeAdventureCampaign_IsAwaitingPilot(void);
void NativeAdventureCampaign_FinishCreation(void);
int NativeAdventureCampaign_RunSelfTest(void);
#if defined(__ANDROID__)
void Platform_Android_ShowCampaignStorageError(void);
#endif

#endif
