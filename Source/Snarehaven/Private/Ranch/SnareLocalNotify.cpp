#include "Ranch/SnareLocalNotify.h"

#include "Snarehaven.h"
#include "HAL/PlatformMisc.h"
#include "LocalNotification.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Ranch/RanchComponent.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	const TCHAR* const NotifySection = TEXT("/Script/Snarehaven.SnareLocalNotify");

	bool& EnabledStorage()
	{
		static bool bEnabled = true;
		static bool bLoaded = false;
		if (!bLoaded && GConfig != nullptr)
		{
			bLoaded = true;
			GConfig->GetBool(NotifySection, TEXT("Enabled"), bEnabled, GGameUserSettingsIni);
		}
		return bEnabled;
	}

	/** 엔진 로컬 알림 서비스 — 플랫폼 ini `[LocalNotification] DefaultPlatformService` 의 모듈(iOS·안드로이드만 있다). */
	ILocalNotificationService* FindPlatformService()
	{
		static ILocalNotificationService* Service = nullptr;
		static bool bLooked = false;
		if (!bLooked && GConfig != nullptr)
		{
			bLooked = true;
			FString ModuleName;
			GConfig->GetString(TEXT("LocalNotification"), TEXT("DefaultPlatformService"), ModuleName, GEngineIni);
			ILocalNotificationModule* Module = ModuleName.IsEmpty()
				? nullptr : FModuleManager::LoadModulePtr<ILocalNotificationModule>(*ModuleName);
			Service = Module != nullptr ? Module->GetLocalNotificationService() : nullptr;
		}
		return Service;
	}

	class FEngineSink : public SnareLocalNotify::ISink
	{
	public:
		explicit FEngineSink(ILocalNotificationService* InService) : Service(InService) {}

		virtual void ClearAll() override
		{
			if (Service != nullptr)
			{
				Service->ClearAllLocalNotifications();
			}
		}

		virtual void Schedule(const SnareLocalNotify::FPlanned& Planned) override
		{
			const bool bFull = Planned.Kind == SnareLocalNotify::EKind::RanchFull;
			const FText Body = bFull
				? LOCTEXT("NotifyRanchFull", "목장이 가득 찼어요. 거둬야 다시 생산해요.")
				: FText::Format(LOCTEXT("NotifyUpkeepLow", "목장 유지가 {0}시간 남았어요. 나무를 넣어 주세요."),
					FText::AsNumber(SnareLocalNotify::FRules().UpkeepWarnHours));
			const FString Event = bFull ? TEXT("ranch_full") : TEXT("upkeep_low");
			if (Service != nullptr)
			{
				Service->ScheduleLocalNotificationAtTime(Planned.FireLocal, true,
					LOCTEXT("NotifyTitle", "목장"), Body, LOCTEXT("NotifyAction", "열기"), Event);
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Notify] 예약 %s 현지 %s%s"), *Event, *Planned.FireLocal.ToString(),
				Service != nullptr ? TEXT("") : TEXT(" (이 기기엔 알림 서비스가 없어 안 걸었다)"));
		}

	private:
		ILocalNotificationService* Service = nullptr;
	};

	const TCHAR* const SentKey = TEXT("SentLocal");

	SnareLocalNotify::ISink* GTestSink = nullptr;

	/** 지난번에 건 현지 시각들 (`GameUserSettings.ini` `SentLocal`). */
	TArray<FDateTime> LoadSentLocal()
	{
		TArray<FString> SentStrings;
		TArray<FDateTime> SentLocal;
		if (GConfig != nullptr)
		{
			GConfig->GetArray(NotifySection, SentKey, SentStrings, GGameUserSettingsIni);
		}
		for (const FString& Sent : SentStrings)
		{
			FDateTime Parsed;
			if (FDateTime::ParseIso8601(*Sent, Parsed))
			{
				SentLocal.Add(Parsed);
			}
		}
		return SentLocal;
	}

	void SaveSentLocal(const TArray<FDateTime>& SentLocal)
	{
		if (GConfig == nullptr)
		{
			return;
		}
		TArray<FString> SentStrings;
		for (const FDateTime& Sent : SentLocal)
		{
			SentStrings.Add(Sent.ToIso8601());
		}
		GConfig->SetArray(NotifySection, SentKey, SentStrings, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	int32 CountOnDay(const TArray<FDateTime>& Times, const FDateTime& Day)
	{
		int32 Count = 0;
		for (const FDateTime& Time : Times)
		{
			if (Time.GetDate() == Day.GetDate())
			{
				++Count;
			}
		}
		return Count;
	}
}

FDateTime SnareLocalNotify::PushOutOfQuietHours(const FDateTime& Local, const FRules& Rules)
{
	const int32 Hour = Local.GetHour();
	const FDateTime MorningToday = Local.GetDate() + FTimespan::FromHours(Rules.QuietEndHour);
	if (Hour < Rules.QuietEndHour)
	{
		return MorningToday;
	}
	if (Hour >= Rules.QuietStartHour)
	{
		return MorningToday + FTimespan::FromDays(1);
	}
	return Local;
}

void SnareLocalNotify::Plan(const FDateTime& NowUtc, const FTimespan& UtcToLocal, const FDateTime* LastHarvestUtc,
	const FDateTime* UpkeepEndUtc, const TArray<FDateTime>& AlreadyScheduledLocal, const FRules& Rules,
	bool bEnabled, TArray<FPlanned>& OutPlanned)
{
	OutPlanned.Reset();
	if (!bEnabled || Rules.DailyCap <= 0)
	{
		return;
	}

	TArray<FPlanned> Candidates;
	if (LastHarvestUtc != nullptr)
	{
		const FDateTime FireUtc = *LastHarvestUtc + FTimespan::FromMinutes(Rules.RanchFullMinutes);
		if (FireUtc > NowUtc)
		{
			Candidates.Add({EKind::RanchFull, FireUtc + UtcToLocal});
		}
	}
	if (UpkeepEndUtc != nullptr)
	{
		const FDateTime FireUtc = *UpkeepEndUtc - FTimespan::FromHours(Rules.UpkeepWarnHours);
		if (FireUtc > NowUtc)
		{
			Candidates.Add({EKind::UpkeepLow, FireUtc + UtcToLocal});
		}
	}
	Candidates.Sort([](const FPlanned& A, const FPlanned& B) { return A.FireLocal < B.FireLocal; });

	TArray<FDateTime> Taken = AlreadyScheduledLocal;
	for (FPlanned& Candidate : Candidates)
	{
		FDateTime Fire = PushOutOfQuietHours(Candidate.FireLocal, Rules);
		while (CountOnDay(Taken, Fire) >= Rules.DailyCap)
		{
			Fire = Fire.GetDate() + FTimespan::FromDays(1) + FTimespan::FromHours(Rules.QuietEndHour);
		}
		if (Candidate.Kind == EKind::UpkeepLow && Fire >= *UpkeepEndUtc + UtcToLocal)
		{
			continue;
		}
		Candidate.FireLocal = Fire;
		Taken.Add(Fire);
		OutPlanned.Add(Candidate);
	}
}

void SnareLocalNotify::ReadRanchTimes(const TArray<FRanchSlotState>& Slots, const FRanchUpkeepState& Upkeep,
	const FDateTime& NowUtc, TOptional<FDateTime>& OutLastHarvestUtc, TOptional<FDateTime>& OutUpkeepEndUtc)
{
	OutLastHarvestUtc.Reset();
	OutUpkeepEndUtc.Reset();
	if (!Upkeep.bReceived || Upkeep.bLocked)
	{
		return;
	}
	for (const FRanchSlotState& Slot : Slots)
	{
		if (Slot.Kind != ERanchSlotKind::AnimalProduct || Slot.IsEmpty() || Slot.OccupiedSinceUtc == FDateTime::MinValue())
		{
			continue;
		}
		if (!OutLastHarvestUtc.IsSet() || Slot.OccupiedSinceUtc < OutLastHarvestUtc.GetValue())
		{
			OutLastHarvestUtc = Slot.OccupiedSinceUtc;
		}
	}
	if (Upkeep.LeftHours >= 0)
	{
		OutUpkeepEndUtc = NowUtc + FTimespan::FromHours(Upkeep.LeftHours);
	}
}

int32 SnareLocalNotify::Reschedule(ISink& Sink, const FDateTime& NowUtc, const FTimespan& UtcToLocal,
	const TOptional<FDateTime>& LastHarvestUtc, const TOptional<FDateTime>& UpkeepEndUtc, const FRules& Rules,
	bool bEnabled, TArray<FDateTime>& InOutSentLocal)
{
	const FDateTime NowLocal = NowUtc + UtcToLocal;
	TArray<FDateTime> FiredToday;
	for (const FDateTime& Sent : InOutSentLocal)
	{
		if (Sent <= NowLocal && Sent.GetDate() == NowLocal.GetDate())
		{
			FiredToday.Add(Sent);
		}
	}

	Sink.ClearAll();
	TArray<FPlanned> Planned;
	Plan(NowUtc, UtcToLocal, LastHarvestUtc.GetPtrOrNull(), UpkeepEndUtc.GetPtrOrNull(), FiredToday, Rules, bEnabled,
		Planned);
	InOutSentLocal = FiredToday;
	for (const FPlanned& One : Planned)
	{
		Sink.Schedule(One);
		InOutSentLocal.Add(One.FireLocal);
	}
	return Planned.Num();
}

void SnareLocalNotify::RescheduleFromRanch(const TArray<FRanchSlotState>& Slots, const FRanchUpkeepState& Upkeep)
{
	const FDateTime NowUtc = FDateTime::UtcNow();
	const FTimespan UtcToLocal = FDateTime::Now() - NowUtc;
	TOptional<FDateTime> LastHarvestUtc;
	TOptional<FDateTime> UpkeepEndUtc;
	ReadRanchTimes(Slots, Upkeep, NowUtc, LastHarvestUtc, UpkeepEndUtc);

	const bool bEnabled = IsEnabled();
	static bool bAskedPermission = false;
	if (bEnabled && !bAskedPermission)
	{
		// iOS 는 이 호출이 「알림 허용」 창을 띄운다(무인 실행·데스크톱은 아무것도 안 한다). 한 실행에 한 번.
		bAskedPermission = true;
		FPlatformMisc::RegisterForRemoteNotifications();
	}

	TArray<FDateTime> SentLocal = LoadSentLocal();
	FEngineSink EngineSink(FindPlatformService());
	ISink& Sink = GTestSink != nullptr ? *GTestSink : static_cast<ISink&>(EngineSink);
	const int32 Count = Reschedule(Sink, NowUtc, UtcToLocal, LastHarvestUtc, UpkeepEndUtc, FRules(), bEnabled, SentLocal);
	UE_LOG(LogSnarehaven, Log, TEXT("[Notify] 다시 걸기: %s · 건 알림 %d개 (수확 기준 %s · 유지 끝 %s)"),
		bEnabled ? TEXT("켬") : TEXT("꺼짐 — 전부 지움"), Count,
		LastHarvestUtc.IsSet() ? *LastHarvestUtc->ToIso8601() : TEXT("없음"),
		UpkeepEndUtc.IsSet() ? *UpkeepEndUtc->ToIso8601() : TEXT("없음"));

	SaveSentLocal(SentLocal);
}

bool SnareLocalNotify::IsEnabled()
{
	return EnabledStorage();
}

void SnareLocalNotify::SetEnabled(bool bEnabled)
{
	EnabledStorage() = bEnabled;
	if (GConfig != nullptr)
	{
		GConfig->SetBool(NotifySection, TEXT("Enabled"), bEnabled, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

void SnareLocalNotify::ApplySetting(bool bEnabled)
{
	SetEnabled(bEnabled);
	if (bEnabled)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Notify] 설정: 켬 — 다음에 목장을 받을 때 다시 건다"));
		return;
	}
	const FDateTime NowUtc = FDateTime::UtcNow();
	TArray<FDateTime> SentLocal = LoadSentLocal();
	FEngineSink EngineSink(FindPlatformService());
	ISink& Sink = GTestSink != nullptr ? *GTestSink : static_cast<ISink&>(EngineSink);
	Reschedule(Sink, NowUtc, FDateTime::Now() - NowUtc, TOptional<FDateTime>(), TOptional<FDateTime>(), FRules(), false,
		SentLocal);
	SaveSentLocal(SentLocal);
	UE_LOG(LogSnarehaven, Log, TEXT("[Notify] 설정: 끔 — 걸어 둔 알림을 전부 지웠다"));
}

void SnareLocalNotify::SetSinkForTest(ISink* Sink)
{
	GTestSink = Sink;
}

#undef LOCTEXT_NAMESPACE
