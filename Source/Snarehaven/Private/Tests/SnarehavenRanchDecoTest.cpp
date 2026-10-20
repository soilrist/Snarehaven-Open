// 가챠 목장 데코 2층 검사 (M35-05b · `docs/specs/gacha-deco.md` 5장 2 · DESIGN 10.9b):
// ① 공개 문서 「목장 데코」 줄마다 `DT_ItemTable` 에 `RanchDeco` 행이 있고 등급 = 문서, 종류 = 서버 네 칸 중 하나,
//    적용할 것(메시·재질·빛 배수) 이 하나 이상 있으며 그 에셋이 실제로 열린다. 못 판다(판매가 0).
// ② `L_Ranch` 에 종류마다 `RanchDeco.<종류>` 태그 액터가 하나 이상 있다(`tools/unreal/setup_ranch_buildings.py` 가 붙인다).
// ③ (M35-05c) 데코 5종을 하나씩 `URanchInteractComponent::ApplyRanchDeco` 로 입히면 그 종류 태그 액터만 겉모습 메시·재질·빛이
//    표 값이 되고 원래 메시 충돌은 그대로, 비우면(해제) 처음 값으로 돌아온다. 종류가 안 맞는 ID 는 아무것도 안 바꾼다.
//    `GetBaseState.ranchDeco` 읽기(`URanchSyncComponent::ParseRanchDeco`)도 함께.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ranch.Deco; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SnarehavenRanchDecoTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* RanchLevelPath = TEXT("/Game/Game/Maps/L_Ranch.L_Ranch");
	// 서버 `RANCH_DECO_KINDS` 와 같은 네 칸 — 태그 이름 = `RanchDeco.` + 첫 글자 대문자.
	const TArray<TPair<FName, FName>> KindTags = {
		{ TEXT("fence"), TEXT("RanchDeco.Fence") }, { TEXT("path"), TEXT("RanchDeco.Path") },
		{ TEXT("barn"), TEXT("RanchDeco.Barn") }, { TEXT("sky"), TEXT("RanchDeco.Sky") } };

	// 레벨에서 그 태그가 붙은 것과 같은 메시 (`setup_ranch_buildings.py` 의 울타리·대문 돌길·닭장).
	const TCHAR* FenceMeshPath = TEXT("/Game/Game/Environment/Buildings/SM_Building_Fence_02.SM_Building_Fence_02");
	const TCHAR* PathMeshPath = TEXT("/Game/Game/Environment/Buildings/SM_Building_Path_01.SM_Building_Path_01");
	const TCHAR* BarnMeshPath = TEXT("/Game/Game/Environment/Buildings/SM_Building_Coop_01.SM_Building_Coop_01");

	TArray<UStaticMeshComponent*> MeshesWithTag(const AActor* Actor, FName Tag, bool bWith)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		return Meshes.FilterByPredicate([Tag, bWith](const UStaticMeshComponent* Mesh) { return Mesh->ComponentHasTag(Tag) == bWith; });
	}

	double WidthOf(const UStaticMeshComponent* Mesh)
	{
		const FVector Extent = Mesh->GetStaticMesh()->GetBounds().BoxExtent * Mesh->GetRelativeScale3D();
		return FMath::Max(Extent.X, Extent.Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchDecoTableTest, "Snarehaven.Ranch.Deco.Table.DisclosureRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchDecoTableTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchDecoTest;
	static const TMap<FString, EItemGrade> GradeByLabel = {
		{ TEXT("커먼"), EItemGrade::Common }, { TEXT("레어"), EItemGrade::Rare },
		{ TEXT("에픽"), EItemGrade::Epic }, { TEXT("레전더리"), EItemGrade::Legendary } };
	FString Doc;
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestTrue(TEXT("공개 문서 읽기"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/GACHA_DISCLOSURE.md"))))
		|| !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}

	// 문서 5칸 줄 `| gacha.x | 이름 | 등급 | 목장 데코 | % |` 만 고른다.
	TArray<TPair<FName, EItemGrade>> Decos;
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.TrimStartAndEnd().ParseIntoArray(Cells, TEXT("|"));
		const EItemGrade* const Grade = Cells.Num() == 5 ? GradeByLabel.Find(Cells[2].TrimStartAndEnd()) : nullptr;
		if (Grade != nullptr && Cells[3].TrimStartAndEnd() == TEXT("목장 데코"))
		{
			Decos.Add({ FName(*Cells[0].TrimStartAndEnd()), *Grade });
		}
	}
	TestEqual(TEXT("공개 문서 목장 데코 줄"), Decos.Num(), 5);

	int32 TableDecoRows = 0;
	ItemTable->ForeachRow<FItemRow>(TEXT("RanchDecoTest"), [&TableDecoRows](const FName&, const FItemRow& Row)
	{
		TableDecoRows += Row.ItemType == EItemType::RanchDeco ? 1 : 0;
	});
	TestEqual(TEXT("표의 RanchDeco 행 수 = 문서 줄 수"), TableDecoRows, Decos.Num());

	for (const TPair<FName, EItemGrade>& Deco : Decos)
	{
		const FString Id = Deco.Key.ToString();
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, Deco.Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *Id), Row))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s 은 목장 데코 종류"), *Id), Row->ItemType == EItemType::RanchDeco);
		TestTrue(*FString::Printf(TEXT("%s 등급 = 문서"), *Id), Row->Grade == Deco.Value);
		TestEqual(*FString::Printf(TEXT("%s 판매가 0"), *Id), Row->SellPrice, 0);
		TestTrue(*FString::Printf(TEXT("%s 종류 = 네 칸 중 하나"), *Id),
			KindTags.ContainsByPredicate([Row](const TPair<FName, FName>& Pair) { return Pair.Key == Row->RanchDecoKind; }));
		const UStaticMesh* const Mesh = Row->RanchDecoMeshRef.IsNull() ? nullptr : Row->RanchDecoMeshRef.LoadSynchronous();
		const UMaterialInterface* const Material = Row->RanchDecoMaterialRef.IsNull() ? nullptr : Row->RanchDecoMaterialRef.LoadSynchronous();
		TestTrue(*FString::Printf(TEXT("%s 메시 경로가 있으면 열린다"), *Id), Row->RanchDecoMeshRef.IsNull() || Mesh != nullptr);
		TestTrue(*FString::Printf(TEXT("%s 재질 경로가 있으면 열린다"), *Id), Row->RanchDecoMaterialRef.IsNull() || Material != nullptr);
		TestTrue(*FString::Printf(TEXT("%s 바꿀 것이 하나 이상"), *Id),
			Mesh != nullptr || Material != nullptr || Row->RanchDecoSunScale > 0.f);
		TestTrue(*FString::Printf(TEXT("%s 빛 배수는 sky 만"), *Id),
			(Row->RanchDecoKind == TEXT("sky")) == (Row->RanchDecoSunScale > 0.f));
		AddInfo(FString::Printf(TEXT("[RanchDeco] %s 종류 %s · 메시 %s · 재질 %s(슬롯 %s) · 해 ×%.2f 하늘 ×%.2f"),
			*Id, *Row->RanchDecoKind.ToString(), Mesh != nullptr ? *Mesh->GetName() : TEXT("-"),
			Material != nullptr ? *Material->GetName() : TEXT("-"), *Row->RanchDecoMaterialSlot.ToString(),
			Row->RanchDecoSunScale, Row->RanchDecoSkyScale));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchDecoLevelTest, "Snarehaven.Ranch.Deco.Level.EveryKindHasTaggedActor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchDecoLevelTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchDecoTest;
	const UWorld* const Level = LoadObject<UWorld>(nullptr, RanchLevelPath);
	if (!TestNotNull(TEXT("L_Ranch"), Level) || !TestNotNull(TEXT("L_Ranch 레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	TMap<FName, int32> CountByTag;
	for (const AActor* Actor : Level->PersistentLevel->Actors)
	{
		for (const TPair<FName, FName>& Pair : KindTags)
		{
			if (Actor != nullptr && Actor->Tags.Contains(Pair.Value))
			{
				++CountByTag.FindOrAdd(Pair.Value);
			}
		}
	}
	FString Summary;
	for (const TPair<FName, FName>& Pair : KindTags)
	{
		const int32 Count = CountByTag.FindRef(Pair.Value);
		TestTrue(*FString::Printf(TEXT("%s 태그 액터가 있다"), *Pair.Value.ToString()), Count > 0);
		Summary += FString::Printf(TEXT(" %s %d"), *Pair.Value.ToString(), Count);
	}
	AddInfo(FString::Printf(TEXT("[RanchDecoLevel]%s"), *Summary));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchDecoApplyTest, "Snarehaven.Ranch.Deco.Apply.DressAndStrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchDecoApplyTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRanchDecoTest;
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	UStaticMesh* const FenceMesh = LoadObject<UStaticMesh>(nullptr, FenceMeshPath);
	UStaticMesh* const PathMesh = LoadObject<UStaticMesh>(nullptr, PathMeshPath);
	UStaticMesh* const BarnMesh = LoadObject<UStaticMesh>(nullptr, BarnMeshPath);
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable) || !TestNotNull(TEXT("울타리 메시"), FenceMesh)
		|| !TestNotNull(TEXT("돌길 메시"), PathMesh) || !TestNotNull(TEXT("닭장 메시"), BarnMesh))
	{
		return false;
	}

	// ── 서버 답 읽기: 빈 문자열은 빼고, 필드가 없으면 false(캐시 그대로) ──
	{
		TSharedPtr<FJsonObject> Reply;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(
			TEXT("{\"ranchDeco\":{\"fence\":\"gacha.deco_wood_fence\",\"sky\":\"\"}}"));
		TestTrue(TEXT("답 JSON"), FJsonSerializer::Deserialize(Reader, Reply));
		TMap<FName, FName> Deco;
		TestTrue(TEXT("ranchDeco 있음"), URanchSyncComponent::ParseRanchDeco(Reply, Deco));
		TestEqual(TEXT("빈 값은 뺀 1칸"), Deco.Num(), 1);
		TestEqual(TEXT("fence 칸"), Deco.FindRef(TEXT("fence")), FName(TEXT("gacha.deco_wood_fence")));
		TestFalse(TEXT("옛 서버 답(필드 없음)"), URanchSyncComponent::ParseRanchDeco(MakeShared<FJsonObject>(), Deco));
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	// 레벨과 같은 모양의 태그 액터 — 종류마다 하나.
	TMap<FName, AStaticMeshActor*> PieceByKind;
	const TPair<FName, UStaticMesh*> MeshKinds[] = { { TEXT("fence"), FenceMesh }, { TEXT("path"), PathMesh }, { TEXT("barn"), BarnMesh } };
	for (const TPair<FName, UStaticMesh*>& Kind : MeshKinds)
	{
		AStaticMeshActor* const Piece = World->SpawnActor<AStaticMeshActor>();
		Piece->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Piece->GetStaticMeshComponent()->SetStaticMesh(Kind.Value);
		Piece->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Piece->Tags.Add(KindTags.FindByPredicate([&Kind](const TPair<FName, FName>& Pair) { return Pair.Key == Kind.Key; })->Value);
		PieceByKind.Add(Kind.Key, Piece);
	}
	ADirectionalLight* const SunActor = World->SpawnActor<ADirectionalLight>();
	ASkyLight* const SkyActor = World->SpawnActor<ASkyLight>();
	SunActor->Tags.Add(TEXT("RanchDeco.Sky"));
	SkyActor->Tags.Add(TEXT("RanchDeco.Sky"));
	ULightComponent* const Sun = SunActor->GetLightComponent();
	USkyLightComponent* const Sky = SkyActor->GetLightComponent();
	Sun->SetIntensity(10.f);
	Sky->SetIntensity(1.f);
	const FLinearColor SunColor0 = Sun->GetLightColor();
	const FName OverlayTag(TEXT("RanchDeco.Overlay"));

	const TCHAR* DecoIds[] = { TEXT("gacha.deco_wood_fence"), TEXT("gacha.deco_dirt_path"), TEXT("gacha.deco_stone_fence"),
		TEXT("gacha.deco_red_barn"), TEXT("gacha.deco_starlight_bg") };
	FString Summary;
	for (const TCHAR* Id : DecoIds)
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(Id));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), Id), Row))
		{
			continue;
		}
		const FName Kind = Row->RanchDecoKind;
		const int32 Changed = URanchInteractComponent::ApplyRanchDeco(World, { { Kind, FName(Id) } }, ItemTable);
		TestTrue(*FString::Printf(TEXT("%s 바꾼 액터 ≥ 1"), Id), Changed >= 1);

		// 다른 종류 액터는 그대로.
		for (const TPair<FName, AStaticMeshActor*>& Other : PieceByKind)
		{
			const int32 Overlays = MeshesWithTag(Other.Value, OverlayTag, true).Num();
			TestEqual(*FString::Printf(TEXT("%s → %s 겉모습 메시 수"), Id, *Other.Key.ToString()), Overlays, Other.Key == Kind ? 1 : 0);
		}
		if (Kind == TEXT("sky"))
		{
			TestEqual(*FString::Printf(TEXT("%s 해 세기"), Id), Sun->Intensity, 10.f * Row->RanchDecoSunScale, 0.001f);
			TestTrue(*FString::Printf(TEXT("%s 해 색"), Id), Sun->GetLightColor().Equals(Row->RanchDecoSunColor, 0.01f));
			TestEqual(*FString::Printf(TEXT("%s 하늘빛 세기"), Id), Sky->Intensity, 1.f * Row->RanchDecoSkyScale, 0.001f);
			Summary += FString::Printf(TEXT(" %s(해 %.2f·하늘 %.2f)"), Id, Sun->Intensity, Sky->Intensity);
		}
		else
		{
			AStaticMeshActor* const Piece = PieceByKind.FindRef(Kind);
			UStaticMeshComponent* const Base = Piece->GetStaticMeshComponent();
			const TArray<UStaticMeshComponent*> Overlays = MeshesWithTag(Piece, OverlayTag, true);
			if (TestEqual(*FString::Printf(TEXT("%s 겉모습 메시 1"), Id), Overlays.Num(), 1))
			{
				UStaticMeshComponent* const Overlay = Overlays[0];
				UStaticMesh* const Expected = Row->RanchDecoMeshRef.IsNull() ? Base->GetStaticMesh().Get() : Row->RanchDecoMeshRef.LoadSynchronous();
				TestTrue(*FString::Printf(TEXT("%s 겉모습 메시 = 표"), Id), Overlay->GetStaticMesh() == Expected);
				TestEqual(*FString::Printf(TEXT("%s 겉모습 충돌 없음"), Id), Overlay->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
				if (!Row->RanchDecoMaterialRef.IsNull())
				{
					const int32 Slot = Row->RanchDecoMaterialSlot.IsNone() ? 0 : Overlay->GetMaterialIndex(Row->RanchDecoMaterialSlot);
					TestTrue(*FString::Printf(TEXT("%s 재질 슬롯 %d = 표"), Id, Slot),
						Slot != INDEX_NONE && Overlay->GetMaterial(Slot) == Row->RanchDecoMaterialRef.LoadSynchronous());
				}
				TestTrue(*FString::Printf(TEXT("%s 폭 ≤ 원래 폭"), Id), WidthOf(Overlay) <= WidthOf(Base) * 1.01);
				Summary += FString::Printf(TEXT(" %s(%s 폭 %.0f/%.0f)"), Id, *Overlay->GetStaticMesh()->GetName(), WidthOf(Overlay), WidthOf(Base));
			}
			TestTrue(*FString::Printf(TEXT("%s 원래 메시 숨김"), Id), Base->bHiddenInGame);
			TestEqual(*FString::Printf(TEXT("%s 원래 충돌 그대로"), Id), Base->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
		}

		// 해제 = 처음 값.
		URanchInteractComponent::ApplyRanchDeco(World, {}, ItemTable);
		for (const TPair<FName, AStaticMeshActor*>& Piece : PieceByKind)
		{
			TestEqual(*FString::Printf(TEXT("%s 해제 뒤 %s 겉모습 메시 0"), Id, *Piece.Key.ToString()),
				MeshesWithTag(Piece.Value, OverlayTag, true).Num(), 0);
			TestFalse(*FString::Printf(TEXT("%s 해제 뒤 %s 보임"), Id, *Piece.Key.ToString()), Piece.Value->GetStaticMeshComponent()->bHiddenInGame);
		}
		TestEqual(*FString::Printf(TEXT("%s 해제 뒤 해 세기"), Id), Sun->Intensity, 10.f, 0.001f);
		TestTrue(*FString::Printf(TEXT("%s 해제 뒤 해 색"), Id), Sun->GetLightColor().Equals(SunColor0, 0.01f));
		TestEqual(*FString::Printf(TEXT("%s 해제 뒤 하늘빛"), Id), Sky->Intensity, 1.f, 0.001f);
	}

	// 두 번 입혀도 겉모습 메시는 하나(지난 것을 벗기고 입힌다), 종류가 안 맞는 ID 는 아무것도 안 바꾼다.
	URanchInteractComponent::ApplyRanchDeco(World, { { TEXT("fence"), TEXT("gacha.deco_wood_fence") } }, ItemTable);
	URanchInteractComponent::ApplyRanchDeco(World, { { TEXT("fence"), TEXT("gacha.deco_stone_fence") } }, ItemTable);
	TestEqual(TEXT("바꿔 입혀도 겉모습 메시 1"), MeshesWithTag(PieceByKind.FindRef(TEXT("fence")), OverlayTag, true).Num(), 1);
	TestEqual(TEXT("종류 안 맞는 ID 는 0"),
		URanchInteractComponent::ApplyRanchDeco(World, { { TEXT("path"), TEXT("gacha.deco_wood_fence") } }, ItemTable), 0);

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	AddInfo(FString::Printf(TEXT("[RanchDecoApply]%s"), *Summary));
	return true;
}

#endif
