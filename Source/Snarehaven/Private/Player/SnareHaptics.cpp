#include "Player/SnareHaptics.h"
#include "Snarehaven.h"

#include "GameFramework/PlayerController.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const HapticsSection = TEXT("/Script/Snarehaven.SnareHaptics");
	int32 PlayCount = 0;

	bool& EnabledStorage()
	{
		static bool bEnabled = true;
		static bool bLoaded = false;
		if (!bLoaded && GConfig != nullptr)
		{
			bLoaded = true;
			GConfig->GetBool(HapticsSection, TEXT("Enabled"), bEnabled, GGameUserSettingsIni);
		}
		return bEnabled;
	}
}

bool SnareHaptics::IsEnabled()
{
	return EnabledStorage();
}

void SnareHaptics::SetEnabled(bool bEnabled)
{
	EnabledStorage() = bEnabled;
	if (GConfig != nullptr)
	{
		GConfig->SetBool(HapticsSection, TEXT("Enabled"), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

bool SnareHaptics::Play(APlayerController* Controller, float Intensity, float Seconds)
{
	if (!IsEnabled() || !IsValid(Controller) || Intensity <= 0.f || Seconds <= 0.f)
	{
		return false;
	}
	Controller->PlayDynamicForceFeedback(FMath::Min(Intensity, 1.f), Seconds, true, true, true, true);
	++PlayCount;
	UE_LOG(LogSnarehaven, Log, TEXT("[Haptics] 진동 %.2f · %.2f초"), Intensity, Seconds);
	return true;
}

bool SnareHaptics::PlayLight(APlayerController* Controller)
{
	return Play(Controller, LightIntensity, LightSeconds);
}

int32 SnareHaptics::GetPlayCount()
{
	return PlayCount;
}
