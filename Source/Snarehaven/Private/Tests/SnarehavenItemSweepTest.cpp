// INBOX 2026-10-15 21:58 「베기 모션 없냐? 아이템 싹 테스트 다 돌려봐」 — 전 아이템 훑기 2층 검사.
// 아이템 표에서 손에 드는 행(무기·도구 칸)을 **전부** 하나씩 들고 공격을 눌러
//   ① 든 자세·쓰기 클립 에셋이 정말 불러와지나 (경로 문자열이라 쿡에서 빠지면 조용히 비는 곳),
//   ② 손에 모델이 붙었나,
//   ③ 누른 뒤 몸에 걸린 클립 이름
// 을 한 줄씩 표로 남긴다. 표의 클립 칸이 비거나 불러오기가 안 되면 실패다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Items.Sweep; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "SnarehavenCharacter.h"
#include "Snarehaven.h"

namespace SnareItemSweepTest
{
	const TCHAR* const ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestTimerStepSeconds = 0.1f;
	constexpr int32 TestLoadedRounds = 5;

	FString YesNo(bool bValue) { return bValue ? TEXT("됨") : TEXT("안 나옴"); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemSweepTest,
	"Snarehaven.Items.Sweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemSweepTest::RunTest(const FString& Parameters)
{
	using namespace SnareItemSweepTest;

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("플레이어"), Player))
	{
		GameInstance->Shutdown();
		return false;
	}
	Player->DispatchBeginPlay();
	Player->ApplyPlayerBody();
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	UEquipmentComponent* const Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("근접"), Melee) || !TestNotNull(TEXT("장비"), Equipment))
	{
		GameInstance->Shutdown();
		return false;
	}
	Melee->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);

	TArray<FString> Lines;
	int32 Swept = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || (Row->EquipSlot != EEquipSlot::Weapon && Row->EquipSlot != EEquipSlot::Tool))
		{
			continue;
		}
		++Swept;
		const FString Id = Pair.Key.ToString();

		// ① 클립 에셋 — 칸이 있으면 정말 불러와져야 한다.
		const FString HoldPath = Row->HoldPoseAnim.ToString();
		const FString UsePath = Row->UseAnim.ToString();
		const UAnimSequence* const Hold = Row->HoldPoseAnim.IsNull() ? nullptr : Row->HoldPoseAnim.LoadSynchronous();
		const UAnimSequence* const Use = Row->UseAnim.IsNull() ? nullptr : Row->UseAnim.LoadSynchronous();
		// 들고만 있어도 일하는 도구(별길 수신기)는 손에 보일 것도 쓸 동작도 없다.
		const bool bPassive = Row->bPassiveWhileCarried;
		// 낚싯대는 든 자세만 있고 던지기는 낚시 컴포넌트 몫이라 쓰기 칸이 비어도 된다.
		TestFalse(*FString::Printf(TEXT("[%s] 클립 칸이 둘 다 비지 않았다"), *Id),
			!bPassive && Row->UseAnim.IsNull() && Row->HoldPoseAnim.IsNull());
		if (!Row->HoldPoseAnim.IsNull())
		{
			TestNotNull(*FString::Printf(TEXT("[%s] 든 자세 클립 불러옴 %s"), *Id, *HoldPath), Hold);
		}
		if (!Row->UseAnim.IsNull())
		{
			TestNotNull(*FString::Printf(TEXT("[%s] 쓰기 클립 불러옴 %s"), *Id, *UsePath), Use);
		}

		// ② 든다 — 호스트 목록 길로 (아이템 메시가 아직 없는 행도 걸리게).
		TArray<FEquippedSlot> Slots;
		FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
		Slot.Slot = Row->EquipSlot;
		Slot.ItemRowName = Pair.Key;
		Equipment->ApplyEquippedSlots(Slots);
		const UStaticMeshComponent* const MeshComp = Equipment->GetEquippedMesh(Row->EquipSlot);
		const bool bMesh = MeshComp != nullptr && MeshComp->GetStaticMesh() != nullptr;
		TestTrue(*FString::Printf(TEXT("[%s] 손에 모델이 붙는다"), *Id), bMesh || bPassive);

		// ③ 누른다 — 몸에 걸린 클립.
		// 총은 탄창이 차야 한 발이 나가고 반동 클립이 돈다.
		Melee->SetLoadedRounds(TestLoadedRounds);
		const int32 ActionBefore = Player->GetItemActionCount();
		Player->BeginAttack();
		Player->UpdateBodyAnimation();
		const UAnimSequence* const Played = Player->GetCurrentBodyAnim();
		const bool bActionStarted = Player->GetItemActionCount() > ActionBefore;
		const FString PlayedName = Played != nullptr ? Played->GetName() : FString(TEXT("(없음)"));
		const FString WantName = Use != nullptr ? Use->GetName() : FString(TEXT("-"));
		const bool bClipMatches = Use == nullptr || PlayedName == WantName;
		Lines.Add(FString::Printf(TEXT("%-24s | 쓰기 %-28s | 걸린 클립 %-28s | 재생 %s | 모델 %s | 동작종류 %s"),
			*Id, *WantName, *PlayedName, *YesNo(bClipMatches), *YesNo(bMesh),
			bActionStarted ? *StaticEnum<EItemAction>()->GetNameStringByValue(static_cast<int64>(Player->GetLastItemAction())) : TEXT("-")));
		Player->EndAttack();
		const int32 Steps = FMath::RoundToInt(TestAttackIntervalSeconds * 3.f / TestTimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestTimerStepSeconds);
		}
	}

	for (const FString& Line : Lines)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[훑기] %s"), *Line);
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[훑기] 손에 드는 행 %d개 훑음"), Swept);
	TestTrue(TEXT("손에 드는 행이 있다"), Swept > 0);

	GameInstance->Shutdown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
