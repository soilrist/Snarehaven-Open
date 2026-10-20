#pragma once

#include "CoreMinimal.h"
#include "Data/ZoneRow.h"
#include "GameFramework/Actor.h"
#include "World/SnareSound.h"
#include "ZonePortalActor.generated.h"

class UDataTable;
class UStaticMeshComponent;

// ROADMAP M6-02a: 존과 존을 잇는 포탈. 맵에 하나 놓고 "여기는 어느 존이고, 어느 존으로 보낼지" 두 칸만
// 채우면 **갈 곳은 `DT_ZoneTable` 이 정한다** (DataDriven.md 1 — 맵 이름을 코드에 박지 않는다).
// 근거는 DESIGN 1.3 "존을 포탈/로딩존으로 잇는 구조" 와 `docs/decisions/005`, 지금 이어진 길은
// `docs/WORLD_ZONES.md` 표 A·B 다.
//
// **표에 없는 곳으로는 못 보낸다.** 이어지지 않은 존·없는 존·맵 이름이 빈 존은 전부 거절하고 포탈이
// 꺼진 채로 선다 (`CanTravel()` false) — 없는 맵으로 떠나면 "이동 중" 에서 멈추기 때문이다.
//
// **이동은 서버가 한다.** 방 안에서 존을 옮기는 길은 `ServerTravel` 하나뿐이고, 클라이언트가 스스로
// `OpenLevel` 을 부르면 방이 갈라진다 (`docs/WORLD_ZONES.md` 표 B). 그래서 `TravelToTargetZone()` 은
// `HasAuthority()` 가 아니면 아무것도 하지 않는다 — 게스트가 포탈을 밟았을 때 서버에 부탁하는 길은
// 버튼과 함께 M6-02b 몫이다.
UCLASS()
class SNAREHAVEN_API AZonePortalActor : public AActor
{
	GENERATED_BODY()

public:
	AZonePortalActor();

	/**
	 * `ZoneTable` 에서 `SourceZoneID` → `TargetZoneID` 를 읽어 갈 맵·표시 이름을 확정한다.
	 * BeginPlay 가 부르고, 2층 검사는 표·행을 직접 넣고 부른다.
	 * @return 갈 곳이 확정됐으면 true (표에 없거나 이어지지 않은 존이면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	bool InitializeFromTable();

	/**
	 * 볼 표와 두 존 ID 를 바꾼다. 맵에 놓을 때 방향을 지정하거나 검사에서 바꿔 끼울 때 쓴다.
	 * @param Table        존 표 (FZoneRow 행 구조체)
	 * @param InSourceZone 이 포탈이 **서 있는** 존의 ID (예: `zone.hunt`)
	 * @param InTargetZone 이 포탈이 **보낼** 존의 ID (예: `zone.ranch`)
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	void SetZoneData(UDataTable* Table, FName InSourceZone, FName InTargetZone);

	/** 갈 곳이 확정됐는지. 표에 없거나 이어지지 않은 존을 적었으면 false — 버튼도 뜨지 않는다(M6-02b). 섬 배는 건너편 부두가 있으면 참. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	bool CanTravel() const { return !TargetLevelName.IsNone() || IsIslandFerry(); }

	/** 같은 맵 안 섬 사이 배인지 (DESIGN 8.14 섬 사이 배) — 존 표·맵 이동 대신 건너편 부두로 그 사람만 옮긴다. */
	bool IsIslandFerry() const { return IsValid(FerryTo); }

	/**
	 * 섬 배로 한 사람을 건너편 부두 뭍 쪽 끝에 내린다. **호스트만** 옮긴다 — 게스트는 `Server_TryTravelToNearest` 로 부탁한다.
	 * 뱃삯은 없다: 가을섬 입장료는 도착한 칸이 받는다(`UZoneEffectComponent`, 거절이면 떠난 부두로 돌려보낸다).
	 * @return 옮겼으면 true
	 */
	bool FerryTraveller(AActor* Traveller);

	/**
	 * 건너기 하나 → 소리 한 번 (DESIGN 8.10 「존·광산 건너기 소리」, M53-03). 규칙은 `SnareSound::ZoneCue` — 확정이 아니면 무음.
	 * 포털 = 호스트 기기 2D, 나룻배 = `Where`(도착 자리)에서 모든 기기(`Multicast_`).
	 */
	void PlayCrossingCue(SnareSound::EZoneCrossing Crossing, bool bConfirmed, const FVector& Where);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCrossingCue(FName Cue, float Volume, FVector Where);

	/** 2층 시험이 읽는다 — 지금까지 낸 건너기 소리 수·마지막 이름·멀티캐스트로 보낸 수. */
	int32 GetCrossingCueCount() const { return CrossingCueCount; }
	FName GetLastCrossingCue() const { return LastCrossingCue; }
	int32 GetCrossingMulticastCount() const { return CrossingMulticastCount; }

	/** 섬 배 건너편 부두·버튼 이름을 넣는다. 배치 스크립트는 속성으로 넣고, 2층 검사가 이걸 부른다. */
	void SetFerryData(AZonePortalActor* InFerryTo, const FText& InDisplayName) { FerryTo = InFerryTo; FerryDisplayName = InDisplayName; }

	AZonePortalActor* GetFerryTo() const { return FerryTo; }
	const FText& GetFerryDisplayName() const { return FerryDisplayName; }

	/** 표가 정한 갈 맵 이름. 확정 전이면 `None`. */
	UFUNCTION(BlueprintPure, Category = "Zone")
	FName GetTargetLevelName() const { return TargetLevelName; }

	/** 화면에 뜨는 갈 곳 이름(표의 `DisplayName`). 버튼 글자가 이걸 쓴다(M6-02b). */
	UFUNCTION(BlueprintPure, Category = "Zone")
	FText GetTargetDisplayName() const { return TargetDisplayName; }

	UFUNCTION(BlueprintPure, Category = "Zone")
	FName GetTargetZoneID() const { return TargetZoneID; }

	UStaticMeshComponent* GetBody() const { return Body; }

	/** 외곽선 하이라이트. `AGatherableActor::SetHighlighted` 와 같은 방식(실제 외곽선은 아트 몫). */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	void SetHighlighted(bool bHighlighted);

	/**
	 * 방 전체를 갈 존으로 옮긴다 — `ServerTravel` 이라 게스트는 엔진이 따라 옮긴다(M3-05 가 쓰는 길).
	 * **클라이언트에서 부르면 아무것도 하지 않는다**: 스스로 맵을 열면 방이 갈라진다.
	 * @return 실제로 떠났으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Zone")
	bool TravelToTargetZone();

	/**
	 * 갈 곳 판정 — **표만 본다.** 액터도 월드도 없이 2층이 같은 길을 걷게 static 이다
	 * (`SnarehavenZoneTableTest` 의 `CollectZoneIssues` 와 같은 짜임).
	 * @param Rows         표 전체 (행 이름 → 행)
	 * @param SourceZoneID 포탈이 서 있는 존
	 * @param TargetZoneID 포탈이 보낼 존
	 * @param OutReason    거절 이유 (통과하면 비어 있다)
	 * @return 갈 존의 행 (못 가면 nullptr — `Rows` 가 살아 있는 동안만 유효)
	 */
	static const FZoneRow* ResolveTargetZone(const TMap<FName, FZoneRow>& Rows,
		FName InSourceZoneID, FName InTargetZoneID, FString& OutReason);

	/** 표를 `ZoneID` 기준 맵으로 읽는다. 위 판정에 먹이는 자리. */
	static TMap<FName, FZoneRow> ReadZoneRows(const UDataTable* Table);

	/**
	 * 지금 서 있는 맵이 어느 존인지 — 표의 `LevelName` 으로 되짚는다(M8-03 분석 이벤트의 `zone_id`).
	 * 표를 또 경로로 찾지 않고 **포탈 기본값(CDO)이 물고 있는 표**를 빌린다: 표 경로가 한 곳에만 남는다.
	 * @param World 볼 월드
	 * @return 존 ID (표에 없는 맵이면 `None` — 이벤트 칸이 비는 것뿐, 게임 흐름은 그대로다)
	 */
	static FName FindCurrentZoneID(const UWorld* World);

protected:
	virtual void BeginPlay() override;
	/** 맵에 놓인 포탈도 부두 배율·위치를 표 값으로 다시 맞춘다 — 저장값이 생성자 값을 덮지 않게 (M13-41). */
	virtual void OnConstruction(const FTransform& Transform) override;

	// 어느 존에 서서 어디로 보내는지는 맵에 놓인 포탈마다 다르다 (engines/unreal.md 코드 규칙 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	TObjectPtr<UDataTable> ZoneTable;

	/** 이 포탈이 서 있는 존. 기본값을 두지 않는 이유: 사냥존·거점존이 대등하다(`AGatherableActor` 와 같다). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName SourceZoneID;

	/** 이 포탈이 보낼 존. 표의 `ConnectedZoneIDs` 에 없으면 포탈이 꺼진다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FName TargetZoneID;

	/** 섬 사이 배의 건너편 부두 (DESIGN 8.14). 채우면 존 표를 안 보고 이 부두로 건넌다 — 사냥터 안 섬끼리는 맵이 같다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	TObjectPtr<AZonePortalActor> FerryTo;

	/** 섬 배 버튼에 뜨는 건너편 섬 이름 (`가을섬`). 존 표의 칸 이름(`숲(가을섬 성격)`)은 버튼 글자가 아니다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zone")
	FText FerryDisplayName;

private:
	/** 부두 메시 긴 변을 600 에, 바다 끝·판자 윗면을 액터 자리에 맞춘다 (DESIGN 8.14 부두 행). 생성자·`OnConstruction` 이 부른다. */
	void FitPierBody();

	// 선착장 부두 (M13-39 · DESIGN 8.14) — 해안에서 바다로 뻗고, 걸어 올라설 수 있다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zone", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Body;

	// 표에서 읽어 둔 값. 밟는 순간에 표를 다시 뒤지지 않으려고 들고 있는다.
	FName TargetLevelName;
	FText TargetDisplayName;

	int32 CrossingCueCount = 0;
	FName LastCrossingCue;
	int32 CrossingMulticastCount = 0;
};
