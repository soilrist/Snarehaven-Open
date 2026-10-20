#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SnareSound.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundBase;
struct FItemRow;
enum class EEquipSlot : uint8;

// 게임 소리 (감독 2026-09-25 · DESIGN 2.2 소리) — 레퍼런스 소리 풍경: 해변 파도 + 잔바람을 늘 깔고,
// 낮에는 머리 위에서 새가 지저귄다. 효과음은 `SFX_<이름>_01..` 중 하나를 무작위로 — 같은 소리 되풀이를 피한다.
// 원본 wav 는 `tools/audio/make_sounds.py`, 들이기는 `tools/unreal/import_audio.py`.
namespace SnareSound
{
	/** 섬 가장자리까지 남은 거리(cm, 바다 위면 음수) → 파도 크기. 물가 1, 3000cm 안쪽부터 바닥값. */
	SNAREHAVEN_API float OceanVolume(float CmInsideShore);

	/** 한 발짝 거리(cm) — 걷기 150, 빠를수록 보폭이 넓어진다. */
	SNAREHAVEN_API float StrideCm(float SpeedCmPerSec);

	/** 서 있는 존(`zone.snow` 등)의 발소리 이름 — 설원은 눈 밟는 소리, 늪은 진흙, 나머지는 풀. 존 밖이면 `NAME_None` 을 준다.
	 *  발밑이 부두·배치 구조물 같은 나무 바닥(`bOnWood`)이면 존과 상관없이 나무 소리. */
	SNAREHAVEN_API FName FootstepCue(FName ZoneID, bool bOnWood = false);

	/** 한 존·밤 정도에서 바탕 소리가 가야 할 자리 (DESIGN 2.2 「존 소리 풍경」). 크기는 「주변 소리」 곱 전. */
	struct FAmbienceMix
	{
		float Wind = 0.f;
		/** 0 이면 새가 울지 않는다(설원·밤). */
		float BirdVolume = 0.f;
		float BirdMinGapSeconds = 0.f;
		float BirdMaxGapSeconds = 0.f;
		/** 늪 고리 `Amb_Swamp`(개구리 + 벌레). */
		float Swamp = 0.f;
		/** 밤 고리 `Amb_Night`(귀뚜라미). */
		float Night = 0.f;
		/** 빗소리 고리 `Amb_Rain` — 존·낮밤 표는 0, 날씨가 곱해 올린다(M104-03b). */
		float Rain = 0.f;
	};

	/** 날씨 소리 (DESIGN 3.3 「날씨」 표, M104-03b). `Rain`·`Snow` 는 `SnareWeather::Amounts` 0~1(섞는 30초 동안 연속).
	 *  바람은 비 ×1.5 · 눈 ×1.2 쪽으로, 새는 비·눈 정도만큼 줄어 꽉 차면 끔, 빗소리 고리는 비 정도 × `RainLoopVolume`. */
	SNAREHAVEN_API FAmbienceMix WeatherAmbience(const FAmbienceMix& Base, float Rain, float Snow);

	/** 발소리 크기 곱 — 비 오는 날 풀·흙(`Footstep_Grass`·`Footstep_Mud`)만 ×0.8 쪽으로. 눈·나무는 그대로. */
	SNAREHAVEN_API float WeatherFootstepScale(FName Cue, float Rain);

	/** 존(`zone.swamp` 등)과 밤 정도(0 낮 ~ 1 한밤) → 고리마다 목표 크기·새 간격. 표에 없는 존은 평원 값. */
	SNAREHAVEN_API FAmbienceMix AmbienceFor(FName ZoneID, float NightAmount);

	/** DESIGN 2.2 「존 소리 풍경」 표에 줄이 있는 존인가 — 자기 줄(숲·설원·늪) 또는 「평원과 같음」(평원·사냥 존·목장).
	 *  `DT_ZoneTable` 에 새 존이 소리 줄 없이 생기면 2층 `Snarehaven.World.Ambience.EveryZone` 이 운다(M46-06). */
	SNAREHAVEN_API bool HasAmbienceLine(FName ZoneID);

	/** 바탕 고리가 지금 어디서 어디로 옮겨 가는 중인지 (M46-03). `Current` 를 그대로 틀면 된다. */
	struct FAmbienceFade
	{
		FAmbienceMix Current;
		FAmbienceMix From;
		FAmbienceMix Target;
		float Elapsed = 0.f;
		bool bStarted = false;
	};

	/** 목표가 바뀌면(존 넘음·낮↔밤) 고리 크기(바람·늪·밤)를 지금 자리에서 표의 초(4초) 동안 고르게 옮긴다.
	 *  처음 한 번은 바로 목표 — 켜자마자 소리가 비어 있지 않게. 새 칸은 목표 그대로. 목표가 바뀌었으면 true. */
	SNAREHAVEN_API bool StepAmbience(FAmbienceFade& Fade, const FAmbienceMix& Target, float DeltaSeconds);

	/** 새 한 번 울 차례에 정할 것 (M46-04). */
	struct FBirdCall
	{
		bool bSing = false;
		/** 「주변 소리」 곱 전 크기. */
		float Volume = 0.f;
		float NextGapSeconds = 0.f;
	};

	/** 지금 자리 바탕 소리(`Mix`)의 새 크기·간격으로 이번에 울지·다음까지 몇 초인지. 새 크기 0(설원·밤)이거나
	 *  물가 `BirdShoreMarginCm` 안이면 울지 않는다. `GapAlpha`(0~1)는 간격 표 사이 자리 — 게임은 무작위, 시험은 고정. */
	SNAREHAVEN_API FBirdCall BirdCallFor(const FAmbienceMix& Mix, float CmInsideShore, float GapAlpha);

	/** 장비·탈것이 바뀔 때 낼 소리 한 줄 (DESIGN 2.2 「장비·탈것 소리」, M47-02). `Cue` 가 비면 무음. */
	struct FChangeCue
	{
		/** `SFX_<Cue>_NN` 의 이름. */
		FName Cue;
		/** 「효과음」 곱 전 크기. */
		float Volume = 0.f;
		/** 같은 사람이 이 간격 안에 또 바꾸면 첫 한 번만 소리. */
		float MinGapSeconds = 0.f;
		/** true = 그 플레이어 자리 월드 소리(모든 기기 `PlayAt`), false = 내 기기만 `Play2D`. */
		bool bWorld = false;
	};

	/** 손 칸에 새로 든 행 → 꺼내기 소리. 총(`AmmoItemID`)·칼날(`Chop`·`Machete`)·둔기(`Club`·`Mine`)·그 밖 = 도구(둔기 소리 빌림).
	 *  `HandRow` 가 nullptr(빈손이 됨)이면 넣기 소리. */
	SNAREHAVEN_API FChangeCue EquipCue(const FItemRow* HandRow);

	/** 옷 부위(상의·하의·배낭·머리·얼굴·신발·장갑)가 바뀜 → 부위와 무관하게 옷 부스럭. 손 칸·`None` 은 무음. */
	SNAREHAVEN_API FChangeCue OutfitCue(EEquipSlot Slot);

	/** 탈것에 탐(`bMounting`) / 내림. */
	SNAREHAVEN_API FChangeCue MountCue(bool bMounting);

	/** 가방·상자 소리가 나는 행동 (DESIGN 8.12 「가방·상자 소리」, M49-02). */
	enum class EBagAction : uint8
	{
		Drop,		// 버리기 `DropItem`
		DeathDrop,	// 죽음 떨굼 `DropOnDeath` — 가방을 둔 때만 받아들임
		Pickup,		// 떨어진 가방 줍기 `PickupDrop`
		OpenChest,	// 전리품 상자 열기 `OpenChest`
	};

	/** 서버 응답 하나 → 소리 한 줄. 받아들이지 않았거나(`bAccepted` false) 오류 코드(`already_taken`·`expired`…)가 있으면 무음.
	 *  가방 셋 = 가방 자리 월드 소리, 상자 = 내 기기만. 한 응답에 한 번이라 간격은 0. */
	SNAREHAVEN_API FChangeCue BagCue(EBagAction Action, bool bAccepted, const FString& ErrorCode);

	/** 목장 서버 응답 하나 → 소리 한 줄 (DESIGN 3.2 「목장 소리」, M50-02). `FunctionName` 은 CloudScript 함수 이름.
	 *  넣기·빼기·꾸미기·데코·칸 늘리기·유지 넣기·낚시터 등록 7갈래만 소리, 전부 내 기기만(목장은 복제 안 함).
	 *  받아들이지 않았거나 오류 코드가 있으면 무음. 수확 3·심기는 기존 길이 따로 내므로 빈 이름, 모르는 함수도 빈 이름. */
	SNAREHAVEN_API FChangeCue RanchCue(const FString& FunctionName, bool bAccepted, const FString& ErrorCode);

	/** 배 소리가 나는 행동 (DESIGN 8.9 「배 소리」, M51-02). */
	enum class EBoatAction : uint8
	{
		Launch,	// 배 띄우기 `LaunchBoat`
		Board,	// 배 타기 `BoardOnAuthority`
		Leave,	// 배 내리기 `LeaveOnAuthority`
		Stow,	// 배 넣기 `StowBoat`
	};

	/** 배 동작 하나 → 소리 한 줄. `bAccepted` 는 서버 답 받아들임(타기·내리기는 호스트가 실제로 상태를 바꿨는가).
	 *  받아들이지 않았거나 오류 코드(`already`·`not_launched`…)가 있거나 모르는 동작이면 무음.
	 *  띄우기·넣기 = 배 자리 월드 소리, 타기·내리기 = 탄/내린 사람 기기만. 한 번에 한 소리라 간격은 0. */
	SNAREHAVEN_API FChangeCue BoatCue(EBoatAction Action, bool bAccepted, const FString& ErrorCode);

	/** 화면 아래 빨간 오류 줄 하나 → 막힘 소리 한 줄 (DESIGN 2.2 「막힘 소리」, M55-02). 문구가 비었거나
	 *  지난 소리 뒤 `SecondsSinceLast` 가 간격(0.4초) 안이면 무음(글자만 바뀐다). 아니면 `Deny`, 내 기기만(`Play2D`). */
	SNAREHAVEN_API FChangeCue DenyCue(bool bMessageEmpty, float SecondsSinceLast);

	/** 존·광산을 건너는 갈래 (DESIGN 8.10 「존·광산 건너기 소리」, M53-02). */
	enum class EZoneCrossing : uint8
	{
		Portal,		// 존 포털 `AZonePortalActor::TravelToTargetZone`
		Ferry,		// 섬 나룻배 `FerryTraveller`
		MineDoor,	// 광산 문 `AMineDoorActor::SendThrough`
	};

	/** 건너기 하나 → 소리 한 줄. `bConfirmed` 는 이동이 실제로 일어남(`ServerTravel` 참 · `TeleportTo` 성공).
	 *  확정이 아니거나 모르는 갈래면 무음. 포털 = 호스트 기기 2D(`bWorld` 거짓), 나룻배·광산 문 = 도착 자리 월드 소리. 간격 0. */
	SNAREHAVEN_API FChangeCue ZoneCue(EZoneCrossing Crossing, bool bConfirmed);

	/** 설정 화면 소리 크기 (0~1, DESIGN 2.2 「설정」). 기기에 남는다(`GameUserSettings.ini`). */
	struct FVolumes
	{
		float Master = 1.f;
		float Effects = 1.f;
		float Ambience = 1.f;
	};

	/** 처음 부를 때 ini 에서 읽는다. */
	SNAREHAVEN_API const FVolumes& GetVolumes();

	/** 0~1 로 잘라 저장하고 전체 크기를 엔진에 바로 건다. */
	SNAREHAVEN_API void SetVolumes(const FVolumes& Volumes);

	/** 검사 스위치 `-SnareActionLog`(M28-09 전수 표 실측) — 누름·몸 동작·소리 요청·화면 알림을 `[실측]` 줄로 남긴다. */
	SNAREHAVEN_API bool IsActionLogOn();
}

USTRUCT()
struct FSnareSoundVariants
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<TObjectPtr<USoundBase>> Sounds;
};

UCLASS()
class SNAREHAVEN_API USnareSoundSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 화면 소리(UI·코인) — 거리 감쇠 없이. */
	static void Play2D(const UObject* WorldContext, FName Cue, float Volume = 1.f);

	/** 월드 자리 소리 — 발소리·찍기·물소리. `Pitch` 는 무작위 ±8% 위에 곱한다(곰 울음 0.75 — DESIGN 8.7). */
	static void PlayAt(const UObject* WorldContext, FName Cue, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);

	/**
	 * 컴포넌트에 붙어 따라다니는 **되풀이 소리**를 만들어 튼다 (M16-03 전기톱 공회전). 없는 소리면 nullptr.
	 * 멈추고 없애는 것은 부른 쪽 몫이다(`Stop()` 뒤 `DestroyComponent()`).
	 * `InnerRadiusCm`·`OuterRadiusCm` 를 주면(둘 다 0 초과) 그 반지름 안은 크기 그대로·바깥 반지름에서 0 인 감쇠를 쓴다
	 * (M108-07d 발전기 300/1500). 안 주면 월드 소리 감쇠(안쪽 2500cm) 그대로.
	 */
	static UAudioComponent* SpawnLoopAttached(const UObject* WorldContext, FName Cue, USceneComponent* AttachTo, float Volume = 1.f,
		float InnerRadiusCm = 0.f, float OuterRadiusCm = 0.f);

	/** `SFX_<Cue>_01..` 가운데 하나. 없으면 nullptr. */
	USoundBase* PickSound(FName Cue);

	/** 들어 있는 변형 개수 — 검사용. */
	int32 CountVariants(FName Cue);

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FSnareSoundVariants& LoadVariants(FName Cue);
	void StartAmbience();

	UPROPERTY(Transient)
	TMap<FName, FSnareSoundVariants> Cache;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Ocean;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Wind;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Swamp;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Night;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RainLoop;

	SnareSound::FAmbienceFade AmbienceFade;
	bool bAmbienceStarted = false;
	float NextBirdSeconds = 2.f;
};
