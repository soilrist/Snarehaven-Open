// M22 2층 검사 — 동물 종별 체력·최상급 드롭 확률·시체 수명.
// 잡는 것: ① 어떤 무기로 때려도 포획 창(0 초과 ~ 임계 이하)을 건너뛰는 종 ② 한 마리와 정면으로 싸워 죽는 종
// ③ 최상급(`pristine_`)이 목록 균등(1/3)으로 쏟아지는 것 ④ 죽거나 포획된 몸이 영원히 남아 길을 막는 것.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Animal; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/PlayerCombatRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* CombatTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerCombatTable.DT_PlayerCombatTable");
	const TCHAR* VitalsTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable");
	const TCHAR* PlayerRowName = TEXT("Player");

	// DESIGN 3.1 「동물 체력(HP)」 줄 그대로. 검사 쪽에 따로 적어야 표가 바뀌면 여기서 걸린다.
	const TPair<const TCHAR*, float> DesignAnimalHp[] = {
		{ TEXT("Animal_Pig"), 80.f }, { TEXT("Animal_Cow"), 80.f }, { TEXT("Animal_Sheep"), 80.f },
		{ TEXT("Animal_Pheasant"), 80.f }, { TEXT("Animal_Peacock"), 80.f },
		{ TEXT("Animal_Ptarmigan"), 80.f }, { TEXT("Animal_Capercaillie"), 80.f },
		{ TEXT("Animal_Fox"), 100.f }, { TEXT("Animal_Goat"), 100.f }, { TEXT("Animal_Reindeer"), 100.f },
		{ TEXT("Animal_Elk"), 140.f }, { TEXT("Animal_Lynx"), 140.f },
		{ TEXT("Animal_Wolf"), 160.f }, { TEXT("Animal_Wolverine"), 160.f },
		{ TEXT("Animal_Bear"), 200.f },
	};

	// DESIGN 3.1 「최상급 드롭 확률」 = 5%, 13.5 사냥 모자 ×1.3.
	constexpr float DesignPristineChance = 0.05f;
	constexpr float DesignHatMul = 1.3f;

	// 검사 전용 값 (Verification.md 6) — 시드·횟수·허용 폭.
	constexpr int32 TestSeed = 20261003;
	constexpr int32 TestRolls = 10000;
	constexpr float TestRateTolerance = 0.01f;
	constexpr float TestHugeDamage = 99999.f;

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

	float PristineRate(const TArray<FName>& Items, float Chance)
	{
		FRandomStream Stream(TestSeed);
		const TArray<FName> Drops = UAnimalStateComponent::RollDropItems(Items, TestRolls, Chance, Stream);
		return static_cast<float>(Drops.FilterByPredicate(&UAnimalStateComponent::IsPristineItem).Num()) / TestRolls;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalHpWindowTest, "Snarehaven.Animal.Hp.CaptureWindowForEveryWeapon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalHpWindowTest::RunTest(const FString& Parameters)
{
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const UDataTable* const CombatTable = LoadObject<UDataTable>(nullptr, CombatTablePath);
	const UDataTable* const VitalsTable = LoadObject<UDataTable>(nullptr, VitalsTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable"), AnimalTable) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable)
		|| !TestNotNull(TEXT("DT_PlayerCombatTable"), CombatTable) || !TestNotNull(TEXT("DT_PlayerVitalsTable"), VitalsTable))
	{
		return false;
	}
	const FPlayerCombatRow* const Combat = CombatTable->FindRow<FPlayerCombatRow>(PlayerRowName, TEXT("HpWindow"));
	const FPlayerVitalsRow* const Vitals = VitalsTable->FindRow<FPlayerVitalsRow>(PlayerRowName, TEXT("HpWindow"));
	if (!TestNotNull(TEXT("전투 Player 행"), Combat) || !TestNotNull(TEXT("체력 Player 행"), Vitals)
		|| !TestTrue(TEXT("플레이어 1타 데미지가 정해졌다"), Combat->AttackDamage > 0.f))
	{
		return false;
	}

	// 무기는 데미지를 따로 갖지 않고 거리 감쇠 배율(최소 배율 ~ 1)만 갖는다 — 1타는 기본 데미지 × 배율.
	TArray<float> Damages = { Combat->AttackDamage };
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Item = reinterpret_cast<const FItemRow*>(Pair.Value);
		if ((Item->EquipSlot == EEquipSlot::Weapon || Item->EquipSlot == EEquipSlot::Tool) && Item->MinDamageRatio > 0.f)
		{
			Damages.AddUnique(Combat->AttackDamage * Item->MinDamageRatio);
		}
	}

	for (const TPair<const TCHAR*, float>& Expected : DesignAnimalHp)
	{
		const FAnimalRow* const Row = AnimalTable->FindRow<FAnimalRow>(Expected.Key, TEXT("HpWindow"), false);
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), Expected.Key), Row))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 체력 = DESIGN"), Expected.Key), Row->MaxHP, Expected.Value);
		const float Window = Row->MaxHP * Row->CaptureThresholdPct / 100.f;

		// ① 포획 창 — 어떤 데미지로 쳐도 0 초과 ~ 임계 이하에 떨어지는 타가 있다.
		for (const float Damage : Damages)
		{
			float Hp = Row->MaxHP;
			while (Hp > Window)
			{
				Hp -= Damage;
			}
			TestTrue(*FString::Printf(TEXT("%s: 데미지 %.1f 로 포획 창에 떨어진다 (HP %.1f, 창 %.1f)"),
				Expected.Key, Damage, Hp, Window), Hp > 0.f);
		}
		TestTrue(*FString::Printf(TEXT("%s: 창 폭 %.1f ≥ 최대 1타 %.1f — 감쇠한 어떤 타도 못 건너뛴다"),
			Expected.Key, Window, Combat->AttackDamage), Window >= Combat->AttackDamage);

		// ② 정면 승부 — Capturable 까지 걸리는 시간 동안 동물이 최악(첫 타 먼저)으로 친 합이 플레이어 체력보다 작다.
		if (Row->AttackDamage > 0.f && Row->AttackIntervalSec > 0.f)
		{
			const int32 PlayerHits = FMath::CeilToInt((Row->MaxHP - Window) / Combat->AttackDamage);
			const float Seconds = (PlayerHits - 1) * Combat->AttackIntervalSeconds;
			const int32 AnimalHits = FMath::FloorToInt(Seconds / Row->AttackIntervalSec) + 1;
			const float Lost = AnimalHits * Row->AttackDamage;
			AddInfo(FString::Printf(TEXT("%s: %d타 %.1f초, 최악 %d대 맞아 %.0f 잃음"), Expected.Key, PlayerHits, Seconds, AnimalHits, Lost));
			TestTrue(*FString::Printf(TEXT("%s 한 마리에 죽지 않는다"), Expected.Key), Lost < Vitals->MaxHealth);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalPristineRateTest, "Snarehaven.Animal.Drop.PristineRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalPristineRateTest::RunTest(const FString& Parameters)
{
	const TArray<FName> WithPristine = { TEXT("item.test_meat"), TEXT("item.test_hide"), TEXT("item.pristine_test") };
	const TArray<FName> NoPristine = { TEXT("item.test_meat"), TEXT("item.test_hide") };

	const float Base = PristineRate(WithPristine, DesignPristineChance);
	const float WithHat = PristineRate(WithPristine, DesignPristineChance * DesignHatMul);
	AddInfo(FString::Printf(TEXT("%d번: 기본 %.2f%% · 모자 %.2f%%"), TestRolls, Base * 100.f, WithHat * 100.f));
	TestTrue(TEXT("기본 최상급 4~6%"), FMath::Abs(Base - DesignPristineChance) <= TestRateTolerance);
	TestTrue(TEXT("모자 최상급 5.5~7.5%"), FMath::Abs(WithHat - DesignPristineChance * DesignHatMul) <= TestRateTolerance);
	TestEqual(TEXT("최상급 없는 목록은 0%"), PristineRate(NoPristine, DesignPristineChance), 0.f);
	TestEqual(TEXT("확률 미정(-1)이면 최상급을 안 굴린다"), PristineRate(WithPristine, -1.f), 0.f);

	// 나머지 95% 는 일반 항목에서 균등 — 둘의 차이가 전체의 3% 안.
	FRandomStream Stream(TestSeed);
	const TArray<FName> Drops = UAnimalStateComponent::RollDropItems(WithPristine, TestRolls, DesignPristineChance, Stream);
	const int32 Meat = Drops.FilterByPredicate([](const FName& Id) { return Id == FName(TEXT("item.test_meat")); }).Num();
	const int32 Hide = Drops.FilterByPredicate([](const FName& Id) { return Id == FName(TEXT("item.test_hide")); }).Num();
	TestTrue(*FString::Printf(TEXT("일반 둘이 균등 (고기 %d · 가죽 %d)"), Meat, Hide), FMath::Abs(Meat - Hide) <= TestRolls * 3 / 100);

	// 표 — 최상급이 든 종은 전부 DESIGN 값.
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (TestNotNull(TEXT("DT_AnimalTable"), AnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row->DeathDropItems.ContainsByPredicate(&UAnimalStateComponent::IsPristineItem))
			{
				TestEqual(*FString::Printf(TEXT("%s 최상급 확률 = DESIGN"), *Pair.Key.ToString()), Row->PristineDropChance, DesignPristineChance);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalCorpseLifeTest, "Snarehaven.Animal.Body.CorpseClearsAfterLife",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalCorpseLifeTest::RunTest(const FString& Parameters)
{
	UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable"), AnimalTable))
	{
		return false;
	}
	UWorld* World = CreateTestWorld();
	for (const bool bCapture : { false, true })
	{
		const TCHAR* const Label = bCapture ? TEXT("포획") : TEXT("죽음");
		AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>();
		if (!TestNotNull(TEXT("동물 스폰"), Animal))
		{
			continue;
		}
		Animal->SetAnimalData(AnimalTable, TEXT("Animal_Pig"));
		TestTrue(TEXT("표 행으로 초기화"), Animal->InitializeFromTable());
		const FAnimalRow* const Row = AnimalTable->FindRow<FAnimalRow>(TEXT("Animal_Pig"), TEXT("CorpseLife"));
		UAnimalStateComponent* const State = Animal->GetAnimalState();
		TestTrue(TEXT("살아 있으면 충돌이 켜져 있다"), Animal->GetActorEnableCollision());
		TestEqual(TEXT("살아 있으면 수명이 없다"), Animal->GetLifeSpan(), 0.f);

		if (bCapture)
		{
			State->ApplyDamage(State->GetMaxHP() - State->GetCaptureThresholdHP());
			TestTrue(TEXT("포획 성공"), State->TryCapture());
		}
		else
		{
			State->ApplyDamage(TestHugeDamage);
		}
		TestEqual(*FString::Printf(TEXT("%s 상태"), Label), State->GetState(), bCapture ? EAnimalState::Captured : EAnimalState::Dead);
		TestFalse(*FString::Printf(TEXT("%s 순간 충돌이 꺼진다"), Label), Animal->GetActorEnableCollision());
		TestTrue(*FString::Printf(TEXT("%s: 표의 시체 수명이 정해졌다"), Label), Row != nullptr && Row->CorpseLifeSeconds > 0.f);
		if (Row != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("%s: 호스트가 표의 수명 뒤 치운다"), Label), Animal->GetLifeSpan(), Row->CorpseLifeSeconds, 0.01f);
		}
	}
	DestroyTestWorld(World);
	return true;
}

#endif
