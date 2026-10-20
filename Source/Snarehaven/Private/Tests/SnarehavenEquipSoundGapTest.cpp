// ROADMAP M47-05 2층 단정 — 장비·탈것 소리 빈 길 0 (DESIGN 2.2 「장비·탈것 소리」).
// `DT_ItemTable` 의 손에 드는 행 전부(`Weapon`·`Tool`)가 `EquipCue` 로, 옷 부위 전부가 `OutfitCue` 로, 빈손 넣기·탈것 타기/내리기가
// **실제로 들어온 소리 에셋**(`CountVariants` > 0)으로 이어지는가. 새 장비 종류·부위가 소리 없이 생기면 여기서 운다.
// 판단은 소리 이름이 아니라 그 이름으로 변형을 실제로 찾았는지로 한다 (Verification.md).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.SoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* GearSoundItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	bool IsHandSlot(EEquipSlot Slot)
	{
		return Slot == EEquipSlot::Weapon || Slot == EEquipSlot::Tool;
	}

	/** 그 소리 줄이 빈 자리인가 — 이름이 없거나, 이름은 있는데 변형을 못 찾음. */
	bool IsSilent(const SnareSound::FChangeCue& Cue, USnareSoundSubsystem* Sounds)
	{
		return Cue.Cue.IsNone() || Sounds->CountVariants(Cue.Cue) <= 0;
	}

	// 장비 행 빈 자리 목록. 손 칸 행 = 꺼내기, 그 밖 장비 행 = 옷 부스럭. `None`(장비 아님) 행은 안 본다.
	TArray<FString> SurveyGearRowGaps(const TArray<TPair<FName, FItemRow>>& Rows, USnareSoundSubsystem* Sounds)
	{
		TArray<FString> Gaps;
		for (const TPair<FName, FItemRow>& Pair : Rows)
		{
			const FItemRow& Row = Pair.Value;
			if (Row.EquipSlot == EEquipSlot::None)
			{
				continue;
			}
			const SnareSound::FChangeCue Cue = IsHandSlot(Row.EquipSlot) ? SnareSound::EquipCue(&Row) : SnareSound::OutfitCue(Row.EquipSlot);
			if (IsSilent(Cue, Sounds))
			{
				Gaps.Add(FString::Printf(TEXT("%s(부위 %d · 소리 %s)"), *Pair.Key.ToString(), static_cast<int32>(Row.EquipSlot), *Cue.Cue.ToString()));
			}
		}
		return Gaps;
	}

	// 표를 메모리 사본으로 — 역검증은 이 사본만 고치고 에셋은 건드리지 않는다.
	TArray<TPair<FName, FItemRow>> CopyItemRows(const UDataTable* Table)
	{
		TArray<TPair<FName, FItemRow>> Rows;
		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FItemRow* Row = Table->FindRow<FItemRow>(RowName, TEXT("EquipSoundGapTest")))
			{
				Rows.Emplace(RowName, *Row);
			}
		}
		return Rows;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNoSilentGearTest, "Snarehaven.Equipment.SoundGap.NoSilentGear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNoSilentGearTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, GearSoundItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), Table))
	{
		return false;
	}
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const TArray<TPair<FName, FItemRow>> Rows = CopyItemRows(Table);
	TestEqual(TEXT("사본 행 수 = 표 행 수(표를 못 읽어 0행 거짓 통과 막기)"), Rows.Num(), Table->GetRowNames().Num());

	int32 HandRows = 0;
	int32 OutfitRows = 0;
	for (const TPair<FName, FItemRow>& Pair : Rows)
	{
		if (Pair.Value.EquipSlot != EEquipSlot::None)
		{
			++(IsHandSlot(Pair.Value.EquipSlot) ? HandRows : OutfitRows);
		}
	}
	TestTrue(TEXT("손에 드는 행·옷 행이 다 있다(한쪽만 보고 통과 막기)"), HandRows > 0 && OutfitRows > 0);

	// ① 표의 장비 행 전부.
	const TArray<FString> RowGaps = SurveyGearRowGaps(Rows, Sounds);
	for (const FString& Gap : RowGaps)
	{
		AddError(FString::Printf(TEXT("장비 소리 빈 길: %s — SnareSound::EquipCue/OutfitCue 와 make_sounds.py 에 넣는다(DESIGN 2.2)"), *Gap));
	}

	// ② 옷 부위 전부 — 표에 아직 행이 없는 부위도. 손 칸은 행이 있어야 소리가 정해지므로 ①이 본다.
	const UEnum* const SlotEnum = StaticEnum<EEquipSlot>();
	int32 OutfitSlots = 0;
	TArray<FString> SlotGaps;
	for (int32 Index = 0; Index < SlotEnum->NumEnums() - 1; ++Index)   // 마지막은 _MAX
	{
		const EEquipSlot Slot = static_cast<EEquipSlot>(SlotEnum->GetValueByIndex(Index));
		if (Slot == EEquipSlot::None || IsHandSlot(Slot))
		{
			continue;
		}
		++OutfitSlots;
		if (IsSilent(SnareSound::OutfitCue(Slot), Sounds))
		{
			SlotGaps.Add(SlotEnum->GetNameStringByIndex(Index));
		}
	}
	for (const FString& Gap : SlotGaps)
	{
		AddError(FString::Printf(TEXT("옷 부위 소리 빈 길: %s — SnareSound::OutfitCue 에 넣는다(DESIGN 2.2)"), *Gap));
	}
	TestTrue(TEXT("옷 부위를 하나 이상 봤다"), OutfitSlots > 0);

	// ③ 빈손 넣기·탈것 타기/내리기.
	TestFalse(TEXT("빈손 넣기 소리가 실제로 있다"), IsSilent(SnareSound::EquipCue(nullptr), Sounds));
	TestFalse(TEXT("탈것 타기 소리가 실제로 있다"), IsSilent(SnareSound::MountCue(true), Sounds));
	TestFalse(TEXT("탈것 내리기 소리가 실제로 있다"), IsSilent(SnareSound::MountCue(false), Sounds));

	// 역검증: 사본에 소리 이름이 없는 가짜 종류(표에 없는 부위 값) 한 줄 → 정확히 그 줄 하나만 빈 길로 잡힌다.
	const FName FakeName(TEXT("Item_GearSoundGapFakeForTest"));
	FItemRow Fake;
	Fake.EquipSlot = static_cast<EEquipSlot>(SlotEnum->GetMaxEnumValue() + 1);
	TArray<TPair<FName, FItemRow>> Broken = Rows;
	Broken.Emplace(FakeName, Fake);
	const TArray<FString> BrokenGaps = SurveyGearRowGaps(Broken, Sounds);
	TestEqual(TEXT("역검증: 가짜 종류 한 줄만 더 잡힌다"), BrokenGaps.Num(), RowGaps.Num() + 1);
	TestTrue(TEXT("역검증: 잡힌 줄이 가짜 행이다"), BrokenGaps.Num() > 0 && BrokenGaps.Last().StartsWith(FakeName.ToString()));

	AddInfo(FString::Printf(TEXT("[GearSoundGaps] 손 행 %d · 옷 행 %d · 옷 부위 %d · 빈 길 %d · 역검증 → %s"),
		HandRows, OutfitRows, OutfitSlots, RowGaps.Num() + SlotGaps.Num(), *FString::Join(BrokenGaps, TEXT(", "))));
	return true;
}

#endif
