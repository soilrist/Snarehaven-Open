#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/ItemRow.h"
#include "EquipmentComponent.generated.h"

class UDataTable;
class USpotLightComponent;
class UAudioComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 한 부위에 지금 들려 있는 것 — **회선에 실리는 전부다** (ROADMAP M9-06).
 * 메시 참조가 아니라 **행 이름**만 간다. 게스트는 같은 `DT_ItemTable` 을 갖고 있으니 이름만 있으면
 * 자기 쪽에서 같은 메시를 찾는다 — 에셋 경로를 패킷에 실으면 없는 쪽에서 조용히 빈다.
 */
USTRUCT(BlueprintType)
struct FEquippedSlot
{
	GENERATED_BODY()

	/** 부위. 표가 적은 부위가 아니라 **실제로 든 부위**다 (검사용 덮어쓰기까지 반영된 값). */
	UPROPERTY(BlueprintReadOnly, Category = "Equipment")
	EEquipSlot Slot = EEquipSlot::None;

	/** `DT_ItemTable` 의 **행 이름** (`Item_Club`). `ItemID`(`item.club`)가 아니다. */
	UPROPERTY(BlueprintReadOnly, Category = "Equipment")
	FName ItemRowName;

	/** 붙은 소켓 이름. 행이 적은 대로 들었으면 행의 값이 그대로 들어온다. 옷 부위는 비어 있다. */
	UPROPERTY(BlueprintReadOnly, Category = "Equipment")
	FName SocketName;
};

// ROADMAP M5-06b: 장비 행이 가리키는 메시를 몸의 소켓에 붙인다 (DESIGN 3.5).
//
// **이건 순수한 겉모습이다.** 장비에는 스탯이 없고(DESIGN 3.5 — 맨손 공격에 종별 차등이 없다),
// 여기서 바뀌는 것은 화면에 무엇이 보이는가 하나다. 백엔드는 안 탄다 —
// 보유 원본은 인벤토리(백엔드)에 있고(CLAUDE.md 8), 이 컴포넌트는 "그중 하나를 손에 들고 있다" 만 그린다.
//
// 멀티(M9-06 · DESIGN 3.4): **부위별 장착 이름만** 복제한다. 판정은 호스트 한 곳이고(게스트가 부르면
// `Server_` 요청이 올라간다), 호스트가 고친 목록이 모두에게 내려가 각자 자기 화면을 그린다.
// **재화·인벤토리는 여전히 회선에 안 탄다** — 이 목록은 "지금 무엇을 걸치고 있나" 뿐이다.
/**
 * 걸친 것이 달라진 순간. HUD 가 이걸 듣고 장비 버튼을 다시 그린다 (M9-07b).
 * 사람이 버튼으로 갈아입을 때만 화면을 고치던 때는, 시작 지급처럼 **사람 손을 안 거친 장착**이
 * 화면에 안 나타났다 — 몽둥이는 손에 붙었는데 HUD 에는 버튼이 없는 화면이 나온다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEquipmentChangedSignature);

UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 손에 든 총(`MagazineSize > 0`)의 보임 배율 — DESIGN 8.6-c 「손에 든 총 보임 배율」. 메시 행 길이 표는 원본 치수 그대로. */
	static constexpr float HeldGunScale = 1.5f;

	UEquipmentComponent();

	/** 걸친 것이 바뀔 때마다 울린다 — 호스트가 갈아입힌 것도, 복제로 내려온 것도 같이. */
	UPROPERTY(BlueprintAssignable, Category = "Equipment")
	FOnEquipmentChangedSignature OnEquipmentChanged;

	/**
	 * `DT_ItemTable` 의 장비 행을 읽어 그 메시를 몸 소켓에 붙인다.
	 * 같은 부위에 이미 뭔가 들려 있으면 그것을 떼고 바꿔 든다 (한 부위 = 한 개 — **부위 안에서만**이라
	 * 무기와 도구는 동시에 들린다).
	 * @param ItemRowName   `DT_ItemTable` 의 **행 이름** (`Item_Club`). `ItemID`(`item.club`)가 아니다
	 * @param SlotOverride  행이 적은 부위 대신 이 부위에 든다. **3층 검사 자리표시 전용** (M9-01) —
	 *                      도구 행이 아직 없어서(M9-04 감독 대기) 몽둥이를 왼손 자리에 걸어 본다.
	 *                      `None` 이면 행이 적은 대로 (평소 길)
	 * @param SocketOverride 같은 이유로 소켓 이름만 바꿔 붙인다. `NAME_None` 이면 행이 적은 대로
	 * @return 실제로 붙었으면 true. 장비가 아닌 행·빈 메시·없는 소켓이면 아무것도 안 하고 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool EquipItem(FName ItemRowName, EEquipSlot SlotOverride = EEquipSlot::None,
		FName SocketOverride = NAME_None);

	/**
	 * **아이템 ID**(`item.club`)로 장비 행을 찾아 든다 (M9-07b). 백엔드가 주는 목록은 행 이름이 아니라
	 * ID 라, 행 이름(`Item_Club`)을 부르는 쪽 코드에 적지 않으려고 표에서 찾는다 (DataDriven.md 1).
	 * 부위·소켓은 행이 적은 대로다 — 덮어쓰기 없이 `EquipItem` 과 **같은 길**로 들어간다.
	 * @return 장비 행을 찾아 실제로 들었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool EquipItemByItemID(FName ItemID);

	/**
	 * 그 아이템 ID 를 가진 **장비 행**의 이름. 표에 없거나 장비가 아닌 행이면 NAME_None.
	 * 찾는 자리를 하나로 두려고 따로 뺐다 — 장착하는 쪽과 검사가 같은 함수를 본다.
	 */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	FName FindEquipmentRowByItemID(FName ItemID) const;

	/** 이 폰의 장착을 여기서 정해도 되는지 (호스트인지). 액터에 안 붙었으면 권위로 본다 (2층 검사가 쓰는 길). */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool HasEquipAuthority() const;

	/** 게스트 -> 호스트 장착 요청 (M9-06). 인자는 `EquipItem` 과 같다 — 붙이는 판정은 호스트가 다시 한다.
	 *  `bQuiet` = 게스트가 `FQuietChanges` 안에서 보낸 요청(맵 이동 복원 등) — 호스트도 소리를 안 낸다 (M47-03). */
	UFUNCTION(Server, Reliable)
	void Server_EquipItem(FName ItemRowName, EEquipSlot SlotOverride, FName SocketOverride, bool bQuiet);

	/** 게스트 -> 호스트 해제 요청 (M9-06). */
	UFUNCTION(Server, Reliable)
	void Server_UnequipSlot(EEquipSlot Slot, bool bQuiet);

	/**
	 * 플레이어가 하지 않은 바꿈(시작 지급·서버 기록 복원·맵 이동 뒤 다시 걺·상인 모자)을 감싼다 — 그동안은
	 * 장비·옷 소리를 안 낸다 (M47-03 · DESIGN 2.2 「장비·탈것 소리」 무음 길). 게스트면 요청에 실려 호스트도 조용하다.
	 */
	struct FQuietChanges
	{
		explicit FQuietChanges(UEquipmentComponent* InEquipment);
		~FQuietChanges();
	private:
		TWeakObjectPtr<UEquipmentComponent> Equipment;
		bool bWasQuiet = false;
	};

	/** 호스트가 낸 장비·옷 소리 요청 수 (간격·같은 행·무음 길을 거른 뒤). 검사용. */
	int32 GetChangeCueCount() const { return ChangeCueCount; }

	/** 마지막으로 낸 소리 이름 (`EquipGun`·`Holster`·`Outfit` …). 검사용. */
	FName GetLastChangeCue() const { return LastChangeCue; }

	/**
	 * 옷 부위에 스켈레탈 메시를 **한 겹 덮는다** (ROADMAP M9-02 · 결정 006 부위별 모듈형 레이어).
	 * 몸 메시를 갈아 끼우지 않는다 — 몸은 그대로 두고 그 위에 컴포넌트를 하나 더 얹은 뒤
	 * `SetLeaderPoseComponent` 로 몸의 뼈를 그대로 따라가게 한다(옷이 자기 애니메이션을 돌리지 않는다).
	 * `EquipItem` 의 옷 갈래도 표에서 메시를 읽어 이 함수로 들어온다 — **붙는 줄은 하나다.**
	 * @param Slot       옷 부위(`OutfitTop`·`OutfitBottom`·`Shoes`·`Gloves`)만 받는다. 손에 드는 부위면 아무것도 안 하고 false
	 * @param OutfitMesh 덮을 스켈레탈 메시. 몸과 같은 스켈레톤이어야 자세가 맞는다 (결정 006 「감수하는 것」)
	 * @return 실제로 덮었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool EquipOutfitMesh(EEquipSlot Slot, USkeletalMesh* OutfitMesh);

	/** 그 부위가 옷 부위인가 (`OutfitTop`·`OutfitBottom`·`Shoes`·`Gloves` — 뼈 따라 휘는 한 겹). 붙이는 종류가 여기서 갈린다. */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	static bool IsOutfitSlot(EEquipSlot Slot);

	/**
	 * 그 부위에 들려 있는 것을 뗀다. 비어 있으면 아무 일도 없다.
	 * @param Slot 뗄 부위
	 * @return 실제로 뗀 것이 있으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool UnequipSlot(EEquipSlot Slot);

	/** 그 부위에 들려 있는 아이템의 행 이름. 비어 있으면 NAME_None. */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	FName GetEquippedItem(EEquipSlot Slot) const;

	/**
	 * 그 부위에 들려 있는 아이템의 **표 한 줄**. 비어 있거나 표에 없으면 nullptr (M11-04).
	 * 사거리·감쇠를 읽으려는 쪽(`UMeleeAttackComponent`)이 표를 따로 들지 않게 하려고 여기서 돌려준다 —
	 * 표를 두 군데서 들면 서로 다른 표를 가리킬 수 있고, 무엇을 들고 있는지는 이미 이 컴포넌트만 안다.
	 * 포인터는 표가 살아 있는 동안만 유효하니 **들고 있지 말고 그 자리에서 읽는다**.
	 */
	const FItemRow* GetEquippedRow(EEquipSlot Slot) const;

	/** 그 부위에 **손에 든** 메시 컴포넌트. 옷 부위는 늘 nullptr 다 (종류가 다르다). */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	UStaticMeshComponent* GetEquippedMesh(EEquipSlot Slot) const;

	/** 그 부위에 **덮인 옷** 컴포넌트. 손에 드는 부위는 늘 nullptr 다. */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	USkeletalMeshComponent* GetEquippedOutfit(EEquipSlot Slot) const;

	/** 지금 걸치고 있는 것 전부 — 회선에 실리는 목록 그대로다 (M9-06). */
	const TArray<FEquippedSlot>& GetEquippedSlots() const { return EquippedSlots; }

	/**
	 * 그 부위에 꾸미기(`EItemType::Cosmetic`)를 **겉모습 덮개**로 입힌다 (`docs/specs/skin-parts.md` 조각 b).
	 * 같은 부위의 장비 줄(`EquippedSlots`)은 건드리지 않는다 — 효과(`GetEquippedRow`·머리 불빛)는 그대로고
	 * 보이는 메시만 꾸미기로 바뀐다. 꾸미기를 벗기면 장비 메시(없으면 기본 겉모습)로 돌아온다.
	 * 가졌는지 판정은 여기서 안 한다 — 서버(`SetOutfit`, 조각 d)가 확인한 뒤에 부르는 그리는 쪽이다 (CLAUDE.md 8).
	 * 게스트는 장착과 같이 호스트에 요청만 올린다.
	 * @param CosmeticRowName `DT_ItemTable` 의 꾸미기 **행 이름**. NAME_None 이면 그 부위 꾸미기를 벗긴다
	 * @return 목록이 실제로 바뀌었으면 true. 꾸미기가 아닌 행·부위가 다른 행이면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool SetCosmetic(EEquipSlot Slot, FName CosmeticRowName);

	/**
	 * **아이템 ID**(서버 `SetOutfit` 의 `itemId`)로 꾸미기 행을 찾아 `SetCosmetic` 과 같은 길로 입힌다 (조각 d).
	 * NAME_None 이면 그 부위 꾸미기를 벗긴다. 서버가 받아들인 뒤에만 부른다 — `UBackpackComponent::RequestSetOutfit`.
	 * @return 목록이 실제로 바뀌었으면 true. 표에 그 ID 의 꾸미기 행이 없으면 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	bool SetCosmeticByItemID(EEquipSlot Slot, FName ItemID);

	/** 게스트 -> 호스트 꾸미기 요청. 인자는 `SetCosmetic` 과 같다. */
	UFUNCTION(Server, Reliable)
	void Server_SetCosmetic(EEquipSlot Slot, FName CosmeticRowName, bool bQuiet);

	/** 그 부위에 입힌 꾸미기의 행 이름. 없으면 NAME_None. */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	FName GetCosmeticItem(EEquipSlot Slot) const;

	/** 입힌 꾸미기 전부 — 회선에 실리는 목록 그대로다. */
	const TArray<FEquippedSlot>& GetCosmeticSlots() const { return CosmeticSlots; }

	/** 호스트가 보낸 꾸미기 목록을 받아 다시 그린다 — `ApplyEquippedSlots` 와 같은 까닭(2층이 복제 길을 태운다). */
	void ApplyCosmeticSlots(const TArray<FEquippedSlot>& InSlots);

	/**
	 * 목록대로 화면을 **처음부터 다시 그린다** (M9-06). 늘어난 것·사라진 것을 따로 세지 않는다 —
	 * 회선에 실려 오는 것이 목록 전체이고, 부위가 다섯뿐이라 전부 다시 거는 값이 싸다.
	 * `OnRep_EquippedSlots` 가 이걸 부르고, **몸이 선 직후**에도 한 번 불러야 한다 —
	 * 초기 복제는 `BeginPlay` 보다 먼저 와서 그때는 소켓이 달릴 몸 메시가 아직 비어 있다
	 * (`ASnarehavenCharacter::BeginPlay` 의 `ApplyPlayerBody` 바로 뒤).
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void RefreshEquippedVisuals();

	/**
	 * 호스트가 보낸 목록을 그대로 받아 화면을 다시 그린다. 게임에서는 복제가 이 길을 만들고,
	 * **2층 검사는 넷드라이버 없이 같은 길을 태우려고** 이 함수로 호스트의 목록을 게스트 쪽에 옮긴다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Equipment")
	void ApplyEquippedSlots(const TArray<FEquippedSlot>& InSlots);

	/** 행에 `HatWidthCm` 이 없을 때(0) 모자가 월드에서 갖는 폭 cm. DESIGN 3.5 「모자별 폭」 기본값. */
	float GetDefaultHatWidthCm() const { return DefaultHatWidthCm; }

	/** 든 모터 도구의 되풀이 소리(`HoldLoopSoundCue`, 전기톱 — M16-03)가 지금 나고 있는가. 쓰는 동안만 난다. 검사용. */
	bool IsHoldLoopPlaying() const;

	/** 든 모터 도구의 되풀이 소리 이름(쓸 때 켜는 것). 모터 도구가 아니면 NAME_None. 검사용. */
	FName GetHoldLoopCue() const { return HoldLoopCue; }

	/**
	 * 도구를 **쓰는 중**이라고 알린다 — 그 시간 동안 모터 도구(되풀이 소리가 있는 도구, 전기톱)가 세게 떨린다
	 * (M18-06 · DESIGN 8.8 「전기톱 떨림」). 동작을 트는 쪽(`ASnarehavenCharacter::StartItemAction`)이 클립 길이를 준다.
	 */
	void SetToolWorkSeconds(float Seconds) { ToolWorkSecondsLeft = FMath::Max(Seconds, 0.f); }

	/** 지금 도구 부착물에 거는 떨림 진폭 cm. 모터 도구가 아니면 0. 검사용. */
	float GetToolVibrationAmplitudeCm() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 머리 불빛 (채굴 모자, DESIGN 8.16). 한 번도 켠 적 없으면 nullptr — 켜졌는지는 `IsVisible`. */
	USpotLightComponent* GetHeadLight() const { return HeadLight; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 부위가 비었을 때 **화면에만** 거는 기본 겉모습 아이템 ID (M15-05 · DESIGN 3.5 「기본 겉모습」).
	 * 목록(`EquippedSlots`)에 넣지 않으니 `GetEquippedItem` 은 None 그대로다 — 효과·배낭 칸·지급이 전혀 없고
	 * (재화·인벤토리는 백엔드 몫, CLAUDE.md 8), 모든 기계가 같은 기본값으로 스스로 그려 복제도 필요 없다.
	 * 진짜 모자·배낭을 들면 그 부위는 그것으로 덮이고, 벗으면 기본으로 돌아온다. 비우면 대머리(상인 등).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<FName> DefaultLookItemIDs;

	/**
	 * 머리 칸이 비었을 때 정수리(`Socket_Head`)에 붙이는 **기본 머리카락 덩어리** (M19-01 · DESIGN 3.5 「기본 겉모습」).
	 * 시트 `character_player.md` "머리카락은 덩어리 하나(모자와 교체)" — 모자를 쓰면 숨고 벗으면 다시 보인다.
	 * 아이템이 아니다: 표·목록·배낭 칸에 안 들어가고(재화·인벤토리는 백엔드 몫, CLAUDE.md 8) 기계마다 스스로 그린다.
	 * 비우면 머리카락이 없다(상인 등).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UStaticMesh> DefaultHairMesh;

	/**
	 * 머리를 덮는 모자를 쓸 때 머리카락에 거는 머티리얼 — 메시 공간 높이 `HairTopZ` 위는 안 그린다
	 * (`tools/unreal/make_hair_under_hat_material.py` · DESIGN 3.5 「모자 밑 머리카락」). 비우면 예전처럼 통째로 숨긴다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSoftObjectPtr<UMaterialInterface> HairUnderHatMaterial;

	/**
	 * 모자 밑 머리카락 윗끝을 모자 뒤 아랫단보다 **얼마나 더 위까지** 남기나, 몸 메시 cm (DESIGN 3.5 「모자 밑 머리카락」 1.0).
	 * 0 이면 아랫단과 머리카락 끝이 딱 맞닿아 비스듬한 각도에서 살색 실금이 보인다 — 모자 안쪽으로 조금 밀어 넣는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HatHairTuckCm = 1.f;

	/** 기본 머리카락 컴포넌트. 한 번도 붙인 적 없으면 nullptr — 보이는지는 `IsHairShown`. 검사용. */
	UStaticMeshComponent* GetHairMesh() const { return HairVisual; }

	/**
	 * 머리카락 색(`DiffuseColor`)을 바꾼다 — 상인처럼 같은 머리카락 메시를 다른 색으로 쓰는 몸(INBOX 보정 10-19 22:45 ②).
	 * 통째로 그릴 때·모자 밑만 그릴 때 둘 다 이 색. 부르지 않으면 메시 제 머티리얼 색(플레이어).
	 */
	void SetHairColor(const FLinearColor& Color);

	/** 기본 머리카락이 **통째로** 보이는가 (머리 칸이 비었거나 왕관처럼 덮지 않는 장식). 모자 밑에 아랫단만 남은 것은 거짓. */
	UFUNCTION(BlueprintPure, Category = "Equipment")
	bool IsHairShown() const;

	/** 덮는 모자 밑으로 머리카락이 **아랫단 아래만** 보이는가 (DESIGN 3.5 「모자 밑 머리카락」). */
	bool IsHairUnderHat() const;

	/** 모자 밑 머리카락을 자르는 높이, 머리카락 메시 공간(정수리 기준 몸 메시 cm). 안 자르면 `TNumericLimits<float>::Max()`. 검사용. */
	float GetHairTopZ() const { return HairTopZ; }

	/** 지금 머리 칸에 보이는 것이 머리카락을 숨기는 모자인가 — 행 `bHatKeepsHair` 가 참인 장식(왕관)은 거짓. 상인도 이 규칙을 쓴다. */
	bool IsHairHiddenByHat() const;

	/** 기본 겉모습을 걸 때 쓰는 배율 (M15-10 · DESIGN 3.5 「기본 겉모습」 — 진짜 장비보다 작아 보여야 한다). */
	float GetDefaultLookScale() const { return DefaultLookScale; }

protected:
	/**
	 * 기본 겉모습에만 곱하는 배율 (M15-10 · DESIGN 3.5 「기본 겉모습」 0.6).
	 * 진짜로 **가진** 배낭과 기본 배낭이 같은 메시라, 크기가 같으면 배낭을 멘 것이 화면에서 티가 안 난다
	 * (INBOX 2026-09-25 ④ "입어도 겉모습이 안 바뀜"). 든 장비는 이 배율을 안 받는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float DefaultLookScale = 0.9f;

	/**
	 * 모자 기본 **월드** 폭 cm (M13-35 · DESIGN 3.5 「모자별 폭」 기본 66, M13-36 머리 실측). 행의 `HatWidthCm` 이 0 이면 이 값.
	 * 폰 몸 배율(약 1.76)·소켓 배율이 무엇이든 붙인 뒤 월드 배율로 맞춘다 — 전에는 소켓 배율이 안 먹어
	 * 100cm 원기둥이 몸을 덮었다(STATUS #0204 감독 3층).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float DefaultHatWidthCm = 66.f;

	/**
	 * 모자 얼굴 앞 아랫단이 내려와도 되는 가장 낮은 곳 = 눈썹선, 정수리 아래 몸 메시 cm (DESIGN 3.5 「모자 아랫단」 14.5).
	 * 더 낮은 모자는 그만큼 정수리 소켓에서 위로 올려 건다(INBOX 결함 10-20 ② 「모자가 눈을 덮음」).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HatBrowBelowCrownCm = 14.5f;

	/** 「얼굴 앞」으로 보는 가로 반폭, 몸 메시 cm (DESIGN 3.5 「모자 아랫단」 16 = 눈 가운데 10.7 + 눈 반폭 5.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HatEyeHalfSpanCm = 16.f;

	/**
	 * 머리 불빛(채굴 모자)이 모자 앞 끝에서 내리비추는 각 ° (DESIGN 13.5 「갱도 투구」 30). 원뿔빛이라 램프 뒤 —
	 * 쓴 사람 얼굴·몸 — 은 비추지 않는다(전수 착용 #1357 `lh1357_Head_05` 얼굴이 하얗게 날아감).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HeadLampPitchDeg = 30.f;

	/** 머리 불빛 원뿔 바깥 반각 ° (DESIGN 13.5 「갱도 투구」 45 — 위 끝이 수평 위 15°, 앞 굴 벽까지). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HeadLampOuterConeDeg = 45.f;

	/** 머리 불빛 원뿔 안쪽(가장 밝은) 반각 ° (DESIGN 13.5 「갱도 투구」 25). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HeadLampInnerConeDeg = 25.f;

	/** 머리 불빛을 모자 앞 끝에서 더 앞으로 띄우는 cm (DESIGN 13.5 「갱도 투구」 5 — 챙 끝에 가리지 않게). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float HeadLampAheadCm = 5.f;

	/** 벨 때 도구 부착물 떨림 진폭 cm (M18-06 · DESIGN 8.8 「전기톱 떨림」 0.45). 공회전은 아래 비율만큼. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float ToolVibrationAmplitudeCm = 0.45f;

	/** 떨림 빠르기 Hz (DESIGN 8.8 「전기톱 떨림」 30). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float ToolVibrationHz = 30.f;

	/** 공회전(들고만 있을 때) 진폭 = 벨 때 × 이 비율 (DESIGN 8.8 「전기톱 떨림」 1/3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	float IdleVibrationRatio = 1.f / 3.f;

	/**
	 * 장비 행을 읽을 표. 기본값은 `DT_ItemTable` 이다
	 * (AAnimalCharacter 의 `AnimalTable` 과 같은 방식 — C++ 전용 프로젝트라 생성자 경로 참조다).
	 * 검사는 표를 갈아 끼우지 않고 `EquipItem` 의 덮어쓰기 두 칸으로 자리표시를 건다 (M9-01).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TObjectPtr<UDataTable> ItemTable;

private:
	/** 몸(스켈레탈 메시) 컴포넌트. 소켓은 여기 달려 있다. 없으면 붙일 자리가 없다. */
	USkeletalMeshComponent* FindBodyMesh() const;

	/** 머리 행이 불빛을 가졌으면 켜고(`HeadLightIntensityCd` > 0), 아니면(nullptr 포함) 끈다. */
	void SetHeadLight(const FItemRow* HeadRow);

	UPROPERTY()
	TObjectPtr<USpotLightComponent> HeadLight;

	/** 도구 행의 `HoldLoopSoundCue` 를 기억하고 바뀌면 나던 소리를 끊는다 (M16-03). 켜고 끄기는 틱이 쓰는 시간으로. 장착 목록이 바뀌는 세 길이 모두 부른다. */
	void UpdateHoldLoop();

	/** 되풀이 소리를 끊고 컴포넌트를 버린다. */
	void StopHoldLoop();

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> HoldLoop;

	FName HoldLoopCue;

	/** 든 도구가 모터 도구인가 (행에 `HoldLoopSoundCue` 가 있다) — 떨림을 거는 조건. 소리가 안 나는 기기에서도 떤다. */
	bool bMotorToolHeld = false;

	float ToolWorkSecondsLeft = 0.f;
	float ToolVibrationPhase = 0.f;

	/** 손에 든 도구의 소켓 상대 자리(쥐는 자리 반영) — 떨림을 이 위에 얹는다. */
	FVector ToolRestLocation = FVector::ZeroVector;

	/** 손에 드는 부위마다 붙어 있는 메시 컴포넌트. 부위당 하나 — 두 개를 겹쳐 들 일이 없다. */
	UPROPERTY()
	TMap<EEquipSlot, TObjectPtr<UStaticMeshComponent>> SlotMeshes;

	/** 옷 부위마다 몸 위에 덮인 컴포넌트. 부위별로 따로라 상의만 갈아입을 수 있다 (결정 006). */
	UPROPERTY()
	TMap<EEquipSlot, TObjectPtr<USkeletalMeshComponent>> OutfitMeshes;

	/**
	 * 부위마다 지금 들고 있는 것. **화면에 무엇이 붙어 있나의 단 하나뿐인 원본**이고, 그대로 복제된다
	 * (M9-06). 로컬 캐시를 따로 두지 않는 이유는 둘이 갈라지면 남의 화면만 틀리기 때문이다.
	 * `TMap` 은 복제가 안 돼서 배열이다 — 부위가 다섯뿐이라 훑어도 싸다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_EquippedSlots)
	TArray<FEquippedSlot> EquippedSlots;

	UFUNCTION()
	void OnRep_EquippedSlots();

	/**
	 * 부위마다 입힌 꾸미기 — 장비 목록과 **따로** 둔다. 한 목록에 섞으면 꾸미기를 입는 순간 효과 장비가 빠진다.
	 * 복제되면 장비 목록과 같은 `OnRep_EquippedSlots` 로 화면을 처음부터 다시 그린다.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_EquippedSlots)
	TArray<FEquippedSlot> CosmeticSlots;

	/** 그 부위에 꾸미기가 있으면 그 메시를 건다 — 머리면 불빛은 장비 행 것으로 되돌린다. 걸었으면 true. */
	bool ApplyCosmeticVisual(EEquipSlot Slot);

	/**
	 * 표에서 메시를 찾아 **화면에만** 건다 — 목록(`EquippedSlots`)은 건드리지 않는다.
	 * 호스트의 `EquipItem` 과 복제본의 `RefreshEquippedVisuals` 가 **같은 이 함수**로 모인다.
	 * @param OutSlot   실제로 든 부위 (덮어쓰기가 없으면 행이 적은 부위)
	 * @param OutSocket 실제로 붙은 소켓 이름 (옷 부위는 `NAME_None`)
	 * @param VisualScale 겉모습에만 겹치는 배율 (M15-10 기본 겉모습 0.6). 든 장비는 1 이다
	 * @return 실제로 붙었으면 true
	 */
	bool ApplyEquipVisual(FName ItemRowName, EEquipSlot SlotOverride, FName SocketOverride,
		EEquipSlot& OutSlot, FName& OutSocket, float VisualScale = 1.f);

	/** 목록에 없는 부위에 `DefaultLookItemIDs` 겉모습을 건다. 몸 메시가 아직 없으면 아무것도 안 한다. */
	void ApplyDefaultLook();

	/** 도구가 무기와 같은 소켓(오른손)에 붙어 있으면 무기를 숨긴다 — 한 손에 하나만 보인다 (감독 09-25, 레퍼런스). */
	void UpdateHandVisibility();

	/** 머리 칸이 비면 머리카락을 통째로 보이고(M19-01), 덮는 모자를 쓰면 모자 뒤 아랫단 아래만 남긴다(DESIGN 3.5 「모자 밑 머리카락」).
	 *  머리를 덮지 않는 장식(행 `bHatKeepsHair`)은 써도 통째로 보인다. */
	void UpdateHair();

	/** 지금 머리 칸에 건 행의 `bHatKeepsHair` — 머리 메시를 걸 때(`ApplyEquipVisual`) 정한다. */
	bool bHeadKeepsHair = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HairVisual;

	/** `HairUnderHatMaterial` 에서 만든 인스턴스 — 머리카락 색(`DiffuseColor`)을 옮겨 두고 `HairTopZ` 만 바꾼다. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HairUnderHatMID;

	/** `SetHairColor` 로 받은 색 — 비었으면 메시 제 머티리얼 색. */
	TOptional<FLinearColor> HairColor;

	/** 통째로 그릴 때 `HairColor` 를 입힌 인스턴스 (`SetHairColor` 를 부른 몸만). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HairColorMID;

	/** 지금 머리카락 자르는 높이 (`GetHairTopZ`). */
	float HairTopZ = TNumericLimits<float>::Max();

	/** 그 부위의 기본 겉모습 행 이름. 기본이 없으면 NAME_None. */
	FName FindDefaultLookRow(EEquipSlot Slot) const;

	/** 그 부위 줄을 목록에 넣거나 덮어쓴다 (호스트 전용). */
	void SetEquippedSlot(EEquipSlot Slot, FName ItemRowName, FName SocketName);

	/**
	 * 호스트가 그 부위가 바뀐 소리를 정한다 (M47-03) — 손 칸은 `SnareSound::EquipCue`(`HandRow` nullptr = 넣기),
	 * 옷 부위는 `OutfitCue`. 무음 길(`bQuietChanges`)·표의 간격 안 연타는 거른다. 낼 소리면 모든 기기에 알린다.
	 */
	void RequestChangeCue(EEquipSlot Slot, const FItemRow* HandRow);

	/** 그 플레이어 자리 월드 소리(`bWorld`) 또는 그 플레이어 기기에서만 화면 소리. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayChangeCue(FName Cue, float Volume, bool bWorld);

	bool bQuietChanges = false;
	double LastHandCueSeconds = -UE_BIG_NUMBER;
	double LastOutfitCueSeconds = -UE_BIG_NUMBER;
	int32 ChangeCueCount = 0;
	FName LastChangeCue;
};
