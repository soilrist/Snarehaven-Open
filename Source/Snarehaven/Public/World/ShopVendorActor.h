#pragma once

#include "CoreMinimal.h"
#include "Backend/SnareStory.h"
#include "Data/ShopRow.h"
#include "GameFramework/Actor.h"
#include "ShopVendorActor.generated.h"

class APawn;
class APlayerController;
class UUserWidget;
class UAnimSequence;
class UEquipmentComponent;
class UShopScreenWidget;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UWidgetComponent;

// DESIGN 8.14 기지 잡화점 상인. 기지 존(`L_Ranch`)에 `ARanchGameMode` 가 한 명 세운다 — 레벨 파일은 안 건드린다.
//
// 하는 일은 "가까이 왔나" 하나뿐이다. 내 폰이 상호작용 거리 안에 들면 상점 화면(`UShopScreenWidget`)을
// 뷰포트에 올리고(닫힌 채 "상점 열기" 버튼만), 멀어지면 내린다. 거래·코인은 전부 화면과 서버 몫이다.
// 거리·갱신 주기는 새로 짓지 않고 포탈 버튼과 같은 값을 빌린다(폰의 이동 사거리·근접 공격 간격).
//
// 모습(M13-39 · DESIGN 8.14 「잡화점·선착장 진짜 모습」): 플레이어와 같은 몸이 상인 모자를 쓰고 좌판 **앞**에 선다.
// 액터 자리 = 상인 몸 **캡슐 중심**(폰과 같은 기준) — 거리 비교가 폰 중심끼리라 150cm 가 그대로 뜻을 갖는다.
// 액터 앞(+X) = 손님 쪽. 좌판은 상인 뒤(−X)에 서고 카운터 앞면이 상인 중심 뒤 40cm 에 온다(M13-41 「상인 자리 = 좌판 앞」).
//
// 상인 자체는 복제만 된다. 화면은 **각자 자기 기기에서** 연다 — 호스트가 게스트 화면을 대신 열 수 없다.
UCLASS()
class SNAREHAVEN_API AShopVendorActor : public AActor
{
	GENERATED_BODY()

public:
	AShopVendorActor();

	/**
	 * 이 폰이 상인 앞에 섰는지 보고 화면을 올리거나 내린다. 타이머가 부르고, 검사도 직접 부른다.
	 * @return 상호작용 거리 안이면 true
	 */
	bool UpdateProximity(APawn* LocalPawn);

	/** 이 폰이 상인 대화 거리 안인가 — `UpdateProximity` 와 HUD 처음 다가가기 안내(M28-05c)가 같은 답을 본다. */
	bool IsPawnInTalkRange(const APawn* Pawn) const;

	/** 이 폰의 대화 거리 안에 선 상인·의뢰인 중 가장 가까운 것. 없으면 nullptr — 상황 버튼 「열기」 가 본다. */
	static AShopVendorActor* FindInTalkRange(const APawn* Pawn);

	/**
	 * 상황 버튼 「열기」 누름 — 화면을 올리고(아직이면) 판을 편다. 「상점 열기」 카드 누름과 같은 `SetOpen(true)`.
	 * @return 대화 거리 안이라 화면이 있으면 true
	 */
	bool OpenScreenFor(APawn* LocalPawn);

	/** 명함 이름 (🟡 감독 가정 — 레퍼런스 상인 이름은 쓰지 않고 새로 지었다). */
	const FText& GetVendorName() const { return VendorName; }

	/**
	 * 가게 종류 (DESIGN 8.14 「가게 여럿」). 권위 쪽이 스폰 직후·BeginPlay 전에 부른다 — 이름·모자가 따라 바뀌고,
	 * 종류는 복제되어 게스트 화면도 같은 줄을 보인다.
	 */
	void SetShopKind(EShopKind InKind);
	EShopKind GetShopKind() const { return ShopKind; }

	/** 종류별 명함 이름 (🟡 감독 — 레퍼런스 원본 이름은 쓰지 않는다). */
	static FText GetKindName(EShopKind Kind);

	/** 종류별 모자 — 멀리서도 무슨 가게인지 보이게 그 분야 모자를 쓴다. */
	static FName GetKindHat(EShopKind Kind);

	/**
	 * 고구마 장수만: 오늘 문을 열었는지 정한다(DESIGN 8.17 「평일 빈 수레」). 닫으면 몸·모자만 숨기고 좌판(빈 수레)은 남기며
	 * 머리 위 팻말 「일요일에 와요」를 띄운다. 말 걸기는 막는다. 다른 가게는 아무 일도 안 한다.
	 */
	void ApplyOpenState(bool bOpen);
	bool IsClosedToday() const { return bClosedToday; }
	/** 일요일에 열린 고구마 장수: 목장 알림 칸에 「왔어요」 를 이 장수당 한 번만 띄운다. @return 띄웠으면 true */
	bool TryShowArrivalNotice(APawn* LocalPawn);
	bool HasShownArrivalNotice() const { return bArrivalNoticeShown; }
	/** 머리 위 「!」「?」 표식을 바꾼다 (메인 스토리 ②). 없음이면 숨긴다. 지금 단계의 대상 NPC 에만 켠다. */
	void SetStoryMark(SnareStory::EGiverMark Mark);
	SnareStory::EGiverMark GetStoryMark() const { return StoryMark; }
	/** 표식이 눈에 보이게 켜져 있나 / 지금 표식 글자. */
	bool IsStoryMarkShown() const;
	FText GetShownStoryMarkText() const;
	/** 이 NPC 가 스토리 대상으로 칠 수 있는 사람 — 의뢰인은 솔방울, 낚시 상인은 모래, 나머지는 None. */
	SnareStory::EStoryNpc GetStoryNpc() const;
	/** 월드에서 그 스토리 상대가 서 있는 자리를 찾는다. 없으면(갈고리처럼 아직 월드에 없음) nullptr. */
	static const AShopVendorActor* FindStoryNpc(UWorld* World, SnareStory::EStoryNpc Npc);

	/**
	 * 상인이 바라보는 월드 방향(도). 기본은 게임 카메라 쪽(카메라는 +X 를 보니 −X = 180도), 말 걸기 반경 안에
	 * 폰이 있으면 그 폰 쪽으로 부드럽게 돌아본다. 몸만 돌고 좌판은 그대로다.
	 */
	float GetFacingYaw() const;
	/** 지금 돌아봐야 할 목표 방향(도) — 폰이 대화 거리 안이면 폰 쪽(카메라 쪽에서 MaxTalkTurnDegrees 까지), 아니면 카메라 쪽. */
	float ComputeFacingTargetYaw(const APawn* Pawn) const;
	/**
	 * 폰 쪽으로 돌아볼 때 카메라 쪽(요 180)에서 벗어나는 최대 각(도). 내려다보는 카메라에서 옆·뒤로 돌면 머리보다 큰 모자 윗면만
	 * 보여 얼굴이 사라진다(루프#1426 갈고리 「유령」) — 이 각 안이면 3/4 얼굴이 남는다 (DESIGN 「NPC 돌아보기 한도」).
	 */
	static constexpr float MaxTalkTurnDegrees = 60.f;
	/** 몸을 목표 방향으로 한 번에 맞춘다(시작·검사용). */
	void SnapFacing(float WorldYaw);

	/** 상인 종류마다 머리카락 색. 머리카락은 장비 컴포넌트가 플레이어와 같은 길(`UpdateHair`)로 붙인다. */
	struct FNpcHair
	{
		FLinearColor Color;
		float Width;
		float Length;
	};
	static FNpcHair GetKindHair(EShopKind Kind);

	/** 팻말(평일 「일요일에 와요」 / 일요일 이름)이 켜져 있나 / 지금 팻말 글자. */
	bool IsSignShown() const;
	FText GetShownSignText() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UShopScreenWidget* GetShopScreen() const;
	/** 지금 올린 화면(상점 또는 의뢰). 아직 안 다가왔으면 nullptr. */
	UUserWidget* GetScreen() const { return Screen; }

	/** 열기 카드가 붙을 월드 점 — 캡슐 중심 위 정수리 (M23-02, `USnarePromptCardWidget::TapCardTargetLiftCm`). */
	FVector GetPromptWorldLocation() const;

	UStaticMeshComponent* GetStall() const { return Stall; }
	USkeletalMeshComponent* GetBody() const { return Body; }
	UEquipmentComponent* GetEquipment() const { return Equipment; }
	/** 머리카락 덩어리(시험·캡처용) — 장비 컴포넌트의 것. 덮는 모자를 쓰면 아랫단 밑만 그린다. */
	UStaticMeshComponent* GetHairMesh() const;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 종류에 맞춰 이름·모자를 정한다. 의뢰인은 상점 종류를 안 따르므로 자기 것을 지킨다(M105-04 보정). */
	virtual void ApplyKindLook();

	/** 다가왔을 때 올릴 화면. 의뢰인(`AQuestGiverActor`)은 의뢰 화면을 만든다. */
	virtual UUserWidget* CreateScreen(APlayerController* Controller);

	/** 멀어질 때 창을 접는다 — 다음에 다가오면 다시 "열기" 버튼부터. */
	virtual void CollapseScreen();

	/** 상황 버튼으로 창을 편다. 의뢰인은 의뢰 판을 편다. */
	virtual void ExpandScreen();

	UPROPERTY(EditAnywhere, Category = "Shop")
	FText VendorName;

	/** 머리에 쓰는 모자 (`DT_ItemTable` 의 ItemID). */
	FName HatItemID;

private:
	UFUNCTION()
	void OnRep_ShopKind();

	/** 레벨에 세운 거점 상인(`setup_outposts.py`)은 여기서 종류를 정한다 — 시작 때 이름·모자를 이 값으로 맞춘다. */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_ShopKind, Category = "Shop")
	EShopKind ShopKind = EShopKind::General;

	/** 폰이 아직 없으면 다음 틱에 다시 — 붙는 순간 그 폰의 공격 간격으로 거리 검사 타이머를 건다. */
	void StartProximityScan();
	void HandleProximityScan();

	/** 고구마 장수만: 일요일이 아니면 숨고 충돌을 끈다. 다른 가게는 아무 일도 안 한다. */
	void ApplySundayOnly();

	/** 잡화점 좌판 — 손님이 밟고 지나가지 않게 충돌이 켜져 있다. */
	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> Stall;

	/** 상인 몸 — 플레이어 몸 메시. 움직이지 않으니 충돌은 끈다. */
	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<USkeletalMeshComponent> Body;

	/** 상인 모자를 플레이어와 같은 길(`EquipItemByItemID`)로 머리 소켓에 붙인다. */
	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<UEquipmentComponent> Equipment;

	/** 평일 팻말 — 빈 수레 위 화면 공간 알약(`USnareWorldLabelWidget`). 고구마 장수가 닫은 날에만 켠다. */
	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<UWidgetComponent> SignCard;

	/** 머리 위 스토리 표식 — 화면 공간 알약, 팻말보다 한 뼘 위. */
	UPROPERTY(VisibleAnywhere, Category = "Quest")
	TObjectPtr<UWidgetComponent> MarkCard;

	SnareStory::EGiverMark StoryMark = SnareStory::EGiverMark::None;

	/** 일요일에 열린 고구마 장수 상인 앞 고구마 바구니. */
	UPROPERTY(VisibleAnywhere, Category = "Shop")
	TObjectPtr<UStaticMeshComponent> PotatoBasket;

	/** 몸이 돌아보는 중인 목표(월드 도). 틱은 현재 방향이 목표와 다를 때만 켠다. */
	float FacingTargetYaw = 180.f;

	bool bClosedToday = false;
	bool bArrivalNoticeShown = false;

	UPROPERTY()
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> Screen;

	FTimerHandle ProximityTimerHandle;
};
