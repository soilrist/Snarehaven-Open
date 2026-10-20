// ROADMAP M65-02 2층 검사 — 사냥 모자 ×1.3 이 막타에서 동물까지 닿는다 (DESIGN 13.5).
// 모자 쓴 사람의 막타만 동물의 대기 배율이 행 `PristineChanceMul` 이 되고, 모자 없으면 1, 막타가 아닌 맞음은 배율을 안 바꾼다.
// 근접(`AttackOnce`)과 총(`StartAttacking` → 도착 시각 `LandShot`) 둘 다 — 둘 다 `ApplyHitOnAuthority` 한 길이다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.HunterHat; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "CoreGlobals.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "SnarehavenCharacter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"

namespace SnareHunterHatTest
{
	// 검사 전용 값 (Verification.md 6).
	constexpr float TestMeleeDamage = 10.f;
	constexpr float TestMeleeRangeCm = 300.f;
	constexpr float TestMeleeIntervalSeconds = 1.f;
	constexpr float TestBigHP = 1000.f;
	constexpr float TestCaptureThresholdPct = 1.f;
	constexpr float TestTimerStepSeconds = 0.005f;
	constexpr float TestTolerance = 0.001f;
	const FName PistolID(TEXT("item.pistol_semi"));
	const FName ClubID(TEXT("item.club"));
	const FName HatID(TEXT("item.hunter_hat"));
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	/** 권총(몽둥이 메시를 빌림 — 총 시험과 같다) + 사냥 모자 실제 행 하나씩 든 시험 전용 표. */
	UDataTable* MakeTable(FAutomationTestBase& Test)
	{
		const UDataTable* const Source = LoadObject<UDataTable>(nullptr, ItemTablePath);
		if (!Test.TestNotNull(TEXT("아이템 표"), Source))
		{
			return nullptr;
		}
		FName PistolName;
		FName HatName;
		const FItemRow* Pistol = nullptr;
		const FItemRow* Club = nullptr;
		const FItemRow* Hat = nullptr;
		for (const TPair<FName, uint8*>& Pair : Source->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row->ItemID == PistolID) { Pistol = Row; PistolName = Pair.Key; }
			else if (Row->ItemID == ClubID) { Club = Row; }
			else if (Row->ItemID == HatID) { Hat = Row; HatName = Pair.Key; }
		}
		if (!Test.TestNotNull(TEXT("권총 행"), Pistol) || !Test.TestNotNull(TEXT("몽둥이 행"), Club)
			|| !Test.TestNotNull(TEXT("사냥 모자 행"), Hat))
		{
			return nullptr;
		}
		UDataTable* const Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FItemRow::StaticStruct();
		FItemRow GunRow = *Pistol;
		GunRow.EquipMeshRef = Club->EquipMeshRef;
		Table->AddRow(PistolName, GunRow);
		Table->AddRow(HatName, *Hat);
		return Table;
	}

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TestTimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TestTimerStepSeconds);
		}
	}

	AAnimalCharacter* SpawnAnimal(UWorld* World, const FVector& Location, float MaxHP)
	{
		AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(Location, FRotator::ZeroRotator);
		if (Animal != nullptr)
		{
			FAnimalRow Row;
			Row.AnimalID = TEXT("animal.test_hunterhat");
			Row.MaxHP = MaxHP;
			Row.CaptureThresholdPct = TestCaptureThresholdPct;
			Animal->GetAnimalState()->InitializeFromRow(Row);
		}
		return Animal;
	}

	/** 원점에서 +X 를 보는 사람. `bHat` 면 사냥 모자를 쓴다. 모자 행 배율은 `OutHatMul` 로 돌려준다. */
	ASnarehavenCharacter* SpawnPlayer(FAutomationTestBase& Test, UWorld* World, bool bHat, float& OutHatMul)
	{
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!Test.TestNotNull(TEXT("사람 스폰"), Player) || !Test.TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
		{
			return nullptr;
		}
		UDataTable* const Table = MakeTable(Test);
		FObjectProperty* const TableProperty = FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
		if (Table == nullptr || !Test.TestNotNull(TEXT("장비 표 칸"), TableProperty))
		{
			return nullptr;
		}
		TableProperty->SetObjectPropertyValue_InContainer(Player->GetEquipment(), Table);
		Player->GetMeleeAttack()->SetAttackSettings(TestMeleeDamage, TestMeleeRangeCm, TestMeleeIntervalSeconds);
		Player->GetMeleeAttack()->SetAttackHitDelaySeconds(0.f);
		OutHatMul = 1.f;
		if (bHat)
		{
			if (!Test.TestTrue(TEXT("사냥 모자를 썼다"), Player->GetEquipment()->EquipItemByItemID(HatID)))
			{
				return nullptr;
			}
			const FItemRow* const Row = Player->GetEquipment()->GetEquippedRow(EEquipSlot::Head);
			if (!Test.TestNotNull(TEXT("쓴 모자 행"), Row) || !Test.TestTrue(TEXT("모자 배율이 1 보다 크다(DESIGN 13.5)"), Row->PristineChanceMul > 1.f))
			{
				return nullptr;
			}
			OutHatMul = Row->PristineChanceMul;
		}
		return Player;
	}
}

using namespace SnareHunterHatTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHunterHatMeleeTest, "Snarehaven.Combat.HunterHat.Melee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHunterHatMeleeTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	float HatMul = 1.f;
	float NoHatMul = 1.f;
	ASnarehavenCharacter* const Hunter = SpawnPlayer(*this, World, true, HatMul);
	ASnarehavenCharacter* const Bare = SpawnPlayer(*this, World, false, NoHatMul);
	if (Hunter == nullptr || Bare == nullptr)
	{
		DestroyTestWorld(World);
		return false;
	}
	// ① 막타가 아닌 맞음 — 체력이 남으면 배율은 그대로 1.
	AAnimalCharacter* const Big = SpawnAnimal(World, FVector(100.f, 0.f, 0.f), TestBigHP);
	TestTrue(TEXT("한 대 들어갔다"), Hunter->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("막타 아니면 배율 1"), Big->GetAnimalState()->GetPendingPristineChanceMul(), 1.f, TestTolerance);
	TestNotEqual(TEXT("죽지 않았다"), Big->GetAnimalState()->GetState(), EAnimalState::Dead);
	Big->Destroy();
	AdvanceTimers(World, TestMeleeIntervalSeconds * 2.f);

	// ② 모자 쓴 사람의 막타 — 행 배율 그대로.
	AAnimalCharacter* const Weak = SpawnAnimal(World, FVector(100.f, 0.f, 0.f), TestMeleeDamage);
	TestTrue(TEXT("막타가 들어갔다"), Hunter->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("죽었다"), Weak->GetAnimalState()->GetState(), EAnimalState::Dead);
	const float MeleeHatSeen = Weak->GetAnimalState()->GetPendingPristineChanceMul();
	TestEqual(TEXT("모자 막타 → 행 배율"), MeleeHatSeen, HatMul, TestTolerance);

	// ③ 모자 없는 사람의 막타 — 1.
	Weak->Destroy();
	AAnimalCharacter* const Weak2 = SpawnAnimal(World, FVector(100.f, 0.f, 0.f), TestMeleeDamage);
	TestTrue(TEXT("맨머리 막타가 들어갔다"), Bare->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("죽었다(맨머리)"), Weak2->GetAnimalState()->GetState(), EAnimalState::Dead);
	const float MeleeBareSeen = Weak2->GetAnimalState()->GetPendingPristineChanceMul();
	TestEqual(TEXT("맨머리 막타 → 배율 1"), MeleeBareSeen, 1.f, TestTolerance);

	AddInfo(FString::Printf(TEXT("[HunterHat] 근접 모자 %.2f · 맨머리 %.2f"), MeleeHatSeen, MeleeBareSeen));
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHunterHatGunTest, "Snarehaven.Combat.HunterHat.Gun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHunterHatGunTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	float HatMul = 1.f;
	float NoHatMul = 1.f;
	ASnarehavenCharacter* const Hunter = SpawnPlayer(*this, World, true, HatMul);
	ASnarehavenCharacter* const Bare = SpawnPlayer(*this, World, false, NoHatMul);
	if (Hunter == nullptr || Bare == nullptr
		|| !TestTrue(TEXT("모자 사람이 권총을 들었다"), Hunter->GetEquipment()->EquipItemByItemID(PistolID))
		|| !TestTrue(TEXT("맨머리 사람이 권총을 들었다"), Bare->GetEquipment()->EquipItemByItemID(PistolID)))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FItemRow* const Gun = Hunter->GetEquipment()->GetEquippedRow(EEquipSlot::Weapon);
	if (!TestNotNull(TEXT("든 무기 행"), Gun)
		|| !TestTrue(TEXT("총 행이다"), Gun->MagazineSize > 0 && Gun->ProjectileSpeedCmPerSecond > 0.f && Gun->AttackDamage > 0.f))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 감쇠 전 구간에 둔다 — 피해 = 행 피해 그대로라 체력을 행 피해에 맞추면 한 발이 막타다.
	const float DistanceCm = Gun->AttackRangeCm * Gun->FalloffStartRatio * 0.5f;
	const float FlightSeconds = DistanceCm / Gun->ProjectileSpeedCmPerSecond;
	const auto ShootAnimal = [&](ASnarehavenCharacter* Shooter, float MaxHP) -> AAnimalCharacter*
		{
			AAnimalCharacter* const Animal = SpawnAnimal(World, FVector(DistanceCm, 0.f, 0.f), MaxHP);
			UMeleeAttackComponent* const Melee = Shooter->GetMeleeAttack();
			Melee->SetLoadedRounds(1);
			Melee->StartAttacking();
			Melee->StopAttacking();
			AdvanceTimers(World, FlightSeconds * 2.f);
			return Animal;
		};

	// ① 막타가 아닌 맞음 → 1
	AAnimalCharacter* const Big = ShootAnimal(Hunter, TestBigHP);
	TestNotEqual(TEXT("죽지 않았다"), Big->GetAnimalState()->GetState(), EAnimalState::Dead);
	TestEqual(TEXT("총 막타 아니면 배율 1"), Big->GetAnimalState()->GetPendingPristineChanceMul(), 1.f, TestTolerance);
	Big->Destroy();
	AdvanceTimers(World, TestMeleeIntervalSeconds * 4.f);

	// ② 모자 쓴 사람의 총 막타
	AAnimalCharacter* const Weak = ShootAnimal(Hunter, Gun->AttackDamage);
	TestEqual(TEXT("죽었다(총)"), Weak->GetAnimalState()->GetState(), EAnimalState::Dead);
	const float GunHatSeen = Weak->GetAnimalState()->GetPendingPristineChanceMul();
	TestEqual(TEXT("모자 총 막타 → 행 배율"), GunHatSeen, HatMul, TestTolerance);

	// ③ 맨머리 총 막타
	Weak->Destroy();
	AAnimalCharacter* const Weak2 = ShootAnimal(Bare, Gun->AttackDamage);
	TestEqual(TEXT("죽었다(총·맨머리)"), Weak2->GetAnimalState()->GetState(), EAnimalState::Dead);
	const float GunBareSeen = Weak2->GetAnimalState()->GetPendingPristineChanceMul();
	TestEqual(TEXT("맨머리 총 막타 → 배율 1"), GunBareSeen, 1.f, TestTolerance);

	AddInfo(FString::Printf(TEXT("[HunterHat] 총 모자 %.2f · 맨머리 %.2f"), GunHatSeen, GunBareSeen));
	DestroyTestWorld(World);
	return true;
}

namespace SnareHunterHatTest
{
	const TCHAR* const MeleeSourceFile = TEXT("Source/Snarehaven/Private/Combat/MeleeAttackComponent.cpp");

	/** `Source` 안 `Name(` 으로 시작하는 함수 정의의 중괄호 몸통. 못 찾으면 빈 문자열. */
	FString BodyOf(const FString& Source, const FString& Header)
	{
		const int32 Start = Source.Find(Header, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 Open = Source.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		int32 Depth = 0;
		for (int32 I = Open; Open != INDEX_NONE && I < Source.Len(); ++I)
		{
			Depth += Source[I] == TEXT('{') ? 1 : Source[I] == TEXT('}') ? -1 : 0;
			if (Depth == 0)
			{
				return Source.Mid(Open, I - Open + 1);
			}
		}
		return FString();
	}

	/** 동물에게 피해를 직접 넣는 길(`AnimalState()->ApplyDamage(`)이 모자 계산을 건너뛰는 곳 — 한 줄씩. */
	TArray<FString> SurveyHatSkippingPaths(const FString& MeleeSource, const FString& LandShotHeader)
	{
		TArray<FString> Gaps;
		const FString Hit = BodyOf(MeleeSource, TEXT("bool UMeleeAttackComponent::ApplyHitOnAuthority("));
		const FString Shot = BodyOf(MeleeSource, LandShotHeader);
		if (!Hit.Contains(TEXT("PristineChanceMul")) || !Hit.Contains(TEXT("State->ApplyDamage(Damage, PristineMul)")))
		{
			Gaps.Add(TEXT("ApplyHitOnAuthority 가 머리 칸 배율을 동물 피해에 넘기지 않는다"));
		}
		if (Shot.IsEmpty() || !Shot.Contains(TEXT("ApplyHitOnAuthority(")) || Shot.Contains(TEXT("ApplyDamage(")))
		{
			Gaps.Add(TEXT("LandShot 이 ApplyHitOnAuthority 를 거치지 않고 따로 피해를 넣는다 — 총에서 모자가 빠진다"));
		}
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private")), TEXT("*.cpp"), true, false);
		for (const FString& File : Files)
		{
			FString Text;
			if (!File.Contains(TEXT("/Tests/")) && FFileHelper::LoadFileToString(Text, *File)
				&& (Text.Contains(TEXT("GetAnimalState()->ApplyDamage(")) || Text.Contains(TEXT("AnimalState->ApplyDamage("))))
			{
				Gaps.Add(FString::Printf(TEXT("%s: 동물 상태에 피해를 직접 넣는다 — 모자 배율이 빠진다"), *FPaths::GetCleanFilename(File)));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHunterHatNoSkipTest, "Snarehaven.Combat.HunterHat.NoSkippingPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHunterHatNoSkipTest::RunTest(const FString& Parameters)
{
	using namespace SnareHunterHatTest;
	FString Source;
	if (!TestTrue(TEXT("소스를 읽었다"), FFileHelper::LoadFileToString(Source, *(FPaths::ProjectDir() / MeleeSourceFile))))
	{
		return false;
	}
	const FString LandShotHeader = TEXT("void UMeleeAttackComponent::LandShot(");
	const TArray<FString> Gaps = SurveyHatSkippingPaths(Source, LandShotHeader);
	AddInfo(FString::Printf(TEXT("[HunterHat] 모자 건너뛰는 길 %d"), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}
	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 사본으로 거름망이 살아 있는지 본다.
	const FString Faked = Source.Replace(TEXT("ApplyHitOnAuthority(Animal, Damage);"), TEXT("Animal->GetAnimalState()->ApplyDamage(Damage);"));
	const TArray<FString> FakeGaps = SurveyHatSkippingPaths(Faked, LandShotHeader);
	TestTrue(TEXT("역검증: 총이 따로 ApplyDamage 를 부르는 사본 → 빈 길 잡음"), FakeGaps.Num() >= 1 && FakeGaps[0].Contains(TEXT("LandShot")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
