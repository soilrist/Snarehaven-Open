// ROADMAP M5-06b·M9-01·M9-02·M9-06·M39-06a 2층 검사 — 몸에 두 손 소켓이 있고, 장비 행을 들면 그 소켓에 **실제로 붙는지**.
// M5-06a 검사(`…Equipment.RowsAreWearable`)는 데이터만 봤다 — 여기서는 몸·소켓·부착까지 본다.
// 왜 소켓부터 보나: 없는 소켓 이름으로 붙이면 엔진이 조용히 액터 원점에 매달아서, 화면에서는
// 무기가 발밑에 떠 있는 것으로만 보이고 로그에는 아무 말도 안 남는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Equipment/SnareGearMemorySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"

namespace
{
	// 소켓 이름은 `DT_ItemTable` 의 `Item_Club` 행이 가리키는 이름이고(M5-06a),
	// 뼈 이름은 바르코 리그에서 읽어 확인한 것이다 (M5-06b · `tools/unreal/setup_equipment_socket.py`).
	const TCHAR* WeaponSocketName = TEXT("Socket_Weapon");
	const TCHAR* WeaponBoneName = TEXT("RightHand");
	// M9-01 왼손 도구 소켓. 가리키는 행은 아직 없다(도구 2종은 M9-04) — 자리만 먼저 있다.
	const TCHAR* ToolSocketName = TEXT("Socket_Tool");
	const TCHAR* ToolBoneName = TEXT("LeftHand");

	// 들 것과 들면 안 되는 것 한 줄씩. 뒤엣것은 재료라 손에 들리면 안 된다.
	const TCHAR* ClubRowName = TEXT("Item_Club");
	const TCHAR* MaterialRowName = TEXT("Item_PigMeat");

	UWorld* CreateEquipTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyEquipTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWeaponSocketTest, "Snarehaven.Equipment.Socket.ExistsOnHandBone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWeaponSocketTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	if (!TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	const USkeletalMeshComponent* Body = Player->GetMesh();
	const TPair<const TCHAR*, const TCHAR*> Expected[] = {
		{ WeaponSocketName, WeaponBoneName },
		{ ToolSocketName, ToolBoneName },
	};

	for (const TPair<const TCHAR*, const TCHAR*>& Pair : Expected)
	{
		const USkeletalMeshSocket* Socket = Body->GetSocketByName(FName(Pair.Key));
		if (TestNotNull(FString::Printf(TEXT("몸에 소켓 %s 가 있다"), Pair.Key), Socket))
		{
			// 아무 뼈에나 달려 있으면 장비가 엉뚱한 데서 흔들린다 — 붙은 뼈까지 본다.
			TestEqual(FString::Printf(TEXT("소켓 %s 가 뼈 %s 에 달렸다"), Pair.Key, Pair.Value),
				Socket->BoneName, FName(Pair.Value));
			TestTrue(FString::Printf(TEXT("뼈 %s 가 몸에 실제로 있다"), Pair.Value),
				Body->GetBoneIndex(FName(Pair.Value)) != INDEX_NONE);
		}
	}

	// 두 소켓이 같은 뼈로 접히면 도구가 무기 자리에 겹쳐 뜬다 — 손이 갈렸는지 못 박아 둔다.
	TestNotEqual(TEXT("무기 소켓과 도구 소켓이 다른 뼈다"), FString(WeaponBoneName), FString(ToolBoneName));

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipAttachTest, "Snarehaven.Equipment.Equip.AttachesToSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipAttachTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	UEquipmentComponent* Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트가 붙어 있다"), Equipment))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	// 몸이 없으면 붙일 소켓도 없다 — 그때 조용히 성공하지 않는지부터 본다.
	TestFalse(TEXT("몸을 올리기 전에는 못 든다"), Equipment->EquipItem(FName(ClubRowName)));

	if (!TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	if (TestTrue(TEXT("몽둥이를 들었다"), Equipment->EquipItem(FName(ClubRowName))))
	{
		UStaticMeshComponent* Visual = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
		if (TestNotNull(TEXT("무기 부위에 메시 컴포넌트가 생겼다"), Visual))
		{
			TestNotNull(TEXT("그 컴포넌트에 메시가 물렸다"), ToRawPtr(Visual->GetStaticMesh()));
			TestEqual(TEXT("몸에 붙었다"), Visual->GetAttachParent(),
				Cast<USceneComponent>(Player->GetMesh()));
			// 여기가 이 검사의 핵심이다 — 소켓 이름이 비어 있으면 액터 원점에 매달린 것이다.
			TestEqual(TEXT("무기 소켓에 붙었다"), Visual->GetAttachSocketName(), FName(WeaponSocketName));
		}
		TestEqual(TEXT("무기 부위가 몽둥이를 들고 있다"),
			Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));
	}

	// 재료를 손에 들리지 않는다 — 표가 장비라고 적은 행만 붙는다.
	TestFalse(TEXT("재료 행은 못 든다"), Equipment->EquipItem(FName(MaterialRowName)));
	TestFalse(TEXT("없는 행은 못 든다"), Equipment->EquipItem(TEXT("Item_NotARealRow")));
	// 위 둘이 실패해도 들고 있던 것이 그대로여야 한다 — 실패가 손에 든 것을 떨어뜨리면 안 된다.
	TestEqual(TEXT("실패해도 몽둥이는 그대로 들려 있다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));

	TestTrue(TEXT("몽둥이를 내려놓았다"), Equipment->UnequipSlot(EEquipSlot::Weapon));
	TestEqual(TEXT("내려놓으면 무기 부위가 빈다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(NAME_None));
	if (const UStaticMeshComponent* Visual = Equipment->GetEquippedMesh(EEquipSlot::Weapon))
	{
		TestNull(TEXT("내려놓으면 메시가 비워진다"), ToRawPtr(Visual->GetStaticMesh()));
	}
	TestFalse(TEXT("빈 부위를 또 내려놓으면 false"), Equipment->UnequipSlot(EEquipSlot::Weapon));

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipWeaponAndToolTest,
	"Snarehaven.Equipment.Equip.WeaponAndToolTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipWeaponAndToolTest::RunTest(const FString& Parameters)
{
	// **"한 부위 한 개" 가 부위를 넘지 않는지**를 본다 (M9-01). 도구 행은 아직 `DT_ItemTable` 에 없어서
	// (도구 에셋 2종은 M9-04 감독 대기) `EquipItem` 의 **검사용 덮어쓰기 두 칸**으로 몽둥이를 왼손 자리에
	// 건다 — 3층 캡처가 쓰는 `-SnareEquipTool=` 과 **같은 길**이다. 여기서 보는 것은 모양이 아니라
	// 어느 소켓에 붙었나와, 한쪽을 들 때 다른 쪽이 떨어지지 않는가 둘이다.
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	UEquipmentComponent* Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트가 붙어 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	TestTrue(TEXT("오른손에 몽둥이를 들었다"), Equipment->EquipItem(FName(ClubRowName)));
	TestTrue(TEXT("왼손 도구 자리에도 들었다"),
		Equipment->EquipItem(FName(ClubRowName), EEquipSlot::Tool, FName(ToolSocketName)));

	// 여기가 이 검사의 핵심이다 — 도구를 들었다고 무기가 떨어지면 "한 부위 한 개" 가 부위를 넘은 것이다.
	TestEqual(TEXT("도구를 들어도 무기 부위는 그대로다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));
	TestEqual(TEXT("도구 부위가 채워졌다"),
		Equipment->GetEquippedItem(EEquipSlot::Tool), FName(ClubRowName));

	UStaticMeshComponent* WeaponVisual = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
	UStaticMeshComponent* ToolVisual = Equipment->GetEquippedMesh(EEquipSlot::Tool);
	if (TestNotNull(TEXT("무기 메시 컴포넌트가 있다"), WeaponVisual)
		&& TestNotNull(TEXT("도구 메시 컴포넌트가 있다"), ToolVisual))
	{
		// 부위마다 컴포넌트가 따로여야 두 손에 동시에 걸린다 — 하나를 돌려 쓰면 나중 것이 앞것을 옮겨 간다.
		TestTrue(TEXT("두 부위가 서로 다른 컴포넌트를 쓴다"), WeaponVisual != ToolVisual);
		TestEqual(TEXT("무기가 오른손 소켓에 붙었다"),
			WeaponVisual->GetAttachSocketName(), FName(WeaponSocketName));
		TestEqual(TEXT("도구가 왼손 소켓에 붙었다"),
			ToolVisual->GetAttachSocketName(), FName(ToolSocketName));
		TestNotNull(TEXT("도구 컴포넌트에 메시가 물렸다"), ToRawPtr(ToolVisual->GetStaticMesh()));
		TestTrue(TEXT("왼손 도구면 무기가 보인다"), WeaponVisual->IsVisible());

		// 감독 09-25 — 휘두르는 도구는 행이 오른손(`Socket_Weapon`)이라 무기와 한 손을 나눈다: 무기가 숨는다.
		TestTrue(TEXT("오른손 도구 자리에 들었다"),
			Equipment->EquipItem(FName(ClubRowName), EEquipSlot::Tool, FName(WeaponSocketName)));
		TestFalse(TEXT("오른손 도구를 들면 무기가 숨는다"), WeaponVisual->IsVisible());
	}

	// 한쪽만 내려놓으면 다른 쪽은 남는다.
	TestTrue(TEXT("도구를 내려놓았다"), Equipment->UnequipSlot(EEquipSlot::Tool));
	if (UStaticMeshComponent* WeaponVisualAfter = Equipment->GetEquippedMesh(EEquipSlot::Weapon))
	{
		TestTrue(TEXT("도구를 내려놓으면 무기가 다시 보인다"), WeaponVisualAfter->IsVisible());
	}
	TestEqual(TEXT("도구를 내려놔도 무기는 들려 있다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));
	TestEqual(TEXT("도구 부위는 빈다"),
		Equipment->GetEquippedItem(EEquipSlot::Tool), FName(NAME_None));

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipOutfitTest,
	"Snarehaven.Equipment.Equip.OutfitLayersOverBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipOutfitTest::RunTest(const FString& Parameters)
{
	// **부위가 붙이는 종류를 정하는가**를 본다 (M9-02 · 결정 006). 옷 에셋은 아직 없어서(M9-05 감독 대기)
	// **몸 메시를 옷 자리표시로 그대로 덮는다** — 여기서 보는 것은 옷의 생김새가 아니라
	// ① 손 부위와 옷 부위가 서로 다른 종류의 컴포넌트를 쓰는가 ② 옷이 몸을 리더로 따라가는가 둘이다.
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	UEquipmentComponent* Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트가 붙어 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	USkeletalMeshComponent* Body = Player->GetMesh();
	USkeletalMesh* PlaceholderOutfit = Body->GetSkeletalMeshAsset();
	if (!TestNotNull(TEXT("자리표시로 쓸 몸 메시가 있다"), PlaceholderOutfit))
	{
		DestroyEquipTestWorld(World);
		return false;
	}

	// 손에 드는 부위에는 옷을 못 덮는다 — 종류 착오가 조용히 통과하면 몽둥이 자리에 셔츠가 뜬다.
	TestFalse(TEXT("무기 부위에는 옷을 못 덮는다"),
		Equipment->EquipOutfitMesh(EEquipSlot::Weapon, PlaceholderOutfit));
	TestFalse(TEXT("빈 메시로는 못 덮는다"),
		Equipment->EquipOutfitMesh(EEquipSlot::OutfitTop, nullptr));
	// 반대쪽도 본다 — 스태틱 메시만 적힌 몽둥이 행을 옷 부위로 보내면 거절해야 한다
	// (`EquipOutfitMeshRef` 가 비어 있다). 표 쪽 길과 코드 쪽 길이 같은 규칙을 쓰는지 확인하는 줄이다.
	TestFalse(TEXT("손에 드는 행은 옷 부위로 못 간다"),
		Equipment->EquipItem(FName(ClubRowName), EEquipSlot::OutfitTop));

	if (TestTrue(TEXT("상의 자리에 옷을 덮었다"),
		Equipment->EquipOutfitMesh(EEquipSlot::OutfitTop, PlaceholderOutfit)))
	{
		USkeletalMeshComponent* Top = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
		if (TestNotNull(TEXT("상의 부위에 스켈레탈 컴포넌트가 생겼다"), Top))
		{
			TestEqual(TEXT("그 컴포넌트에 옷 메시가 물렸다"), Top->GetSkeletalMeshAsset(), PlaceholderOutfit);
			TestEqual(TEXT("몸에 붙었다"), Top->GetAttachParent(), Cast<USceneComponent>(Body));
			// 여기가 이 검사의 핵심이다 — 리더가 없으면 걸을 때 옷만 T 포즈로 남는다.
			TestEqual(TEXT("몸을 리더로 따라간다"),
				Cast<USkinnedMeshComponent>(Top->LeaderPoseComponent.Get()),
				Cast<USkinnedMeshComponent>(Body));
			// 옷은 소켓에 매다는 물건이 아니다 — 소켓 이름이 붙어 있으면 한 점에 매달린 것이다.
			TestEqual(TEXT("소켓 없이 몸 자체에 겹쳤다"), Top->GetAttachSocketName(), FName(NAME_None));
		}
		// 부위마다 **맞는 종류**로만 생긴다 — 옷 부위에 스태틱 컴포넌트가 생기면 길이 섞인 것이다.
		TestNull(TEXT("옷 부위에는 스태틱 컴포넌트가 안 생긴다"),
			Equipment->GetEquippedMesh(EEquipSlot::OutfitTop));
	}

	// 상의와 하의가 따로여야 상의만 갈아입을 수 있다 (결정 006 부위별 모듈형 레이어).
	TestTrue(TEXT("하의 자리에도 덮었다"),
		Equipment->EquipOutfitMesh(EEquipSlot::OutfitBottom, PlaceholderOutfit));
	TestTrue(TEXT("상의와 하의가 서로 다른 컴포넌트를 쓴다"),
		Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop) != Equipment->GetEquippedOutfit(EEquipSlot::OutfitBottom));

	// 손에 든 것과 입은 것이 같이 있는다 — 종류가 갈렸어도 서로를 밀어내면 안 된다.
	TestTrue(TEXT("옷을 입은 채로 몽둥이를 든다"), Equipment->EquipItem(FName(ClubRowName)));

	USkeletalMeshComponent* TopLayer = Equipment->GetEquippedOutfit(EEquipSlot::OutfitTop);
	USkeletalMeshComponent* BottomLayer = Equipment->GetEquippedOutfit(EEquipSlot::OutfitBottom);
	if (TopLayer == nullptr || BottomLayer == nullptr)
	{
		// 여기까지 왔는데 컴포넌트가 없으면 위 검사가 이미 실패를 적었다 — 뒤 줄이 널 참조로
		// **터져서 자동화 실행 전체를 죽이지 않게** 여기서 끝낸다.
		AddError(TEXT("옷 컴포넌트가 없어 벗기는 검사를 못 한다"));
		DestroyEquipTestWorld(World);
		return false;
	}
	TestNotNull(TEXT("몽둥이를 들어도 상의 메시는 그대로다"), TopLayer->GetSkeletalMeshAsset());

	// 벗기는 길도 같은 함수 하나다 — 옷만 벗고 하의·무기는 그대로 남는다.
	TestTrue(TEXT("상의를 벗었다"), Equipment->UnequipSlot(EEquipSlot::OutfitTop));
	TestNull(TEXT("벗으면 상의 메시가 비워진다"), TopLayer->GetSkeletalMeshAsset());
	TestFalse(TEXT("빈 상의를 또 벗으면 false"), Equipment->UnequipSlot(EEquipSlot::OutfitTop));
	TestNotNull(TEXT("상의를 벗어도 하의는 남는다"), BottomLayer->GetSkeletalMeshAsset());
	TestEqual(TEXT("상의를 벗어도 무기는 들려 있다"),
		Equipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEquipNetTest, "Snarehaven.Equipment.Net.OtherScreensSeeEquippedSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEquipNetTest::RunTest(const FString& Parameters)
{
	// M9-06 — **남의 화면에도 보이는가**. 진짜 두 프로세스 왕복은 여기서 못 한다(넷드라이버가 없는
	// 테스트 월드다 — 그건 3층 몫이다). 대신 ① 목록이 실제로 복제 대상으로 등록됐는지
	// ② 게스트 역할에서는 **요청만 가고 자기 화면을 혼자 안 고치는지** ③ 호스트 목록이 내려오면
	// 복제본이 같은 메시를 거는지 셋을 본다 (`SnarehavenAnimalReplicationTest` 와 같은 짜임).

	// 복제용 `RepIndex` 는 UClass 가 늦게 채운다 — 테스트는 그 길을 안 타서 먼저 한 번 세워 둔다
	// (안 세우면 부모가 등록한 0번과 겹쳐 `bIsPushBased` assert 로 엔진이 죽는다, 루프#40).
	UEquipmentComponent::StaticClass()->SetUpRuntimeReplicationData();

	const FProperty* SlotsProperty =
		UEquipmentComponent::StaticClass()->FindPropertyByName(TEXT("EquippedSlots"));
	if (TestNotNull(TEXT("EquippedSlots 프로퍼티가 있다"), SlotsProperty))
	{
		// `ReplicatedUsing` 만 적고 DOREPLIFETIME 을 빼먹으면 조용히 안 나간다 — 둘 다 본다.
		TestTrue(TEXT("EquippedSlots 가 복제 플래그를 달았다"), SlotsProperty->HasAnyPropertyFlags(CPF_Net));
		TestEqual(TEXT("EquippedSlots 의 복제 알림"), SlotsProperty->RepNotifyFunc,
			FName(TEXT("OnRep_EquippedSlots")));

		TArray<FLifetimeProperty> Props;
		NewObject<UEquipmentComponent>()->GetLifetimeReplicatedProps(Props);
		TestTrue(TEXT("EquippedSlots 가 복제 목록에 있다"),
			Props.ContainsByPredicate([SlotsProperty](const FLifetimeProperty& Lifetime)
				{ return Lifetime.RepIndex == SlotsProperty->RepIndex; }));
	}
	TestTrue(TEXT("장비 컴포넌트가 복제 대상"), NewObject<UEquipmentComponent>()->GetIsReplicated());

	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Host = World->SpawnActor<ASnarehavenCharacter>();
	// 겹쳐 세우면 스폰이 막힐 수 있다 — 떨어뜨려 놓는다 (거리는 검사 전용 값이다).
	ASnarehavenCharacter* Other =
		World->SpawnActor<ASnarehavenCharacter>(FVector(300.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (Host == nullptr || Other == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	UEquipmentComponent* HostEquipment = Host->GetEquipment();
	UEquipmentComponent* OtherEquipment = Other->GetEquipment();
	if (HostEquipment == nullptr || OtherEquipment == nullptr
		|| !TestTrue(TEXT("두 몸을 다 올렸다"), Host->ApplyPlayerBody() && Other->ApplyPlayerBody()))
	{
		AddError(TEXT("장비 컴포넌트나 몸이 없어 복제 검사를 못 한다"));
		DestroyEquipTestWorld(World);
		return false;
	}

	// ① 호스트가 든다 — 목록에 **행 이름·부위·소켓**만 남는다 (에셋 참조는 안 실린다).
	TestTrue(TEXT("호스트가 몽둥이를 들었다"), HostEquipment->EquipItem(FName(ClubRowName)));
	if (TestEqual(TEXT("목록에 한 줄"), HostEquipment->GetEquippedSlots().Num(), 1))
	{
		const FEquippedSlot& Entry = HostEquipment->GetEquippedSlots()[0];
		TestEqual(TEXT("부위는 무기"), Entry.Slot, EEquipSlot::Weapon);
		TestEqual(TEXT("행 이름이 실렸다"), Entry.ItemRowName, FName(ClubRowName));
		// 소켓까지 실려야 검사용 덮어쓰기(도구 자리표시)가 남의 화면에서도 같은 손에 뜬다.
		TestEqual(TEXT("붙은 소켓 이름도 실렸다"), Entry.SocketName, FName(WeaponSocketName));
	}

	// ② 게스트 역할에서는 **요청만** 간다. 넷드라이버가 없어 요청은 아무 데도 안 가고,
	//    그래서 자기 목록이 그대로 비어 있는 것이 곧 "혼자 안 고쳤다" 는 증거다.
	Other->SetRole(ROLE_AutonomousProxy);
	TestFalse(TEXT("게스트 폰은 장착 권위가 없다"), OtherEquipment->HasEquipAuthority());
	TestFalse(TEXT("게스트가 들면 요청만 — 여기서 안 걸린다"),
		OtherEquipment->EquipItem(FName(ClubRowName)));
	TestEqual(TEXT("게스트 목록은 그대로 비어 있다"), OtherEquipment->GetEquippedSlots().Num(), 0);
	TestEqual(TEXT("게스트 화면에도 아직 아무것도 없다"),
		OtherEquipment->GetEquippedItem(EEquipSlot::Weapon), FName(NAME_None));
	TestFalse(TEXT("게스트가 벗겨도 요청만"), OtherEquipment->UnequipSlot(EEquipSlot::Weapon));

	// ③ 호스트 목록이 내려온다(게임에서는 OnRep 이 이 길을 만든다) — 복제본 화면에 같은 것이 걸린다.
	Other->SetRole(ROLE_SimulatedProxy);
	OtherEquipment->ApplyEquippedSlots(HostEquipment->GetEquippedSlots());
	TestEqual(TEXT("남의 화면도 같은 행을 들고 있다"),
		OtherEquipment->GetEquippedItem(EEquipSlot::Weapon), FName(ClubRowName));

	UStaticMeshComponent* OtherVisual = OtherEquipment->GetEquippedMesh(EEquipSlot::Weapon);
	if (TestNotNull(TEXT("남의 화면에 메시 컴포넌트가 생겼다"), OtherVisual))
	{
		// 이름만 받아 **자기 표에서** 찾은 메시라, 호스트가 들고 있는 것과 같은 에셋이어야 한다.
		TestNotNull(TEXT("메시가 실제로 물렸다"), OtherVisual->GetStaticMesh().Get());
		TestEqual(TEXT("호스트와 같은 메시다"), OtherVisual->GetStaticMesh().Get(),
			HostEquipment->GetEquippedMesh(EEquipSlot::Weapon)->GetStaticMesh().Get());
		TestEqual(TEXT("같은 소켓에 붙었다"), OtherVisual->GetAttachSocketName(), FName(WeaponSocketName));
		// 남의 몸에 붙어야 한다 — 내 몸에 붙으면 한 사람이 두 개를 든 것으로 보인다.
		TestEqual(TEXT("남의 몸에 붙었다"), OtherVisual->GetAttachParent(),
			Cast<USceneComponent>(Other->GetMesh()));
	}

	// 호스트가 내려놓으면 빈 목록이 내려온다 — 복제본 화면에서도 사라진다.
	TestTrue(TEXT("호스트가 내려놓았다"), HostEquipment->UnequipSlot(EEquipSlot::Weapon));
	OtherEquipment->ApplyEquippedSlots(HostEquipment->GetEquippedSlots());
	TestEqual(TEXT("남의 화면에서도 사라졌다"),
		OtherEquipment->GetEquippedItem(EEquipSlot::Weapon), FName(NAME_None));
	if (OtherVisual != nullptr)
	{
		TestNull(TEXT("남의 화면 메시가 비워졌다"), OtherVisual->GetStaticMesh().Get());
	}

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenToolMeshSizeTest, "Snarehaven.Equipment.Tools.OwnMeshWorldSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenToolMeshSizeTest::RunTest(const FString& Parameters)
{
	// M13-36 — DESIGN 3.5 「도구 크기」 표의 메시마다 그것을 드는 행이 **딱 하나**(손도끼 메시 돌려쓰기 0)이고,
	// 손에 들었을 때 메시 모양의 긴 변(x·y·z 최대 × 월드 배율) ≈ 표 값 ±3cm. 월드 상자(`CalcBounds`)가 아니라
	// 모양으로 본다 — 손에 쥔 각도가 기울면 상자만 커진다(`EquipmentComponent` 로그의 `모양` 과 같은 값).
	constexpr double SizeToleranceCm = 3.0;
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	TMap<FString, double> WantCm;   // SM_Tool_Chainsaw_01 → 55
	FRegexMatcher Match(FRegexPattern(TEXT("`(SM_Tool_\\w+)`\\)\\s*\\|\\s*([0-9.]+)cm")), Doc);
	while (Match.FindNext())
	{
		WantCm.Add(Match.GetCaptureGroup(1), FCString::Atod(*Match.GetCaptureGroup(2)));
	}
	TestEqual(TEXT("DESIGN 3.5 도구 크기 줄 8개(손도끼·바구니 + 13.5 도구 3 + 8.16 곡괭이 3 — 위성 통신기는 손에 들지 않아 뺐다)"), WantCm.Num(), 8);

	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("플레이어"), Player)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}
	UEquipmentComponent* Equipment = Player->GetEquipment();

	TMap<FString, int32> Users;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		const FString MeshName = Row.EquipMeshRef.ToSoftObjectPath().GetAssetName();
		const double* Want = WantCm.Find(MeshName);
		if (Row.EquipSlot != EEquipSlot::Tool || Want == nullptr)
		{
			continue;
		}
		++Users.FindOrAdd(MeshName);
		const FString RowName = Pair.Key.ToString();
		if (!TestTrue(FString::Printf(TEXT("[%s] 손에 들었다"), *RowName), Equipment->EquipItem(Pair.Key)))
		{
			continue;
		}
		const UStaticMeshComponent* Visual = Equipment->GetEquippedMesh(EEquipSlot::Tool);
		const FVector Shape = Visual->GetStaticMesh()->GetBounds().BoxExtent * 2.f * Visual->GetComponentScale();
		const double Longest = Shape.GetMax();
		AddInfo(FString::Printf(TEXT("[Tools] %s %s 모양 %.1f×%.1f×%.1fcm · 긴 변 %.1f · 표 %.1f"),
			*RowName, *MeshName, Shape.X, Shape.Y, Shape.Z, Longest, *Want));
		TestTrue(FString::Printf(TEXT("[%s] 긴 변 %.1f ≈ 표 %.1f"), *RowName, Longest, *Want),
			FMath::IsNearlyEqual(Longest, *Want, SizeToleranceCm));
	}
	for (const TPair<FString, double>& Pair : WantCm)
	{
		TestEqual(FString::Printf(TEXT("%s 를 드는 도구 행 = 1 (돌려쓰기 없음)"), *Pair.Key), Users.FindRef(Pair.Key), 1);
	}

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenToolGripTest, "Snarehaven.Equipment.Tools.GripAtHandleEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenToolGripTest::RunTest(const FString& Parameters)
{
	// M18-11 — `GripOffset` 이 든 행 11개(곡괭이 3·손도끼·몽둥이·마체테·전기톱 + 2026-10-05 낚싯대 4, DESIGN 3.1 「쥐는 자리」·3.5 낚싯대 행)를 손에 들면
	// 도구 머리 끝(긴 축에서 쥔 쪽 반대 끝)이 손목(소켓 원점)에서 메시 길이 절반 넘게 떨어져 있다.
	// 기준 0.6 — 옛 붙임(메시 가운데가 손목)은 정확히 0.5 라 "절반 이상" 으로는 못 가른다.
	constexpr double MinReachRatio = 0.6;
	const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("아이템 표"), ItemTable) || !TestNotNull(TEXT("플레이어"), Player)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}
	UEquipmentComponent* Equipment = Player->GetEquipment();

	int32 GripRows = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow& Row = *reinterpret_cast<const FItemRow*>(Pair.Value);
		if (!Row.GripOffset.IsSet())
		{
			continue;
		}
		++GripRows;
		const FString RowName = Pair.Key.ToString();
		if (!TestTrue(FString::Printf(TEXT("[%s] 손에 들었다"), *RowName), Equipment->EquipItem(Pair.Key)))
		{
			continue;
		}
		const UStaticMeshComponent* Visual = Equipment->GetEquippedMesh(Row.EquipSlot);
		const USceneComponent* Hand = Visual->GetAttachParent();
		const FBoxSphereBounds Local = Visual->GetStaticMesh()->GetBounds();
		const int32 Axis = Local.BoxExtent.X >= FMath::Max(Local.BoxExtent.Y, Local.BoxExtent.Z) ? 0
			: (Local.BoxExtent.Y >= Local.BoxExtent.Z ? 1 : 2);
		FVector TipLocal = Local.Origin;
		// 머리 끝 = 쥔 점이 있는 쪽의 반대 끝. 몽둥이는 피벗이 손잡이 끝이라 상자 가운데(Origin)와 견준다.
		TipLocal[Axis] += Row.GripOffset.Location[Axis] < Local.Origin[Axis] ? Local.BoxExtent[Axis] : -Local.BoxExtent[Axis];
		const double Reach = FVector::Dist(Visual->GetComponentTransform().TransformPosition(TipLocal),
			Hand->GetSocketLocation(Visual->GetAttachSocketName()));
		const double Length = Local.BoxExtent[Axis] * 2.0 * Visual->GetComponentScale()[Axis];
		AddInfo(FString::Printf(TEXT("[Grip] %s 머리 끝–손목 %.1fcm · 길이 %.1fcm · 비 %.2f"),
			*RowName, Reach, Length, Reach / Length));
		TestTrue(FString::Printf(TEXT("[%s] 머리 끝–손목 %.1f ≥ 길이 %.1f × %.1f"), *RowName, Reach, Length, MinReachRatio),
			Reach >= Length * MinReachRatio);
	}
	// 2026-10-05 낚싯대 4종도 오른손 쥐는 각(위치 0)을 든다(`create_data_tables.py` ROD_GRIP · 레퍼런스 UlFyyrxMNt8 1:50).
	TestEqual(TEXT("쥐는 자리가 든 행 = 11 (곡괭이 3·손도끼·몽둥이·마체테·전기톱·낚싯대 4)"), GripRows, 11);

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDefaultLookTest, "Snarehaven.Equipment.DefaultLook.BareHeadSmallBackpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-10 (INBOX 2026-09-25 ④) — 기본 겉모습은 **맨머리 + 작은 배낭**이다 (DESIGN 3.5 「기본 겉모습」).
// 전에는 기본 사냥 모자가 늘 씌워져 있어 진짜 모자를 써도 머리가 그대로였고, 기본 배낭이 진짜 배낭과
// 같은 크기라 배낭을 멘 것도 티가 안 났다. 판단은 이름이 아니라 **붙은 메시와 그 크기**로 한다.
bool FSnarehavenDefaultLookTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateEquipTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr || !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyEquipTestWorld(World);
		return false;
	}
	UEquipmentComponent* const Equipment = Player->GetEquipment();
	Equipment->RefreshEquippedVisuals();   // 몸이 선 뒤에야 소켓이 있다 (게임에서도 BeginPlay 가 이 순서다)

	// ① 머리는 비어 있다 — 기본 모자가 없어야 진짜 모자를 썼을 때 달라진 것이 보인다.
	const UStaticMeshComponent* const Head = Equipment->GetEquippedMesh(EEquipSlot::Head);
	TestTrue(TEXT("기본 겉모습에 모자가 없다(맨머리)"), Head == nullptr || Head->GetStaticMesh() == nullptr);

	// ② 등에는 배낭이 있다 — 레퍼런스 시트 `character_player.md` "등에 배낭 — 항상".
	const UStaticMeshComponent* const Back = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
	if (!TestNotNull(TEXT("등에 기본 배낭이 붙었다"), Back) || !TestTrue(TEXT("기본 배낭 메시가 있다"), Back->GetStaticMesh() != nullptr))
	{
		DestroyEquipTestWorld(World);
		return false;
	}
	// 크기는 **같은 소켓에 붙은 같은 메시**끼리 견준다 — 소켓이 같으니 상대 배율의 비 = 화면에서 보이는 비다.
	// 월드 배율은 몸 뼈대가 갱신된 뒤에야 따라와서, 건 직후에 읽으면 아직 옛 값을 준다(첫 시험이 이걸로 틀렸다).
	const auto ShapeOf = [](const UStaticMeshComponent* Visual)
		{
			return Visual != nullptr && Visual->GetStaticMesh() != nullptr
				? Visual->GetStaticMesh()->GetBounds().BoxExtent * 2.f * Visual->GetRelativeScale3D()
				: FVector::ZeroVector;
		};
	const FVector SmallShape = ShapeOf(Back);
	const FName BackSocket = Back->GetAttachSocketName();

	// ③ 기본 겉모습은 **든 것이 아니다** — 목록이 비어 있어야 배낭 칸·HUD 버튼이 안 생긴다 (CLAUDE.md 8).
	TestTrue(TEXT("기본 배낭은 장착 목록에 없다"), Equipment->GetEquippedItem(EEquipSlot::Backpack).IsNone());

	// ④ 진짜 배낭을 메면 같은 메시가 **눈에 띄게 커진다** — 표에서 행을 찾아 든다(행 이름을 검사에 안 적는다).
	const FName BackpackRow = Equipment->FindEquipmentRowByItemID(FName(TEXT("item.hiking_backpack")));
	if (!TestFalse(TEXT("표에 하이킹 배낭 장비 행이 있다"), BackpackRow.IsNone())
		|| !TestTrue(TEXT("진짜 배낭을 멨다"), Equipment->EquipItem(BackpackRow)))
	{
		DestroyEquipTestWorld(World);
		return false;
	}
	const UStaticMeshComponent* const Worn = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
	const FVector WornShape = ShapeOf(Worn);
	TestEqual(TEXT("기본 배낭과 진짜 배낭이 같은 소켓에 붙는다 (그래야 크기를 견줄 수 있다)"),
		Worn->GetAttachSocketName(), BackSocket);
	const double Ratio = SmallShape.GetMax() / FMath::Max(WornShape.GetMax(), UE_KINDA_SMALL_NUMBER);
	TestTrue(FString::Printf(TEXT("기본 배낭이 진짜 배낭의 %.2f배 = DESIGN 3.5 의 %.2f"), Ratio, Equipment->GetDefaultLookScale()),
		FMath::IsNearlyEqual(Ratio, static_cast<double>(Equipment->GetDefaultLookScale()), 0.01));

	// ⑤ 벗으면 다시 작은 기본 배낭으로 돌아온다 (등이 비는 것이 아니다).
	TestTrue(TEXT("진짜 배낭을 벗었다"), Equipment->UnequipSlot(EEquipSlot::Backpack));
	const FVector AgainShape = ShapeOf(Equipment->GetEquippedMesh(EEquipSlot::Backpack));
	TestTrue(TEXT("벗은 뒤 다시 작은 기본 배낭"), FMath::IsNearlyEqual(AgainShape.GetMax(), SmallShape.GetMax(), 0.1));

	AddInfo(FString::Printf(TEXT("[DefaultLook] 머리 %s · 기본 배낭 긴 변 %.1fcm · 진짜 배낭 %.1fcm · 비 %.2f · 벗은 뒤 %.1fcm"),
		(Head == nullptr || Head->GetStaticMesh() == nullptr) ? TEXT("비었다") : TEXT("모자가 있다"),
		SmallShape.GetMax(), WornShape.GetMax(), Ratio, AgainShape.GetMax()));

	DestroyEquipTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGearMemoryTest, "Snarehaven.Equipment.Memory.KeepsToolAcrossMaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-10 (INBOX 2026-09-25 ④ "든 도구가 맵 이동 뒤 사라짐") — 존을 옮겨 폰이 새로 생겨도 도구·배낭이 손에 남는다.
// 맵 이동 자체(`ServerTravel`)는 2층에서 못 돌리므로 **같은 길의 두 끝**을 탄다: 떠나는 폰의 `EndPlay`(파괴)와
// 새 폰의 `BeginPlay`·조종 붙음. 모자는 서버가 기억하므로(`GetBaseState` 의 `hat`) 기기 기억에서 빠져야 한다.
bool FSnarehavenGearMemoryTest::RunTest(const FString& Parameters)
{
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareGearMemorySubsystem* const Memory = GameInstance->GetSubsystem<USnareGearMemorySubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("장비 기억"), Memory))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	Memory->Forget();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 내가 조종하는 폰만 기억·복원한다 — 조종을 붙여 줘야 `IsLocallyControlled` 가 참이 된다.
	const auto TakeControl = [World, &Params](ASnarehavenCharacter* Pawn)
		{
			APlayerController* const Controller = World->SpawnActor<APlayerController>(Params);
			Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
			Pawn->Controller = Controller;
		};

	// ① 떠나는 폰 — 도구·배낭·모자를 걸고 맵을 떠난다(파괴).
	ASnarehavenCharacter* const Leaving = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("떠나는 폰"), Leaving))
	{
		return false;
	}
	Leaving->DispatchBeginPlay();
	TakeControl(Leaving);
	UEquipmentComponent* const LeavingGear = Leaving->GetEquipment();
	const FName ToolRow = LeavingGear->FindEquipmentRowByItemID(FName(TEXT("item.hatchet")));
	const FName BackpackRow = LeavingGear->FindEquipmentRowByItemID(FName(TEXT("item.hiking_backpack")));
	const FName HatRow = LeavingGear->FindEquipmentRowByItemID(FName(TEXT("item.hunter_hat")));
	if (!TestFalse(TEXT("표에 손도끼·배낭·모자 행이 있다"), ToolRow.IsNone() || BackpackRow.IsNone() || HatRow.IsNone()))
	{
		return false;
	}
	TestTrue(TEXT("손도끼를 들었다"), LeavingGear->EquipItem(ToolRow));
	TestTrue(TEXT("배낭을 멨다"), LeavingGear->EquipItem(BackpackRow));
	TestTrue(TEXT("모자를 썼다"), LeavingGear->EquipItem(HatRow));

	Leaving->Destroy();
	const TArray<FEquippedSlot> Remembered = Memory->GetRememberedSlots();
	TestEqual(TEXT("기억한 칸 = 도구·배낭 둘 (모자는 서버 몫이라 빠진다)"), Remembered.Num(), 2);
	TestFalse(TEXT("모자는 기억하지 않는다"), Remembered.ContainsByPredicate(
		[](const FEquippedSlot& Entry) { return Entry.Slot == EEquipSlot::Head; }));

	// ② 새 맵의 폰 — 아직 내 폰이 아닐 때는 아무것도 안 걸고, 조종이 붙는 순간 되건다.
	ASnarehavenCharacter* const Arriving = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("도착한 폰"), Arriving))
	{
		return false;
	}
	Arriving->DispatchBeginPlay();
	UEquipmentComponent* const ArrivingGear = Arriving->GetEquipment();
	TestTrue(TEXT("조종이 붙기 전에는 아무것도 안 건다"), ArrivingGear->GetEquippedSlots().IsEmpty());

	TakeControl(Arriving);
	Arriving->NotifyControllerChanged();
	TestEqual(TEXT("도착 폰 도구 = 손도끼"), ArrivingGear->GetEquippedItem(EEquipSlot::Tool), ToolRow);
	TestEqual(TEXT("도착 폰 등 = 하이킹 배낭"), ArrivingGear->GetEquippedItem(EEquipSlot::Backpack), BackpackRow);
	TestTrue(TEXT("도착 폰 머리는 비었다 (모자는 서버 답이 씌운다)"),
		ArrivingGear->GetEquippedItem(EEquipSlot::Head).IsNone());
	TestNotNull(TEXT("손도끼 메시가 실제로 붙었다"), ArrivingGear->GetEquippedMesh(EEquipSlot::Tool));

	AddInfo(FString::Printf(TEXT("[Gear] 기억 %d칸 · 도착 폰 도구 %s · 등 %s · 머리 %s"),
		Remembered.Num(), *ArrivingGear->GetEquippedItem(EEquipSlot::Tool).ToString(),
		*ArrivingGear->GetEquippedItem(EEquipSlot::Backpack).ToString(),
		*ArrivingGear->GetEquippedItem(EEquipSlot::Head).ToString()));

	Memory->Forget();
	return true;
}

namespace
{
	// M39-06a·08b — 총 행이 제 메시(`make_weapon_meshes.py`)를 가리키고, 들면 오른손 `Socket_Weapon` 에 **실제로 붙는다**.
	// 메시 전에는 `ApplyEquipVisual` 이 메시 없는 행을 거절해 사도 손에 못 들었다. 크기는 DESIGN 8.6-c 「<Label> 메시 길이」 줄에서
	// 읽는다(시험에 숫자를 안 적는다). 삼각형 한도 1,500 은 명세 `hunting-gun.md` 4절 값.
	bool TestGunMesh(FAutomationTestBase& Test, const TCHAR* ItemID, const TCHAR* Label)
	{
		constexpr int32 TriangleLimit = 1500;
		constexpr double SizeToleranceCm = 1.0;
		FString Doc;
		if (!Test.TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
		{
			return false;
		}
		FRegexMatcher Match(FRegexPattern(FString(Label) + TEXT(" 메시 길이 \\(`(SM_Weapon_\\w+)`[^|]*\\|\\s*([0-9.]+)cm")), Doc);
		if (!Test.TestTrue(FString::Printf(TEXT("DESIGN 8.6-c 「%s 메시 길이」 줄이 있다"), Label), Match.FindNext()))
		{
			return false;
		}
		const FString WantMesh = Match.GetCaptureGroup(1);
		const double WantCm = FCString::Atod(*Match.GetCaptureGroup(2));

		UWorld* World = CreateEquipTestWorld();
		ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
		if (!Test.TestNotNull(TEXT("플레이어"), Player) || !Test.TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
		{
			DestroyEquipTestWorld(World);
			return false;
		}
		UEquipmentComponent* Equipment = Player->GetEquipment();
		const FName GunRow = Equipment->FindEquipmentRowByItemID(FName(ItemID));
		const UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
		const FItemRow* Row = ItemTable != nullptr && !GunRow.IsNone() ? ItemTable->FindRow<FItemRow>(GunRow, TEXT("GunMesh")) : nullptr;
		if (!Test.TestNotNull(FString::Printf(TEXT("표에 %s 장비 행이 있다"), Label), Row))
		{
			DestroyEquipTestWorld(World);
			return false;
		}
		Test.TestEqual(TEXT("행 메시 = 설계서 줄의 메시"), Row->EquipMeshRef.ToSoftObjectPath().GetAssetName(), WantMesh);
		Test.TestFalse(TEXT("원점이 쥐는 점이라 GripOffset 없음"), Row->GripOffset.IsSet());
		UStaticMesh* Mesh = Row->EquipMeshRef.LoadSynchronous();
		if (!Test.TestNotNull(FString::Printf(TEXT("%s 메시가 읽힌다"), Label), Mesh))
		{
			DestroyEquipTestWorld(World);
			return false;
		}
		const int32 Triangles = Mesh->GetNumTriangles(0);
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Size = Box.GetSize();
		Test.AddInfo(FString::Printf(TEXT("[GunMesh] %s 삼각형 %d · 크기 %.1f×%.1f×%.1fcm · 표 %.1f · 슬롯 %d"),
			*Mesh->GetName(), Triangles, Size.X, Size.Y, Size.Z, WantCm, Mesh->GetStaticMaterials().Num()));
		Test.TestTrue(FString::Printf(TEXT("삼각형 %d ∈ (0, %d]"), Triangles, TriangleLimit), Triangles > 0 && Triangles <= TriangleLimit);
		Test.TestTrue(FString::Printf(TEXT("총구 방향 X 길이 %.1f ≈ 표 %.1f"), Size.X, WantCm), FMath::IsNearlyEqual(Size.X, WantCm, SizeToleranceCm));
		Test.TestTrue(TEXT("가장 긴 변 = 총구 방향 X"), Size.X >= Size.Y && Size.X >= Size.Z);
		Test.TestTrue(TEXT("원점(쥐는 점)이 메시 안"), Box.IsInside(FVector::ZeroVector));
		for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
		{
			Test.TestTrue(FString::Printf(TEXT("슬롯 %s 에 색 머티리얼"), *Slot.MaterialSlotName.ToString()),
				Slot.MaterialInterface != nullptr && Slot.MaterialInterface->GetName().StartsWith(TEXT("MI_Weapon_Gun_")));
		}
	#if WITH_EDITORONLY_DATA
		Test.TestFalse(TEXT("Nanite 꺼짐(모바일 렌더러)"), Mesh->NaniteSettings.bEnabled);
	#endif

		// 들면 오른손 소켓에 이 메시가 붙는다 — 화면 크기 = 표 값 × 보임 배율.
		if (Test.TestTrue(FString::Printf(TEXT("%s 을 손에 들었다"), Label), Equipment->EquipItem(GunRow)))
		{
			const UStaticMeshComponent* Visual = Equipment->GetEquippedMesh(EEquipSlot::Weapon);
			if (Test.TestNotNull(TEXT("무기 칸 메시 컴포넌트"), Visual))
			{
				Test.TestEqual(TEXT("붙은 메시 = 행 메시"), Visual->GetStaticMesh().Get(), Mesh);
				Test.TestEqual(TEXT("오른손 소켓"), Visual->GetAttachSocketName(), FName(WeaponSocketName));
				Test.TestEqual(TEXT("몸에 붙었다"), Visual->GetAttachParent(), Cast<USceneComponent>(Player->GetMesh()));
				const double WorldLong = (Size * Visual->GetComponentScale()).GetMax();
				Test.AddInfo(FString::Printf(TEXT("[GunMesh] 손에 든 긴 변 %.1fcm (배율 %.2f)"), WorldLong, Visual->GetComponentScale().X));
				// 표 길이 × 손에 든 총 보임 배율(DESIGN 8.6-c, 루프#1134) — 게임 카메라에서 실루엣이 읽히게 키운 값.
				const float WantHeld = WantCm * UEquipmentComponent::HeldGunScale;
				Test.TestTrue(FString::Printf(TEXT("손에 든 긴 변 %.1f ≈ 표 %.1f × %.1f"), WorldLong, WantCm, UEquipmentComponent::HeldGunScale),
					FMath::IsNearlyEqual(WorldLong, WantHeld, SizeToleranceCm * UEquipmentComponent::HeldGunScale));
			}
		}

		// 이 메시를 드는 행은 하나 — 권총·소총이 서로 메시를 빌리지 않는다.
		int32 Users = 0;
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			Users += reinterpret_cast<const FItemRow*>(Pair.Value)->EquipMeshRef.ToSoftObjectPath().GetAssetName() == WantMesh ? 1 : 0;
		}
		Test.TestEqual(TEXT("이 메시를 드는 행 = 1"), Users, 1);

		DestroyEquipTestWorld(World);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPistolMeshTest, "Snarehaven.Combat.Gun.PistolMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPistolMeshTest::RunTest(const FString& Parameters)
{
	return TestGunMesh(*this, TEXT("item.pistol_semi"), TEXT("권총"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRifleMeshTest, "Snarehaven.Combat.Gun.RifleMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRifleMeshTest::RunTest(const FString& Parameters)
{
	// M39-08b — 소총은 원점이 손잡이 위라 개머리가 뒤로 30cm 나가도 `GripOffset` 없이 같은 단정을 지킨다.
	return TestGunMesh(*this, TEXT("item.rifle_modern"), TEXT("소총"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
