// 스킨 조각 d (`docs/specs/skin-parts.md` 5장 4 · 6장 d) 2층 검사 — 꾸미기는 서버 `SetOutfit` 이 받아들인 뒤에만 입는다.
//  ① 요청 = `SetOutfit{slot, itemId}` (칸 이름은 서버 `OUTFIT_SLOTS` 와 같다) · 답 전엔 겉모습 그대로
//  ② 거절(안 산 꾸미기)이면 그대로 · 받아들이면 덮개가 걸린다 · 벗기도 답 뒤에만
//  ③ 손에 드는 칸은 꾸미기 칸이 아니라 묻지도 않는다
// 가짜 서버(`TestServerOverride`)로 진짜 응답 길을 태운다. 꾸미기 행은 아직 표에 없다(메시는 조각 f~l) —
// 표 사본에 **검사 전용 행**을 하나 더한다 (Verification.md 6, `SnarehavenCosmeticCoverTest` 와 같은 방식).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Inventory/BackpackComponent.h"
#include "SnarehavenCharacter.h"

namespace SnareOutfitRequestTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const FName CosmeticHatRow(TEXT("Item_TestCosmeticHat"));
	const FName CosmeticHatID(TEXT("test.cosmetic_hat"));
	const FName MeshSourceRow(TEXT("Item_CookHat"));

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 부른 함수·인자를 적어 두고 답은 검사가 줄 때까지 붙잡는다 (`SnarehavenBackpackTest` 의 가짜 서버와 같은 꼴). */
	struct FFakeServer
	{
		FString LastFunction;
		TSharedPtr<FJsonObject> LastArgs;
		TArray<FDone> Waiting;
		int32 Calls = 0;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++Calls;
					LastFunction = Name;
					LastArgs = Args;
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void Answer(const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(true, Response, FString());
		}

		void AnswerFail(const FString& Code, const FString& Message)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), Message);
			Response->SetStringField(TEXT("code"), Code);
			Answer(Response);
		}

		FString LastArg(const TCHAR* Field) const
		{
			return LastArgs.IsValid() ? LastArgs->GetStringField(Field) : FString(TEXT("?"));
		}
	};

	/** 표 사본(검사 전용 꾸미기 행 하나 더)을 건 플레이어 + 백엔드에 묶은 배낭 컴포넌트. */
	struct FRig
	{
		UWorld* World = nullptr;
		UDataTable* Table = nullptr;
		UStaticMesh* CosmeticMesh = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		UEquipmentComponent* Equipment = nullptr;
		UBackpackComponent* Backpack = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			const UDataTable* const Original = LoadObject<UDataTable>(nullptr, ItemTablePath);
			const FItemRow* const Source = Original != nullptr ? Original->FindRow<FItemRow>(MeshSourceRow, TEXT("OutfitTest")) : nullptr;
			if (Source == nullptr)
			{
				return;
			}
			Table = DuplicateObject<UDataTable>(Original, GetTransientPackage());
			FItemRow Cosmetic = *Source;
			Cosmetic.ItemID = CosmeticHatID;
			Cosmetic.ItemType = EItemType::Cosmetic;
			Table->AddRow(CosmeticHatRow, Cosmetic);
			CosmeticMesh = Cosmetic.EquipMeshRef.LoadSynchronous();

			Player = World->SpawnActor<ASnarehavenCharacter>();
			if (Player == nullptr || !Player->ApplyPlayerBody())
			{
				Player = nullptr;
				return;
			}
			Equipment = Player->GetEquipment();
			const FObjectProperty* const TableProp =
				FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
			if (Equipment == nullptr || TableProp == nullptr)
			{
				Player = nullptr;
				return;
			}
			TableProp->SetObjectPropertyValue_InContainer(Equipment, Table);

			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			Backpack = Player->FindComponentByClass<UBackpackComponent>();
			if (Backpack == nullptr)
			{
				Backpack = NewObject<UBackpackComponent>(Player, TEXT("OutfitTestBackpack"));
				Backpack->RegisterComponent();
			}
			Backpack->BindTargets(Backend, nullptr);
		}

		const UStaticMesh* ShownHeadMesh() const
		{
			const UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Head);
			return Visual != nullptr ? Visual->GetStaticMesh().Get() : nullptr;
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutfitRequestTest, "Snarehaven.Item.Cosmetic.OutfitWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutfitRequestTest::RunTest(const FString& Parameters)
{
	using namespace SnareOutfitRequestTest;

	// 칸 이름 = 서버 `OUTFIT_SLOTS` (tools/playfab/cloudscript_main.js) — 한 줄씩 맞댄다.
	TestEqual(TEXT("머리"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::Head), FString(TEXT("head")));
	TestEqual(TEXT("얼굴 장식"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::FaceAccessory), FString(TEXT("face")));
	TestEqual(TEXT("상의"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::OutfitTop), FString(TEXT("top")));
	TestEqual(TEXT("하의"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::OutfitBottom), FString(TEXT("bottom")));
	TestEqual(TEXT("신발"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::Shoes), FString(TEXT("shoes")));
	TestEqual(TEXT("장갑"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::Gloves), FString(TEXT("gloves")));
	TestEqual(TEXT("배낭"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::Backpack), FString(TEXT("back")));
	TestTrue(TEXT("무기는 꾸미기 칸이 아니다"), UBackpackComponent::GetOutfitSlotKey(EEquipSlot::Weapon).IsEmpty());

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("꾸미기 메시"), Rig.CosmeticMesh))
	{
		return false;
	}
	const UStaticMesh* const BareHead = Rig.ShownHeadMesh();
	TestNotEqual(TEXT("처음 머리엔 꾸미기 메시가 없다"), BareHead, static_cast<const UStaticMesh*>(Rig.CosmeticMesh));

	// ③ 손에 드는 칸 — 묻지 않는다.
	TestFalse(TEXT("무기 칸은 묻지 않는다"), Rig.Backpack->RequestSetOutfit(EEquipSlot::Weapon, CosmeticHatID));
	TestEqual(TEXT("서버 호출 0"), Server.Calls, 0);

	// ② 거절 — 안 산 꾸미기. 답 전에도 뒤에도 그대로.
	TestTrue(TEXT("입기를 물었다"), Rig.Backpack->RequestSetOutfit(EEquipSlot::Head, CosmeticHatID));
	TestEqual(TEXT("부른 함수 = SetOutfit"), Server.LastFunction, FString(TEXT("SetOutfit")));
	TestEqual(TEXT("보낸 slot = head"), Server.LastArg(TEXT("slot")), FString(TEXT("head")));
	TestEqual(TEXT("보낸 itemId"), Server.LastArg(TEXT("itemId")), CosmeticHatID.ToString());
	TestTrue(TEXT("답 전엔 꾸미기 없음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	TestEqual(TEXT("답 전엔 겉모습 그대로"), Rig.ShownHeadMesh(), BareHead);
	Server.AnswerFail(TEXT("not_enough_items"), TEXT("꾸미기가 없다"));
	TestTrue(TEXT("거절이면 꾸미기 없음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	TestEqual(TEXT("거절이면 겉모습 그대로"), Rig.ShownHeadMesh(), BareHead);

	// ① 받아들임 — 그 뒤에야 덮개.
	TestTrue(TEXT("다시 물었다"), Rig.Backpack->RequestSetOutfit(EEquipSlot::Head, CosmeticHatID));
	TestTrue(TEXT("답 전엔 여전히 없음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	const TSharedPtr<FJsonObject> Worn = MakeShared<FJsonObject>();
	Worn->SetStringField(TEXT("slot"), TEXT("head"));
	Worn->SetStringField(TEXT("itemId"), CosmeticHatID.ToString());
	Server.Answer(Worn);
	TestEqual(TEXT("받아들인 뒤 꾸미기 행"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);
	TestEqual(TEXT("받아들인 뒤 보이는 메시 = 꾸미기"), Rig.ShownHeadMesh(), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));

	// 벗기 — 빈 itemId, 답 뒤에만.
	TestTrue(TEXT("벗기를 물었다"), Rig.Backpack->RequestSetOutfit(EEquipSlot::Head, NAME_None));
	TestEqual(TEXT("벗기 itemId = 빈 문자열"), Server.LastArg(TEXT("itemId")), FString());
	TestEqual(TEXT("답 전엔 아직 입고 있다"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);
	const TSharedPtr<FJsonObject> Off = MakeShared<FJsonObject>();
	Off->SetStringField(TEXT("slot"), TEXT("head"));
	Off->SetStringField(TEXT("itemId"), FString());
	Server.Answer(Off);
	TestTrue(TEXT("받아들인 뒤 벗었다"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	TestEqual(TEXT("벗은 뒤 겉모습 처음대로"), Rig.ShownHeadMesh(), BareHead);
	TestEqual(TEXT("서버 호출 = 거절 1 + 입기 1 + 벗기 1"), Server.Calls, 3);
	AddInfo(FString::Printf(TEXT("[Outfit] 호출 %d번 · 답 전 덮개 0 · 거절 뒤 덮개 0 · 받은 뒤 덮개 %s"),
		Server.Calls, *CosmeticHatRow.ToString()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
