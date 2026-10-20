#include "Animals/AnimalCharacter.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Animals/AnimalStateComponent.h"
#include "SnarehavenCharacter.h"
#include "Data/AnimalRow.h"
#include "Components/CapsuleComponent.h"
#include "World/WaterRules.h"
#include "World/SnareSound.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "Engine/DataTable.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// DESIGN 3.1 표 「동물 몸 세로(캡슐 높이)」 = 90cm (감독 결정 2026-09-22).
	// MVP 는 전 종 공통이라 행이 아니라 여기 한 곳에 둔다 — 종별로 달라지면 그때 `FAnimalRow` 로 내린다.
	constexpr float AnimalCapsuleHeight = 90.f;

	// DESIGN 3.1 표 「동물 도주 이탈 한계」 = 500cm (루프#138). 캡슐 높이와 같은 이유로 여기 한 곳에 둔다 —
	// 전 종 공통이라 아직 행으로 내릴 이유가 없다.
	constexpr float AnimalFleeLeashRadius = 500.f;

	// 경계를 도는 쪽을 **놓아 주는** 반지름 = 한계의 0.9배. 한계 그 자체를 놓아 주는 선으로 쓰면,
	// 도는 동안 동물은 경계에 딱 붙어 있어서 반지름이 한 걸음마다 선의 안팎을 오가고 그때마다
	// 도는 쪽이 지워진다(루프#173 이 2층에서 실제로 밟은 함정). 확실히 안으로 들어왔을 때만 놓는다.
	// 밸런스 숫자가 아니라 히스테리시스 여유라 DESIGN 3.1 은 문장으로만 적는다(클립 문턱 9/3 과 같은 취급).
	constexpr float AnimalFleeOrbitReleaseRatio = 0.9f;

	// **막혔다**고 보는 선 = 한 걸음에 갈 수 있었던 거리의 0.25배도 못 갔다. 캐릭터 이동은 부딪히면
	// 실제 변위로 속도를 다시 계산하므로, 벽·바위·사람에 눌린 동물은 속도가 스스로 0 에 붙는다.
	// 0.25 인 이유: 비스듬히 스치기만 해도 절반쯤은 나아가므로 그건 막힌 것이 아니다.
	constexpr float AnimalBlockedMoveRatio = 0.25f;

	// 그 상태가 0.2초(12프레임) 이어져야 우회를 건다 — 한두 프레임짜리 스침으로 방향을 꺾으면
	// 동물이 잔걸음으로 떤다. 우회는 1.0초 동안 유지한다: 그보다 짧으면 막은 것을 못 벗어나 다시 붙고,
	// 길면 사람에게서 멀어지는 방향을 그만큼 오래 버린다.
	// 셋 다 밸런스 숫자가 아니라 히스테리시스·조향 값이라 DESIGN 3.1 은 문장으로만 적는다
	// (클립 문턱 9/3 · 놓아 주는 반지름 0.9 와 같은 취급).
	constexpr float AnimalBlockedGraceSeconds = 0.2f;
	constexpr float AnimalBlockedDetourSeconds = 1.0f;

	// 넘친 거리를 되돌릴 때 쓸 수 있는 몫 = 속도의 절반까지. 전부 안쪽으로 쓰면 접선 성분이 0 이 돼
	// 동물이 안쪽(대개 사람이 있는 쪽)으로 곧장 박고 거기서 또 굳는다 — 2층에서 실제로 그랬다.
	// 절반이면 22cm/s 라 한 걸음(0.75cm)마다 생기는 밀림(0.1cm 미만)을 되돌리기엔 차고 넘친다.
	constexpr float AnimalLeashReturnSpeedRatio = 0.5f;

	// 꺾을 쪽을 고를 때 **캡슐을 실제로 쓸어 본다** — 막힌 자리는 벽 하나가 아니라 벽·사람·도주 한계가
	// 만든 구석이라, 90도처럼 한 쪽을 미리 정해 두면 그 쪽이 또 막혀 그대로 굳는다(2층에서 굳었다).
	// 「열렸다」 = 한 몸 너비(캡슐 반지름 2배)만큼 지나갈 틈이 있다. 쓸어 보는 캡슐은 1cm 줄인다 —
	// 맞닿아 있는 몸·벽이 매번 초기 겹침으로 잡혀 모든 쪽이 막힌 것으로 보이는 걸 피한다.
	constexpr float AnimalBlockedProbeRadii = 2.f;
	constexpr float AnimalBlockedProbeShrink = 1.f;

	// 쓸어 본 것이 **스치는 것**이면 막힌 게 아니다 — 맞닿은 몸 옆을 지나갈 때는 늘 한 번 닿는다.
	// 부딪힌 면이 가려는 쪽을 마주 볼 때(법선과의 내적이 -0.25 아래)만 막혔다고 본다.
	constexpr float AnimalBlockedFacingDot = -0.25f;

	// 꺾어 볼 각도(도). 가려던 쪽에 가까운 것부터 본다 — 덜 꺾을수록 사람에게서 멀어지는 방향을 덜 버린다.
	constexpr float AnimalBlockedDetourAngles[] = { 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f };

	// 끼임을 푸는 동안 도주 한계 밖으로 나가도 되는 여유 = 60cm. 구석에서 빠져나갈 틈이 바깥뿐일 때가
	// 있어서 0 으로 두면 동물이 굳는다. 여기까지 나가면 바깥쪽 탈출을 그만두고 선 안으로 되돌아온다 —
	// 「끼임 풀이」가 「이탈」이 되면 안 된다 (DESIGN 3.1 한계 500cm).
	constexpr float AnimalBlockedEscapeMarginCm = 60.f;

	// 막혀서 다시 고를 때 **방금 고른 쪽 언저리는 뺀다**(±30도). 그 쪽이 막혀 있어서 다시 고르는 것이라
	// 같은 쪽을 또 고르면 한 자리에서 계속 고르기만 한다.
	constexpr float AnimalBlockedRetryAwayDegrees = 30.f;

	// 평소 배회 (사용자 2026-09-25 "동물들이 자율적으로 움직이게" · DESIGN 3.1 🟡 「동물 배회」).
	// 레퍼런스 원본처럼 **서 있다 → 몇 걸음 걷다 → 선다** 를 되풀이한다. 처음 선 자리에서 600cm 안만 돈다 —
	// 도주 한계(500cm)와 비슷한 크기라 사람이 찾아온 무리가 흩어지지 않는다. 걷기는 도주 속도의 절반.
	constexpr float AnimalWanderRadiusCm = 600.f;
	constexpr float AnimalWanderSpeedRatio = 0.5f;
	constexpr float AnimalWanderPauseMinSeconds = 2.f;
	constexpr float AnimalWanderPauseMaxSeconds = 6.f;
	constexpr float AnimalWanderWalkMinSeconds = 1.5f;
	constexpr float AnimalWanderWalkMaxSeconds = 4.f;
	// 물가(바다·낚시 포인트) 150cm 안으로는 걸어 들어가지 않는다 — 섬 가장자리 동물이 바다로 빠지지 않게.
	constexpr float AnimalWanderWaterProbeCm = 150.f;
	constexpr int32 AnimalWanderTries = 8;

	// 피격 파편 모양 (M21-02). **보이는 모양만 정하는 연출 튜닝값**이라 DESIGN 3.1 엔 문장으로만 적는다
	// (개수 6~10 은 표 → `DT_PlayerCombatTable` 에서 온다). 수명 0.35초 = 공격 간격 0.6초 안에 다 사라진다 —
	// 다음 타격의 파편과 겹쳐 쌓이지 않는다. 크기 4cm = 엔진 기본 상자(100cm)의 0.04배, 동물 몸(90cm)의 1/20.
	constexpr float HitDebrisLifeSeconds = 0.35f;
	constexpr float HitDebrisSizeScale = 0.04f;
	constexpr float HitDebrisMinSpeed = 150.f;
	constexpr float HitDebrisMaxSpeed = 300.f;
	constexpr float HitDebrisGravity = 980.f;
	// 털·먼지 색(마른 흙 갈색). 종별 털색은 행에 칸이 생기면 그때 옮긴다.
	const FLinearColor HitDebrisColor(0.45f, 0.35f, 0.25f);

	// 색 변종 겉모습 (M21-07a2, DESIGN 10.6 「변종 겉모습」) — 흰 깜빡임과 같은 머티리얼에 색·불투명도만 바꾼 한 겹.
	// 금빛 = 바르코 12종 털색(갈색·회색·흰색)과 겹치지 않는 색, 0.6 = 무늬는 비치되 금빛이 먼저 읽히는 정도
	// (깜빡임 0.8 보다 아래, 0.35 는 공작처럼 진한 몸에서 묻혔다 — 감독 PIE 10-06). 보이는 모양만 정하는 연출값이라 파편 색처럼 여기 둔다.
	const FLinearColor VariantLookColor(1.0f, 0.78f, 0.2f);
	constexpr float VariantLookOpacity = 0.6f;
	// 이야기 종(안개뿔) 겉모습 — 같은 겹에 안개 낀 연한 청록. 순록 갈색 털이 거의 가려지는 0.85 (금빛 변종 0.6 보다 진하게).
	// 순백(0.94·0.97·1.0)은 설원 바닥과 같은 밝기라 몸이 눈에 묻혔다(#1190 캡처) — 바닥보다 한 단계 짙은 청록으로 윤곽이 읽히게 한다.
	const FLinearColor StoryLookColor(0.3f, 0.72f, 0.9f);
	constexpr float StoryLookOpacity = 0.85f;
	// 검사 스위치 `-SnareForceVariant` — 호스트가 서는 동물을 번갈아 변종으로(변종·일반 나란히 캡처용, 명세 §6).
	int32 ForcedVariantSpawnCount = 0;

	// 도망칠 상대 = 가장 가까운 사람 몸. 때린 쪽이 누구인지는 상태 컴포넌트가 안 들고 있고
	// (`ApplyDamage(float)` 뿐이다), 4~8인 PVE 라 사람이 몇 안 된다 — 가장 가까운 사람에게서
	// 멀어지면 때린 사람에게서도 멀어진다. 못 찾으면 nullptr 이고 그러면 동물은 안 움직인다.
	AActor* FindNearestPlayerBody(const AActor& From)
	{
		const UWorld* World = From.GetWorld();
		if (World == nullptr)
		{
			return nullptr;
		}

		AActor* Nearest = nullptr;
		float NearestDistSq = TNumericLimits<float>::Max();
		for (TActorIterator<ASnarehavenCharacter> It(World); It; ++It)
		{
			const float DistSq = FVector::DistSquared(From.GetActorLocation(), It->GetActorLocation());
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = *It;
			}
		}
		return Nearest;
	}
}

AAnimalCharacter::AAnimalCharacter()
{
	// Fight 상태면 매 프레임 도망가야 한다 (M1-11b · DESIGN 3.1). 그 밖의 상태에서는
	// `GetFleeDirection` 이 영벡터를 돌려줘 틱이 아무 일도 안 한다.
	PrimaryActorTick.bCanEverTick = true;

	// ACharacter 기본 캡슐은 사람 크기(176cm)다 — 그대로 두면 돼지가 플레이어만 하다
	// (사용자 실사용 지적 2026-09-22). 반지름은 세로가 줄어든 비율만큼 같이 줄여 몸통 비례를 지킨다
	// — 배율을 숫자로 박지 않고 물려받은 값에서 뽑아, 고칠 숫자가 위의 90cm 하나로 남는다.
	{
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float InheritedHeight = Capsule->GetUnscaledCapsuleHalfHeight() * 2.f;
		const float Shrink = AnimalCapsuleHeight / InheritedHeight;
		Capsule->InitCapsuleSize(Capsule->GetUnscaledCapsuleRadius() * Shrink, AnimalCapsuleHeight * 0.5f);
	}

	// 도망갈 때 뒷걸음질치지 않게 가는 쪽을 보게 한다. 동물에겐 사람을 조종하는 컨트롤러가 없어서
	// 컨트롤러 회전으로는 아무 일도 안 일어난다 — 이동 방향이 곧 바라보는 방향이다.
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;

		// **컨트롤러 없는 동물도 움직이게 한다.** 엔진은 조종되지 않는 캐릭터의 이동을 통째로 건너뛴다
		// (`CharacterMovementComponent.cpp` 의 `bShouldPerformControlledCharMove`). 동물은 AI 컨트롤러를
		// 안 쓰고(AutoPossessAI 는 레벨에 미리 놓은 것만 가진다 — 위험구역 스포너가 세운 동물엔 없다)
		// C++ 이 직접 방향을 주므로, 컨트롤러 대신 이 플래그로 물리를 돌린다 (DESIGN 3.1 = AIController 안 만든다).
		Movement->bRunPhysicsWithNoController = true;
	}

	AnimalState = CreateDefaultSubobject<UAnimalStateComponent>(TEXT("AnimalState"));

	// 아트가 들어오기 전까지 화면에 보이는 몸 역할을 하는 엔진 기본 실린더
	// (ASnarehavenCharacter 와 같은 이유·같은 방식 — 캡슐은 렌더되지 않는다).
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(RootComponent);
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 레벨에 미리 놓는 동물이라 Static 으로 두면 "LIGHTING NEEDS TO BE REBUILT" 경고가 화면에 뜬다
	// (루프#26 캡처에서 실제로 떴다). 동물은 어차피 돌아다니므로 Movable 이 맞다.
	PlaceholderBody->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (PlaceholderMeshAsset.Succeeded())
	{
		PlaceholderBody->SetStaticMesh(PlaceholderMeshAsset.Object);
		// 엔진 기본 실린더는 지름·높이 100cm(중심이 원점) — 캡슐 치수에 맞추려면 그 절반으로 나눈다.
		const float EngineCylinderHalfExtent = 50.f;
		const UCapsuleComponent* Capsule = GetCapsuleComponent();
		const float RadiusScale = Capsule->GetUnscaledCapsuleRadius() / EngineCylinderHalfExtent;
		const float HeightScale = Capsule->GetUnscaledCapsuleHalfHeight() / EngineCylinderHalfExtent;
		PlaceholderBody->SetRelativeScale3D(FVector(RadiusScale, RadiusScale, HeightScale));
	}

	// 맞은 순간 (M21-02): 흰 깜빡임 머티리얼(`tools/unreal/create_hit_flash_material.py`)과 파편 모양.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HitFlashAsset(
		TEXT("/Game/Game/VFX/Hit/M_HitFlash.M_HitFlash"));
	HitFlashMaterial = HitFlashAsset.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HitDebrisMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	HitDebrisMesh = HitDebrisMeshAsset.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HitDebrisMaterialAsset(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	HitDebrisBaseMaterial = HitDebrisMaterialAsset.Object;

	// C++ 전용 프로젝트라 블루프린트 없이 기본 테이블을 주려면 생성자 경로 참조뿐이다
	// (ASnarehavenCharacter 의 입력 에셋과 같은 방식 — 하드 참조라 런타임 문자열 조회가 아니다).
	static ConstructorHelpers::FObjectFinder<UDataTable> AnimalTableAsset(
		TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	if (AnimalTableAsset.Succeeded())
	{
		AnimalTable = AnimalTableAsset.Object;
	}
}

void AAnimalCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// 상태 전이를 ABP 대신 C++ 이 한다 (M1-09). **몸을 올리는 자리에서 걸지 않는다** —
	// 거기 걸면 행에 메시가 없는 종(자리표시 실린더)은 포획해도 사라지지 않는다 (M1-10e2 에서 걸렸다).
	// 상태는 몸이 있든 없든 바뀌므로 컴포넌트가 준비된 이 자리가 맞다.
	if (IsValid(AnimalState) && !AnimalState->OnStateChanged.IsAlreadyBound(this, &AAnimalCharacter::HandleStateChanged))
	{
		AnimalState->OnStateChanged.AddDynamic(this, &AAnimalCharacter::HandleStateChanged);
	}
}

void AAnimalCharacter::BeginPlay()
{
	Super::BeginPlay();

	InitializeFromTable();
}

bool AAnimalCharacter::ShouldStandPlaceholderSpecies()
{
	// 명령줄 스위치 하나로 갈린다 — 게임 값(표·설계서)은 건드리지 않는다 (Verification.md 6).
	return SnareCheckSwitch::Param(TEXT("SnareStageAnimals"));
}

bool AAnimalCharacter::ShouldFreezeAnimals()
{
	return SnareCheckSwitch::Param(TEXT("SnareFreezeAnimals"));
}

bool AAnimalCharacter::ShouldHuntHerd()
{
	return SnareCheckSwitch::Param(TEXT("SnareHuntHerd"));
}

FVector AAnimalCharacter::HuntHerdSpot(const FVector& Origin, const FVector& Forward, int32 Index, float ReachCm)
{
	// 0번: 사거리의 70% 안쪽, 오른쪽 옆 70° (폰 몸에 가리지 않을 만큼 옆). 1~3번: 앞쪽 호 (각도, 거리cm) — 가로로 넓게
	// 벌리고 앞쪽은 450cm 안쪽에 둔다(위쪽 안내 띠·체력 띠에 안 걸리고 한 화면에 든다, 2868×1320 가로 화면 실측).
	static constexpr float DefaultReachCm = 200.f;
	static constexpr float NearReachRatio = 0.7f;
	static constexpr float NearMinCm = 140.f;
	static constexpr float NearAngleDeg = 70.f;
	static const FVector2D FarSpots[] = { FVector2D(-60.f, 420.f), FVector2D(-20.f, 360.f), FVector2D(30.f, 430.f) };

	const float Reach = ReachCm > 0.f ? ReachCm : DefaultReachCm;
	float AngleDeg = NearAngleDeg;
	float DistanceCm = FMath::Max(Reach * NearReachRatio, NearMinCm);
	if (Index > 0)
	{
		const FVector2D& Far = FarSpots[FMath::Clamp(Index - 1, 0, UE_ARRAY_COUNT(FarSpots) - 1)];
		AngleDeg = Far.X;
		DistanceCm = Far.Y;
	}
	FVector Heading = Forward.GetSafeNormal2D();
	if (Heading.IsNearlyZero())
	{
		Heading = FVector::ForwardVector;
	}
	return Origin + Heading.RotateAngleAxis(AngleDeg, FVector::UpVector) * DistanceCm;
}

bool AAnimalCharacter::IsPlaceholderSpeciesRow(const UDataTable* Table, FName RowName)
{
	if (!IsValid(Table))
	{
		return false;
	}

	const FAnimalRow* Row = Table->FindRow<FAnimalRow>(RowName, FString(), /*bWarnIfRowMissing=*/false);
	return Row != nullptr && Row->bPlaceholderSpecies;
}

void AAnimalCharacter::SetAnimalData(UDataTable* Table, FName RowName)
{
	AnimalTable = Table;
	AnimalRowName = RowName;
}

bool AAnimalCharacter::InitializeFromTable()
{
	if (!IsValid(AnimalTable))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 테이블이 없다 — 상태 머신을 켜지 않는다"), *GetName());
		return false;
	}

	// FindRow 는 못 찾으면 로그를 남긴다. 우리가 부르는 곳을 알 수 있게 문맥 문자열을 넘긴다.
	const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(AnimalRowName, TEXT("AAnimalCharacter::InitializeFromTable"));
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 행 %s 를 못 찾았다"), *GetName(), *AnimalRowName.ToString());
		return false;
	}

	// 보이는 몸부터 갈아 끼운다 — 상태 초기화가 실패해도 화면에는 진짜 동물이 서 있어야 한다.
	ApplyBodyFromRow(*Row);
	bStoryLook = Row->bStorySpecies;
	ApplyMoveSpeedFromRow(*Row);
	ApplyAttackFromRow(*Row);
	ApplySoundFromRow(*Row);
	CorpseLifeSeconds = Row->CorpseLifeSeconds;

	if (!IsValid(AnimalState))
	{
		return false;
	}

	if (!AnimalState->InitializeFromRow(*Row))
	{
		return false;
	}

	// M21-07 색 변종 — 호스트가 설 때 한 번 굴리고 결과만 복제한다(분산·위험구역·레벨 배치가 다 이 길).
	if (HasAuthority())
	{
		FRandomStream Stream(FMath::Rand());
		const bool bForced = SnareCheckSwitch::Param(TEXT("SnareForceVariant"))
			&& AnimalState->SetVariant(ForcedVariantSpawnCount++ % 2 == 0);
		if (bForced ? AnimalState->IsVariant() : AnimalState->RollVariantOnSpawn(Stream))
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Variant] %s: %s 변종이 섰다 — 포획하면 %s"),
				*GetName(), *AnimalRowName.ToString(), *AnimalState->GetPetItemID().ToString());
		}
	}
	return true;
}

bool AAnimalCharacter::ApplyBodyFromRow(const FAnimalRow& Row)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!IsValid(Body) || Row.MeshRef.IsNull())
	{
		// 행에 메시가 없으면 자리표시 실린더 그대로 (AGatherableActor 와 같은 규칙) —
		// 데이터 무결성 검사가 "참조가 비었다" 경고로 세 준다.
		return false;
	}

	USkeletalMesh* RowMesh = Row.MeshRef.LoadSynchronous();
	if (RowMesh == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 행의 메시를 못 불러왔다 — 자리표시를 그대로 둔다"), *GetName());
		return false;
	}

	Body->SetSkeletalMesh(RowMesh);

	// **몸 크기는 행이 정한다** (`MeshFitHeightCm`). 바르코 12종은 가져올 때 이미 DESIGN 8.7
	// "크기" 표의 배율로 구워져 있어 배율 1.0 그대로 붙인다 — 전에는 여기서 **캡슐 높이**에
	// 맞춰 굽는 바람에 캡슐이 전 종 공통 90cm 인 탓에 곰도 엘크도 뇌조도 전부 90cm 가 됐다
	// (감독 실측 결함 2026-09-23). 크기 표가 없는 자리표시 종만 그 칸으로 계속 굽는다.
	if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		const FBoxSphereBounds MeshBounds = RowMesh->GetBounds();
		const float MeshHeight = MeshBounds.BoxExtent.Z * 2.f;

		float UniformScale = 1.f;
		if (Row.MeshFitHeightCm > 0.f && MeshHeight > KINDA_SMALL_NUMBER)
		{
			UniformScale = Row.MeshFitHeightCm / MeshHeight;
		}
		Body->SetRelativeScale3D(FVector(UniformScale));

		// **발을 캡슐 바닥에 맞춘다.** 메시 원점이 꼭 발밑이라는 보장이 없어 바운드의 맨 아랫점을
		// 직접 잰다 — 원점이 발밑인 메시면 아랫점이 0 이라 예전 계산과 같은 자리에 선다.
		const float MeshBottomZ = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
		Body->SetRelativeLocation(FVector(0.f, 0.f,
			-Capsule->GetUnscaledCapsuleHalfHeight() - MeshBottomZ * UniformScale));

		// 3층은 캡처 픽셀로 크기를 못 잰다(상시 지시 2026-09-23 15:45 — 수치 판정은 로그로).
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 몸 크기 - 행 %s · 메시 높이 %.1fcm · 배율 %.2f · 선 높이 %.1fcm (캡슐 %.0fcm)"),
			*GetName(), *AnimalRowName.ToString(), MeshHeight, UniformScale, MeshHeight * UniformScale,
			Capsule->GetUnscaledCapsuleHalfHeight() * 2.f);

		// 임포트한 몸은 +Y 를 보고 서 있는데 액터의 앞은 +X 다 — 안 돌리면 동물이 옆으로 걷는다.
		// UE 마네킹이 쓰는 것과 같은 보정이다.
		Body->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	}

	// 애님 블루프린트가 아직 없다. 한 클립을 그냥 돌리는 SingleNode 모드면 ABP 없이도 Idle 이 돈다
	// — 빈 AnimGraph 짜리 ABP 를 붙이면 오히려 레퍼런스 포즈로 굳는다(감독 결정, DESIGN 3.1 의 (c)).
	IdleAnim = Row.IdleAnimRef.LoadSynchronous();
	DeathAnim = Row.DeathAnimRef.LoadSynchronous();

	// M5-07d — 걷기·도주·공격·피격도 같은 자리에서 읽는다. **비어 있는 칸은 nullptr 그대로 둔다** —
	// 없는 클립을 다른 것으로 채우면(예: 걷기가 없으면 도주로) 표를 안 채운 사실이 화면에서 안 보인다.
	// 어느 클립으로 떨어지는지는 `ChooseLocomotionAnim` 한 곳이 정한다.
	WalkAnim = Row.WalkAnimRef.LoadSynchronous();
	FleeAnim = Row.FleeAnimRef.LoadSynchronous();
	AttackAnim = Row.AttackAnimRef.LoadSynchronous();
	HitAnim = Row.HitAnimRef.LoadSynchronous();

	if (IdleAnim != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		CurrentBodyAnim = nullptr;
		bBodyMoving = false;
		UpdateLocomotionAnimation();
	}

	// 진짜 몸이 올라왔으니 자리표시를 내린다. 지우지 않고 숨기기만 — 메시가 없는 종은 아직 이게 몸이다.
	if (IsValid(PlaceholderBody))
	{
		PlaceholderBody->SetVisibility(false);
	}

	return true;
}

void AAnimalCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// **보이는 클립은 권한과 상관없다** — 게스트도 복제된 속도만큼 다리를 움직여야 한다 (M5-07d).
	// 판정이 아니라 그림이라 여기서 먼저 한다. 클립이 바뀔 때만 갈아 끼운다.
	BodyActionSecondsLeft = FMath::Max(BodyActionSecondsLeft - DeltaSeconds, 0.f);
	UpdateLocomotionAnimation();
	UpdateHitDebris(DeltaSeconds);
	UpdateVariantLook();

	// 움직이는 판정은 호스트만 한다 — 게스트는 액터 위치 복제로 같은 장면을 본다 (DESIGN 4.1 권위 표).
	// 게스트가 스스로 밀면 호스트가 보내 주는 위치와 싸워 동물이 떨린다.
	if (!IsValid(AnimalState) || !AnimalState->HasAnimalAuthority())
	{
		return;
	}

	// 검사 전용 — 동물을 선 자리에 세운다 (맞는 판정·상태 전이는 위쪽 호스트 길 그대로 아래에서 돈다).
	if (ShouldFreezeAnimals())
	{
		AnimalState->AdvanceEscape(DeltaSeconds);
		GetCharacterMovement()->StopMovementImmediately();
		return;
	}

	// 올가미 링 실패 도주 시간을 흘린다 — 다 차면 이 프레임부터 Capturable 로 선다 (M21-09b).
	AnimalState->AdvanceEscape(DeltaSeconds);

	// 맞고 밀리는 중이면 먼저 민다 — 도주 입력은 그대로 겹친다(같은 쪽이라 더 멀리 간다).
	UpdateKnockback(DeltaSeconds);

	// 선공 감지·추격 포기·한 대가 이동보다 먼저다 — 포기한 프레임에 한 걸음 더 쫓아가지 않게 (M5-07d2·d3).
	UpdateAttack(DeltaSeconds);

	// 경계 밖으로 나갔는지·어느 쪽으로 도는지를 먼저 정한다 — 아래의 방향과 속도가 둘 다 이 값을 쓴다.
	UpdateFleeOrbit();

	// 앞이 막혀 제자리에 굳었는지도 여기서 본다 — 방향을 고르기 **전**이어야 이번 프레임부터 우회한다.
	UpdateBlockedDetour(DeltaSeconds);

	// 도망갈 상황이 아니면 영벡터라 아무 일도 안 한다 — 조건은 `GetFleeDirection` 한 곳에만 있다.
	// 쫓는 갈래(반격·선공)는 도망 방향이 늘 영벡터이고 대신 `GetChaseDirection` 이 다가가는 쪽을 준다 —
	// 둘 중 하나만 0 이 아니라 더해도 섞이지 않는다.
	// 배회는 Idle 에서만 0 이 아니다 — 도주·추격과 겹치지 않는다. 크기가 1 보다 작아 그만큼 느리게 걷는다.
	const FVector FleeInput = GetFleeDirection();
	AddMovementInput(FleeInput + GetChaseDirection() + UpdateWander(DeltaSeconds));

	// **방향만 꺾으면 안 멈춘다.** 입력은 이미 실린 속도를 천천히만 돌린다(가속·마찰을 거친다) —
	// 2층 검사에서 경계에 닿고도 605cm → 2482cm 까지 밀려 나갔다. 선을 실제로 긋는 것은 아래다.
	ApplyLeashToVelocity(FleeInput, DeltaSeconds);

	// 3층은 한 장짜리 캡처라 "안 나갔다" 를 화면으로 못 보여 준다 — 한계에 닿은 순간을 한 줄 남긴다.
	// (`HandleStateChanged` 의 도주 시작 줄과 짝이다.)
	if (!bFleeLeashReported && AnimalState->GetState() == EAnimalState::Fight && IsBeyondFleeLeash())
	{
		bFleeLeashReported = true;
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 도주 한계 %.0fcm 에 닿았다 — 경계를 따라 돈다 (싸움 시작 자리 %s · 지금 %s)"),
			*GetName(), AnimalFleeLeashRadius, *FleeHome.ToCompactString(), *GetActorLocation().ToCompactString());
	}

}

float AAnimalCharacter::GetWanderRadiusCm()
{
	return AnimalWanderRadiusCm;
}

FVector AAnimalCharacter::UpdateWander(float DeltaSeconds)
{
	// 싸움·포획·죽음·선공 추격 중에는 배회하지 않는다. 다시 Idle 로 돌아오면 그 자리를 새 집으로 삼는다.
	if (MoveSpeed <= 0.f || bAggroed || !IsValid(AnimalState) || AnimalState->GetState() != EAnimalState::Idle)
	{
		bWanderHomeSet = false;
		WanderDirection = FVector::ZeroVector;
		return FVector::ZeroVector;
	}

	// 처음엔 선 채로 시작한다 — 막 스폰된 무리가 한꺼번에 같은 박자로 걷지 않게 쉬는 시간을 굴린다.
	if (!bWanderHomeSet)
	{
		bWanderHomeSet = true;
		WanderHome = GetActorLocation();
		WanderDirection = FVector::ZeroVector;
		WanderSecondsLeft = FMath::FRandRange(AnimalWanderPauseMinSeconds, AnimalWanderPauseMaxSeconds);
		return FVector::ZeroVector;
	}

	WanderSecondsLeft -= DeltaSeconds;
	if (WanderSecondsLeft > 0.f)
	{
		return WanderDirection * AnimalWanderSpeedRatio;
	}

	if (!WanderDirection.IsNearlyZero())
	{
		// 다 걸었다 → 선다(풀 뜯는 자리).
		WanderDirection = FVector::ZeroVector;
		WanderSecondsLeft = FMath::FRandRange(AnimalWanderPauseMinSeconds, AnimalWanderPauseMaxSeconds);
		return FVector::ZeroVector;
	}

	WanderSecondsLeft = FMath::FRandRange(AnimalWanderWalkMinSeconds, AnimalWanderWalkMaxSeconds);
	WanderDirection = ChooseWanderDirection(MoveSpeed * AnimalWanderSpeedRatio * WanderSecondsLeft);
	return WanderDirection * AnimalWanderSpeedRatio;
}

FVector AAnimalCharacter::ChooseWanderDirection(float StrideCm) const
{
	const FVector Here = GetActorLocation();
	for (int32 Try = 0; Try < AnimalWanderTries; ++Try)
	{
		const FVector Direction = FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f).Vector();
		const FVector Target = Here + Direction * StrideCm;
		if (FVector::DistSquared2D(Target, WanderHome) > FMath::Square(AnimalWanderRadiusCm)
			|| SnareWater::IsNearWater(this, Target, AnimalWanderWaterProbeCm)
			|| !IsDetourOpen(Direction))
		{
			continue;
		}
		return Direction;
	}

	// 열린 쪽을 못 찾았다 — 집에서 멀면 집 쪽으로, 아니면 이번엔 그냥 선다.
	FVector Home = WanderHome - Here;
	Home.Z = 0.f;
	return Home.SizeSquared() > FMath::Square(AnimalWanderRadiusCm * 0.5f) ? Home.GetSafeNormal() : FVector::ZeroVector;
}

void AAnimalCharacter::ApplyMoveSpeedFromRow(const FAnimalRow& Row)
{
	MoveSpeed = Row.MoveSpeed;

	// 미정(음수)이면 이동 컴포넌트를 안 건드린다 — 엔진 기본 600cm/s 로 달리게 두면
	// 행을 안 채웠다는 사실이 화면에서 안 보인다 (Verification.md 7).
	if (MoveSpeed <= 0.f)
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->MaxWalkSpeed = MoveSpeed;
	}
}

void AAnimalCharacter::ApplyAttackFromRow(const FAnimalRow& Row)
{
	BehaviorKind = Row.BehaviorKind;
	AttackDamage = Row.AttackDamage;
	AttackIntervalSec = Row.AttackIntervalSec;
	AttackRangeCm = Row.AttackRangeCm;
	AggroRadiusCm = Row.AggroRadiusCm;

	// 다시 초기화하면(스폰 재활용·검사) 지난 싸움의 흔적을 지운다.
	AttackCooldownSeconds = 0.f;
	BodyActionSecondsLeft = 0.f;
	bAggroed = false;
	bReturningHome = false;
	LandedAttackCount = 0;
}

void AAnimalCharacter::ApplySoundFromRow(const FAnimalRow& Row)
{
	HitSoundCue = Row.HitSoundCue;
	DeathSoundCue = Row.DeathSoundCue;
	AggroSoundCue = Row.AggroSoundCue;
	SoundPitch = Row.SoundPitch > 0.f ? Row.SoundPitch : 1.f;
	LastVoiceSeconds.Reset();
}

bool AAnimalCharacter::PlayVoice(FName Cue, float Volume)
{
	// DESIGN 8.7 「같은 동물 같은 소리 최소 간격」 0.6초 = 3.1 사람 공격 간격 — 여럿이 같이 때려도 0.6초에 한 번.
	constexpr double MinGapSeconds = 0.6;

	UWorld* const World = GetWorld();
	if (Cue.IsNone() || World == nullptr)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	if (const double* const Last = LastVoiceSeconds.Find(Cue); Last != nullptr && Now - *Last < MinGapSeconds)
	{
		return false;
	}
	LastVoiceSeconds.Add(Cue, Now);
	++VoiceRequestCounts.FindOrAdd(Cue);
	USnareSoundSubsystem::PlayAt(this, Cue, GetActorLocation(), Volume, SoundPitch);
	// 3층 실측 줄(M43-06) — 어느 동물이 내는지·가장 가까운 사람까지 거리. `-SnareActionLog` 일 때만.
	if (SnareSound::IsActionLogOn())
	{
		const AActor* const Nearest = FindNearestPlayerBody(*this);
		UE_LOG(LogSnarehaven, Log, TEXT("[실측] 동물 소리 %s · %s · 사람까지 %.0fcm"), *Cue.ToString(), *GetName(),
			Nearest != nullptr ? FVector::Dist(GetActorLocation(), Nearest->GetActorLocation()) : -1.f);
	}
	return true;
}

bool AAnimalCharacter::CanEverAttack() const
{
	// 도주 종·데미지 0 인 종(새 4종)·칸이 미정(-1)인 종은 치지 않는다 — 숫자를 지어내 채우지 않는다
	// (DESIGN 3.1 "새 4종 0" · Verification.md 7). 종 이름으로 가르는 `if` 는 없다 (DataDriven.md 3).
	return BehaviorKind != EAnimalBehaviorKind::Flee
		&& AttackDamage > 0.f && AttackIntervalSec > 0.f && AttackRangeCm > 0.f;
}

bool AAnimalCharacter::IsChaserKind() const
{
	return CanEverAttack()
		&& (BehaviorKind == EAnimalBehaviorKind::RetaliateWhenHit || BehaviorKind == EAnimalBehaviorKind::Aggressive);
}

bool AAnimalCharacter::WantsToAttackNow() const
{
	if (!CanEverAttack() || bReturningHome || !IsValid(AnimalState))
	{
		return false;
	}

	// Capturable·Dead·Captured 는 아래 어느 갈래에도 안 걸린다 — DESIGN 3.1 "Capturable 동안 공격 안 한다".
	const EAnimalState State = AnimalState->GetState();
	switch (BehaviorKind)
	{
	case EAnimalBehaviorKind::CorneredCounter:
		// 도망가다 도주 한계에 닿은 뒤에만 돌아선다 (8.7 "도주, 궁지에서 반격").
		return State == EAnimalState::Fight && IsOrbitingLeash();
	case EAnimalBehaviorKind::RetaliateWhenHit:
		return State == EAnimalState::Fight;
	case EAnimalBehaviorKind::Aggressive:
		return State == EAnimalState::Fight || (State == EAnimalState::Idle && bAggroed);
	default:
		return false;
	}
}

bool AAnimalCharacter::TryAttackOnAuthority(ASnarehavenCharacter* Target)
{
	// 데미지 판정은 호스트 몫이다 (DESIGN 4.1 권위 표) — 게스트 화면의 동물이 치면 두 번 깎인다.
	if (!IsValid(AnimalState) || !AnimalState->HasAnimalAuthority())
	{
		return false;
	}

	if (!IsValid(Target) || Target->IsDowned() || !WantsToAttackNow())
	{
		return false;
	}

	// 사거리는 캡슐 중심 거리 (DESIGN 3.1 — 플레이어 근접 사거리와 같은 잣대). 높이는 뺀다.
	const float Distance = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
	if (Distance > AttackRangeCm)
	{
		return false;
	}

	// 돌아서서 친다 — 도망가던 쪽을 본 채로 치면 뒷발질처럼 보인다.
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	if (!ToTarget.IsNearlyZero())
	{
		SetActorRotation(ToTarget.Rotation());
	}

	// 체력은 맞은 사람 기기의 `UPlayerVitalsComponent` 가 깎는다 — 호스트 자기 폰이면 그 자리에서 실행된다.
	Target->Client_ReceiveAnimalHit(AttackDamage);
	// 맞은 사람의 신음은 모든 기기가 그 사람 자리에서 듣는다 (M44-02 · DESIGN 2.2) — 같이 사냥하는 친구도 화면 밖에서 안다.
	Target->Multicast_PlayHurtVoice();
	++LandedAttackCount;
	Multicast_PlayAttackAnim();

	UE_LOG(LogSnarehaven, Log, TEXT("%s: 동물 공격 - %s 를 쳤다 (데미지 %.1f · 거리 %.0fcm / 사거리 %.0fcm · 간격 %.1f초 · %d번째)"),
		*GetName(), *Target->GetName(), AttackDamage, Distance, AttackRangeCm, AttackIntervalSec, LandedAttackCount);
	return true;
}

void AAnimalCharacter::Multicast_PlayAttackAnim_Implementation()
{
	// 공격 동작마다 덤빔 소리 (M43-04 · DESIGN 8.7 크기 1.0) — 클립이 없는 몸이어도 소리는 낸다.
	// 간격은 동물 `AttackIntervalSec` 이 이미 묶고, 선공으로 막 잡은 울음과 0.6초 안이면 `PlayVoice` 가 하나로 묶는다.
	PlayVoice(AggroSoundCue, 1.f);

	USkeletalMeshComponent* const Body = GetMesh();
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr || AttackAnim == nullptr)
	{
		return;
	}

	// 되풀이 없이 한 번. 도는 동안은 이동 클립이 덮지 않는다(`UpdateLocomotionAnimation`) — 길이는 클립에서 읽는다.
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->PlayAnimation(AttackAnim, false);
	CurrentBodyAnim = AttackAnim;
	CurrentBodyPlayRate = 1.f;   // `PlayAnimation` 은 빠르기를 1 로 되돌린다
	BodyActionSecondsLeft = AttackAnim->GetPlayLength();
}

void AAnimalCharacter::Multicast_PlayAggroVoice_Implementation()
{
	PlayVoice(AggroSoundCue, 1.f);
}

void AAnimalCharacter::Multicast_PlayHitReaction_Implementation(float FlashSeconds, uint8 DebrisCount)
{
	const bool bFinished = !IsValid(AnimalState) || AnimalState->IsFinished();

	// ① 피격 클립 — 공격 클립과 같은 자리(한 번, 도는 동안 이동 클립이 안 덮는다). 죽었으면 죽는 자세가 이긴다.
	USkeletalMeshComponent* const Body = GetMesh();
	if (!bFinished && HitAnim != nullptr && IsValid(Body) && Body->GetSkeletalMeshAsset() != nullptr)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->PlayAnimation(HitAnim, false);
		CurrentBodyAnim = HitAnim;
		CurrentBodyPlayRate = 1.f;   // `PlayAnimation` 은 빠르기를 1 로 되돌린다
		BodyActionSecondsLeft = HitAnim->GetPlayLength();
	}

	// ①-2 맞은 울음 — 크기 0.8(같은 순간 타격 소리 1.0 아래, DESIGN 8.7). 막타면 죽음 소리가 대신 운다.
	if (!bFinished)
	{
		constexpr float HitVoiceVolume = 0.8f;
		PlayVoice(HitSoundCue, HitVoiceVolume);
	}

	// ② 흰 깜빡임 — 몸 위에 한 겹 더 그린다. 연달아 맞으면 시간만 새로 건다.
	UWorld* const World = GetWorld();
	UMeshComponent* const FlashBody = GetFlashBody();
	if (FlashSeconds > 0.f && HitFlashMaterial != nullptr && FlashBody != nullptr && World != nullptr)
	{
		FlashBody->SetOverlayMaterial(HitFlashMaterial);
		World->GetTimerManager().SetTimer(HitFlashTimerHandle, this, &AAnimalCharacter::ClearHitFlash, FlashSeconds, false);
	}

	// ③ 털·먼지 파편 — 인스턴스 메시 하나에 DebrisCount 개. 몸 가운데에서 위·바깥으로 튄다.
	if (DebrisCount > 0 && HitDebrisMesh != nullptr)
	{
		if (!IsValid(HitDebris))
		{
			HitDebris = NewObject<UInstancedStaticMeshComponent>(this, TEXT("HitDebris"));
			HitDebris->SetStaticMesh(HitDebrisMesh);
			HitDebris->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HitDebris->SetCastShadow(false);
			HitDebris->SetMobility(EComponentMobility::Movable);
			// 동물이 밀려도 파편은 맞은 자리에 남는다 — 월드 기준으로 둔다.
			HitDebris->SetUsingAbsoluteLocation(true);
			HitDebris->SetUsingAbsoluteRotation(true);
			HitDebris->SetUsingAbsoluteScale(true);
			if (HitDebrisBaseMaterial != nullptr)
			{
				UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(HitDebrisBaseMaterial, this);
				Mid->SetVectorParameterValue(TEXT("Color"), HitDebrisColor);
				HitDebris->SetMaterial(0, Mid);
			}
			HitDebris->SetupAttachment(RootComponent);
			HitDebris->RegisterComponent();
		}

		HitDebris->ClearInstances();
		HitDebris->SetWorldLocationAndRotation(GetActorLocation(), FRotator::ZeroRotator);
		HitDebrisVelocities.Reset(DebrisCount);
		for (int32 Index = 0; Index < DebrisCount; ++Index)
		{
			// 위쪽 반구로만 튄다 — 아래로 튀면 바닥에 바로 묻혀 안 보인다.
			FVector Dir = FMath::VRand();
			Dir.Z = FMath::Abs(Dir.Z);
			HitDebrisVelocities.Add(Dir * FMath::FRandRange(HitDebrisMinSpeed, HitDebrisMaxSpeed));
			HitDebris->AddInstance(FTransform(FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f),
				FVector::ZeroVector, FVector(HitDebrisSizeScale)));
		}
		HitDebrisSecondsLeft = HitDebrisLifeSeconds;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("%s: 피격 반응 - 클립 %s · 흰 깜빡임 %.2f초 · 파편 %d개"),
		*GetName(), CurrentBodyAnim == HitAnim && HitAnim != nullptr ? *HitAnim->GetName() : TEXT("(안 틂)"),
		FlashSeconds, static_cast<int32>(DebrisCount));
}

UMeshComponent* AAnimalCharacter::GetFlashBody() const
{
	USkeletalMeshComponent* const Body = GetMesh();
	if (IsValid(Body) && Body->GetSkeletalMeshAsset() != nullptr)
	{
		return Body;
	}
	return PlaceholderBody;
}

bool AAnimalCharacter::IsHitFlashActive() const
{
	const UMeshComponent* const FlashBody = GetFlashBody();
	return FlashBody != nullptr && HitFlashMaterial != nullptr && FlashBody->GetOverlayMaterial() == HitFlashMaterial;
}

void AAnimalCharacter::ClearHitFlash()
{
	// 변종이면 맨몸이 아니라 변종 색 겹으로 돌아간다.
	if (UMeshComponent* const FlashBody = GetFlashBody())
	{
		FlashBody->SetOverlayMaterial(GetRestingOverlay());
	}
}

UMaterialInterface* AAnimalCharacter::GetRestingOverlay()
{
	if (HitFlashMaterial != nullptr && bStoryLook)
	{
		if (StoryLookMaterial == nullptr)
		{
			StoryLookMaterial = UMaterialInstanceDynamic::Create(HitFlashMaterial, this);
			StoryLookMaterial->SetVectorParameterValue(TEXT("FlashColor"), StoryLookColor);
			StoryLookMaterial->SetScalarParameterValue(TEXT("FlashOpacity"), StoryLookOpacity);
		}
		return StoryLookMaterial;
	}

	if (!IsValid(AnimalState) || !AnimalState->IsVariant() || HitFlashMaterial == nullptr)
	{
		return nullptr;
	}

	if (VariantLookMaterial == nullptr)
	{
		VariantLookMaterial = UMaterialInstanceDynamic::Create(HitFlashMaterial, this);
		VariantLookMaterial->SetVectorParameterValue(TEXT("FlashColor"), VariantLookColor);
		VariantLookMaterial->SetScalarParameterValue(TEXT("FlashOpacity"), VariantLookOpacity);
	}
	return VariantLookMaterial;
}

void AAnimalCharacter::UpdateVariantLook()
{
	UMeshComponent* const Body = GetFlashBody();
	if (Body == nullptr || IsHitFlashActive())
	{
		return;
	}

	// 게스트는 `bVariant` 가 복제로 늦게 온다 — 매 틱 포인터만 비교하고 다를 때만 건다.
	UMaterialInterface* const Wanted = GetRestingOverlay();
	if (Body->GetOverlayMaterial() != Wanted)
	{
		Body->SetOverlayMaterial(Wanted);
	}
}

bool AAnimalCharacter::IsStoryLookActive() const
{
	const UMeshComponent* const Body = GetFlashBody();
	return Body != nullptr && StoryLookMaterial != nullptr && Body->GetOverlayMaterial() == StoryLookMaterial;
}

bool AAnimalCharacter::IsVariantLookActive() const
{
	const UMeshComponent* const Body = GetFlashBody();
	return Body != nullptr && VariantLookMaterial != nullptr && Body->GetOverlayMaterial() == VariantLookMaterial;
}

int32 AAnimalCharacter::GetHitDebrisCount() const
{
	return IsValid(HitDebris) ? HitDebris->GetInstanceCount() : 0;
}

void AAnimalCharacter::UpdateHitDebris(float DeltaSeconds)
{
	if (HitDebrisSecondsLeft <= 0.f || !IsValid(HitDebris))
	{
		return;
	}

	HitDebrisSecondsLeft -= DeltaSeconds;
	if (HitDebrisSecondsLeft <= 0.f)
	{
		HitDebris->ClearInstances();
		return;
	}

	// 끝으로 갈수록 작아져 툭 사라지지 않는다.
	const float Shrink = HitDebrisSecondsLeft / HitDebrisLifeSeconds;
	const int32 Count = FMath::Min(HitDebris->GetInstanceCount(), HitDebrisVelocities.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FTransform Instance;
		HitDebris->GetInstanceTransform(Index, Instance, false);
		HitDebrisVelocities[Index].Z -= HitDebrisGravity * DeltaSeconds;
		Instance.AddToTranslation(HitDebrisVelocities[Index] * DeltaSeconds);
		Instance.SetScale3D(FVector(HitDebrisSizeScale * Shrink));
		HitDebris->UpdateInstanceTransform(Index, Instance, false, Index == Count - 1);
	}
}

bool AAnimalCharacter::StartKnockback(const FVector& Offset, float Seconds)
{
	// Capturable·죽음은 즉시 정지(DESIGN 3.1) — 밀면 포획 거리 150cm 밖으로 나갈 수 있다.
	if (Seconds <= 0.f || !IsValid(AnimalState) || !AnimalState->HasAnimalAuthority()
		|| AnimalState->GetState() != EAnimalState::Fight)
	{
		return false;
	}

	KnockbackVelocity = FVector(Offset.X, Offset.Y, 0.f) / Seconds;
	KnockbackSecondsLeft = Seconds;
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 넉백 시작 - %.0fcm 를 %.2f초 (자리 %s)"),
		*GetName(), Offset.Size2D(), Seconds, *GetActorLocation().ToCompactString());
	return true;
}

void AAnimalCharacter::UpdateKnockback(float DeltaSeconds)
{
	if (KnockbackSecondsLeft <= 0.f)
	{
		return;
	}

	// 싸움이 끝나면(이번 타로 Capturable 이 됐으면) 그 자리에 선다.
	if (!IsValid(AnimalState) || AnimalState->GetState() != EAnimalState::Fight)
	{
		KnockbackSecondsLeft = 0.f;
		return;
	}

	// 시간으로 민다 — 마찰·가속을 거치는 발사(`LaunchCharacter`)는 밀린 거리가 바닥 상태에 따라 달라진다.
	// 쓸어서 옮기므로 벽·바위를 뚫지 않는다. 막히면 거기서 멈춘다.
	const float Step = FMath::Min(DeltaSeconds, KnockbackSecondsLeft);
	KnockbackSecondsLeft -= Step;
	FHitResult Hit;
	AddActorWorldOffset(KnockbackVelocity * Step, true, &Hit);
	if (Hit.bBlockingHit)
	{
		KnockbackSecondsLeft = 0.f;
	}
	if (KnockbackSecondsLeft <= 0.f)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 넉백 끝 (자리 %s)"), *GetName(), *GetActorLocation().ToCompactString());
	}
}

FVector AAnimalCharacter::GetChaseDirection() const
{
	if (MoveSpeed <= 0.f || !CanEverAttack())
	{
		return FVector::ZeroVector;
	}

	// 추격을 포기했으면 싸움 시작 자리로 걸어 돌아간다 (DESIGN 3.1 "추격 포기"). 도착은 `UpdateAttack` 이 본다.
	if (bReturningHome)
	{
		FVector ToHome = FleeHome - GetActorLocation();
		ToHome.Z = 0.f;
		return ToHome.GetSafeNormal();
	}

	if (!IsChaserKind() || !WantsToAttackNow())
	{
		return FVector::ZeroVector;
	}

	const AActor* const Target = FleeFrom.Get();
	if (Target == nullptr)
	{
		return FVector::ZeroVector;
	}

	// 사거리 안이면 선다 — 더 파고들면 사람 몸을 밀어낸다. 접근 속도는 도주 속도(`MoveSpeed`)를 빌린다 (DESIGN 3.1).
	FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	ToTarget.Z = 0.f;
	if (ToTarget.SizeSquared() <= FMath::Square(AttackRangeCm))
	{
		return FVector::ZeroVector;
	}
	return ToTarget.GetSafeNormal();
}

void AAnimalCharacter::UpdateAttack(float DeltaSeconds)
{
	AttackCooldownSeconds = FMath::Max(AttackCooldownSeconds - DeltaSeconds, 0.f);
	if (!CanEverAttack() || !IsValid(AnimalState))
	{
		return;
	}

	const EAnimalState State = AnimalState->GetState();

	// 돌아가는 중에는 새로 쫓지도 치지도 않는다. 캡슐 반지름 안에 들면 도착이다.
	if (bReturningHome)
	{
		const UCapsuleComponent* const Capsule = GetCapsuleComponent();
		const float ArriveRadius = IsValid(Capsule) ? Capsule->GetScaledCapsuleRadius() : 0.f;
		if (FVector::DistSquared2D(GetActorLocation(), FleeHome) <= FMath::Square(ArriveRadius))
		{
			bReturningHome = false;
			if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
			{
				Movement->StopMovementImmediately();
			}
			UE_LOG(LogSnarehaven, Log, TEXT("%s: 싸움 시작 자리로 돌아왔다 - Idle (자리 %s · 체력 %.1f)"),
				*GetName(), *GetActorLocation().ToCompactString(), AnimalState->GetCurrentHP());
		}
		return;
	}

	// 선공: 반경 안에 사람이 들면 먼저 다가간다 (DESIGN 3.1 "선공 감지 반경").
	// ponytail: 선공 종마다 틱마다 사람 목록을 훑는다 — 사람은 4~8명뿐이라 싸다. 동물이 수백이면 타이머로 성기게.
	if (BehaviorKind == EAnimalBehaviorKind::Aggressive && State == EAnimalState::Idle && !bAggroed && AggroRadiusCm > 0.f)
	{
		AActor* const Nearest = FindNearestPlayerBody(*this);
		if (Nearest != nullptr
			&& FVector::DistSquared2D(GetActorLocation(), Nearest->GetActorLocation()) <= FMath::Square(AggroRadiusCm))
		{
			bAggroed = true;
			FleeFrom = Nearest;
			FleeHome = GetActorLocation();
			// 덤빔 소리 (M43-04 · DESIGN 8.7) — 대상을 처음 잡는 이 순간 한 번. 대상 유지 중엔 `bAggroed` 가 막는다.
			Multicast_PlayAggroVoice();
			UE_LOG(LogSnarehaven, Log, TEXT("%s: 선공 - %s 가 반경 %.0fcm 안(%.0fcm)이라 다가간다 (내 자리 %s · 속도 %.0fcm/s)"),
				*GetName(), *Nearest->GetName(), AggroRadiusCm,
				FVector::Dist2D(GetActorLocation(), Nearest->GetActorLocation()),
				*FleeHome.ToCompactString(), MoveSpeed);
		}
	}

	// 추격 포기: 싸움 시작 자리에서 도주 한계만큼 나가면 그만두고 돌아간다 — 쫓는 쪽도 같은 끈이다 (DESIGN 3.1).
	if (IsChaserKind() && (bAggroed || State == EAnimalState::Fight) && IsBeyondFleeLeash())
	{
		bAggroed = false;
		bReturningHome = true;
		AnimalState->ReturnToIdle();
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 추격 포기 - 시작 자리에서 %.0fcm (한계 %.0fcm), 돌아간다 (체력 %.1f 그대로)"),
			*GetName(), FVector::Dist2D(GetActorLocation(), FleeHome), AnimalFleeLeashRadius, AnimalState->GetCurrentHP());
		return;
	}

	if (AttackCooldownSeconds > 0.f)
	{
		return;
	}

	if (TryAttackOnAuthority(Cast<ASnarehavenCharacter>(FleeFrom.Get())))
	{
		AttackCooldownSeconds = AttackIntervalSec;
	}
}

FVector AAnimalCharacter::GetFleeDirection() const
{
	// 앞이 막힌 채로 굳었으면 잠시 옆으로 돈다 (`UpdateBlockedDetour`). 막은 것 쪽으로 계속 밀면
	// 동물은 안 나아가고, 등 뒤에서 미는 사람까지 같이 선다 (감독 PIE 결함 2026-09-24: 3초 동반 정지).
	if (BlockedDetourSeconds > 0.f && !BlockedDetourDirection.IsNearlyZero())
	{
		return BlockedDetourDirection;
	}

	return GetBaseFleeDirection();
}

FVector AAnimalCharacter::GetBaseFleeDirection() const
{
	// 도망은 Fight 동안과 링 실패 도주(Escaping) 동안뿐이다 — Capturable(HP 25% 이하)부터는 그 자리에 선다
	// (DESIGN 3.1: 포획 직전에 도망가면 좌절 요소가 된다). Dead·Captured 도 당연히 안 움직인다.
	const EAnimalState State = IsValid(AnimalState) ? AnimalState->GetState() : EAnimalState::Idle;
	if (MoveSpeed <= 0.f || (State != EAnimalState::Fight && State != EAnimalState::Escaping))
	{
		return FVector::ZeroVector;
	}

	// 쫓는 갈래(맞으면 반격·선공)는 Fight 에서 도망가지 않는다 — 다가가는 쪽은 `GetChaseDirection` 이 준다.
	// 링 실패 도주는 종과 상관없이 달아난다(반격 없음 — DESIGN 3.1 「올가미 링 실패 도주」).
	if (State == EAnimalState::Fight && IsChaserKind())
	{
		return FVector::ZeroVector;
	}

	const AActor* Threat = FleeFrom.Get();
	if (Threat == nullptr)
	{
		return FVector::ZeroVector;
	}

	// 궁지 반격: 경계에 몰린 뒤 사람이 사거리 안이면 서서 친다 — 돌면서 치면 한 대도 안 닿는다 (8.7).
	if (WantsToAttackNow() && FVector::DistSquared2D(GetActorLocation(), Threat->GetActorLocation())
		<= FMath::Square(AttackRangeCm))
	{
		return FVector::ZeroVector;
	}

	// 위아래로는 도망가지 않는다 — 땅을 밟은 채 멀어져야 한다.
	FVector Away = GetActorLocation() - Threat->GetActorLocation();
	Away.Z = 0.f;
	if (Away.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	Away = Away.GetSafeNormal();

	// 여기까지가 「사람 반대쪽으로」다. 그대로 두면 **직선으로 무한히** 밀려 맵 밖으로 나간다
	// (사용자 실사용 결함 2026-09-22 ① — 3타 전에 놓친다). DESIGN 3.1 「동물 도주 이탈 한계」 밖에서는
	// **바깥으로 나가는 성분만 깎는다** — 멈추지도(도망을 없애면 조각상으로 되돌아간다) 되돌아오지도
	// 않고 경계를 따라 돈다. 네브메시를 깔지 않는 이유는 DESIGN 3.1 이 AIController 를 안 만들기로 했기 때문.
	if (!IsOrbitingLeash())
	{
		return Away;
	}

	FVector Outward = GetActorLocation() - FleeHome;
	Outward.Z = 0.f;
	const FVector OutDirection = Outward.GetSafeNormal();
	if (FVector::DotProduct(Away, OutDirection) <= 0.f)
	{
		// 사람이 바깥쪽에 섰다 — 도망 방향이 이미 안쪽이라 경계가 막을 것이 없다.
		return Away;
	}

	// **경계 밖에서는 접선 한 방향으로 계속 돈다.** 예전에는 「사람 반대쪽」에서 바깥 성분만 깎아 썼는데,
	// 그 방향은 경계 위에서 **사람과 가장 먼 점으로 늘 되돌아오게** 작용한다 — 동물이 그 점 언저리에서
	// 좌우로 왔다갔다 하며 속도가 0 근처까지 죽었고, 대기 ↔ 달리기 클립이 3초마다 갈렸다
	// (감독 PIE 실측 2026-09-24: 32초에 전환 10번 · 속도 1.9 ↔ 15~21cm/s).
	// 도는 쪽은 경계에 닿은 순간 한 번만 정하고 안으로 돌아올 때까지 유지한다 (DESIGN 3.1).
	return GetLeashTangent() * static_cast<float>(FleeOrbitSign != 0 ? FleeOrbitSign : ChooseFleeOrbitSign());
}

FVector AAnimalCharacter::GetLeashTangent() const
{
	// 경계 원의 접선(반시계). 높이는 빼고 잰다 — `IsBeyondFleeLeash` 와 같은 평면이어야 한다.
	FVector Outward = GetActorLocation() - FleeHome;
	Outward.Z = 0.f;
	const FVector OutDirection = Outward.GetSafeNormal();
	return FVector(-OutDirection.Y, OutDirection.X, 0.f);
}

int8 AAnimalCharacter::ChooseFleeOrbitSign() const
{
	// 돌던 쪽을 이어 간다 — 접선은 두 방향이라 아무 쪽이나 고르면 이미 실린 속도와 맞서 제자리에 선다.
	// 경계에 닿는 순간에는 늘 도주 속도가 실려 있으므로 그 접선 성분의 부호가 "돌던 쪽" 이다.
	return FVector::DotProduct(GetVelocity(), GetLeashTangent()) < 0.f ? -1 : 1;
}

void AAnimalCharacter::UpdateFleeOrbit()
{
	// **확실히** 안으로 돌아왔을 때만 지운다 — 다음에 다시 나갈 때 그때의 속도로 새로 고른다.
	// 선을 한계 그 자체로 잡으면 경계에 붙어 도는 동안 매 걸음 지워졌다 다시 정해진다.
	const float ReleaseRadius = AnimalFleeLeashRadius * AnimalFleeOrbitReleaseRatio;
	if (FVector::DistSquared2D(GetActorLocation(), FleeHome) < FMath::Square(ReleaseRadius))
	{
		FleeOrbitSign = 0;
		return;
	}

	if (FleeOrbitSign == 0 && IsBeyondFleeLeash())
	{
		FleeOrbitSign = ChooseFleeOrbitSign();
	}
}

void AAnimalCharacter::UpdateBlockedDetour(float DeltaSeconds)
{
	// 도망가는 중이 아니면 막힐 일도 없다 — 재던 값만 비우고 자리를 새로 찍는다.
	const FVector Base = GetBaseFleeDirection();
	if (DeltaSeconds <= 0.f || MoveSpeed <= 0.f || Base.IsNearlyZero())
	{
		BlockedSeconds = 0.f;
		FreeSeconds = 0.f;
		BlockedDetourSeconds = 0.f;
		BlockedDetourDirection = FVector::ZeroVector;
		LastTickLocation = GetActorLocation();
		return;
	}

	// **속도가 아니라 실제로 간 거리로 잰다.** 속도는 우리가 매 프레임 도주 속도로 되돌리므로
	// (`ApplyLeashToVelocity`) 막혀 있어도 커 보인다 — 몸이 정말 나아갔는지는 자리만 안다.
	const float Moved = FVector::Dist2D(GetActorLocation(), LastTickLocation);
	LastTickLocation = GetActorLocation();

	BlockedDetourSeconds = FMath::Max(BlockedDetourSeconds - DeltaSeconds, 0.f);

	// 바깥으로 빠져나가던 중에 여유까지 왔으면 거기서 그만둔다 — 그 뒤는 선이 다시 끌어당긴다.
	if (BlockedDetourSeconds > 0.f && FVector::DistSquared2D(GetActorLocation(), FleeHome)
		>= FMath::Square(AnimalFleeLeashRadius + AnimalBlockedEscapeMarginCm))
	{
		BlockedDetourSeconds = 0.f;
		BlockedDetourDirection = FVector::ZeroVector;
	}

	// **우회 중에도 계속 잰다.** 고른 쪽까지 막혀 있으면 1초를 기다리지 않고 바로 다른 쪽으로 바꾼다 —
	// 기다리는 그 1초가 사람 눈에는 "둘이 붙어서 멈췄다" 로 보인다.
	if (Moved >= MoveSpeed * DeltaSeconds * AnimalBlockedMoveRatio)
	{
		BlockedSeconds = 0.f;
		FreeSeconds += DeltaSeconds;

		// **빠져나왔으면 1초를 채우지 않고 바로 끝낸다.** 우회는 도망을 잠시 버리는 것이라
		// 길게 끌수록 사람 쪽으로 돌아가거나 한계 밖으로 더 나간다.
		if (BlockedDetourSeconds > 0.f && FreeSeconds >= AnimalBlockedGraceSeconds)
		{
			BlockedDetourSeconds = 0.f;
			BlockedDetourDirection = FVector::ZeroVector;
		}
		return;
	}

	FreeSeconds = 0.f;
	BlockedSeconds += DeltaSeconds;
	if (BlockedSeconds < AnimalBlockedGraceSeconds)
	{
		return;
	}

	BlockedSeconds = 0.f;
	const FVector Detour = ChooseBlockedDetourDirection(Base);
	BlockedDetourDirection = Detour;
	BlockedDetourSeconds = Detour.IsNearlyZero() ? 0.f : AnimalBlockedDetourSeconds;

	// **되돌아가는 쪽으로 빠져나가면 도는 쪽도 뒤집는다.** 안 그러면 우회가 끝나는 순간 다시
	// 막힌 쪽으로 돌아가 같은 자리에서 맴돈다 — 사람이 앞을 가로막았을 때가 정확히 그 모양이다.
	// 도는 쪽을 래치해 둔 이유(클립 깜빡임, 루프#173)는 지켜진다: 끼였을 때만 뒤집는다.
	if (FleeOrbitSign != 0 && !Detour.IsNearlyZero()
		&& FVector::DotProduct(Detour, GetLeashTangent() * static_cast<float>(FleeOrbitSign)) < 0.f)
	{
		FleeOrbitSign = -FleeOrbitSign;
	}
}

FVector AAnimalCharacter::ChooseBlockedDetourDirection(const FVector& Base) const
{
	const UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (World == nullptr || !IsValid(Capsule))
	{
		return FVector::ZeroVector;
	}

	FVector Outward = GetActorLocation() - FleeHome;
	Outward.Z = 0.f;
	const FVector OutDirection = Outward.GetSafeNormal();

	// 한계 + 여유까지 나와 있으면 바깥쪽은 아예 후보가 아니다 — 끼임 풀이가 이탈이 되면 안 된다.
	const bool bKeepInside = IsOrbitingLeash()
		&& Outward.SizeSquared() >= FMath::Square(AnimalFleeLeashRadius + AnimalBlockedEscapeMarginCm);
	const bool bOrbiting = IsOrbitingLeash();

	// 두 번 훑는다. 먼저 **도주 한계 안쪽으로 가는 쪽**만 보고, 그런 쪽이 다 막혔을 때만 바깥쪽도 본다 —
	// 벽과 사람과 경계가 만든 구석에서는 빠져나갈 틈이 바깥밖에 없을 수 있다. 그 1초 동안만 선을 놓는다
	// (`ApplyLeashToVelocity` 가 우회 중에는 안 끌어당긴다). 선을 지키려다 굳는 것이 더 나쁘다.
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const float Angle : AnimalBlockedDetourAngles)
		{
			const FVector Candidate = Base.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal2D();
			if (Candidate.IsNearlyZero())
			{
				continue;
			}
			if ((Pass == 0 || bKeepInside) && bOrbiting && FVector::DotProduct(Candidate, OutDirection) > 0.f)
			{
				continue;
			}

			// 방금 고른 쪽(그리고 그 언저리)은 뺀다 — 막혀서 다시 고르는 것이라 또 고르면 제자리다.
			if (BlockedDetourSeconds > 0.f && !BlockedDetourDirection.IsNearlyZero()
				&& FVector::DotProduct(Candidate, BlockedDetourDirection)
					> FMath::Cos(FMath::DegreesToRadians(AnimalBlockedRetryAwayDegrees)))
			{
				continue;
			}
			if (IsDetourOpen(Candidate))
			{
				return Candidate;
			}
		}
	}

	return FVector::ZeroVector;
}

bool AAnimalCharacter::IsDetourOpen(const FVector& Direction) const
{
	const UWorld* World = GetWorld();
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (World == nullptr || !IsValid(Capsule))
	{
		return false;
	}

	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FCollisionShape Probe = FCollisionShape::MakeCapsule(
		FMath::Max(Radius - AnimalBlockedProbeShrink, 1.f),
		FMath::Max(Capsule->GetScaledCapsuleHalfHeight() - AnimalBlockedProbeShrink, 1.f));

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AnimalBlockedDetour), false, this);
	FHitResult Hit;
	const FVector Start = GetActorLocation();
	const FVector End = Start + Direction * (Radius * AnimalBlockedProbeRadii);
	if (!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
		Capsule->GetCollisionObjectType(), Probe, Params))
	{
		return true;
	}

	// 스친 것과 막은 것을 가른다 — 맞닿아 있는 몸 옆으로 빠져나가는 길까지 막혔다고 세면
	// 끼인 동물에게 갈 곳이 하나도 안 남는다.
	const FVector Normal = Hit.ImpactNormal.GetSafeNormal2D();
	return Normal.IsNearlyZero() || FVector::DotProduct(Normal, Direction) > AnimalBlockedFacingDot;
}

bool AAnimalCharacter::IsOrbitingLeash() const
{
	// 한계에 한 번 닿으면 안으로 확실히 들어올 때까지 경계를 도는 중이다 (`UpdateFleeOrbit`).
	return FleeOrbitSign != 0 || IsBeyondFleeLeash();
}

void AAnimalCharacter::ApplyLeashToVelocity(const FVector& FleeInput, float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!IsValid(Movement) || !IsOrbitingLeash())
	{
		return;
	}

	// **우회하는 1초 동안은 선을 놓는다.** 끼인 자리에서 빠져나갈 틈이 경계 바깥뿐일 때, 여기서
	// 바깥 성분을 깎으면 우리가 고른 탈출 방향을 우리가 지운다 — 동물은 굳은 채로 남는다.
	// 우회가 끝나면 아래의 되돌림이 넘친 거리를 그 걸음에 다시 끌어당긴다.
	if (BlockedDetourSeconds > 0.f)
	{
		return;
	}

	FVector Outward = GetActorLocation() - FleeHome;
	Outward.Z = 0.f;
	const FVector OutDirection = Outward.GetSafeNormal();

	// **입력이 접선이면 속도를 깎지 않고 돌린다.** 바깥 성분만 0 으로 깎으면, 경계에 닿는 순간의 속도는
	// 거의 통째로 바깥 방향이라 동물이 한 번 섰다가 다시 가속한다 — 그 한 번이 클립 되감김으로 보인다.
	// 크기를 지키고 방향만 돌리면 도주 속도가 그대로 이어진다.
	if (!FleeInput.IsNearlyZero() && FVector::DotProduct(FleeInput, OutDirection) <= KINDA_SMALL_NUMBER)
	{
		// **지켜야 할 크기는 도주 속도다.** 지금 실린 속도를 그대로 쓰면, 한 번 부딪혀 속도가 죽은 뒤
		// (캐릭터 이동은 실제 변위로 속도를 다시 계산한다) 그 죽은 값이 매 프레임 다시 실려
		// 동물이 스스로 못 빠져나온다 — 감독 PIE 결함 2026-09-24 의 동반 정지가 이 자기잠금이다.
		const float PlanarSpeed = FMath::Max(Movement->Velocity.Size2D(), MoveSpeed);

		// 접선은 직선이고 경계는 원이라, 한 걸음마다 걸음길이²/(2×반지름)만큼 밖으로 밀린다.
		// 한 걸음엔 0.1cm 라도 30초면 180cm 라 선이 조용히 늘어난다(2층 검사가 656cm 를 잡았다).
		// **넘친 만큼을 다음 한 걸음에 되돌리도록 접선을 안쪽으로 기울인다** — 속도 크기는 그대로다.
		const float Overshoot = Outward.Size() - AnimalFleeLeashRadius;
		float InwardSpeed = 0.f;
		if (Overshoot > 0.f && DeltaSeconds > 0.f)
		{
			InwardSpeed = FMath::Min(Overshoot / DeltaSeconds, PlanarSpeed * AnimalLeashReturnSpeedRatio);
		}
		const float TangentSpeed = FMath::Sqrt(FMath::Max(PlanarSpeed * PlanarSpeed - InwardSpeed * InwardSpeed, 0.f));

		Movement->Velocity = FleeInput * TangentSpeed - OutDirection * InwardSpeed
			+ FVector(0.f, 0.f, Movement->Velocity.Z);
		return;
	}

	// 도망갈 상황이 아니면(Capturable·죽음·상대 없음) 선만 지킨다 — 바깥 성분을 깎아 밀려나지 않게.
	// 안쪽으로 가는 중이면 깎을 것이 없다.
	const float OutwardSpeed = FVector::DotProduct(Movement->Velocity, OutDirection);
	if (IsBeyondFleeLeash() && OutwardSpeed > 0.f)
	{
		Movement->Velocity -= OutDirection * OutwardSpeed;
	}
}

bool AAnimalCharacter::IsBeyondFleeLeash() const
{
	// 높이는 빼고 잰다 — 경사·낙하로 한계가 달라지면 안 된다.
	return FVector::DistSquared2D(GetActorLocation(), FleeHome) >= FMath::Square(AnimalFleeLeashRadius);
}

UAnimSequence* AAnimalCharacter::ChooseLocomotionAnim() const
{
	float PlayRate = 1.f;
	return ChooseLocomotionClip(PlayRate);
}

float AAnimalCharacter::ChooseLocomotionPlayRate() const
{
	float PlayRate = 1.f;
	ChooseLocomotionClip(PlayRate);
	return PlayRate;
}

UAnimSequence* AAnimalCharacter::ChooseLocomotionClip(float& OutPlayRate) const
{
	OutPlayRate = 1.f;

	// 죽었거나 잡힌 뒤에는 되풀이 클립을 고르지 않는다 — 죽음 클립이 끝 프레임에서 멈춰 있어야 한다.
	const EAnimalState State = IsValid(AnimalState) ? AnimalState->GetState() : EAnimalState::Idle;
	if (State == EAnimalState::Dead || State == EAnimalState::Captured)
	{
		return nullptr;
	}

	if (!bBodyMoving)
	{
		return IdleAnim;
	}

	// 움직이는 이유가 둘이다: 맞고 **달아나는 중**(Fight)이면 도주 클립, 그 밖에 움직이면 걷기다.
	// 걷기 쪽은 반격·선공 종이 다가오거나 제자리로 돌아갈 때다 (M5-07d3 — 접근 속도 45cm/s 는 걷는 빠르기다).
	// 링 실패 도주(Escaping)는 쫓는 종도 달아나는 것이라 도주 클립이다 (M21-09b).
	const bool bFleeing = (State == EAnimalState::Fight && !IsChaserKind()) || State == EAnimalState::Escaping;
	if (bFleeing)
	{
		// 도주 칸이 비면 대기로 떨어진다.
		return FleeAnim != nullptr ? ToRawPtr(FleeAnim) : ToRawPtr(IdleAnim);
	}
	return PickWalkClip(WalkAnim, FleeAnim, IdleAnim, OutPlayRate);
}

UAnimSequence* AAnimalCharacter::PickWalkClip(UAnimSequence* Walk, UAnimSequence* Flee, UAnimSequence* Idle, float& OutPlayRate)
{
	OutPlayRate = 1.f;
	if (Walk != nullptr)
	{
		return Walk;
	}

	// **걷기 칸이 비면 도주 클립을 느리게 빌린다** (새 4종 — DESIGN 8.7 "날갯짓 도주 하나로 겸한다").
	// 빠르기 = 걷기 속도 ÷ 도주 속도. 배회는 도주 속도의 `AnimalWanderSpeedRatio` 배로 걸으니 그 비가 곧 답이다
	// — 새 숫자를 짓지 않는다. 대기로 떨어지면 몸은 가는데 다리가 서 있어 미끄러진다(M41-01 조사).
	if (Flee != nullptr)
	{
		OutPlayRate = AnimalWanderSpeedRatio;   // 도주 = `MoveSpeed` 1배
		return Flee;
	}
	return Idle;
}

void AAnimalCharacter::UpdateLocomotionAnimation()
{
	USkeletalMeshComponent* const Body = GetMesh();
	if (!IsValid(Body) || Body->GetSkeletalMeshAsset() == nullptr)
	{
		// 몸이 자리표시 실린더면 갈아 끼울 클립이 없다 (행에 메시가 없는 종).
		return;
	}

	// 문턱 둘로 가른다 — 하나면 가속·마찰 때문에 경계를 여러 번 넘나들며 클립이 되감긴다(루프#139).
	const float GroundSpeed = GetVelocity().Size2D();
	if (GroundSpeed >= MoveAnimEnterSpeedCmPerSecond)
	{
		bBodyMoving = true;
	}
	else if (GroundSpeed <= MoveAnimExitSpeedCmPerSecond)
	{
		bBodyMoving = false;
	}

	// 공격 클립이 도는 동안은 덮지 않는다 — 끝나면 아래가 이동 클립으로 되돌린다.
	if (BodyActionSecondsLeft > 0.f)
	{
		return;
	}

	float WantedPlayRate = 1.f;
	UAnimSequence* const Wanted = ChooseLocomotionClip(WantedPlayRate);
	if (Wanted == nullptr)
	{
		return;
	}

	// 같은 도주 클립이 걷기(느리게)↔도주(1배)로 바뀌면 빠르기만 바꾼다 — 다시 걸면 프레임 0 으로 되감긴다.
	if (Wanted == CurrentBodyAnim)
	{
		if (!FMath::IsNearlyEqual(WantedPlayRate, CurrentBodyPlayRate))
		{
			CurrentBodyPlayRate = WantedPlayRate;
			Body->SetPlayRate(WantedPlayRate);
		}
		return;
	}

	// 클립을 바꾸는 것은 곧 프레임 0 으로 되감는 것이다 — 전환할 때만 찍으므로 평소엔 로그가 안 는다.
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 동물 클립 전환 %s -> %s (속도 %.1fcm/s · 상태 %d)"),
		*GetName(), IsValid(CurrentBodyAnim) ? *CurrentBodyAnim->GetName() : TEXT("없음"),
		*Wanted->GetName(), GroundSpeed,
		IsValid(AnimalState) ? static_cast<int32>(AnimalState->GetState()) : -1);

	CurrentBodyAnim = Wanted;
	CurrentBodyPlayRate = WantedPlayRate;
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->PlayAnimation(Wanted, true);
	Body->SetPlayRate(WantedPlayRate);
}

void AAnimalCharacter::HandleStateChanged(EAnimalState NewState)
{
	if (NewState == EAnimalState::Escaping)
	{
		// 링 실패 도주 (M21-09b) — 가장 가까운 사람 반대쪽으로, 지금 자리에서 이탈 한계 500cm 안을 달아난다.
		// 쫓던 것도 끝이다(반격 없음). 남은 시간은 상태 컴포넌트가 재고 다 차면 Capturable 로 선다.
		FleeFrom = FindNearestPlayerBody(*this);
		FleeHome = GetActorLocation();
		bAggroed = false;
		bReturningHome = false;
		bFleeLeashReported = false;
		FleeOrbitSign = 0;
		LastTickLocation = FleeHome;
		BlockedSeconds = 0.f;
		FreeSeconds = 0.f;
		BlockedDetourSeconds = 0.f;
		BlockedDetourDirection = FVector::ZeroVector;
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 올가미를 놓쳤다 — %s 반대쪽으로 %.2f초 도주 (내 자리 %s · 체력 %.1f 그대로)"),
			*GetName(), FleeFrom.IsValid() ? *FleeFrom->GetName() : TEXT("(상대 없음)"),
			AnimalState->GetEscapeSecondsLeft(), *GetActorLocation().ToCompactString(), AnimalState->GetCurrentHP());
	}
	else if (NewState == EAnimalState::Fight)
	{
		// 선공 종이 이미 쫓던 중이면 상대와 시작 자리를 그대로 둔다 — 맞은 자리로 다시 찍으면 추격 포기 끈이 늘어난다.
		if (!bAggroed || !FleeFrom.IsValid())
		{
			// 도망칠 상대는 Fight 에 들어가는 이 순간 한 번만 찾는다 — 매 프레임 월드를 훑으면
			// 동물 수 × 액터 수만큼 일이 는다(모바일·4~8인 PVE).
			FleeFrom = FindNearestPlayerBody(*this);

			// 도주 한계를 재는 기준점도 같은 순간에 찍는다 — 싸움이 시작된 자리에서 500cm 안이 도망칠 수 있는 범위다.
			FleeHome = GetActorLocation();
		}
		bReturningHome = false;
		bFleeLeashReported = false;
		FleeOrbitSign = 0;
		LastTickLocation = FleeHome;
		BlockedSeconds = 0.f;
		FreeSeconds = 0.f;
		BlockedDetourSeconds = 0.f;
		BlockedDetourDirection = FVector::ZeroVector;

		// 3층 캡처는 한 장짜리라 "도망가는 중" 을 숫자로 못 보여 준다 — 시작 자리와 상대를 한 줄 남긴다.
		// 틱이 아니라 상태가 바뀌는 순간에만 찍으므로 동물이 몇 마리든 비용이 늘지 않는다.
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 맞았다 — %s %s (내 자리 %s · 속도 %.0fcm/s)"),
			*GetName(), FleeFrom.IsValid() ? *FleeFrom->GetName() : TEXT("(상대 없음)"),
			IsChaserKind() ? TEXT("에게 반격 시작") : TEXT("반대쪽으로 도망 시작"),
			*GetActorLocation().ToCompactString(), MoveSpeed);
	}
	else if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		// Capturable·죽음·포획이면 쫓기·돌아가기도 끝이다 — 그 자리에 선다 (DESIGN 3.1).
		if (NewState != EAnimalState::Idle)
		{
			bAggroed = false;
			bReturningHome = false;
		}

		// DESIGN 3.1 「Capturable 진입 시 즉시 정지」 — 입력만 끊으면 마찰로 몇 프레임 더 미끄러져
		// 포획 버튼을 누르는 순간의 자리가 어긋난다. 죽거나 포획된 뒤에도 같은 이유로 즉시 멈춘다.
		Movement->StopMovementImmediately();
	}

	if (NewState == EAnimalState::Dead)
	{
		// 죽는 울음 (M43-03 · DESIGN 8.7 크기 1.0) — 게스트도 복제된 상태로 여기 와서 제 손으로 낸다.
		PlayVoice(DeathSoundCue, 1.f);
	}

	if (NewState == EAnimalState::Dead || NewState == EAnimalState::Captured)
	{
		// M22 — 시체가 영원히 남아 길을 막고 액터가 쌓였다. 끝 상태가 되는 순간 몸을 비우고(모든 기기 —
		// 게스트도 복제된 상태로 여기 온다), 호스트가 행의 수명 뒤 치운다(파괴는 복제로 모두에게 간다).
		// 지급 요청은 이 순간 이미 쌓였다(드롭·포획 델리게이트는 같은 프레임에 동기로 울린다).
		SetActorEnableCollision(false);
		if (UCharacterMovementComponent* const Movement = GetCharacterMovement())
		{
			// 충돌을 끈 몸이 중력으로 바닥을 뚫고 떨어지지 않게 이동 자체를 끈다.
			Movement->DisableMovement();
		}
		if (HasAuthority())
		{
			if (CorpseLifeSeconds > 0.f)
			{
				SetLifeSpan(CorpseLifeSeconds);
			}
			else
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("%s: 시체 수명이 미정이라 치우지 않는다"), *GetName());
			}
		}
	}

	if (NewState == EAnimalState::Captured)
	{
		// 포획했으면 사냥터에서 사라진다 (감독 결정 2026-09-20, DESIGN 3.1). 그대로 서 있으면
		// 화면에서 포획 성공과 죽음이 똑같아 보인다 — 감독이 `-game` 창에서 그걸로 헷갈렸다.
		// 바로 파괴하지 않고 숨기는 이유: 포획을 들은 쪽이 이 프레임에 이 액터로 펫 등록 요청을 쌓는다 (M3-06).
		// 충돌은 위에서 이미 껐고, 액터는 위 수명 뒤 호스트가 치운다(M22).
		SetActorHiddenInGame(true);
		return;
	}

	USkeletalMeshComponent* Body = GetMesh();
	if (!IsValid(Body) || NewState != EAnimalState::Dead || DeathAnim == nullptr)
	{
		// 죽음만 여기서 건다. 걷기·도주는 상태가 아니라 **속도**로 갈려서 `Tick` 의
		// `UpdateLocomotionAnimation` 이 맡는다 (M5-07d) — 도망가기로 한 프레임과 실제로 움직이기
		// 시작하는 프레임이 달라, 상태가 바뀌는 순간에 걸면 첫 몇 프레임이 미끄러진다.
		return;
	}

	// 반복하지 않는다 — 끝 프레임에서 멈춰 죽은 자세로 누워 있어야 한다.
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->PlayAnimation(DeathAnim, false);
	// 이동 클립 쪽에도 "지금 이게 걸려 있다" 를 알린다 — 안 알리면 죽은 뒤 첫 틱이 예전 클립과
	// 비교해 대기로 되돌려 시체가 일어선다. (`ChooseLocomotionAnim` 이 죽음 뒤엔 nullptr 을 준다.)
	CurrentBodyAnim = DeathAnim;
	CurrentBodyPlayRate = 1.f;
	bBodyMoving = false;
}
