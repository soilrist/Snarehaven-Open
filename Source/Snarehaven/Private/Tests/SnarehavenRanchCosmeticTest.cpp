// 목장 동물 꾸미기 2층 검사 (DESIGN 10.9) — 씌우기·벗기기는 서버가 판정하고 화면은 응답 뒤에만 바뀌는가 (CLAUDE.md 8):
// ① `QueueSetCosmetic` 이면 `SetPetCosmetic {slot, itemId}` 를 보내고, 응답 전·다시 받기 전엔 칸 꾸미기가 그대로다,
//    `GetBaseState` 칸 `cosmetic` 을 받은 뒤에야 칸에 들어간다. 거절이면 그대로. 벗기기는 `itemId: ""`.
// ② 펫 몸에 씌우면 꾸미기 메시가 보이고 몸 위쪽 절반 높이에 붙는다, nullptr 이면 숨는다.
// ③ 스킨 조각 n — 리본·목걸이는 전용 메시(모자 자리표시 아님, 삼각형 ≤ 1500)이고, 돼지·공작에서 리본 밑면은 머리 뼈 위,
//    목걸이 원점(고리 가운데)은 목 뼈(새는 `neck_01`)에서 행 `PetNeckCenterCm` 만큼 옮긴 목 단면 가운데에 있고 고리가 목에 직각으로
//    그 자리를 두른다(`docs/specs/skin-parts.md` 6장 n · M40-02 감독 보정 — 뼈는 목 뒤쪽·가슴 속이라 뼈를 두르면 훌라후프가 됐다).
// ④ M35-04 — 공개 문서 「펫 의상」 가챠 경품마다 `DT_ItemTable` 에 목장 꾸미기 행(등급 = 문서, 메시·뼈·폭)이 있고 펫 몸에 붙는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ranch.Cosmetic; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchPetActor.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/WaterTravelComponent.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestSlot = 0;
	const FName TestAnimal = TEXT("animal.pig");
	const FName TestRibbon = TEXT("item.ranch_ribbon");
	const FName TestHat = TEXT("item.ranch_hat");

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 가짜 서버 — `SnarehavenRanchVariantTest` 와 같은 짜임. */
	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<TSharedPtr<FJsonObject>> Args;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& InArgs, FDone OnDone)
				{
					Functions.Add(Name);
					Args.Add(InArgs);
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(bOk, Response, FString());
		}
	};

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	/** 서버 `GetBaseState` 모양 — 0번 칸에 돼지, `cosmetic` 은 빈 문자열이면 뺀다. */
	TSharedPtr<FJsonObject> MakeBaseState(const FString& Cosmetic)
	{
		const FString CosmeticField = Cosmetic.IsEmpty() ? FString() : FString::Printf(TEXT(",\"cosmetic\":\"%s\""), *Cosmetic);
		return ParseJson(FString::Printf(
			TEXT("{\"ranch\":[{\"slot\":%d,\"animalId\":\"%s\",\"occupiedSinceUtc\":\"2026-10-04T01:00:00.000Z\",\"stock\":0%s},null,null,null,null]}"),
			TestSlot, *TestAnimal.ToString(), *CosmeticField));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCosmeticSyncTest, "Snarehaven.Ranch.Cosmetic.Sync.AppliesOnlyAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCosmeticSyncTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	URanchComponent* const Ranch = NewObject<URanchComponent>(GetTransientPackage());
	URanchComponent* const Fishing = NewObject<URanchComponent>(GetTransientPackage());
	Fishing->SetStationKind(ERanchStationKind::Fishing);
	URanchSyncComponent* const Sync = NewObject<URanchSyncComponent>(GetTransientPackage());
	Sync->BindTargets(Ranch, Fishing, Backend, NewObject<USnareBaseSubsystem>(GameInstance),
		NewObject<UWaterTravelComponent>(GetTransientPackage()));

	// 시작: 0번 칸에 맨몸 돼지.
	TestTrue(TEXT("GetBaseState 를 묻는다"), Sync->RequestBaseState());
	Server.Answer(true, MakeBaseState(FString()));
	const FRanchSlotState* Slot = Ranch->GetSlot(TestSlot);
	if (!TestTrue(TEXT("돼지 칸이 섰다"), Slot != nullptr && Slot->AnimalID == TestAnimal))
	{
		return false;
	}
	TestTrue(TEXT("처음엔 맨몸"), Slot->CosmeticItemID.IsNone());
	TestFalse(TEXT("빈 칸엔 못 씌운다"), Ranch->QueueSetCosmetic(TestSlot + 1, TestRibbon));
	TestFalse(TEXT("맨몸에 벗기기는 안 쌓인다"), Ranch->QueueSetCosmetic(TestSlot, NAME_None));

	// ① 씌우기 — 요청만, 칸은 그대로.
	const int32 CallsBefore = Server.Functions.Num();
	TestTrue(TEXT("씌우기 요청이 쌓인다"), Ranch->QueueSetCosmetic(TestSlot, TestRibbon));
	if (!TestEqual(TEXT("요청 한 번"), Server.Functions.Num(), CallsBefore + 1))
	{
		return false;
	}
	TestEqual(TEXT("함수 = SetPetCosmetic"), Server.Functions.Last(), FString(TEXT("SetPetCosmetic")));
	TestEqual(TEXT("인자 slot"), static_cast<int32>(Server.Args.Last()->GetNumberField(TEXT("slot"))), TestSlot);
	TestEqual(TEXT("인자 itemId"), Server.Args.Last()->GetStringField(TEXT("itemId")), TestRibbon.ToString());
	TestTrue(TEXT("응답 전 칸 꾸미기 그대로(맨몸)"), Ranch->GetSlot(TestSlot)->CosmeticItemID.IsNone());

	const TSharedPtr<FJsonObject> Ok = ParseJson(FString::Printf(
		TEXT("{\"slot\":%d,\"cosmetic\":\"%s\",\"returned\":null}"), TestSlot, *TestRibbon.ToString()));
	Server.Answer(true, Ok);
	TestEqual(TEXT("성공 뒤 GetBaseState 로 다시 묻는다"), Server.Functions.Last(), URanchSyncComponent::GetBaseStateFunctionName());
	TestTrue(TEXT("다시 받기 전엔 그대로(클라이언트가 씌우지 않는다)"), Ranch->GetSlot(TestSlot)->CosmeticItemID.IsNone());
	Server.Answer(true, MakeBaseState(TestRibbon.ToString()));
	TestEqual(TEXT("다시 받은 뒤 리본"), Ranch->GetSlot(TestSlot)->CosmeticItemID, TestRibbon);
	TestFalse(TEXT("같은 것을 또 씌우진 않는다"), Ranch->QueueSetCosmetic(TestSlot, TestRibbon));

	// 거절 — 칸 그대로.
	TestTrue(TEXT("모자로 바꾸기 요청"), Ranch->QueueSetCosmetic(TestSlot, TestHat));
	const TSharedPtr<FJsonObject> Refused = ParseJson(TEXT("{\"error\":\"시험 거절\",\"code\":\"not_enough_items\"}"));
	Server.Answer(true, Refused);
	TestEqual(TEXT("거절이면 리본 그대로"), Ranch->GetSlot(TestSlot)->CosmeticItemID, TestRibbon);
	TestFalse(TEXT("거절 뒤 대기 요청 없음"), Ranch->HasPendingRequest(TestSlot));

	// 벗기기 — itemId "" 로 보낸다.
	TestTrue(TEXT("벗기기 요청"), Ranch->QueueSetCosmetic(TestSlot, NAME_None));
	TestEqual(TEXT("벗기기 itemId = 빈 문자열"), Server.Args.Last()->GetStringField(TEXT("itemId")), FString());
	TestEqual(TEXT("응답 전 리본 그대로"), Ranch->GetSlot(TestSlot)->CosmeticItemID, TestRibbon);

	AddInfo(FString::Printf(TEXT("[Cosmetic] 전송 - 호출 %s"), *FString::Join(Server.Functions, TEXT(","))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCosmeticPetTest, "Snarehaven.Ranch.Cosmetic.Pet.SitsOnUpperBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCosmeticPetTest::RunTest(const FString& Parameters)
{
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	const FAnimalRow* const Pig = AnimalTable != nullptr ? AnimalTable->FindRow<FAnimalRow>(TEXT("Animal_Pig"), TEXT("CosmeticTest")) : nullptr;
	if (!TestNotNull(TEXT("Animal_Pig 행"), Pig) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		return false;
	}
	Slot->ShowPet(Pig);
	ARanchPetActor* const Pet = Slot->GetPet();
	if (!TestNotNull(TEXT("돼지 몸"), Pet))
	{
		return false;
	}
	const USkeletalMeshComponent* const Body = Pet->FindComponentByClass<USkeletalMeshComponent>();
	const UStaticMeshComponent* const Mesh = Pet->GetCosmeticMesh();
	if (!TestNotNull(TEXT("몸 메시"), Body) || !TestNotNull(TEXT("꾸미기 메시 컴포넌트"), Mesh))
	{
		return false;
	}
	TestFalse(TEXT("처음엔 안 보인다"), Mesh->IsVisible());

	const FBox BodyBox = Body->Bounds.GetBox();
	for (const FName ItemID : { TestRibbon, TestHat, FName(TEXT("item.ranch_necklace")) })
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *ItemID.ToString()), Row))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s 은 목장 꾸미기 종류"), *ItemID.ToString()), Row->ItemType == EItemType::PetCosmetic);
		Pet->SetCosmetic(Row);
		TestEqual(TEXT("씌운 ID"), Pet->GetCosmeticItemID(), ItemID);
		TestTrue(*FString::Printf(TEXT("%s 메시가 보인다"), *ItemID.ToString()), Mesh->IsVisible() && Mesh->GetStaticMesh() != nullptr);
		// 붙은 자리(컴포넌트 원점)로 본다 — 목걸이는 고리 가운데가 원점이고 고리·방울이 목 아래로 늘어져
		// 경계 가운데는 목보다 낮다(스킨 조각 n 부터 목 굵기로 벌린다).
		const float Z = Mesh->GetComponentLocation().Z;
		const float MidZ = BodyBox.GetCenter().Z;
		// 머리·목 = 몸 위쪽 절반, 머리 위 모자는 몸 상자보다 조금 위로 나와도 된다(몸 키의 절반까지).
		TestTrue(*FString::Printf(TEXT("%s 몸 위쪽 절반 높이"), *ItemID.ToString()),
			Z >= MidZ && Z <= BodyBox.Max.Z + BodyBox.GetSize().Z * 0.5f);
		AddInfo(FString::Printf(TEXT("[Cosmetic] %s 뼈 %s · 높이 %.0fcm (몸 %.0f~%.0f) · 폭 %.0fcm / 몸 폭 %.0fcm"),
			*ItemID.ToString(), *Row->EquipSocketName.ToString(), Z, BodyBox.Min.Z, BodyBox.Max.Z,
			Mesh->Bounds.BoxExtent.Y * 2.f, BodyBox.GetSize().Y));
	}

	Pet->SetCosmetic(nullptr);
	TestTrue(TEXT("벗기면 맨몸"), Pet->GetCosmeticItemID().IsNone());
	TestFalse(TEXT("벗기면 안 보인다"), Mesh->IsVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCosmeticFitTest, "Snarehaven.Ranch.Cosmetic.Pet.RibbonNecklaceFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCosmeticFitTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 (Verification.md 6) — 한도 1500 은 명세 4장 스태틱 꾸미기 한도, 1cm 는 부동소수 여유.
	constexpr int32 TriangleLimit = 1500;
	constexpr float ToleranceCm = 1.f;
	// 목걸이 메시 안쪽 지름 / 바깥 폭 — 관 굵기 r 0.017 · 반지름 R 0.12 (`ranch_cosmetic_make.py`): (R − r) / (R + r) ≈ 0.75.
	constexpr float NecklaceInnerPerOuter = (0.12f - 0.017f) / (0.12f + 0.017f);
	const FName Necklace = TEXT("item.ranch_necklace");
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_AnimalTable"), AnimalTable) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	const FItemRow* const RibbonRow = SnareItemRows::FindByItemID(ItemTable, TestRibbon);
	const FItemRow* const NecklaceRow = SnareItemRows::FindByItemID(ItemTable, Necklace);
	if (!TestNotNull(TEXT("리본 행"), RibbonRow) || !TestNotNull(TEXT("목걸이 행"), NecklaceRow))
	{
		return false;
	}
	for (const FItemRow* const Row : { RibbonRow, NecklaceRow })
	{
		const FString Path = Row->EquipMeshRef.ToSoftObjectPath().ToString();
		TestTrue(*FString::Printf(TEXT("%s 전용 메시(Equipment/Pet) — %s"), *Row->ItemID.ToString(), *Path),
			Path.Contains(TEXT("/Equipment/Pet/")));
		const UStaticMesh* const Mesh = Row->EquipMeshRef.LoadSynchronous();
		if (TestNotNull(*FString::Printf(TEXT("%s 메시 로드"), *Row->ItemID.ToString()), Mesh))
		{
			TestTrue(*FString::Printf(TEXT("%s 삼각형 %d ≤ %d"), *Row->ItemID.ToString(), Mesh->GetNumTriangles(0), TriangleLimit),
				Mesh->GetNumTriangles(0) <= TriangleLimit);
		}
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	for (const TCHAR* const AnimalName : { TEXT("Animal_Pig"), TEXT("Animal_Peacock") })
	{
		const FAnimalRow* const Animal = AnimalTable->FindRow<FAnimalRow>(AnimalName, TEXT("CosmeticFitTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), AnimalName), Animal))
		{
			continue;
		}
		ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		Slot->ShowPet(Animal);
		ARanchPetActor* const Pet = Slot->GetPet();
		const USkeletalMeshComponent* const Body = Pet != nullptr ? Pet->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 몸"), AnimalName), Body))
		{
			continue;
		}
		UStaticMeshComponent* const Mesh = Pet->GetCosmeticMesh();
		// 새들은 목이 마디로 갈려 `neck_01`(목 밑동)이 목 뼈다 — `SetCosmetic` 과 같은 규칙.
		const FName NeckBone = Body->DoesSocketExist(NecklaceRow->EquipSocketName) ? NecklaceRow->EquipSocketName
			: FName(*(NecklaceRow->EquipSocketName.ToString() + TEXT("_01")));
		TestTrue(*FString::Printf(TEXT("%s 뼈 head·%s"), AnimalName, *NeckBone.ToString()),
			Body->DoesSocketExist(RibbonRow->EquipSocketName) && Body->DoesSocketExist(NeckBone));

		Pet->SetCosmetic(RibbonRow);
		const FVector Head = Body->GetSocketLocation(RibbonRow->EquipSocketName);
		const FBox RibbonBox = Mesh->Bounds.GetBox();
		TestTrue(*FString::Printf(TEXT("%s 리본 밑면 %.1f ≥ 머리 뼈 %.1f"), AnimalName, RibbonBox.Min.Z, Head.Z),
			RibbonBox.Min.Z >= Head.Z - ToleranceCm);

		Pet->SetCosmetic(NecklaceRow);
		const FVector Neck = Body->GetSocketLocation(NeckBone);
		// 고리 자리 = 목 뼈 + 행 목 단면 가운데(컴포넌트 축, 액터 배율 전).
		const FVector RingAt = Body->GetComponentTransform().TransformPosition(
			Body->GetSocketTransform(NeckBone, RTS_Component).GetLocation() + Animal->PetNeckCenterCm);
		const FVector Origin = Mesh->GetComponentLocation();
		const FBox NecklaceBox = Mesh->Bounds.GetBox();
		// 고리는 목에 직각 — 메시 위(+Z)가 목 → 머리 방향(돼지는 앞으로 누운 목이라 고리를 눕히면 목 속에 묻힌다).
		const FVector NeckDir = (Body->GetSocketLocation(RibbonRow->EquipSocketName) - Neck).GetSafeNormal();
		const float Alignment = FVector::DotProduct(Mesh->GetUpVector(), NeckDir);
		TestTrue(*FString::Printf(TEXT("%s 목걸이 고리가 목에 직각(위·목 내적 %.2f)"), AnimalName, Alignment), Alignment >= 0.99f);
		// 고리 안쪽이 목보다 넓다 — 키 비율로는 목이 굵은 돼지에서 고리가 목 속에 묻혔다(3층 첫 캡처).
		// 목 굵기는 표 행 값이 메시에서 지금 잰 값과 같아야 한다(스크립트가 채운 뒤 메시가 바뀌면 잡힌다).
		const float Measured = ARanchPetActor::MeasureNeckWidthCm(Body->GetSkeletalMeshAsset());
		TestTrue(*FString::Printf(TEXT("%s 목 굵기 행 %.1f = 잰 값 %.1f"), AnimalName, Animal->PetNeckWidthCm, Measured),
			Measured > 0.f && FMath::IsNearlyEqual(Animal->PetNeckWidthCm, Measured, 0.5f));
		const FVector MeasuredCenter = ARanchPetActor::MeasureNeckCenterCm(Body->GetSkeletalMeshAsset());
		TestTrue(*FString::Printf(TEXT("%s 목 단면 가운데 행 %s = 잰 값 %s"), AnimalName, *Animal->PetNeckCenterCm.ToCompactString(),
			*MeasuredCenter.ToCompactString()), Animal->PetNeckCenterCm.Equals(MeasuredCenter, 0.5f));
		const FBoxSphereBounds RingBounds = Mesh->GetStaticMesh()->GetBounds();
		const float RingOuter = 2.f * FMath::Max(RingBounds.BoxExtent.X, RingBounds.BoxExtent.Y) * Mesh->GetComponentScale().X;
		const float RingInner = RingOuter * NecklaceInnerPerOuter;
		const float NeckWorld = Measured * Pet->GetActorScale3D().X;
		TestTrue(*FString::Printf(TEXT("%s 고리 안쪽 %.1fcm > 목 %.1fcm"), AnimalName, RingInner, NeckWorld), RingInner > NeckWorld);
		TestTrue(*FString::Printf(TEXT("%s 목걸이 원점 ↔ 목 단면 가운데 %.1fcm"), AnimalName, FVector::Dist(Origin, RingAt)),
			FVector::Dist(Origin, RingAt) <= ToleranceCm);
		TestTrue(*FString::Printf(TEXT("%s 목걸이 고리가 목 단면 가운데를 두른다(z %.1f~%.1f ∋ %.1f)"), AnimalName,
			NecklaceBox.Min.Z, NecklaceBox.Max.Z, RingAt.Z), NecklaceBox.IsInsideOrOn(RingAt));
		AddInfo(FString::Printf(TEXT("[CosmeticFit] %s 리본 밑 %.1f/머리 %.1f · 목걸이 원점 (%.1f,%.1f,%.1f) 목 (%.1f,%.1f,%.1f) · 고리 바깥 %.1f 안쪽 %.1f · 목 %.1fcm"),
			AnimalName, RibbonBox.Min.Z, Head.Z, Origin.X, Origin.Y, Origin.Z, Neck.X, Neck.Y, Neck.Z, RingOuter, RingInner, NeckWorld));
		Slot->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCosmeticGachaTest, "Snarehaven.Ranch.Cosmetic.Gacha.DisclosurePetOutfits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCosmeticGachaTest::RunTest(const FString& Parameters)
{
	static const TMap<FString, EItemGrade> GradeByLabel = {
		{ TEXT("커먼"), EItemGrade::Common }, { TEXT("레어"), EItemGrade::Rare },
		{ TEXT("에픽"), EItemGrade::Epic }, { TEXT("레전더리"), EItemGrade::Legendary } };
	FString Doc;
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	const FAnimalRow* const Pig = AnimalTable != nullptr ? AnimalTable->FindRow<FAnimalRow>(TEXT("Animal_Pig"), TEXT("CosmeticTest")) : nullptr;
	if (!TestTrue(TEXT("공개 문서 읽기"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/GACHA_DISCLOSURE.md"))))
		|| !TestNotNull(TEXT("Animal_Pig 행"), Pig) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	// 문서 5칸 줄 `| gacha.x | 이름 | 등급 | 펫 의상 | % |` 만 고른다.
	TArray<TPair<FName, EItemGrade>> Outfits;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.TrimStartAndEnd().ParseIntoArray(Cells, TEXT("|"));
		const EItemGrade* const Grade = Cells.Num() == 5 ? GradeByLabel.Find(Cells[2].TrimStartAndEnd()) : nullptr;
		if (Grade != nullptr && Cells[3].TrimStartAndEnd() == TEXT("펫 의상"))
		{
			Outfits.Add({ FName(*Cells[0].TrimStartAndEnd()), *Grade });
		}
	}
	TestEqual(TEXT("공개 문서 펫 의상 줄"), Outfits.Num(), 6);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		return false;
	}
	Slot->ShowPet(Pig);
	ARanchPetActor* const Pet = Slot->GetPet();
	const USkeletalMeshComponent* const Body = IsValid(Pet) ? Pet->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const UStaticMeshComponent* const Mesh = IsValid(Pet) ? Pet->GetCosmeticMesh() : nullptr;
	if (!TestNotNull(TEXT("몸 메시"), Body) || !TestNotNull(TEXT("꾸미기 메시 컴포넌트"), Mesh))
	{
		return false;
	}
	const FBox BodyBox = Body->Bounds.GetBox();
	for (const TPair<FName, EItemGrade>& Outfit : Outfits)
	{
		const FString Id = Outfit.Key.ToString();
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, Outfit.Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *Id), Row))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s 은 목장 꾸미기 종류"), *Id), Row->ItemType == EItemType::PetCosmetic);
		TestTrue(*FString::Printf(TEXT("%s 등급 = 문서"), *Id), Row->Grade == Outfit.Value);
		TestTrue(*FString::Printf(TEXT("%s 뼈 = head·neck"), *Id),
			Row->EquipSocketName == TEXT("head") || Row->EquipSocketName == TEXT("neck"));
		TestTrue(*FString::Printf(TEXT("%s 폭 비율 > 0"), *Id), Row->PetCosmeticWidthRatio > 0.f);
		Pet->SetCosmetic(Row);
		TestTrue(*FString::Printf(TEXT("%s 메시가 보인다"), *Id), Mesh->IsVisible() && Mesh->GetStaticMesh() != nullptr);
		const float Z = Mesh->GetComponentLocation().Z;
		TestTrue(*FString::Printf(TEXT("%s 몸 위쪽 절반 높이"), *Id),
			Z >= BodyBox.GetCenter().Z && Z <= BodyBox.Max.Z + BodyBox.GetSize().Z * 0.5f);
		AddInfo(FString::Printf(TEXT("[CosmeticGacha] %s 뼈 %s · 높이 %.0fcm (몸 %.0f~%.0f) · 폭 %.0fcm"),
			*Id, *Row->EquipSocketName.ToString(), Z, BodyBox.Min.Z, BodyBox.Max.Z, Mesh->Bounds.BoxExtent.Y * 2.f));
	}
	Pet->SetCosmetic(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchCosmeticGachaMeshTest, "Snarehaven.Ranch.Cosmetic.Gacha.OwnMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchCosmeticGachaMeshTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 (Verification.md 6) — 한도 1500 은 명세 4장 스태틱 꾸미기 한도, 1cm 는 부동소수 여유.
	// 안쪽 지름 / 바깥 폭은 `ranch_cosmetic_make.py` 치수: 깃발 줄 2 × (0.12 − 0.014) / 바깥 폭 0.313(10-17 깃발 75° 늘어뜨림 — 목 × 1.5 면 안쪽 ≈ 목 × 1.02 로 목에 닿는다) · 갈기 고리 (0.122 − 0.022) / 반폭 0.1715
	// · 방울 끈 2 × (0.12 − 0.0036 − 0.0042) / 앞뒤 길이 0.271(방울이 앞으로 늘어져 y 가 더 길다, M40-02)
	// · 리본 띠 2 × (0.12 − 0.003) / 옆 폭 0.284(나비가 옆으로 나온다, M40-04).
	constexpr int32 TriangleLimit = 1500;
	constexpr float ToleranceCm = 1.f;
	// 행 배수 0 = 목걸이 기본 1.4 를 쓴다(DESIGN 10.9). 머리에 앉는 왕관은 고리 단정 대신 밑면 단정.
	constexpr float CollarDefaultPerNeck = 1.4f;
	// M40-02 감독 보정 「고리 지름 목 × 1.1 이하」 — 끈·띠 가운데 지름 / 바깥 폭(0 = 안 잼). 방울 = 옆 바깥 폭 0.253 과
	// 안쪽의 평균 / 0.271 · 리본 띠 = 0.24 / 0.284.
	constexpr float CordMaxPerNeck = 1.1f;
	struct FCase { const TCHAR* Id; const TCHAR* Folder; float InnerPerOuter; float PerNeck; bool bHead; float CordPerOuter = 0.f; };
	const FCase Cases[] = {
		{ TEXT("gacha.pet_crown"), TEXT("/Equipment/Pet/GachaCrown/"), 0.f, 0.f, true },
		{ TEXT("gacha.pet_festival_flags"), TEXT("/Equipment/Pet/GachaFestivalFlags/"), 0.212f / 0.313f, 1.5f, false },
		{ TEXT("gacha.pet_gold_mane"), TEXT("/Equipment/Pet/GachaGoldMane/"), 0.1f / 0.1715f, 1.8f, false },
		{ TEXT("gacha.pet_bell"), TEXT("/Equipment/Pet/GachaBell/"), 0.2244f / 0.271f, 1.25f, false, (0.253f / 0.271f + 0.2244f / 0.271f) * 0.5f },
		{ TEXT("gacha.pet_strawhat"), TEXT("/Equipment/Pet/GachaStrawhat/"), 0.f, 0.f, true },
		{ TEXT("gacha.pet_ribbon"), TEXT("/Equipment/Pet/GachaRibbon/"), 0.234f / 0.284f, 1.3f, false, 0.24f / 0.284f },
		// M40-05 목장 상점 동물 모자 — 가챠 경품은 아니나 같은 머리 받침·전용 메시 단정을 쓴다(농부 모자 빌림 끝).
		{ TEXT("item.ranch_hat"), TEXT("/Equipment/Pet/RanchHat/"), 0.f, 0.f, true },
	};
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_AnimalTable"), AnimalTable) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	for (const FCase& Case : Cases)
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(Case.Id));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), Case.Id), Row))
		{
			return false;
		}
		const FString Path = Row->EquipMeshRef.ToSoftObjectPath().ToString();
		TestTrue(*FString::Printf(TEXT("%s 전용 메시 — %s"), Case.Id, *Path), Path.Contains(Case.Folder));
		// 목장 상점 꾸미기(리본·목걸이·모자)·사람 농부 모자 메시를 빌리지 않는다 — M40 「행끼리 메시 빌려 쓰기 0」
		for (const TCHAR* const RanchId : { TEXT("item.ranch_ribbon"), TEXT("item.ranch_necklace"), TEXT("item.ranch_hat"), TEXT("item.farmer_hat") })
		{
			if (FCString::Strcmp(RanchId, Case.Id) == 0)
			{
				continue;
			}
			const FItemRow* const RanchRow = SnareItemRows::FindByItemID(ItemTable, FName(RanchId));
			TestTrue(*FString::Printf(TEXT("%s 메시 ≠ %s"), Case.Id, RanchId),
				RanchRow == nullptr || RanchRow->EquipMeshRef.ToSoftObjectPath() != Row->EquipMeshRef.ToSoftObjectPath());
		}
		TestFalse(*FString::Printf(TEXT("%s 아이콘"), Case.Id), Row->IconRef.IsNull());
		TestTrue(*FString::Printf(TEXT("%s 목 굵기 배수 %.2f = %.2f"), Case.Id, Row->PetCollarWidthPerNeck, Case.PerNeck),
			FMath::IsNearlyEqual(Row->PetCollarWidthPerNeck, Case.PerNeck));
		const UStaticMesh* const Mesh = Row->EquipMeshRef.LoadSynchronous();
		if (TestNotNull(*FString::Printf(TEXT("%s 메시 로드"), Case.Id), Mesh))
		{
			TestTrue(*FString::Printf(TEXT("%s 삼각형 %d ≤ %d"), Case.Id, Mesh->GetNumTriangles(0), TriangleLimit),
				Mesh->GetNumTriangles(0) <= TriangleLimit);
		}
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	for (const TCHAR* const AnimalName : { TEXT("Animal_Peacock"), TEXT("Animal_Goat"), TEXT("Animal_Pig") })
	{
		const FAnimalRow* const Animal = AnimalTable->FindRow<FAnimalRow>(AnimalName, TEXT("CosmeticGachaMeshTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), AnimalName), Animal))
		{
			continue;
		}
		ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		Slot->ShowPet(Animal);
		ARanchPetActor* const Pet = Slot->GetPet();
		const USkeletalMeshComponent* const Body = Pet != nullptr ? Pet->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 몸"), AnimalName), Body))
		{
			continue;
		}
		UStaticMeshComponent* const Mesh = Pet->GetCosmeticMesh();
		const FName NeckBone = Body->DoesSocketExist(TEXT("neck")) ? FName(TEXT("neck")) : FName(TEXT("neck_01"));
		const FVector Head = Body->GetSocketLocation(TEXT("head"));
		const FVector Neck = Body->GetSocketLocation(NeckBone);
		const FVector RingAt = Body->GetComponentTransform().TransformPosition(
			Body->GetSocketTransform(NeckBone, RTS_Component).GetLocation() + Animal->PetNeckCenterCm);
		const float NeckWorld = ARanchPetActor::MeasureNeckWidthCm(Body->GetSkeletalMeshAsset()) * Pet->GetActorScale3D().X;
		// M40-04 감독 보정 — 고리 자리는 머리 뒤쪽 끝보다 몸 쪽(목 → 머리 축, 메시 단위). 돼지는 머리 안으로 들어가 얼굴을 둘렀다.
		const FVector NeckToHeadCS = Body->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation()
			- Body->GetSocketTransform(NeckBone, RTS_Component).GetLocation();
		const float RingAlong = FVector::DotProduct(Animal->PetNeckCenterCm, NeckToHeadCS.GetSafeNormal());
		const float HeadRear = ARanchPetActor::MeasureHeadRearAlongCm(Body->GetSkeletalMeshAsset());
		TestTrue(*FString::Printf(TEXT("%s 고리 자리 %.1f ≤ 머리 뒤쪽 끝 %.1f(목 → 머리 축)"), AnimalName, RingAlong, HeadRear),
			HeadRear > 0.f && RingAlong <= HeadRear);
		// 10-17 감독 판정 — 돼지 고리 가운데가 목 위쪽 껍질로 떠 등 위에 걸렸다(10-07 리본 목줄과 같은 뿌리). 가운데는 머리 뼈보다 높지 않다.
		if (FCString::Strcmp(AnimalName, TEXT("Animal_Pig")) == 0)
		{
			TestTrue(*FString::Printf(TEXT("돼지 고리 가운데 높이 %.1f ≤ 머리 뼈 %.1f"), RingAt.Z, Head.Z), RingAt.Z <= Head.Z);
		}
		// 10-17 감독 반려 — 돼지 고리가 훌라후프처럼 등선 위로 솟았다. 고리 위끝 ≤ 등선(목 단면 위쪽 = 가운데 + 앞뒤 높이 반)
		// + 여유(앞뒤 높이의 5%, 고리 관 굵기). 멱까지 넣어 재는 몸만(돼지).
		TOptional<float> BackLineZ;
		if (FCString::Strcmp(AnimalName, TEXT("Animal_Pig")) == 0)
		{
			const float NeckDepth = ARanchPetActor::MeasureNeckDepthCm(Body->GetSkeletalMeshAsset());
			const FVector AxisCS = NeckToHeadCS.GetSafeNormal();
			const FVector DepthCS = FVector::CrossProduct(FVector::CrossProduct(AxisCS, FVector::UpVector).GetSafeNormal(), AxisCS);
			const FVector NeckTopCS = Body->GetSocketTransform(NeckBone, RTS_Component).GetLocation() + Animal->PetNeckCenterCm
				+ DepthCS * (0.5f * NeckDepth);
			BackLineZ = Body->GetComponentTransform().TransformPosition(NeckTopCS).Z + 0.05f * NeckDepth * Pet->GetActorScale3D().X;
			TestTrue(*FString::Printf(TEXT("돼지 목 앞뒤 높이 %.1f > 0"), NeckDepth), NeckDepth > 0.f);
		}
		// 목 자리 꾸미기는 전부 같은 자리 — 처음 씌운 것의 원점에 나머지를 맞댄다.
		TOptional<FVector> FirstCollarAt;
		for (const FCase& Case : Cases)
		{
			const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(Case.Id));
			Pet->SetCosmetic(Row);
			const FBox Box = Mesh->Bounds.GetBox();
			if (Case.bHead)
			{
				TestTrue(*FString::Printf(TEXT("%s %s 밑면 %.1f ≥ 머리 뼈 %.1f"), AnimalName, Case.Id, Box.Min.Z, Head.Z),
					Box.Min.Z >= Head.Z - ToleranceCm);
				AddInfo(FString::Printf(TEXT("[GachaMesh] %s %s 밑 %.1f/머리 %.1f · 폭 %.1fcm"), AnimalName, Case.Id, Box.Min.Z, Head.Z,
					2.f * FMath::Max(Box.GetExtent().X, Box.GetExtent().Y)));
				continue;
			}
			const float PerNeck = Case.PerNeck > 0.f ? Case.PerNeck : CollarDefaultPerNeck;
			const FBoxSphereBounds Local = Mesh->GetStaticMesh()->GetBounds();
			const float Outer = 2.f * FMath::Max(Local.BoxExtent.X, Local.BoxExtent.Y) * Mesh->GetComponentScale().X;
			const float Inner = Outer * Case.InnerPerOuter;
			const float Alignment = FVector::DotProduct(Mesh->GetUpVector(), (Head - Neck).GetSafeNormal());
			TestTrue(*FString::Printf(TEXT("%s %s 바깥 %.1f = 목 %.1f × %.1f"), AnimalName, Case.Id, Outer, NeckWorld, PerNeck),
				FMath::IsNearlyEqual(Outer, NeckWorld * PerNeck, ToleranceCm));
			TestTrue(*FString::Printf(TEXT("%s %s 안쪽 %.1f > 목 %.1f"), AnimalName, Case.Id, Inner, NeckWorld), Inner > NeckWorld);
			TestTrue(*FString::Printf(TEXT("%s %s 고리가 목에 직각(%.2f)"), AnimalName, Case.Id, Alignment), Alignment >= 0.99f);
			TestTrue(*FString::Printf(TEXT("%s %s 원점 ↔ 목 단면 가운데 %.1fcm"), AnimalName, Case.Id, FVector::Dist(Mesh->GetComponentLocation(), RingAt)),
				FVector::Dist(Mesh->GetComponentLocation(), RingAt) <= ToleranceCm);
			if (BackLineZ.IsSet())
			{
				// 상자 위끝은 기운 고리에서 실제보다 높다 — 꼭짓점을 월드로 옮겨 가장 높은 점을 잰다.
				const FMeshDescription* const Description = Mesh->GetStaticMesh()->GetMeshDescription(0);
				float TopZ = Box.Max.Z;
				if (Description != nullptr && Description->Vertices().Num() > 0)
				{
					TopZ = TNumericLimits<float>::Lowest();
					for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
					{
						TopZ = FMath::Max(TopZ, static_cast<float>(Mesh->GetComponentTransform().TransformPosition(FVector(Description->GetVertexPosition(Vertex))).Z));
					}
				}
				// 끈·띠만 있는 목걸이(방울·리본)만 단정 — 깃발 삼각기·갈기 털은 관 바깥으로 부푼 장식이라 등선 위로 나온다
				// (10-17: 깃발 +2.9 · 갈기 +7.3cm, 감독 판정 대기). 값은 아래 [GachaMesh] 줄로 남긴다.
				if (Case.CordPerOuter > 0.f)
				{
					TestTrue(*FString::Printf(TEXT("%s %s 고리 위끝 %.1f ≤ 등선 + 여유 %.1f"), AnimalName, Case.Id, TopZ, *BackLineZ),
						TopZ <= *BackLineZ);
				}
				AddInfo(FString::Printf(TEXT("[GachaMesh] %s %s 고리 위끝 %.1f(상자 %.1f) · 등선 + 여유 %.1f"), AnimalName, Case.Id, TopZ, Box.Max.Z, *BackLineZ));
			}
			if (!FirstCollarAt.IsSet())
			{
				FirstCollarAt = Mesh->GetComponentLocation();
			}
			TestTrue(*FString::Printf(TEXT("%s %s 원점 ↔ 다른 목 꾸미기 %.1fcm"), AnimalName, Case.Id, FVector::Dist(Mesh->GetComponentLocation(), *FirstCollarAt)),
				FVector::Dist(Mesh->GetComponentLocation(), *FirstCollarAt) <= ToleranceCm);
			if (Case.CordPerOuter > 0.f)
			{
				const float Cord = Outer * Case.CordPerOuter;
				TestTrue(*FString::Printf(TEXT("%s %s 끈 지름 %.1f ≤ 목 %.1f × %.1f"), AnimalName, Case.Id, Cord, NeckWorld, CordMaxPerNeck),
					Cord <= NeckWorld * CordMaxPerNeck + ToleranceCm);
			}
			AddInfo(FString::Printf(TEXT("[GachaMesh] %s %s 바깥 %.1f 안쪽 %.1f · 목 %.1fcm × %.1f"), AnimalName, Case.Id, Outer, Inner,
				NeckWorld, PerNeck));
		}
		AddInfo(FString::Printf(TEXT("[GachaMesh] %s 고리 자리 %.1f · 머리 뒤쪽 끝 %.1f(목 → 머리 축, 메시 단위)"), AnimalName, RingAlong, HeadRear));
		Slot->Destroy();
	}
	return true;
}

#endif
