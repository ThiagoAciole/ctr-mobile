#include <platform/native_adventure_campaign.h>

#include <namespace_Memcard.h>
#include <SDL3/SDL.h>

#include <string.h>

enum
{
	NATIVE_ADVENTURE_CAMPAIGN_VERSION = 1,
};

static const char s_campaignFile[] = "CTR-ADVENTURE-CAMPAIGN";
static const unsigned char s_campaignMagic[8] = {'C', 'T', 'R', 'A', 'D', 'V', 'C', '1'};
static int s_creationState;

enum
{
	NATIVE_ADVENTURE_CAMPAIGN_CREATE_NAME = 1,
	NATIVE_ADVENTURE_CAMPAIGN_CREATE_PILOT = 2,
};

struct NativeAdventureCampaignRecord
{
	unsigned char magic[8];
	u32 version;
	u32 progressSize;
	u32 checksum;
	struct AdvProgress progress;
};

static u32 NativeAdventureCampaign_Checksum(const void *data, int size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	u32 checksum = 2166136261u;

	for (int i = 0; i < size; i++)
	{
		checksum = (checksum ^ bytes[i]) * 16777619u;
	}
	return checksum;
}

int NativeAdventureCampaign_Exists(void)
{
	return NativeMemcard_FileExists(s_campaignFile);
}

enum NativeAdventureCampaignResult NativeAdventureCampaign_Load(struct AdvProgress *progress)
{
	struct NativeAdventureCampaignRecord record;
	int size;

	if (progress == NULL)
	{
		return NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR;
	}
	if (!NativeAdventureCampaign_Exists())
	{
		return NATIVE_ADVENTURE_CAMPAIGN_NOT_FOUND;
	}
	size = NativeMemcard_GetFileSize(s_campaignFile);
	if (size != (int)sizeof(record))
	{
		return (size < 0) ? NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR : NATIVE_ADVENTURE_CAMPAIGN_INVALID;
	}
	if (NativeMemcard_ReadSaveData(s_campaignFile, (unsigned char *)&record, sizeof(record), 0) != NATIVE_MEMCARD_OK)
	{
		return NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR;
	}
	if ((memcmp(record.magic, s_campaignMagic, sizeof(s_campaignMagic)) != 0) ||
	    (record.version != NATIVE_ADVENTURE_CAMPAIGN_VERSION) || (record.progressSize != sizeof(record.progress)) ||
	    (record.checksum != NativeAdventureCampaign_Checksum(&record.progress, sizeof(record.progress))) ||
	    (record.progress.characterID < 0) || (record.progress.name[0] == '\0') ||
	    (memchr(record.progress.name, '\0', sizeof(record.progress.name)) == NULL))
	{
		return NATIVE_ADVENTURE_CAMPAIGN_INVALID;
	}
	*progress = record.progress;
	return NATIVE_ADVENTURE_CAMPAIGN_OK;
}

enum NativeAdventureCampaignResult NativeAdventureCampaign_Save(const struct AdvProgress *progress)
{
	struct NativeAdventureCampaignRecord record;
	enum NativeAdventureCampaignResult currentResult;
	struct AdvProgress current;

	if ((progress == NULL) || (progress->characterID < 0) || (progress->name[0] == '\0'))
	{
		return NATIVE_ADVENTURE_CAMPAIGN_INVALID;
	}
	if (NativeAdventureCampaign_Exists())
	{
		currentResult = NativeAdventureCampaign_Load(&current);
		if (currentResult != NATIVE_ADVENTURE_CAMPAIGN_OK)
		{
			return currentResult;
		}
	}
	memset(&record, 0, sizeof(record));
	memcpy(record.magic, s_campaignMagic, sizeof(s_campaignMagic));
	record.version = NATIVE_ADVENTURE_CAMPAIGN_VERSION;
	record.progressSize = sizeof(record.progress);
	record.progress = *progress;
	record.checksum = NativeAdventureCampaign_Checksum(&record.progress, sizeof(record.progress));
	return (NativeMemcard_WriteSaveDataAtomic(s_campaignFile, (const unsigned char *)&record, sizeof(record)) == NATIVE_MEMCARD_OK)
	           ? NATIVE_ADVENTURE_CAMPAIGN_OK
	           : NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR;
}

void NativeAdventureCampaign_BeginCreation(void)
{
	s_creationState = NATIVE_ADVENTURE_CAMPAIGN_CREATE_NAME;
}

void NativeAdventureCampaign_CancelInput(void)
{
	s_creationState = 0;
}

int NativeAdventureCampaign_IsCreating(void)
{
	return s_creationState == NATIVE_ADVENTURE_CAMPAIGN_CREATE_NAME;
}

int NativeAdventureCampaign_IsAwaitingPilot(void)
{
	return s_creationState == NATIVE_ADVENTURE_CAMPAIGN_CREATE_PILOT;
}

void NativeAdventureCampaign_FinishCreation(void)
{
	s_creationState = 0;
}

int NativeAdventureCampaign_NameEntered(const char *name)
{
	int length;

	if ((name == NULL) || (s_creationState != NATIVE_ADVENTURE_CAMPAIGN_CREATE_NAME))
	{
		return 0;
	}
	length = (int)strlen(name);
	if ((length <= 0) || (length >= (int)sizeof(((struct AdvProgress *)0)->name)))
	{
		return 0;
	}
	s_creationState = NATIVE_ADVENTURE_CAMPAIGN_CREATE_PILOT;
	return 1;
}

enum NativeAdventureCampaignResult NativeAdventureCampaign_Delete(void)
{
	enum NativeMemcardResult result = NativeMemcard_RemoveFile(s_campaignFile);

	if (result == NATIVE_MEMCARD_OK)
	{
		return NATIVE_ADVENTURE_CAMPAIGN_OK;
	}
	return NativeAdventureCampaign_Exists() ? NATIVE_ADVENTURE_CAMPAIGN_IO_ERROR : NATIVE_ADVENTURE_CAMPAIGN_NOT_FOUND;
}

int NativeAdventureCampaign_RunSelfTest(void)
{
	static const char campaignFile[] = "CTR-ADVENTURE-CAMPAIGN";
	static const char legacyFile[] = "BASCUS-94426-SLOTS";
	static const unsigned char legacyData[] = {0x43, 0x54, 0x52, 0x00, 0xff, 0x19};
	unsigned char legacyReadback[sizeof(legacyData)];
	struct AdvProgress progress;
	struct AdvProgress loaded;
	char testRoot[512];
	const char *basePath = SDL_GetBasePath();
	int ok = 0;

	if (SDL_snprintf(testRoot, sizeof(testRoot), "%sctr-native-campaign-test-%llu", (basePath != NULL) ? basePath : ".",
	                 (unsigned long long)SDL_GetTicksNS()) >= (int)sizeof(testRoot))
	{
		SDL_free((void *)basePath);
		return 0;
	}
	SDL_free((void *)basePath);
	if ((NativeMemcard_SetRoot(testRoot) != NATIVE_MEMCARD_OK) ||
	    (NativeMemcard_WriteSaveData(legacyFile, NULL, 0, legacyData, sizeof(legacyData)) != NATIVE_MEMCARD_OK))
	{
		goto cleanup;
	}

	memset(&progress, 0, sizeof(progress));
	SDL_strlcpy(progress.name, "TEST RACER", sizeof(progress.name));
	progress.characterID = 3;
	progress.HubLevYouSavedOn = 0x27;
	progress.storyFlags = 0x12345678;
	if ((NativeAdventureCampaign_Save(&progress) != NATIVE_ADVENTURE_CAMPAIGN_OK) ||
	    (NativeAdventureCampaign_Load(&loaded) != NATIVE_ADVENTURE_CAMPAIGN_OK) || (memcmp(&loaded, &progress, sizeof(progress)) != 0))
	{
		goto cleanup;
	}
	if ((NativeMemcard_WriteSaveDataAtomic("missing/subdir", legacyData, sizeof(legacyData)) == NATIVE_MEMCARD_OK) ||
	    (NativeAdventureCampaign_Load(&loaded) != NATIVE_ADVENTURE_CAMPAIGN_OK) ||
	    (NativeMemcard_ReadSaveData(legacyFile, legacyReadback, sizeof(legacyReadback), 0) != NATIVE_MEMCARD_OK) ||
	    (memcmp(legacyData, legacyReadback, sizeof(legacyData)) != 0))
	{
		goto cleanup;
	}
	if ((NativeMemcard_WriteSaveDataAtomic(campaignFile, legacyData, 1) != NATIVE_MEMCARD_OK) ||
	    (NativeAdventureCampaign_Load(&loaded) != NATIVE_ADVENTURE_CAMPAIGN_INVALID) ||
	    (NativeAdventureCampaign_Save(&progress) != NATIVE_ADVENTURE_CAMPAIGN_INVALID) || (NativeMemcard_GetFileSize(campaignFile) != 1))
	{
		goto cleanup;
	}
	if ((NativeAdventureCampaign_Delete() != NATIVE_ADVENTURE_CAMPAIGN_OK) || NativeAdventureCampaign_Exists() ||
	    (NativeMemcard_ReadSaveData(legacyFile, legacyReadback, sizeof(legacyReadback), 0) != NATIVE_MEMCARD_OK) ||
	    (memcmp(legacyData, legacyReadback, sizeof(legacyData)) != 0))
	{
		goto cleanup;
	}
	ok = 1;

cleanup:
	NativeMemcard_ClearRoot();
	if (NativeMemcard_RemoveRoot(testRoot) != NATIVE_MEMCARD_OK)
	{
		ok = 0;
	}
	return ok;
}
