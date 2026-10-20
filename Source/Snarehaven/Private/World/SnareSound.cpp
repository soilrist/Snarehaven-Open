#include "World/SnareSound.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/StrongObjectPtr.h"
#include "Building/SnareBuildRules.h"
#include "Data/ItemRow.h"
#include "Data/ZoneRow.h"
#include "World/SeaActor.h"
#include "World/SnareDayNight.h"
#include "World/SnareWeatherView.h"
#include "World/SnareZoneRules.h"

namespace
{
	constexpr int32 MaxVariants = 9;
	constexpr float OceanFloorVolume = 0.12f;     // 섬 한가운데서도 멀리 파도가 들린다
	constexpr float OceanFadeCm = 3000.f;
	constexpr float WindVolume = 0.3f;
	constexpr float BirdMinGapSeconds = 2.5f;
	constexpr float BirdMaxGapSeconds = 7.f;
	constexpr float BirdVolume = 0.45f;
	constexpr float BirdShoreMarginCm = 300.f;   // 물 위·물가에선 새 대신 파도
	// 존 소리 풍경 (DESIGN 2.2 M46-00) — 평원 = 위 값 그대로, 다른 존은 곱·간격만 다르다.
	constexpr float ForestWindScale = 0.8f;
	constexpr float ForestBirdMinGapSeconds = 1.5f;
	constexpr float ForestBirdMaxGapSeconds = 4.5f;
	constexpr float SnowWindScale = 2.f;
	constexpr float SwampWindScale = 0.7f;
	constexpr float SwampBirdMinGapSeconds = 5.f;
	constexpr float SwampBirdMaxGapSeconds = 12.f;
	constexpr float SwampLoopVolume = 0.5f;
	constexpr float NightLoopVolume = 0.4f;
	// 날씨 소리 (DESIGN 3.3 「날씨」 M104-03b)
	constexpr float RainWindScale = 1.5f;
	constexpr float SnowWeatherWindScale = 1.2f;
	constexpr float RainLoopVolume = 0.5f;
	constexpr float RainFootstepScale = 0.8f;
	constexpr float NightThreshold = 0.5f;       // 새소리 줄과 같은 낮·밤 경계
	constexpr float AmbienceFadeSeconds = 4.f;   // 달리기 600cm/s × 4초 = 24m 동안 섞여 들림
	// 카메라가 위에서 내려다봐(줌 900~2500cm) 발밑까지 멀다 — 안쪽 2500cm 는 크기 그대로, 방향만 준다.
	constexpr float HearFullRadiusCm = 2500.f;
	constexpr float HearFalloffCm = 4000.f;

	// 장비·탈것 소리 (DESIGN 2.2 M47-00) — 크기·같은 사람 최소 간격.
	constexpr float HandChangeVolume = 0.6f;     // 줍기 0.7 보다 한 칸 아래
	constexpr float OutfitVolume = 0.5f;         // 판 열기/닫기와 같게
	constexpr float MountOnVolume = 0.8f;        // 동물 맞음과 같게
	constexpr float MountOffVolume = 0.6f;
	constexpr float HandChangeGapSeconds = 0.25f;
	constexpr float OutfitGapSeconds = 0.25f;
	constexpr float MountGapSeconds = 0.6f;      // 8.7 같은 동물 같은 소리 간격

	// 가방·상자 소리 (DESIGN 8.12 M49-00) — 크기.
	constexpr float BagDropVolume = 0.8f;        // 철거 1.0 보다 한 칸 아래(천 자루)
	constexpr float BagPickupVolume = 0.7f;      // 지급 `Pickup` 과 같게
	constexpr float ChestOpenVolume = 0.8f;      // 가챠 상자와 같게

	// 목장 소리 (DESIGN 3.2 M50-00) — 크기. 새 숫자 0개(전부 다른 표 값 빌림).
	constexpr float RanchWelcomeVolume = 0.8f;   // 상자 열기와 같게(포획의 보상 순간)
	constexpr float RanchReleaseVolume = 0.7f;   // 보관함과 같게(칸에서 꺼냄)
	constexpr float RanchCosmeticVolume = 0.5f;  // 옷 그대로
	constexpr float RanchDecoVolume = 1.f;       // 설치 확인·심기 그대로
	constexpr float RanchExpandVolume = 1.f;     // 철거와 같게
	constexpr float RanchUpkeepVolume = 0.7f;    // 유지 나무 그대로
	constexpr float RanchFishingVolume = 0.5f;   // 찌 떨어짐 그대로

	// 배 소리 (DESIGN 8.9 M51-00) — 크기. 새 숫자 0개.
	constexpr float BoatLaunchVolume = 0.8f;     // 물에 들어감 `Splash` 와 같게
	constexpr float BoatBoardVolume = 0.7f;      // 보관함과 같게
	constexpr float BoatLeaveVolume = 0.6f;      // 타기보다 한 단 낮게(떠나는 몸짓)
	constexpr float BoatStowVolume = 0.7f;       // 보관함 빌림
	constexpr float DenyVolume = 0.45f;          // 막힘 소리 (DESIGN 2.2 M55-00)
	constexpr float DenyMinGapSeconds = 0.4f;

	// 존·광산 건너기 소리 (DESIGN 8.10 M53-00) — 크기. 새 숫자 0개.
	constexpr float ZonePortalVolume = 0.8f;     // 빌리던 `Chop` 과 같게(큰 몸짓)
	constexpr float ZoneFerryVolume = 0.7f;      // 배 타기 `BoatBoard` 그대로
	constexpr float ZoneMineDoorVolume = 0.8f;   // 빌리던 `Mine` 과 같게

	USoundAttenuation* WorldAttenuation()
	{
		static TStrongObjectPtr<USoundAttenuation> Attenuation;
		if (!Attenuation.IsValid())
		{
			Attenuation.Reset(NewObject<USoundAttenuation>());
			FSoundAttenuationSettings& S = Attenuation->Attenuation;
			S.bAttenuate = true;
			S.bSpatialize = true;
			S.AttenuationShape = EAttenuationShape::Sphere;
			S.AttenuationShapeExtents = FVector(HearFullRadiusCm, 0.f, 0.f);
			S.FalloffDistance = HearFalloffCm;
		}
		return Attenuation.Get();
	}
}

namespace
{
	const TCHAR* const VolumeSection = TEXT("/Script/Snarehaven.SnareSound");

	SnareSound::FVolumes& VolumesStorage()
	{
		static SnareSound::FVolumes Volumes;
		static bool bLoaded = false;
		if (!bLoaded && GConfig != nullptr)
		{
			bLoaded = true;
			GConfig->GetFloat(VolumeSection, TEXT("Master"), Volumes.Master, GGameUserSettingsIni);
			GConfig->GetFloat(VolumeSection, TEXT("Effects"), Volumes.Effects, GGameUserSettingsIni);
			GConfig->GetFloat(VolumeSection, TEXT("Ambience"), Volumes.Ambience, GGameUserSettingsIni);
			FApp::SetVolumeMultiplier(Volumes.Master);
		}
		return Volumes;
	}

	/** `-SnareActionLog` 실측 줄 — 발소리는 걸음마다 나서 표 칸(누름 한 번의 소리)과 무관하니 뺀다. */
	void LogActionSound(USnareSoundSubsystem* Self, FName Cue, const TCHAR* Where)
	{
		if (!SnareSound::IsActionLogOn() || Cue.ToString().StartsWith(TEXT("Footstep_")))
		{
			return;
		}
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 소리 요청 %s (%s · 변형 %d)"), *Cue.ToString(), Where,
			Self ? Self->CountVariants(Cue) : 0);
	}
}

bool SnareSound::IsActionLogOn()
{
	static const bool bOn = SnareCheckSwitch::Param(TEXT("SnareActionLog"));
	return bOn;
}

const SnareSound::FVolumes& SnareSound::GetVolumes()
{
	return VolumesStorage();
}

void SnareSound::SetVolumes(const FVolumes& In)
{
	FVolumes& Volumes = VolumesStorage();
	Volumes.Master = FMath::Clamp(In.Master, 0.f, 1.f);
	Volumes.Effects = FMath::Clamp(In.Effects, 0.f, 1.f);
	Volumes.Ambience = FMath::Clamp(In.Ambience, 0.f, 1.f);
	FApp::SetVolumeMultiplier(Volumes.Master);
	if (GConfig != nullptr)
	{
		GConfig->SetFloat(VolumeSection, TEXT("Master"), Volumes.Master, GGameUserSettingsIni);
		GConfig->SetFloat(VolumeSection, TEXT("Effects"), Volumes.Effects, GGameUserSettingsIni);
		GConfig->SetFloat(VolumeSection, TEXT("Ambience"), Volumes.Ambience, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

float SnareSound::OceanVolume(float CmInsideShore)
{
	const float T = FMath::Clamp(CmInsideShore / OceanFadeCm, 0.f, 1.f);
	return FMath::Lerp(1.f, OceanFloorVolume, T);
}

float SnareSound::StrideCm(float SpeedCmPerSec)
{
	// 걷기(≈300cm/s) 150cm, 달리기(≈600) 210cm — 발 박자가 클립 발 박자와 대충 맞는다.
	return 90.f + 0.2f * FMath::Max(SpeedCmPerSec, 0.f);
}

SnareSound::FChangeCue SnareSound::EquipCue(const FItemRow* HandRow)
{
	static const FName Holster(TEXT("Holster"));
	static const FName Gun(TEXT("EquipGun"));
	static const FName Blade(TEXT("EquipBlade"));
	static const FName Blunt(TEXT("EquipBlunt"));

	FChangeCue Out;
	Out.Volume = HandChangeVolume;
	Out.MinGapSeconds = HandChangeGapSeconds;
	Out.bWorld = true;
	if (HandRow == nullptr)
	{
		Out.Cue = Holster;
	}
	else if (!HandRow->AmmoItemID.IsNone())
	{
		Out.Cue = Gun;
	}
	else if (HandRow->UseSoundCue == TEXT("Chop") || HandRow->UseSoundCue == TEXT("Machete"))
	{
		Out.Cue = Blade;
	}
	else
	{
		// 둔기(`Club`·`Mine`)와 그 밖 도구(낚싯대·바구니·전기톱…)는 같은 「나무 자루 쥐기」 — 모르는 종류도 여기라 비지 않는다.
		Out.Cue = Blunt;
	}
	return Out;
}

SnareSound::FChangeCue SnareSound::OutfitCue(EEquipSlot Slot)
{
	FChangeCue Out;
	switch (Slot)
	{
	case EEquipSlot::OutfitTop:
	case EEquipSlot::OutfitBottom:
	case EEquipSlot::Backpack:
	case EEquipSlot::Head:
	case EEquipSlot::FaceAccessory:
	case EEquipSlot::Shoes:
	case EEquipSlot::Gloves:
		Out.Cue = TEXT("Outfit");
		Out.Volume = OutfitVolume;
		Out.MinGapSeconds = OutfitGapSeconds;
		Out.bWorld = false;
		break;
	default:
		break;
	}
	return Out;
}

SnareSound::FChangeCue SnareSound::MountCue(bool bMounting)
{
	FChangeCue Out;
	Out.Cue = bMounting ? FName(TEXT("MountOn")) : FName(TEXT("MountOff"));
	Out.Volume = bMounting ? MountOnVolume : MountOffVolume;
	Out.MinGapSeconds = MountGapSeconds;
	Out.bWorld = true;
	return Out;
}

SnareSound::FChangeCue SnareSound::BagCue(EBagAction Action, bool bAccepted, const FString& ErrorCode)
{
	FChangeCue Out;
	if (!bAccepted || !ErrorCode.IsEmpty())
	{
		// 거절은 무음 — 남이 먼저 주운 가방에 줍기 소리가 나면 거짓 지급으로 들린다(규칙 8).
		return Out;
	}
	switch (Action)
	{
	case EBagAction::Drop:
	case EBagAction::DeathDrop:
		Out.Cue = TEXT("BagDrop");
		Out.Volume = BagDropVolume;
		Out.bWorld = true;
		break;
	case EBagAction::Pickup:
		Out.Cue = TEXT("BagPickup");
		Out.Volume = BagPickupVolume;
		Out.bWorld = true;
		break;
	case EBagAction::OpenChest:
		Out.Cue = TEXT("ChestOpen");
		Out.Volume = ChestOpenVolume;
		Out.bWorld = false;
		break;
	default:
		break;
	}
	return Out;
}

SnareSound::FChangeCue SnareSound::BoatCue(EBoatAction Action, bool bAccepted, const FString& ErrorCode)
{
	FChangeCue Out;
	if (!bAccepted || !ErrorCode.IsEmpty())
	{
		// 거절·월드만 맞추는 길(`already`·`not_launched`)은 무음 — 응답 전에 됐다는 티를 내지 않는다(규칙 8).
		return Out;
	}
	switch (Action)
	{
	case EBoatAction::Launch:
		Out.Cue = TEXT("BoatLaunch");
		Out.Volume = BoatLaunchVolume;
		Out.bWorld = true;
		break;
	case EBoatAction::Board:
		Out.Cue = TEXT("BoatBoard");
		Out.Volume = BoatBoardVolume;
		Out.bWorld = false;
		break;
	case EBoatAction::Leave:
		Out.Cue = TEXT("BoatLeave");
		Out.Volume = BoatLeaveVolume;
		Out.bWorld = false;
		break;
	case EBoatAction::Stow:
		Out.Cue = TEXT("Storage");
		Out.Volume = BoatStowVolume;
		Out.bWorld = true;
		break;
	default:
		break;
	}
	return Out;
}

SnareSound::FChangeCue SnareSound::DenyCue(bool bMessageEmpty, float SecondsSinceLast)
{
	FChangeCue Out;
	if (bMessageEmpty || SecondsSinceLast < DenyMinGapSeconds)
	{
		return Out;
	}
	Out.Cue = TEXT("Deny");
	Out.Volume = DenyVolume;
	Out.MinGapSeconds = DenyMinGapSeconds;
	Out.bWorld = false;
	return Out;
}

SnareSound::FChangeCue SnareSound::ZoneCue(EZoneCrossing Crossing, bool bConfirmed)
{
	FChangeCue Out;
	if (!bConfirmed)
	{
		// 막힘·문지기 거절·클라이언트 직접 호출은 무음 — 일어나지 않은 이동에 소리를 내지 않는다(규칙 8).
		return Out;
	}
	switch (Crossing)
	{
	case EZoneCrossing::Portal:
		Out.Cue = TEXT("ZoneWarp");
		Out.Volume = ZonePortalVolume;
		Out.bWorld = false;
		break;
	case EZoneCrossing::Ferry:
		Out.Cue = TEXT("BoatBoard");
		Out.Volume = ZoneFerryVolume;
		Out.bWorld = true;
		break;
	case EZoneCrossing::MineDoor:
		Out.Cue = TEXT("MineDoor");
		Out.Volume = ZoneMineDoorVolume;
		Out.bWorld = true;
		break;
	default:
		break;
	}
	return Out;
}

SnareSound::FChangeCue SnareSound::RanchCue(const FString& FunctionName, bool bAccepted, const FString& ErrorCode)
{
	FChangeCue Out;
	if (!bAccepted || !ErrorCode.IsEmpty())
	{
		// 거절·「서버 준비 중」은 무음 — 잠긴 목장에 넣기 소리가 나면 들어간 걸로 들린다(규칙 8).
		return Out;
	}
	struct FRanchRow
	{
		const TCHAR* Function;
		FName Cue;
		float Volume;
	};
	const FRanchRow Rows[] = {
		{ TEXT("RegisterPet"), TEXT("RanchWelcome"), RanchWelcomeVolume },
		{ TEXT("RemovePet"), TEXT("RanchRelease"), RanchReleaseVolume },
		{ TEXT("SetPetCosmetic"), TEXT("Outfit"), RanchCosmeticVolume },
		{ TEXT("SetRanchDeco"), SnareBuildRules::PlaceConfirmCue(), RanchDecoVolume },
		{ TEXT("ExpandRanchSlot"), TEXT("RanchExpand"), RanchExpandVolume },
		{ TEXT("FeedRanchUpkeep"), TEXT("Storage"), RanchUpkeepVolume },
		{ TEXT("RegisterFishingSpot"), TEXT("Splash"), RanchFishingVolume },
	};
	for (const FRanchRow& Row : Rows)
	{
		if (FunctionName == Row.Function)
		{
			// 빼기 답이 생산물을 먼저 거둬도 빼기 소리 하나만 — 한 응답 한 번이라 간격 0.
			Out.Cue = Row.Cue;
			Out.Volume = Row.Volume;
			Out.bWorld = false;
			break;
		}
	}
	return Out;
}

SnareSound::FAmbienceMix SnareSound::AmbienceFor(FName ZoneID, float NightAmount)
{
	static const FName ForestZone(TEXT("zone.forest"));
	static const FName SnowZone(TEXT("zone.snow"));
	static const FName SwampZone(TEXT("zone.swamp"));

	FAmbienceMix Mix;
	Mix.Wind = WindVolume;
	Mix.BirdVolume = BirdVolume;
	Mix.BirdMinGapSeconds = BirdMinGapSeconds;
	Mix.BirdMaxGapSeconds = BirdMaxGapSeconds;
	if (ZoneID == ForestZone)
	{
		Mix.Wind = WindVolume * ForestWindScale;
		Mix.BirdMinGapSeconds = ForestBirdMinGapSeconds;
		Mix.BirdMaxGapSeconds = ForestBirdMaxGapSeconds;
	}
	else if (ZoneID == SnowZone)
	{
		Mix.Wind = WindVolume * SnowWindScale;
		Mix.BirdVolume = 0.f;
	}
	else if (ZoneID == SwampZone)
	{
		Mix.Wind = WindVolume * SwampWindScale;
		Mix.BirdMinGapSeconds = SwampBirdMinGapSeconds;
		Mix.BirdMaxGapSeconds = SwampBirdMaxGapSeconds;
		Mix.Swamp = SwampLoopVolume;   // 개구리는 밤에도 그대로
	}

	if (NightAmount > NightThreshold)
	{
		Mix.BirdVolume = 0.f;
		Mix.Night = ZoneID == SnowZone ? 0.f : NightLoopVolume;   // 눈밭 귀뚜라미 없음
	}
	return Mix;
}

SnareSound::FAmbienceMix SnareSound::WeatherAmbience(const FAmbienceMix& Base, float Rain, float Snow)
{
	Rain = FMath::Clamp(Rain, 0.f, 1.f);
	Snow = FMath::Clamp(Snow, 0.f, 1.f);
	FAmbienceMix Mix = Base;
	Mix.Wind = Base.Wind * (1.f + (RainWindScale - 1.f) * Rain + (SnowWeatherWindScale - 1.f) * Snow);
	Mix.BirdVolume = Base.BirdVolume * (1.f - FMath::Max(Rain, Snow));
	Mix.Rain = Base.Rain + RainLoopVolume * Rain;
	return Mix;
}

float SnareSound::WeatherFootstepScale(FName Cue, float Rain)
{
	if (Cue != FName(TEXT("Footstep_Grass")) && Cue != FName(TEXT("Footstep_Mud")))
	{
		return 1.f;
	}
	return 1.f - (1.f - RainFootstepScale) * FMath::Clamp(Rain, 0.f, 1.f);
}

bool SnareSound::HasAmbienceLine(FName ZoneID)
{
	// 표 순서 그대로: 평원(기준) · 숲 · 설원 · 늪 · 사냥 존·목장(평원과 같음).
	static const FName Listed[] = {
		FName(TEXT("zone.plains")), FName(TEXT("zone.forest")), FName(TEXT("zone.snow")),
		FName(TEXT("zone.swamp")), FName(TEXT("zone.hunt")), FName(TEXT("zone.ranch")),
	};
	for (const FName& Zone : Listed)
	{
		if (Zone == ZoneID)
		{
			return true;
		}
	}
	return false;
}

bool SnareSound::StepAmbience(FAmbienceFade& Fade, const FAmbienceMix& Target, float DeltaSeconds)
{
	const bool bNewTarget = !Fade.bStarted || Fade.Target.Wind != Target.Wind || Fade.Target.Swamp != Target.Swamp
		|| Fade.Target.Night != Target.Night;
	if (!Fade.bStarted)
	{
		Fade.bStarted = true;
		Fade.Current = Target;
		Fade.From = Target;
		Fade.Elapsed = AmbienceFadeSeconds;
	}
	else if (bNewTarget)
	{
		Fade.From = Fade.Current;   // 옮겨 가던 중이어도 지금 들리는 자리에서 다시 — 툭 튀지 않게
		Fade.Elapsed = 0.f;
	}
	Fade.Target = Target;
	Fade.Elapsed = FMath::Min(Fade.Elapsed + FMath::Max(DeltaSeconds, 0.f), AmbienceFadeSeconds);
	const float Alpha = Fade.Elapsed / AmbienceFadeSeconds;

	FAmbienceMix Mix = Target;   // 새 크기·간격은 목표 그대로(M46-04)
	Mix.Wind = FMath::Lerp(Fade.From.Wind, Target.Wind, Alpha);
	Mix.Swamp = FMath::Lerp(Fade.From.Swamp, Target.Swamp, Alpha);
	Mix.Night = FMath::Lerp(Fade.From.Night, Target.Night, Alpha);
	Fade.Current = Mix;
	return bNewTarget;
}

SnareSound::FBirdCall SnareSound::BirdCallFor(const FAmbienceMix& Mix, float CmInsideShore, float GapAlpha)
{
	FBirdCall Call;
	const bool bHasGap = Mix.BirdMaxGapSeconds > 0.f;   // 빈 칸이면 평원 간격 — 0초 간격으로 매 틱 부르지 않게
	Call.NextGapSeconds = FMath::Lerp(bHasGap ? Mix.BirdMinGapSeconds : BirdMinGapSeconds,
		bHasGap ? Mix.BirdMaxGapSeconds : BirdMaxGapSeconds, FMath::Clamp(GapAlpha, 0.f, 1.f));
	Call.bSing = Mix.BirdVolume > 0.f && CmInsideShore > BirdShoreMarginCm;
	Call.Volume = Call.bSing ? Mix.BirdVolume : 0.f;
	return Call;
}

FName SnareSound::FootstepCue(FName ZoneID, bool bOnWood)
{
	if (bOnWood)
	{
		return FName(TEXT("Footstep_Wood"));
	}
	// 시트·DESIGN 8.7: 설원은 눈, DESIGN 2.2 「존 소리 풍경」: 늪은 진흙(M46-05). 나머지는 풀.
	static const FName SnowZone(TEXT("zone.snow"));
	static const FName SwampZone(TEXT("zone.swamp"));
	if (ZoneID == SnowZone)
	{
		return FName(TEXT("Footstep_Snow"));
	}
	return ZoneID == SwampZone ? FName(TEXT("Footstep_Mud")) : FName(TEXT("Footstep_Grass"));
}

bool USnareSoundSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnareSoundSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnareSoundSubsystem, STATGROUP_Tickables);
}

FSnareSoundVariants& USnareSoundSubsystem::LoadVariants(FName Cue)
{
	if (FSnareSoundVariants* const Found = Cache.Find(Cue))
	{
		return *Found;
	}
	FSnareSoundVariants& Variants = Cache.Add(Cue);
	for (int32 I = 1; I <= MaxVariants; ++I)
	{
		const FString Name = FString::Printf(TEXT("SFX_%s_%02d"), *Cue.ToString(), I);
		const FString Path = FString::Printf(TEXT("/Game/Game/Audio/SFX/%s.%s"), *Name, *Name);
		USoundBase* const Sound = LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Sound == nullptr)
		{
			break;
		}
		Variants.Sounds.Add(Sound);
	}
	return Variants;
}

USoundBase* USnareSoundSubsystem::PickSound(FName Cue)
{
	const TArray<TObjectPtr<USoundBase>>& Sounds = LoadVariants(Cue).Sounds;
	return Sounds.IsEmpty() ? nullptr : Sounds[FMath::RandRange(0, Sounds.Num() - 1)].Get();
}

int32 USnareSoundSubsystem::CountVariants(FName Cue)
{
	return LoadVariants(Cue).Sounds.Num();
}

void USnareSoundSubsystem::Play2D(const UObject* WorldContext, FName Cue, float Volume)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USnareSoundSubsystem* const Self = World ? World->GetSubsystem<USnareSoundSubsystem>() : nullptr;
	LogActionSound(Self, Cue, TEXT("화면"));
	if (USoundBase* const Sound = Self ? Self->PickSound(Cue) : nullptr)
	{
		UGameplayStatics::PlaySound2D(World, Sound, Volume * SnareSound::GetVolumes().Effects, FMath::FRandRange(0.95f, 1.05f));
	}
}

void USnareSoundSubsystem::PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume, float Pitch)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USnareSoundSubsystem* const Self = World ? World->GetSubsystem<USnareSoundSubsystem>() : nullptr;
	LogActionSound(Self, Cue, TEXT("자리"));
	if (USoundBase* const Sound = Self ? Self->PickSound(Cue) : nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume * SnareSound::GetVolumes().Effects,
			Pitch * FMath::FRandRange(0.92f, 1.08f), 0.f, WorldAttenuation());
	}
}

UAudioComponent* USnareSoundSubsystem::SpawnLoopAttached(const UObject* WorldContext, FName Cue, USceneComponent* AttachTo, float Volume,
	float InnerRadiusCm, float OuterRadiusCm)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USnareSoundSubsystem* const Self = World ? World->GetSubsystem<USnareSoundSubsystem>() : nullptr;
	USoundBase* const Sound = (Self != nullptr && AttachTo != nullptr) ? Self->PickSound(Cue) : nullptr;
	if (Sound == nullptr)
	{
		return nullptr;
	}
	USoundAttenuation* Attenuation = WorldAttenuation();
	if (InnerRadiusCm > 0.f && OuterRadiusCm > InnerRadiusCm)
	{
		// 붙인 컴포넌트가 감쇠를 칸(`AttenuationSettings`)으로 쥐어 같이 산다.
		Attenuation = NewObject<USoundAttenuation>(AttachTo);
		FSoundAttenuationSettings& S = Attenuation->Attenuation;
		S.bAttenuate = true;
		S.bSpatialize = true;
		S.AttenuationShape = EAttenuationShape::Sphere;
		S.AttenuationShapeExtents = FVector(InnerRadiusCm, 0.f, 0.f);
		S.FalloffDistance = OuterRadiusCm - InnerRadiusCm;
	}
	return UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		false, Volume * SnareSound::GetVolumes().Effects, 1.f, 0.f, Attenuation, nullptr, true);
}

void USnareSoundSubsystem::StartAmbience()
{
	bAmbienceStarted = true;
	UWorld* const World = GetWorld();
	if (USoundBase* const Sound = PickSound(TEXT("Amb_Ocean")))
	{
		Ocean = UGameplayStatics::CreateSound2D(World, Sound, OceanFloorVolume, 1.f, 0.f, nullptr, false, false);
	}
	if (USoundBase* const Sound = PickSound(TEXT("Amb_Wind")))
	{
		Wind = UGameplayStatics::CreateSound2D(World, Sound, WindVolume, 1.f, 0.f, nullptr, false, false);
	}
	// 늪·밤 고리는 늘 돌고 크기만 0 ↔ 목표 — 존을 넘을 때 처음부터 다시 시작하는 티가 안 나게.
	if (USoundBase* const Sound = PickSound(TEXT("Amb_Swamp")))
	{
		Swamp = UGameplayStatics::CreateSound2D(World, Sound, 0.f, 1.f, 0.f, nullptr, false, false);
	}
	if (USoundBase* const Sound = PickSound(TEXT("Amb_Night")))
	{
		Night = UGameplayStatics::CreateSound2D(World, Sound, 0.f, 1.f, 0.f, nullptr, false, false);
	}
	if (USoundBase* const Sound = PickSound(TEXT("Amb_Rain")))
	{
		RainLoop = UGameplayStatics::CreateSound2D(World, Sound, 0.f, 1.f, 0.f, nullptr, false, false);
	}
	for (UAudioComponent* const Loop : {Ocean.Get(), Wind.Get(), Swamp.Get(), Night.Get(), RainLoop.Get()})
	{
		if (Loop != nullptr)
		{
			Loop->Play(FMath::FRandRange(0.f, 10.f));   // 반복 시작점을 흩어 매번 같은 파도로 시작하지 않게
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 바탕 소리 시작 — 파도 %s · 바람 %s · 늪 %s · 밤 %s"),
		Ocean ? TEXT("켬") : TEXT("없음"), Wind ? TEXT("켬") : TEXT("없음"), Swamp ? TEXT("켬") : TEXT("없음"),
		Night ? TEXT("켬") : TEXT("없음"));
}

void USnareSoundSubsystem::Tick(float DeltaTime)
{
	UWorld* const World = GetWorld();
	const APlayerController* const Player = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* const Pawn = Player ? Player->GetPawn() : nullptr;
	if (Pawn == nullptr || !Player->IsLocalController())
	{
		return;
	}
	if (!bAmbienceStarted)
	{
		StartAmbience();
	}

	const FVector Feet = Pawn->GetActorLocation();
	float Inside = OceanFadeCm;   // 바다 없는 맵(사냥 존)은 섬 한가운데 취급
	if (const ASeaActor* const Sea = ASeaActor::FindSea(World))
	{
		Inside = -Sea->CoastDistance(Feet);
	}
	// 설정 화면에서 바꾸면 다음 틱에 바로 들린다.
	const float Ambience = SnareSound::GetVolumes().Ambience;
	if (Ocean != nullptr)
	{
		Ocean->SetVolumeMultiplier(SnareSound::OceanVolume(Inside) * Ambience);
	}
	// 존·낮밤 바탕 고리 (DESIGN 2.2 「존 소리 풍경」) — 목표가 바뀌면 4초 동안 옮겨 간다.
	const USnareDayNightSubsystem* const DayNight = World->GetSubsystem<USnareDayNightSubsystem>();
	const FZoneRow* const Region = SnareZones::FindRegionAt(this, Feet);
	const FName ZoneID = Region ? Region->ZoneID : NAME_None;
	const SnareSound::FAmbienceMix Target = SnareSound::AmbienceFor(ZoneID, DayNight ? DayNight->GetNightAmount() : 0.f);
	if (SnareSound::StepAmbience(AmbienceFade, Target, DeltaTime))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 바탕 소리 — 존 %s · 고리 크기 바람 %.2f 늪 %.2f 밤 %.2f"),
			*ZoneID.ToString(), Target.Wind, Target.Swamp, Target.Night);
	}
	// 날씨는 자기가 30초 동안 섞이며 오므로 존 옮김(4초) 뒤에 곱한다.
	float Rain = 0.f;
	float Snow = 0.f;
	if (const USnareWeatherSubsystem* const Weather = World->GetSubsystem<USnareWeatherSubsystem>())
	{
		Weather->GetAmounts(Rain, Snow);
	}
	const SnareSound::FAmbienceMix Mix = SnareSound::WeatherAmbience(AmbienceFade.Current, Rain, Snow);
	if (RainLoop != nullptr)
	{
		RainLoop->SetVolumeMultiplier(Mix.Rain * Ambience);
	}
	if (Wind != nullptr)
	{
		Wind->SetVolumeMultiplier(Mix.Wind * Ambience);
	}
	if (Swamp != nullptr)
	{
		Swamp->SetVolumeMultiplier(Mix.Swamp * Ambience);
	}
	if (Night != nullptr)
	{
		Night->SetVolumeMultiplier(Mix.Night * Ambience);
	}

	NextBirdSeconds -= DeltaTime;
	if (NextBirdSeconds > 0.f)
	{
		return;
	}
	// 새 켜짐·크기·간격은 존 표(설원·밤 = 0, 숲 촘촘, 늪 드묾) — M46-04.
	const SnareSound::FBirdCall Call = SnareSound::BirdCallFor(Mix, Inside, FMath::FRand());
	NextBirdSeconds = Call.NextGapSeconds;
	if (Call.bSing)
	{
		const FVector Offset = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector() * FMath::FRandRange(900.f, 2000.f);
		if (USoundBase* const Sound = PickSound(TEXT("Bird")))   // 새는 바탕 소리 — 효과음 크기가 아니라 주변음 크기를 탄다
		{
			UGameplayStatics::PlaySoundAtLocation(World, Sound, Feet + Offset + FVector(0.f, 0.f, 600.f), FRotator::ZeroRotator,
				Call.Volume * Ambience, FMath::FRandRange(0.92f, 1.08f), 0.f, WorldAttenuation());
		}
	}
}
