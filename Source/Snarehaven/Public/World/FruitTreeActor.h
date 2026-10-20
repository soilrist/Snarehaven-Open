#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SnareFruitTree.h"
#include "FruitTreeActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class APawn;
class USceneComponent;
class UStaticMeshComponent;
class UWidgetComponent;

// DESIGN 8.18 목장 과일나무 한 자리의 **몸**. 어떤 과일이 몇 개 열렸는지는 여기 없다 — 서버 `GetFruitTrees` 답을
// `SnareFruitTree::ParseTrees` 로 옮긴 값을 `ApplyView` 로 받아 그리기만 한다(CLAUDE.md 8).
// 나무는 월드 활엽수 에셋 한 종 — 과일 5종은 가지에 달린 열매 색만 다르다.
UCLASS()
class SNAREHAVEN_API AFruitTreeActor : public AActor
{
	GENERATED_BODY()

public:
	AFruitTreeActor();

	/** 이 자리가 서버 `slot`(0~3) 몇 번인지. 맵에 세울 때 정한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fruit", meta = (ExposeOnSpawn = "true"))
	int32 SlotIndex = INDEX_NONE;

	/** 서버 값대로 그린다: 단계에 맞는 키, 가지에 열매 수만큼, 열매 색은 과일 종류대로. */
	void ApplyView(const SnareFruitTree::FTreeView& View);

	/**
	 * 손님 화면에 방장 거점 나무 한 그루를 세운다(DESIGN 8.18 「남의 과일 얻기」). 나무는 복제되지 않아 손님 쪽에 직접 만든다.
	 * 과일은 방장 계정에서 계산하고(`NativeFruitOf`), 열매 3개 단 다 자란 모양이다. 방장 id 가 비면 세우지 않는다.
	 * 내 나무 자리가 아니라서 `SlotIndex` 는 비워 둔다 — 심기·따기·베기는 이 나무에 안 걸린다.
	 * @return 세운 나무, 못 세우면 nullptr
	 */
	static AFruitTreeActor* SpawnFriendTree(UWorld* World, const FTransform& Where, const FString& HostPlayFabId, const FString& HostNickname);

	/** 손님 화면 친구 나무 자리(목장 L_Ranch, 시작점 곁 빈 풀밭 — 🟡 루프#942 가정). */
	static FTransform GetFriendTreeSpot();

	/**
	 * 손님 화면에 방장 나무가 **딱 한 그루** 서 있게 한다. 같은 방장 나무가 이미 있으면 그대로 두고, 다른 방장 것이면 치우고 새로 세운다.
	 * `HostId` 가 비면 있던 친구 나무를 치우고 nullptr. 폰이 방장 본인이면 부르지 않는다(호출한 쪽이 거른다).
	 */
	static AFruitTreeActor* EnsureFriendTree(UWorld* World, const FString& HostPlayFabId, const FString& HostNickname);

	bool IsFriendTree() const { return !FriendHostId.IsEmpty(); }
	const FString& GetFriendHostId() const { return FriendHostId; }
	/** 친구 나무 이름표 「○○의 복숭아나무」. 내 나무면 빈 글자. */
	const FText& GetFriendLabel() const { return FriendLabel; }

	/** 이 폰의 대화 거리 안 가장 가까운 **내** 나무 자리 (상황 버튼 「따기」·「심기」 가 본다). 친구 나무는 건너뛴다. 없으면 nullptr. */
	static AFruitTreeActor* FindInRange(const APawn* Pawn);

	/** 이 폰의 대화 거리 안 가장 가까운 **친구** 나무 (상황 버튼 「흔들기」 가 본다). 없으면 nullptr. */
	static AFruitTreeActor* FindFriendInRange(const APawn* Pawn);

	/** 후보 중 `Origin` 에서 `RangeCm` 안 가장 가까운 것. `bFriend` 가 나무 종류(친구/내)를 가른다 — 갈라 고르는 규칙 시험용으로 분리. */
	static AFruitTreeActor* PickNearest(const TArray<AFruitTreeActor*>& Candidates, const FVector& Origin, float RangeCm, bool bFriend);

	/** 친구 나무 이름표(화면 공간 알약)가 지금 켜져 있나 — 내 나무는 항상 꺼짐. 2층 검사용. */
	bool IsLabelShown() const;
	/** 이름표 위젯이 실제로 들고 있는 글자. 위젯이 아직 없으면 빈 글자. */
	FText GetShownLabelText() const;

	SnareFruitTree::EStage GetStage() const { return Stage; }
	int32 GetShownFruitCount() const;
	FLinearColor GetFruitColor() const { return FruitColor; }

	/** 다 자란 나무의 월드 키(cm). 2층 검사·열매 자리가 쓴다. */
	static constexpr float GrownHeightCm = 310.f;
	static constexpr int32 FruitSlots = 3;

private:
	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TObjectPtr<USceneComponent> Root;

	/** 나무 몸 묶음 — 단계별 배율은 이 뿌리에 준다. 월드 활엽수 에셋 한 개(`SM_Nature_Tree_04`, 맵 나무와 같은 몸). */
	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TObjectPtr<USceneComponent> TreeRoot;

	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TObjectPtr<UStaticMeshComponent> TreeBody;

	/** 빈 자리 표시 — 납작한 흙 둔덕. 심을 자리인 걸 알린다. */
	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TObjectPtr<UStaticMeshComponent> Mound;

	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TArray<TObjectPtr<UStaticMeshComponent>> Fruits;

	/** 친구 나무 이름표 「○○의 복숭아나무」 — 나무 꼭대기 위 화면 공간 알약(`USnareWorldLabelWidget`). 친구 나무일 때만 켠다. */
	UPROPERTY(VisibleAnywhere, Category = "Fruit")
	TObjectPtr<UWidgetComponent> LabelCard;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> FruitBaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FruitMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MoundMaterial;

	FString FriendHostId;
	FText FriendLabel;
	SnareFruitTree::EStage Stage = SnareFruitTree::EStage::Empty;
	FLinearColor FruitColor = FLinearColor::Gray;
};
