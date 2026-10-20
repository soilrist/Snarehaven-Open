// 스킨 조각 b (`docs/specs/skin-parts.md` 5장 1 · 6장 b) 2층 검사 — 꾸미기 「겉모습 덮개」.
//  ① 효과 모자(채굴 모자 — 불빛) 위에 꾸미기 모자를 입히면 **보이는 메시만** 꾸미기다. 장비 줄·효과 행·불빛은 그대로.
//  ② 덮인 채로 장비를 갈아 들거나 벗어도 보이는 것은 꾸미기 그대로, 꾸미기만 벗기면 장비 메시로 돌아온다.
//  ③ 꾸미기 행은 장비로 안 들고, 장비 행·부위가 다른 행은 꾸미기로 안 입는다.
//  ④ 꾸미기 목록이 복제 대상이고, 호스트 목록을 받은 남의 화면도 같은 메시를 건다.
// 꾸미기 행은 아직 표에 없다(메시는 조각 f~l) — 표 사본에 **검사 전용 행**을 하나 더한다 (Verification.md 6).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Net/UnrealNetwork.h"
#include "SnarehavenCharacter.h"

namespace SnareCosmeticTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const FName MiningHatRow(TEXT("Item_MiningHat"));
	const FName RunnerHatRow(TEXT("Item_RunnerHat"));
	/** 검사 전용 꾸미기 행 — 요리 모자 메시를 빌린다(효과 모자들과 메시가 달라 구별된다). */
	const FName CosmeticHatRow(TEXT("Item_TestCosmeticHat"));
	const FName MeshSourceRow(TEXT("Item_CookHat"));

	/**
	 * 표 사본(검사 전용 꾸미기 행 하나 더)을 건 플레이어. 원본 표는 건드리지 않는다 — 사본을 장비 컴포넌트의
	 * `ItemTable` 칸에 반사로 꽂는다(게임 코드에 검사용 문을 안 낸다).
	 */
	struct FRig
	{
		UWorld* World = nullptr;
		UDataTable* Table = nullptr;
		UStaticMesh* CosmeticMesh = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			const UDataTable* const Original = LoadObject<UDataTable>(nullptr, ItemTablePath);
			const FItemRow* const Source = Original != nullptr ? Original->FindRow<FItemRow>(MeshSourceRow, TEXT("CosmeticTest")) : nullptr;
			if (Source == nullptr)
			{
				return;
			}
			Table = DuplicateObject<UDataTable>(Original, GetTransientPackage());
			FItemRow Cosmetic = *Source;
			Cosmetic.ItemID = TEXT("test.cosmetic_hat");
			Cosmetic.ItemType = EItemType::Cosmetic;
			Table->AddRow(CosmeticHatRow, Cosmetic);
			CosmeticMesh = Cosmetic.EquipMeshRef.LoadSynchronous();
		}

		ASnarehavenCharacter* SpawnPlayer() const
		{
			ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
			if (Player == nullptr || !Player->ApplyPlayerBody() || Table == nullptr)
			{
				return nullptr;
			}
			UEquipmentComponent* const Equipment = Player->GetEquipment();
			const FObjectProperty* const TableProp =
				FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
			if (Equipment == nullptr || TableProp == nullptr)
			{
				return nullptr;
			}
			TableProp->SetObjectPropertyValue_InContainer(Equipment, Table);
			return Player;
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	const UStaticMesh* ShownHeadMesh(const UEquipmentComponent* Equipment)
	{
		const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
		return Visual != nullptr ? Visual->GetStaticMesh().Get() : nullptr;
	}

	bool HeadLightOn(const UEquipmentComponent* Equipment)
	{
		const USpotLightComponent* const Light = Equipment->GetHeadLight();
		return Light != nullptr && Light->IsVisible() && Light->Intensity > 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticCoverTest, "Snarehaven.Item.Cosmetic.CoversVisualKeepsEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticCoverTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticTest;

	// 열거형은 끝에만 붙었다 — 표·세이브에 저장된 앞 순번이 그대로다.
	TestEqual(TEXT("EItemType::PetCosmetic 순번 그대로"), static_cast<int32>(EItemType::PetCosmetic), 5);
	TestEqual(TEXT("EItemType::Cosmetic 은 끝"), static_cast<int32>(EItemType::Cosmetic), 6);
	TestEqual(TEXT("EEquipSlot::Head 순번 그대로"), static_cast<int32>(EEquipSlot::Head), 6);
	TestEqual(TEXT("EEquipSlot 새 칸 셋은 끝"), static_cast<int32>(EEquipSlot::Gloves), 9);

	FRig Rig;
	ASnarehavenCharacter* const Player = Rig.SpawnPlayer();
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Player) || !TestNotNull(TEXT("꾸미기 메시"), Rig.CosmeticMesh))
	{
		return false;
	}
	UEquipmentComponent* const Equipment = Player->GetEquipment();

	if (!TestTrue(TEXT("채굴 모자를 들었다"), Equipment->EquipItem(MiningHatRow)))
	{
		return false;
	}
	const UStaticMesh* const MiningMesh = ShownHeadMesh(Equipment);
	TestNotNull(TEXT("채굴 모자 메시가 보인다"), MiningMesh);
	TestNotEqual(TEXT("검사 꾸미기 메시는 채굴 모자와 다르다"), static_cast<const UStaticMesh*>(Rig.CosmeticMesh), MiningMesh);
	TestTrue(TEXT("채굴 모자 → 불빛 켜짐"), HeadLightOn(Equipment));

	// ① 덮기 — 보이는 것만 바뀐다.
	TestTrue(TEXT("꾸미기 모자를 덮었다"), Equipment->SetCosmetic(EEquipSlot::Head, CosmeticHatRow));
	TestEqual(TEXT("꾸미기 목록에 들었다"), Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);
	TestEqual(TEXT("보이는 메시 = 꾸미기"), ShownHeadMesh(Equipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	TestEqual(TEXT("장비 줄은 채굴 모자 그대로"), Equipment->GetEquippedItem(EEquipSlot::Head), MiningHatRow);
	const FItemRow* const EffectRow = Equipment->GetEquippedRow(EEquipSlot::Head);
	TestTrue(TEXT("효과 행 = 채굴 모자(불빛 값 있음)"), EffectRow != nullptr && EffectRow->HeadLightIntensityCd > 0.f);
	TestTrue(TEXT("덮어도 불빛(효과)은 켜짐"), HeadLightOn(Equipment));
	TestFalse(TEXT("덮은 머리엔 머리카락이 안 보인다"), Equipment->IsHairShown());

	// ③ 종류 착오는 받지 않는다.
	TestFalse(TEXT("꾸미기 행은 장비로 안 든다"), Equipment->EquipItem(CosmeticHatRow));
	TestEqual(TEXT("장비 줄은 그대로"), Equipment->GetEquippedItem(EEquipSlot::Head), MiningHatRow);
	TestFalse(TEXT("장비 행은 꾸미기로 안 입는다"), Equipment->SetCosmetic(EEquipSlot::Head, RunnerHatRow));
	TestFalse(TEXT("부위가 다른 칸엔 안 입는다"), Equipment->SetCosmetic(EEquipSlot::Weapon, CosmeticHatRow));
	TestEqual(TEXT("꾸미기는 그대로"), Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);

	// ② 덮인 채 장비를 바꿔도 보이는 것은 꾸미기, 효과만 바뀐다.
	TestTrue(TEXT("달리기 모자로 갈아 듦"), Equipment->EquipItem(RunnerHatRow));
	TestEqual(TEXT("갈아 들어도 보이는 메시 = 꾸미기"), ShownHeadMesh(Equipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	TestEqual(TEXT("장비 줄 = 달리기 모자"), Equipment->GetEquippedItem(EEquipSlot::Head), RunnerHatRow);
	TestFalse(TEXT("달리기 모자 → 불빛 꺼짐"), HeadLightOn(Equipment));
	TestTrue(TEXT("다시 채굴 모자"), Equipment->EquipItem(MiningHatRow));
	TestTrue(TEXT("덮인 채 채굴 모자 → 불빛 켜짐"), HeadLightOn(Equipment));
	TestEqual(TEXT("그래도 보이는 메시 = 꾸미기"), ShownHeadMesh(Equipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));

	TestTrue(TEXT("장비 모자를 벗었다"), Equipment->UnequipSlot(EEquipSlot::Head));
	TestEqual(TEXT("벗어도 꾸미기가 보인다"), ShownHeadMesh(Equipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	TestFalse(TEXT("벗으면 불빛 꺼짐"), HeadLightOn(Equipment));
	TestFalse(TEXT("빈 장비 칸을 또 벗기면 「뗐다」 가 아니다"), Equipment->UnequipSlot(EEquipSlot::Head));

	TestTrue(TEXT("채굴 모자를 다시 들었다"), Equipment->EquipItem(MiningHatRow));
	TestTrue(TEXT("꾸미기만 벗겼다"), Equipment->SetCosmetic(EEquipSlot::Head, NAME_None));
	TestEqual(TEXT("꾸미기를 벗기면 장비 메시로 돌아온다"), ShownHeadMesh(Equipment), MiningMesh);
	TestTrue(TEXT("불빛은 그대로 켜짐"), HeadLightOn(Equipment));
	TestFalse(TEXT("빈 꾸미기 칸을 또 벗기면 false"), Equipment->SetCosmetic(EEquipSlot::Head, NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCosmeticReplicationTest, "Snarehaven.Item.Cosmetic.GuestSeesSameCover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCosmeticReplicationTest::RunTest(const FString& Parameters)
{
	using namespace SnareCosmeticTest;

	// 복제용 `RepIndex` 는 UClass 가 늦게 채운다 — 먼저 세운다 (`SnarehavenEquipmentAttachTest` 와 같은 까닭).
	UEquipmentComponent::StaticClass()->SetUpRuntimeReplicationData();
	const FProperty* const CosmeticProp = UEquipmentComponent::StaticClass()->FindPropertyByName(TEXT("CosmeticSlots"));
	if (TestNotNull(TEXT("CosmeticSlots 프로퍼티가 있다"), CosmeticProp))
	{
		TestTrue(TEXT("CosmeticSlots 가 복제 플래그를 달았다"), CosmeticProp->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("CosmeticSlots 복제 알림"), CosmeticProp->RepNotifyFunc, FName(TEXT("OnRep_EquippedSlots")));
		TArray<FLifetimeProperty> Props;
		NewObject<UEquipmentComponent>()->GetLifetimeReplicatedProps(Props);
		TestTrue(TEXT("CosmeticSlots 가 복제 목록에 있다"),
			Props.ContainsByPredicate([CosmeticProp](const FLifetimeProperty& Lifetime)
				{ return Lifetime.RepIndex == CosmeticProp->RepIndex; }));
	}

	FRig Rig;
	ASnarehavenCharacter* const Host = Rig.SpawnPlayer();
	ASnarehavenCharacter* const Guest = Rig.SpawnPlayer();
	if (!TestNotNull(TEXT("호스트"), Host) || !TestNotNull(TEXT("게스트"), Guest))
	{
		return false;
	}
	UEquipmentComponent* const HostEquipment = Host->GetEquipment();
	UEquipmentComponent* const GuestEquipment = Guest->GetEquipment();
	TestTrue(TEXT("호스트가 채굴 모자를 들었다"), HostEquipment->EquipItem(MiningHatRow));
	TestTrue(TEXT("호스트가 꾸미기를 덮었다"), HostEquipment->SetCosmetic(EEquipSlot::Head, CosmeticHatRow));

	// 게스트 역할에서는 요청만 간다 — 넷드라이버가 없어 자기 목록이 그대로 비면 「혼자 안 고쳤다」.
	Guest->SetRole(ROLE_AutonomousProxy);
	TestFalse(TEXT("게스트가 덮으면 요청만"), GuestEquipment->SetCosmetic(EEquipSlot::Head, CosmeticHatRow));
	TestEqual(TEXT("게스트 꾸미기 목록은 비어 있다"), GuestEquipment->GetCosmeticSlots().Num(), 0);

	// 호스트 목록이 내려온다(게임에서는 OnRep 이 이 길) — 장비 줄 먼저, 꾸미기 다음 어느 순서든 같은 화면.
	Guest->SetRole(ROLE_SimulatedProxy);
	GuestEquipment->ApplyCosmeticSlots(HostEquipment->GetCosmeticSlots());
	GuestEquipment->ApplyEquippedSlots(HostEquipment->GetEquippedSlots());
	TestEqual(TEXT("남의 화면 보이는 메시 = 꾸미기"), ShownHeadMesh(GuestEquipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	TestEqual(TEXT("남의 화면 장비 줄 = 채굴 모자"), GuestEquipment->GetEquippedItem(EEquipSlot::Head), MiningHatRow);
	TestTrue(TEXT("남의 화면 불빛도 켜짐"), HeadLightOn(GuestEquipment));

	TestTrue(TEXT("호스트가 꾸미기를 벗겼다"), HostEquipment->SetCosmetic(EEquipSlot::Head, NAME_None));
	GuestEquipment->ApplyCosmeticSlots(HostEquipment->GetCosmeticSlots());
	TestEqual(TEXT("남의 화면도 장비 메시로 돌아온다"), ShownHeadMesh(GuestEquipment), ShownHeadMesh(HostEquipment));
	TestNotEqual(TEXT("그건 꾸미기 메시가 아니다"), ShownHeadMesh(GuestEquipment), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	return true;
}

#endif
