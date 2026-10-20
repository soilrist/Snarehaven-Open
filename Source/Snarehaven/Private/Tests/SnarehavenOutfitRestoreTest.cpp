// 스킨 조각 e2 (`docs/specs/skin-parts.md` 5장 4) 2층 검사 — 재접속하면 서버가 기억한 꾸미기를 다시 입는다.
//  ① `GetBaseState.outfit {칸: itemId}` 의 칸을 입힌다 · 모르는 칸 이름은 넘긴다 · `SetOutfit` 을 다시 묻지 않는다
//  ② `outfit` 이 없는 옛 서버 답이면 그대로 · 빈 `outfit` 이면 남은 덮개를 벗긴다
//  ③ 표에 없는 아이템 ID 는 입히지 않는다
// 가짜 서버(`TestServerOverride`)로 `URanchSyncComponent::RequestBaseState` 의 진짜 응답 길을 태운다.
// 꾸미기 행은 아직 표에 없다(메시는 조각 f~l) — 표 사본에 **검사 전용 행**을 하나 더한다 (Verification.md 6).
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
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"

namespace SnareOutfitRestoreTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const FName CosmeticHatRow(TEXT("Item_TestRestoreHat"));
	const FName CosmeticHatID(TEXT("test.restore_hat"));
	const FName MeshSourceRow(TEXT("Item_CookHat"));

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 부른 함수를 적어 두고 답은 검사가 줄 때까지 붙잡는다 (`SnarehavenOutfitRequestTest` 의 가짜 서버와 같은 꼴). */
	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					Functions.Add(Name);
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
	};

	/** 표 사본(검사 전용 꾸미기 행 하나 더)을 건 플레이어 + 백엔드에 묶은 배낭·거점 전송기. */
	struct FRig
	{
		UWorld* World = nullptr;
		UDataTable* Table = nullptr;
		UStaticMesh* CosmeticMesh = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		UEquipmentComponent* Equipment = nullptr;
		UBackpackComponent* Backpack = nullptr;
		URanchSyncComponent* Sync = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			const UDataTable* const Original = LoadObject<UDataTable>(nullptr, ItemTablePath);
			const FItemRow* const Source = Original != nullptr ? Original->FindRow<FItemRow>(MeshSourceRow, TEXT("OutfitRestoreTest")) : nullptr;
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
			Sync = Player->FindComponentByClass<URanchSyncComponent>();
			if (Equipment == nullptr || TableProp == nullptr || Sync == nullptr)
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
				Backpack = NewObject<UBackpackComponent>(Player, TEXT("OutfitRestoreTestBackpack"));
				Backpack->RegisterComponent();
			}
			Backpack->BindTargets(Backend, nullptr);
			Sync->BindTargets(nullptr, nullptr, Backend, nullptr, nullptr, Backpack);
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

	TSharedPtr<FJsonObject> BaseStateWithOutfit(const TSharedPtr<FJsonObject>& Outfit)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		if (Outfit.IsValid())
		{
			Response->SetObjectField(TEXT("outfit"), Outfit);
		}
		return Response;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutfitRestoreTest, "Snarehaven.Item.Cosmetic.OutfitRestoreFromServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutfitRestoreTest::RunTest(const FString& Parameters)
{
	using namespace SnareOutfitRestoreTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("꾸미기 메시"), Rig.CosmeticMesh))
	{
		return false;
	}
	const UStaticMesh* const BareHead = Rig.ShownHeadMesh();
	TestNotEqual(TEXT("처음 머리엔 꾸미기 메시가 없다"), BareHead, static_cast<const UStaticMesh*>(Rig.CosmeticMesh));

	// ① 재접속 — 서버가 기억한 모자 + 모르는 칸 이름 하나.
	TestTrue(TEXT("거점 상태를 물었다"), Rig.Sync->RequestBaseState());
	const TSharedPtr<FJsonObject> Worn = MakeShared<FJsonObject>();
	Worn->SetStringField(TEXT("head"), CosmeticHatID.ToString());
	Worn->SetStringField(TEXT("tail"), TEXT("test.nothing"));
	Server.Answer(BaseStateWithOutfit(Worn));
	TestEqual(TEXT("서버 값대로 머리 꾸미기 행"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);
	TestEqual(TEXT("보이는 메시 = 꾸미기"), Rig.ShownHeadMesh(), static_cast<const UStaticMesh*>(Rig.CosmeticMesh));
	TestTrue(TEXT("다른 칸은 그대로 비었다"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Shoes).IsNone());

	// ② 옛 서버 답(`outfit` 없음) — 그대로 입고 있다.
	TestTrue(TEXT("다시 물었다"), Rig.Sync->RequestBaseState());
	Server.Answer(BaseStateWithOutfit(nullptr));
	TestEqual(TEXT("outfit 없는 답이면 그대로"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), CosmeticHatRow);

	// ② 빈 `outfit` — 이제 안 가진(또는 벗은) 것. 덮개를 벗긴다.
	TestTrue(TEXT("또 물었다"), Rig.Sync->RequestBaseState());
	Server.Answer(BaseStateWithOutfit(MakeShared<FJsonObject>()));
	TestTrue(TEXT("빈 outfit 이면 벗었다"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	TestEqual(TEXT("벗은 뒤 겉모습 처음대로"), Rig.ShownHeadMesh(), BareHead);

	// ③ 표에 없는 아이템 — 입히지 않는다.
	FJsonObject Unknown;
	Unknown.SetStringField(TEXT("head"), TEXT("test.not_in_table"));
	TestEqual(TEXT("표에 없는 꾸미기 = 0칸"), Rig.Backpack->ApplyBackendOutfit(Unknown), 0);
	TestTrue(TEXT("표에 없으면 머리 비었다"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());

	const int32 SetOutfitCalls = Server.Functions.FilterByPredicate([](const FString& Name)
		{
			return Name == TEXT("SetOutfit");
		}).Num();
	TestEqual(TEXT("서버 호출 = GetBaseState 3번"), Server.Functions.Num(), 3);
	TestEqual(TEXT("복원은 SetOutfit 을 묻지 않는다"), SetOutfitCalls, 0);
	AddInfo(FString::Printf(TEXT("[OutfitRestore] 호출 %d번(SetOutfit %d) · 복원 덮개 %s · 옛 답 그대로 · 빈 outfit 벗김"),
		Server.Functions.Num(), SetOutfitCalls, *CosmeticHatRow.ToString()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
