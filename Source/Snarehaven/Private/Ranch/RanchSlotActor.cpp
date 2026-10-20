#include "Ranch/RanchSlotActor.h"
#include "Snarehaven.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/World.h"
#include "Ranch/RanchPetActor.h"
#include "Engine/StaticMesh.h"
#include "Components/WidgetComponent.h"
#include "UI/SnarePromptCardWidget.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 말뚝 월드 높이 — DESIGN 3.2 「목장 칸 말뚝 진짜 모습」 120cm(자리표시 기둥 높이 그대로). 한 배율이라 바닥 폭은 원본 비율.
	constexpr float PostHeightCm = 120.0f;

	// 프롬프트 카드를 띄울 높이 — 말뚝 꼭대기에서 40cm 위. 이름표(캡슐 꼭대기 +60cm)보다 낮게 잡는다:
	// 칸에 선 펫·플레이어 이름표와 같은 줄에 겹치면 둘 다 못 읽는다.
	constexpr float PromptLiftCm = 40.0f;
}

ARanchSlotActor::ARanchSlotActor()
{
	// 칸의 내용은 백엔드 스냅샷이 올 때만 바뀐다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;

	// 루트를 빈 씬 컴포넌트로 두는 이유: 메시를 루트로 삼으면 스폰 위치가 메시 한가운데가 돼
	// 기둥 절반이 바닥에 묻히고, 자식 메시가 부모 배율까지 물려받아 크기 계산이 꼬인다.
	// 루트는 발밑(Z=0)에 두고 두 메시를 각각 월드 크기로 잡는다.
	SlotRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SlotRoot"));
	RootComponent = SlotRoot;

	Post = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Post"));
	Post->SetupAttachment(SlotRoot);

	Loop = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Loop"));
	Loop->SetupAttachment(SlotRoot);

	// 말뚝 메시 하나(M13-40) — 밧줄·쇠고리·밑돌이 메시에 들어 있어 예전 기둥·고리 둘을 대신한다.
	// 높이를 맞추는 한 배율, 메시 가운데를 루트 위에·밑면을 바닥(z 0)에 둔다.
	// `Loop` 는 메시 없이 남겨 둔다 — 레벨 `BP_RanchSlot` 인스턴스가 이 이름의 하위 오브젝트를 들고 있어 지우지 않는다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TetherMeshAsset(
		TEXT("/Game/Game/Environment/Props/TetherPost/SM_Prop_TetherPost_01.SM_Prop_TetherPost_01"));
	if (TetherMeshAsset.Succeeded())
	{
		const FBox Box = TetherMeshAsset.Object->GetBoundingBox();
		const float Scale = PostHeightCm / FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(Box.GetSize().Z));
		Post->SetStaticMesh(TetherMeshAsset.Object);
		Post->SetRelativeScale3D(FVector(Scale));
		Post->SetRelativeLocation(FVector(-Box.GetCenter().X, -Box.GetCenter().Y, -Box.Min.Z) * Scale);
	}

	// 충돌은 이번 조각 범위 밖이다 — 상호작용은 거리로 판정하고(M2-04c), 막고 못 막고는
	// 그때 정한다. 지금 켜 두면 자리표시 크기에 플레이어 동선이 묶인다 (`AGatherableActor` 와 같은 판단).
	Post->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Loop->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 라이트맵을 굽지 않는 자리표시 무대라 Movable 이 맞다 — Static 이면 캡처에
	// 빨간 "LIGHTING NEEDS TO BE REBUILT" 가 같이 찍힌다 (루프#26 에서 실제로 겪음).
	Post->SetMobility(EComponentMobility::Movable);
	Loop->SetMobility(EComponentMobility::Movable);

	// 상호작용 프롬프트 (M15-03) — 말뚝 위 화면 공간 카드. 이름표와 같은 길이라 설정도 같다:
	// 화면 공간이라 거리에 따라 안 작아지고, 바닥 쪽을 기준점으로 잡아 말뚝 위에 앉는다.
	PromptCard = CreateDefaultSubobject<UWidgetComponent>(TEXT("PromptCard"));
	PromptCard->SetupAttachment(SlotRoot);
	PromptCard->SetRelativeLocation(FVector(0.f, 0.f, PostHeightCm + PromptLiftCm));
	PromptCard->SetWidgetSpace(EWidgetSpace::Screen);
	PromptCard->SetDrawAtDesiredSize(true);
	PromptCard->SetPivot(FVector2D(0.5f, 1.f));
	PromptCard->SetWidgetClass(USnarePromptCardWidget::StaticClass());
	PromptCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PromptCard->SetGenerateOverlapEvents(false);
	// 시트: "대상이 없으면 안 보임". 켜는 것은 `SetHighlighted` 하나뿐이다.
	PromptCard->SetVisibility(false);
}

void ARanchSlotActor::SetHighlighted(bool bInHighlighted)
{
	bHighlighted = bInHighlighted;

	// 기둥과 고리가 한 몸으로 읽혀야 해서 둘 다 켠다 (`AGatherableActor` 는 몸이 하나라 한 줄이었다).
	if (IsValid(Post))
	{
		Post->SetRenderCustomDepth(bHighlighted);
	}
	if (IsValid(Loop))
	{
		Loop->SetRenderCustomDepth(bHighlighted);
	}

	// 프롬프트 카드도 같은 스위치로 (M15-03) — 자동 상호작용 대상일 때만 뜬다.
	if (IsValid(PromptCard))
	{
		PromptCard->SetVisibility(bHighlighted);
	}
}

void ARanchSlotActor::ShowPet(const FAnimalRow* Row)
{
	const FName Wanted = Row != nullptr ? Row->AnimalID : NAME_None;
	if (IsValid(Pet) && Wanted == PetAnimalID)
	{
		return;
	}
	if (IsValid(Pet))
	{
		Pet->Destroy();
	}
	Pet = nullptr;
	PetAnimalID = NAME_None;

	UWorld* const World = GetWorld();
	if (Row == nullptr || World == nullptr)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Params.Owner = this;
	ARanchPetActor* const Spawned = World->SpawnActor<ARanchPetActor>(GetActorLocation(), GetActorRotation(), Params);
	if (Spawned == nullptr)
	{
		return;
	}
	if (!Spawned->ApplyRow(*Row))
	{
		Spawned->Destroy();   // 메시 없는 종(자리표시)은 몸 없이 — 말뚝만 보인다
		return;
	}
	// 앞쪽 = 칸에 붙은 것 중 가장 먼 것(뒤 난간)의 반대 — 발판은 말뚝 한가운데라 방향을 안 준다.
	// 떨어진 게 없으면(확장 칸 줄) 온 둘레를 쓴다.
	FVector Front = FVector::ZeroVector;
	TArray<AActor*> Pieces;
	GetAttachedActors(Pieces);
	for (const AActor* const Piece : Pieces)
	{
		const FVector Away = (GetActorLocation() - Piece->GetActorLocation()) * FVector(1.f, 1.f, 0.f);
		if (Away.Size() > FMath::Max(Front.Size(), 50.f))
		{
			Front = Away;
		}
	}
	Spawned->SetTetherHome(GetActorLocation(), Front);
	Pet = Spawned;
	PetAnimalID = Wanted;
	UE_LOG(LogSnarehaven, Log, TEXT("[RanchPet] %d번 칸에 %s 세움"), SlotIndex, *Wanted.ToString());
}

void ARanchSlotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShowPet(nullptr);
	Super::EndPlay(EndPlayReason);
}
