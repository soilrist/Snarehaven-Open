// ROADMAP M16-03 2층 검사 — 전기톱 되풀이 소리는 **벨 때만** 나고 동작이 끝나면 멈춘다(사람 2026-10-05 「소리가 계속 나고 안 멈춘다」 —
// 들고만 있어도 공회전이 돌던 것), 벗으면 바로 끊기고, 손도끼는 아무것도 안 켠다.
// 판정은 소리 이름 문자열이 아니라 실제 오디오 컴포넌트의 `IsPlaying()` 이다 (PROMPT 12).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/ItemRow.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "Sound/SoundWave.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Building/SnarePlacedStructureActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHoldLoopTest, "Snarehaven.Equipment.HoldLoop.ChainsawSoundOnlyWhileCutting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHoldLoopTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	if (TestNotNull(TEXT("플레이어와 장비 컴포넌트"), Equipment) && TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		TestFalse(TEXT("맨손이면 되풀이 소리가 없다"), Equipment->IsHoldLoopPlaying());

		TestTrue(TEXT("손도끼를 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
		TestFalse(TEXT("손도끼는 되풀이 소리를 안 켠다"), Equipment->IsHoldLoopPlaying());
		TestEqual(TEXT("손도끼 되풀이 소리 이름"), Equipment->GetHoldLoopCue(), NAME_None);

		// 소리 장치가 없는 실행(`-NoSound`)에선 오디오 컴포넌트가 안 생겨 "켜졌다" 쪽을 잴 수 없다 — 그때는 경고로 알리고
		// 켜짐 판정만 건너뛴다. 장치 유무는 명령줄 문자열이 아니라 이 월드의 실제 오디오 장치로 본다 (PROMPT 12, 루프#291).
		const bool bHasAudioDevice = World->GetAudioDeviceRaw() != nullptr;
		if (!bHasAudioDevice)
		{
			AddWarning(TEXT("소리 장치가 없다(-NoSound?) — 전기톱 공회전이 켜지는지는 이번 실행에서 못 잰다"));
		}

		constexpr float FrameSeconds = 1.f / 60.f;
		const auto TickFrames = [&](int32 Frames)
			{
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					Equipment->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
				}
			};

		TestTrue(TEXT("전기톱을 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw"))));
		TestEqual(TEXT("전기톱 되풀이 소리 이름"), Equipment->GetHoldLoopCue(), FName(TEXT("Chainsaw_Idle")));
		TickFrames(30);
		TestFalse(TEXT("들고만 있으면 소리가 안 난다"), Equipment->IsHoldLoopPlaying());

		// 베기 한 번 = 쓰기 클립 길이(전기톱 1.5초) 동안만 — 클립 길이는 표 값이라 시간 대신 「끝난 뒤」 로 잰다.
		Player->PlayItemAction(EItemAction::Chop);
		TickFrames(1);
		const bool bDuringCut = Equipment->IsHoldLoopPlaying();
		if (bHasAudioDevice)
		{
			TestTrue(TEXT("벨 때 소리가 난다"), bDuringCut);
		}
		TickFrames(60 * 4);
		const bool bAfterCut = Equipment->IsHoldLoopPlaying();
		AddInfo(FString::Printf(TEXT("[HoldLoop] 전기톱 들기만 → 소리 0 · 벨 때 %d · 4초 뒤 %d"), bDuringCut ? 1 : 0, bAfterCut ? 1 : 0));
		TestFalse(TEXT("동작이 끝나면 소리가 멈춘다"), bAfterCut);

		Player->PlayItemAction(EItemAction::Chop);
		TickFrames(1);
		TestTrue(TEXT("전기톱을 벗는다"), Equipment->UnequipSlot(EEquipSlot::Tool));
		TestFalse(TEXT("벨 때 벗으면 바로 멈춘다"), Equipment->IsHoldLoopPlaying());

		TestTrue(TEXT("다시 전기톱"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw"))));
		Player->PlayItemAction(EItemAction::Chop);
		TickFrames(1);
		TestTrue(TEXT("손도끼로 바꿔 들기"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
		TestFalse(TEXT("바꿔 들면 소리가 멈춘다"), Equipment->IsHoldLoopPlaying());
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// INBOX 2026-10-05 18:20 2층 검사 — 한 번 트는 소리(아이템 `UseSoundCue`)의 웨이브가 되풀이로 반입되면 폰에서 영영 안 멈춘다
// (`SFX_Chainsaw_Rev_01` 이 `import_audio.py` 의 `SFX_Chainsaw_` 접두에 걸려 looping=True — 폰 로그 「orphaned sound which is not a one-shot」).
// 되풀이 웨이브는 배경(`Amb_`)·릴 감기(`Reel_`)·든 동안 소리(행 `HoldLoopSoundCue`)·켜진 가구 소리(행 `PlacedOnLoopCue`, M108-08d-2 라디오)·
// 도는 발전기 공회전(`ASnarePlacedStructureActor::GetGeneratorLoopCue`, M108-07d)뿐이어야 한다. 판정은 웨이브 에셋의 실제 `bLooping` 값.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOneShotNotLoopingTest, "Snarehaven.Audio.Waves.OneShotsDoNotLoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOneShotNotLoopingTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	TSet<FString> UseCues;
	TSet<FString> HoldCues;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && !Row->UseSoundCue.IsNone())
		{
			UseCues.Add(Row->UseSoundCue.ToString());
		}
		if (Row != nullptr && !Row->HoldLoopSoundCue.IsNone())
		{
			HoldCues.Add(Row->HoldLoopSoundCue.ToString());
		}
		if (Row != nullptr && !Row->PlacedOnLoopCue.IsNone())
		{
			HoldCues.Add(Row->PlacedOnLoopCue.ToString());
		}
	}
	HoldCues.Add(ASnarePlacedStructureActor::GetGeneratorLoopCue().ToString());

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(TEXT("/Game/Game/Audio/SFX")), Assets, /*bRecursive=*/true);
	int32 Waves = 0;
	TArray<FString> Looping;
	TArray<FString> Wrong;
	for (const FAssetData& Asset : Assets)
	{
		const USoundWave* const Wave = Cast<USoundWave>(Asset.GetAsset());
		if (Wave == nullptr)
		{
			continue;
		}
		++Waves;
		// 이름 = SFX_<소리 이름>_NN (`USnareSoundSubsystem::LoadVariants`).
		FString Cue = Wave->GetName();
		Cue.RemoveFromStart(TEXT("SFX_"));
		const int32 Under = Cue.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Cue = Under > 0 ? Cue.Left(Under) : Cue;
		if (!Wave->bLooping)
		{
			continue;
		}
		Looping.Add(Wave->GetName());
		const bool bAllowed = !UseCues.Contains(Cue)
			&& (Cue.StartsWith(TEXT("Amb_")) || Cue.StartsWith(TEXT("Reel")) || HoldCues.Contains(Cue));
		if (!bAllowed)
		{
			Wrong.Add(Wave->GetName());
		}
	}
	AddInfo(FString::Printf(TEXT("[Loop] 웨이브 %d개 · 되풀이 %d개 [%s] · 한 번 소리인데 되풀이 %d개 [%s]"), Waves, Looping.Num(),
		*FString::Join(Looping, TEXT(", ")), Wrong.Num(), *FString::Join(Wrong, TEXT(", "))));
	TestTrue(TEXT("SFX 웨이브를 찾았다"), Waves > 0);
	TestEqual(TEXT("한 번 트는 소리 웨이브가 되풀이로 반입되지 않았다"), Wrong.Num(), 0);
	return true;
}

namespace SnarehavenHoldLoopTest
{
	/** 쥐는 자리가 손목에서 벗어나도 되는 한도 — 벨 때 떨림 진폭(상대 0.45 × 몸 배율)에 1cm 여유. */
	float ToolGripGapMaxCm(const ASnarehavenCharacter* Player)
	{
		const UEquipmentComponent* const Equipment = Player->GetEquipment();
		return Equipment->GetToolVibrationAmplitudeCm() * static_cast<float>(Player->GetMesh()->GetComponentScale().GetMax()) + 1.f;
	}
}
using SnarehavenHoldLoopTest::ToolGripGapMaxCm;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChainsawShakeTest, "Snarehaven.Equipment.HoldLoop.ChainsawShakesHarderWhenCutting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChainsawShakeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	if (TestNotNull(TEXT("플레이어와 장비 컴포넌트"), Equipment) && TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		constexpr float FrameSeconds = 1.f / 60.f;
		// 든 직후(틱 전) 상대 자리 = 쥐는 자리. 한 프레임 굴리고 그 자리에서 벗어난 거리를 잰다.
		FVector Rest = FVector::ZeroVector;
		const auto TakeRest = [&]()
		{
			const UStaticMeshComponent* const Tool = Equipment->GetEquippedMesh(EEquipSlot::Tool);
			Rest = IsValid(Tool) ? Tool->GetRelativeLocation() : FVector::ZeroVector;
		};
		auto MeasureOffsetCm = [&]()
		{
			Equipment->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
			const UStaticMeshComponent* const Tool = Equipment->GetEquippedMesh(EEquipSlot::Tool);
			return IsValid(Tool) ? static_cast<float>((Tool->GetRelativeLocation() - Rest).Size()) : -1.f;
		};

		TestTrue(TEXT("손도끼를 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
		TakeRest();
		const float HatchetCm = MeasureOffsetCm();
		TestEqual(TEXT("손도끼는 안 떤다"), HatchetCm, 0.f, 1e-4f);

		// 모터 도구는 이름이 아니라 행의 `HoldLoopSoundCue` 칸으로 가른다 — 전기 곡괭이도 같은 칸으로 떤다 (M19-09).
		struct FMotorTool { const TCHAR* ItemID; EItemAction Action; };
		for (const FMotorTool& Motor : { FMotorTool{ TEXT("item.chainsaw"), EItemAction::Chop }, FMotorTool{ TEXT("item.pickaxe_electric"), EItemAction::Mine } })
		{
			TestTrue(FString::Printf(TEXT("[%s] 든다"), Motor.ItemID), Equipment->EquipItemByItemID(FName(Motor.ItemID)));
			TakeRest();
			const float IdleCm = MeasureOffsetCm();

			Player->PlayItemAction(Motor.Action);
			const float CutCm = MeasureOffsetCm();

			// 쓰는 동작(0.6초 안팎)이 끝나면 다시 공회전 진폭으로 내려온다.
			for (int32 Frame = 0; Frame < 90; ++Frame)
			{
				Equipment->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
			}
			const float AfterCm = MeasureOffsetCm();

			// 떨려도 쥐는 자리(행 `GripOffset` 점)는 손목 소켓에 붙어 있다 — 떨림이 자리를 0 으로 덮으면
			// 전기톱이 손목에서 약 22cm 밀려 몸에 묻혔다(INBOX 2026-10-05 18:20 「손에 전기톱이 안 보인다」).
			const UStaticMeshComponent* const Held = Equipment->GetEquippedMesh(EEquipSlot::Tool);
			const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
			if (IsValid(Held) && Row != nullptr && Row->GripOffset.IsSet())
			{
				const FVector GripWorld = Held->GetComponentTransform().TransformPosition(Row->GripOffset.Location);
				const FVector Socket = Player->GetMesh()->GetSocketLocation(Held->GetAttachSocketName());
				const float GripGapCm = static_cast<float>(FVector::Dist(GripWorld, Socket));
				const float GapMaxCm = ToolGripGapMaxCm(Player);
				AddInfo(FString::Printf(TEXT("[Grip] %s 90프레임 뒤 쥐는 자리 ↔ 손목 소켓 %.2fcm (한도 %.2f)"), Motor.ItemID, GripGapCm, GapMaxCm));
				TestTrue(FString::Printf(TEXT("[%s] 떨려도 쥐는 자리가 손목에 %.2f ≤ %.2fcm"), Motor.ItemID, GripGapCm, GapMaxCm),
					GripGapCm <= GapMaxCm);
			}

			AddInfo(FString::Printf(TEXT("[Shake] 손도끼 %.3fcm · %s 공회전 %.3fcm · 쓸 때 %.3fcm · 쓴 뒤 %.3fcm"),
				HatchetCm, Motor.ItemID, IdleCm, CutCm, AfterCm));
			TestTrue(FString::Printf(TEXT("[%s] 공회전 진폭 > 0"), Motor.ItemID), IdleCm > 0.f);
			TestTrue(FString::Printf(TEXT("[%s] 쓸 때 진폭 > 공회전 진폭"), Motor.ItemID), CutCm > IdleCm);
			TestEqual(FString::Printf(TEXT("[%s] 쓴 뒤엔 공회전 진폭으로"), Motor.ItemID), AfterCm, IdleCm, 1e-3f);

			TestTrue(FString::Printf(TEXT("[%s] 벗는다"), Motor.ItemID), Equipment->UnequipSlot(EEquipSlot::Tool));
			TestEqual(FString::Printf(TEXT("[%s] 벗으면 떨림 0"), Motor.ItemID), Equipment->GetToolVibrationAmplitudeCm(), 0.f);
		}

		TestTrue(TEXT("다시 손도끼"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
		TakeRest();
		TestEqual(TEXT("모터 도구 뒤에 든 손도끼도 안 떤다"), MeasureOffsetCm(), 0.f, 1e-4f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// INBOX 2026-10-05 18:20 ⑶ 2층 검사 — 폰에서 「손에 전기톱이 안 보이고, 무기도 안 보인다」. 손에 드는 행(`Weapon`·`Tool`) 전부를
// 실제 몸에 들고 90프레임 굴린 뒤(틱이 쥐는 자리를 지운 #469 같은 일을 잡으려고) 메시·보임·재질·크기·쥐는 자리를 잰다.
// 그리고 무기를 든 채 같은 손 도구(전기톱)를 들면 무기만 숨고, 도구를 내리면 무기가 다시 보여야 한다(감독 09-25 한 손 나눔).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHeldItemsVisibleTest, "Snarehaven.Equipment.HoldLoop.EveryHeldItemShowsOnHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHeldItemsVisibleTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	if (TestNotNull(TEXT("플레이어와 장비 컴포넌트"), Equipment) && TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		constexpr float FrameSeconds = 1.f / 60.f;
		const auto Tick90 = [&]()
		{
			for (int32 Frame = 0; Frame < 90; ++Frame)
			{
				Equipment->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
			}
		};
		/** 그 자리 메시가 보이는 상태인가 — 이유를 글로 돌려준다(빈 문자열 = 보인다). */
		const auto WhyHidden = [&](EEquipSlot Slot, const FItemRow& Row) -> FString
		{
			const UStaticMeshComponent* const Held = Equipment->GetEquippedMesh(Slot);
			if (!IsValid(Held) || Held->GetStaticMesh() == nullptr)
			{
				return TEXT("메시 없음");
			}
			if (!Held->IsVisible() || Held->bHiddenInGame || !Held->IsRegistered())
			{
				return TEXT("숨김");
			}
			for (int32 Index = 0; Index < Held->GetNumMaterials(); ++Index)
			{
				const UMaterialInterface* const Material = Held->GetMaterial(Index);
				if (Material == nullptr || Material->GetMaterial() == UMaterial::GetDefaultMaterial(MD_Surface))
				{
					return FString::Printf(TEXT("재질 %d 빈 칸/격자"), Index);
				}
			}
			const FBox Box = Held->Bounds.GetBox();
			const float SizeCm = static_cast<float>(Box.GetSize().GetMax());
			if (SizeCm < 10.f)
			{
				return FString::Printf(TEXT("너무 작음 %.1fcm"), SizeCm);
			}
			// 몸(캡슐) 밖으로 나온 데가 하나도 없으면 몸에 묻혀 안 보인다.
			const UCapsuleComponent* const Capsule = Player->GetCapsuleComponent();
			const FVector Center = Capsule->GetComponentLocation();
			const float Radius = Capsule->GetScaledCapsuleRadius();
			const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			bool bSticksOut = false;
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector Point((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
				bSticksOut |= FVector::Dist2D(Point, Center) > Radius || FMath::Abs(Point.Z - Center.Z) > HalfHeight;
			}
			if (!bSticksOut)
			{
				return TEXT("몸 안에 묻힘");
			}
			if (Row.GripOffset.IsSet())
			{
				const FVector GripWorld = Held->GetComponentTransform().TransformPosition(Row.GripOffset.Location);
				const float GapCm = static_cast<float>(FVector::Dist(GripWorld, Player->GetMesh()->GetSocketLocation(Held->GetAttachSocketName())));
				if (GapCm > ToolGripGapMaxCm(Player))
				{
					return FString::Printf(TEXT("쥐는 자리가 손목에서 %.2fcm"), GapCm);
				}
			}
			return FString();
		};

		int32 Checked = 0;
		TArray<FString> Wrong;
		ItemTable->ForeachRow<FItemRow>(TEXT("HeldVisible"), [&](const FName& RowName, const FItemRow& Row)
		{
			if ((Row.EquipSlot != EEquipSlot::Weapon && Row.EquipSlot != EEquipSlot::Tool) || Row.EquipMeshRef.IsNull())
			{
				return;
			}
			Equipment->UnequipSlot(EEquipSlot::Weapon);
			Equipment->UnequipSlot(EEquipSlot::Tool);
			if (!Equipment->EquipItem(RowName))
			{
				Wrong.Add(FString::Printf(TEXT("%s(못 듦)"), *RowName.ToString()));
				return;
			}
			Tick90();
			++Checked;
			const FString Why = WhyHidden(Row.EquipSlot, Row);
			if (!Why.IsEmpty())
			{
				Wrong.Add(FString::Printf(TEXT("%s(%s)"), *RowName.ToString(), *Why));
			}
		});
		AddInfo(FString::Printf(TEXT("[Held] 손에 드는 행 %d개 들고 90프레임 · 안 보이는 것 %d개 [%s]"), Checked, Wrong.Num(), *FString::Join(Wrong, TEXT(", "))));
		TestTrue(TEXT("손에 드는 행을 찾았다"), Checked > 0);
		TestEqual(TEXT("손에 드는 것 전부가 손에 보인다"), Wrong.Num(), 0);

		// 무기만 든 상태 → 전기톱(같은 오른손) → 내림. 폰의 「베기」 자동 들기와 같은 `EquipItem` 길이다.
		Equipment->UnequipSlot(EEquipSlot::Weapon);
		Equipment->UnequipSlot(EEquipSlot::Tool);
		const FItemRow* const Club = ItemTable->FindRow<FItemRow>(FName(TEXT("Item_Club")), TEXT("HeldVisible"));
		if (TestNotNull(TEXT("몽둥이 행"), Club) && TestTrue(TEXT("몽둥이만 든다"), Equipment->EquipItem(FName(TEXT("Item_Club")))))
		{
			Tick90();
			const FString ClubOnly = WhyHidden(EEquipSlot::Weapon, *Club);
			TestTrue(FString::Printf(TEXT("무기만 들면 보인다 [%s]"), *ClubOnly), ClubOnly.IsEmpty());

			TestTrue(TEXT("전기톱을 같이 든다"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw"))));
			Tick90();
			const UStaticMeshComponent* const Weapon = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
			const bool bWeaponShownWithSaw = IsValid(Weapon) && Weapon->IsVisible();
			TestTrue(TEXT("전기톱이 오른손을 쓰면 무기는 숨는다"), IsValid(Weapon) && !bWeaponShownWithSaw);
			const FItemRow* const Saw = Equipment->GetEquippedRow(EEquipSlot::Tool);
			const FString SawWhy = Saw != nullptr ? WhyHidden(EEquipSlot::Tool, *Saw) : FString(TEXT("행 없음"));
			TestTrue(FString::Printf(TEXT("무기와 함께여도 전기톱은 보인다 [%s]"), *SawWhy), SawWhy.IsEmpty());

			TestTrue(TEXT("전기톱을 내린다"), Equipment->UnequipSlot(EEquipSlot::Tool));
			Tick90();
			const FString ClubAgain = WhyHidden(EEquipSlot::Weapon, *Club);
			AddInfo(FString::Printf(TEXT("[Held] 몽둥이만 [%s] · 전기톱 든 동안 무기 보임=%d · 전기톱 [%s] · 내린 뒤 몽둥이 [%s]"),
				*ClubOnly, bWeaponShownWithSaw ? 1 : 0, *SawWhy, *ClubAgain));
			TestTrue(FString::Printf(TEXT("도구를 내리면 무기가 다시 보인다 [%s]"), *ClubAgain), ClubAgain.IsEmpty());
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
