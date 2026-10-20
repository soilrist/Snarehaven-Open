// ROADMAP M15-20 2층 검사 — 엔진 기본 도형(`/Engine/BasicShapes`)이 화면에 붙는 자리를 전수하는 자.
// ① 액터 여섯 종의 기본 객체에서 **보이는** 정적 메시 컴포넌트 중 메시 경로가 `/Engine/` 인 것을 센다.
// ② 표 행(채집물·동물·배치물)에 진짜 메시가 없어 자리표시로 떨어지는 행을 센다.
// 두 결과는 아래 허용 목록과 **정확히 같아야** 한다 — 목록에 없는 새 자리표시가 생겨도, 목록에 있는데 이미 바뀐 줄이 남아도 실패한다.
// (그래서 M15-21 이 자리표시를 바꾸면 이 목록이 짧아진다.) 판정은 이름 패턴이 아니라 실제 붙은 메시 경로다.
// 한계: 기본 객체(CDO)만 본다 — `BeginPlay` 에서 나중에 갈아 끼우는 재질은 못 잰다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.Placeholder; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/GatherableRow.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Gathering/GatherableActor.h"
#include "SnarehavenCharacter.h"
#include "World/DangerZoneActor.h"
#include "World/SeaActor.h"

namespace SnarehavenPlaceholderTest
{
	// 허용 목록 — "클래스.컴포넌트" 와 표 행 ID. 각 줄의 분류(① 우리 메시로 바꿀 수 있음 / ② 감독 에셋 필요 / 유지)는 STATUS 표에 있다.
	const TArray<FString>& AllowedComponents()
	{
		static const TArray<FString> List = {
			TEXT("SeaActor.WaterSurface"),
			TEXT("GatherableActor.Body"),
			TEXT("SnarePlacedStructureActor.Body"),
			TEXT("AnimalCharacter.PlaceholderBody"),
			TEXT("SnarehavenCharacter.PlaceholderBody"),
		};
		return List;
	}

	const TArray<FString>& AllowedRows()
	{
		static const TArray<FString> List = {
			TEXT("map:L_Hunt:Cube"),
		};
		return List;
	}

	bool IsEngineMesh(const UStaticMesh* Mesh)
	{
		return Mesh != nullptr && Mesh->GetPathName().StartsWith(TEXT("/Engine/"));
	}

	template <typename ActorType>
	void ScanComponents(const TCHAR* ClassName, TSet<FString>& Found)
	{
		TArray<UStaticMeshComponent*> Components;
		GetDefault<ActorType>()->template GetComponents<UStaticMeshComponent>(Components);
		for (const UStaticMeshComponent* const Component : Components)
		{
			if (Component->IsVisible() && IsEngineMesh(Component->GetStaticMesh()))
			{
				Found.Add(FString::Printf(TEXT("%s.%s"), ClassName, *Component->GetName()));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlaceholderTest, "Snarehaven.World.Placeholder.NoEngineBasicShapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlaceholderTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPlaceholderTest;

	TSet<FString> FoundComponents;
	ScanComponents<ASeaActor>(TEXT("SeaActor"), FoundComponents);
	ScanComponents<ADangerZoneActor>(TEXT("DangerZoneActor"), FoundComponents);
	ScanComponents<AGatherableActor>(TEXT("GatherableActor"), FoundComponents);
	ScanComponents<ASnarePlacedStructureActor>(TEXT("SnarePlacedStructureActor"), FoundComponents);
	ScanComponents<AAnimalCharacter>(TEXT("AnimalCharacter"), FoundComponents);
	ScanComponents<ASnarehavenCharacter>(TEXT("SnarehavenCharacter"), FoundComponents);
	// 위험 지대 경계 띠는 컴포넌트가 아니라 런타임에 조각을 붙이는 메시 칸이다.
	if (const FObjectProperty* const Ring = FindFProperty<FObjectProperty>(ADangerZoneActor::StaticClass(), TEXT("RingMesh")))
	{
		if (IsEngineMesh(Cast<UStaticMesh>(Ring->GetObjectPropertyValue_InContainer(GetDefault<ADangerZoneActor>()))))
		{
			FoundComponents.Add(TEXT("DangerZoneActor.RingMesh"));
		}
	}

	TSet<FString> FoundRows;
	const UDataTable* const GatherTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable"));
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_GatherableTable"), GatherTable) || !TestNotNull(TEXT("DT_AnimalTable"), AnimalTable)
		|| !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	for (const TPair<FName, uint8*>& Pair : GatherTable->GetRowMap())
	{
		const FGatherableRow* const Row = reinterpret_cast<const FGatherableRow*>(Pair.Value);
		// 벌 수 있는 나무는 몸이 맵에 이미 깔린 장식 나무(`DecorMeshPrefix`)라 행에 메시가 없어도 자리표시가 아니다
		if (Row->MeshRef.IsNull() && Row->DecorMeshPrefix.IsEmpty())
		{
			FoundRows.Add(FString::Printf(TEXT("gatherable:%s"), *Row->GatherableID.ToString()));
		}
	}
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row->MeshRef.IsNull())
		{
			FoundRows.Add(FString::Printf(TEXT("animal:%s"), *Row->AnimalID.ToString()));
		}
	}
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row->FootprintX > 0 && (Row->PlacedMeshRef.IsNull() || Row->PlacedMeshWidthCm <= 0.f))
		{
			FoundRows.Add(FString::Printf(TEXT("placed:%s"), *Row->ItemID.ToString()));
		}
	}

	// ③ 실맵 — 두 맵에 미리 놓인 정적 메시 액터 중 엔진 도형인 것을 (맵:메시 이름) 로 모은다(개수는 안 센다).
	TMap<FString, int32> MapCounts;
	for (const TCHAR* MapName : { TEXT("L_Hunt"), TEXT("L_Ranch") })
	{
		const UWorld* const Level = LoadObject<UWorld>(nullptr, *FString::Printf(TEXT("/Game/Game/Maps/%s.%s"), MapName, MapName));
		if (!TestNotNull(*FString::Printf(TEXT("맵 %s"), MapName), Level) || Level->PersistentLevel == nullptr)
		{
			return false;
		}
		for (const AActor* const Actor : Level->PersistentLevel->Actors)
		{
			const AStaticMeshActor* const MeshActor = Cast<AStaticMeshActor>(Actor);
			const UStaticMeshComponent* const Component = MeshActor != nullptr ? MeshActor->GetStaticMeshComponent() : nullptr;
			if (Component != nullptr && IsEngineMesh(Component->GetStaticMesh()))
			{
				const FString Key = FString::Printf(TEXT("map:%s:%s"), MapName, *Component->GetStaticMesh()->GetName());
				FoundRows.Add(Key);
				MapCounts.FindOrAdd(Key)++;
			}
		}
	}
	for (const TPair<FString, int32>& Pair : MapCounts)
	{
		AddInfo(FString::Printf(TEXT("[Placeholder] 실맵 %s 액터 %d개"), *Pair.Key, Pair.Value));
	}

	const auto Compare = [this](const TCHAR* What, const TSet<FString>& Found, const TArray<FString>& Allowed)
	{
		for (const FString& Name : Found)
		{
			AddInfo(FString::Printf(TEXT("[Placeholder] %s 발견: %s"), What, *Name));
			TestTrue(*FString::Printf(TEXT("%s 허용 목록에 있다: %s"), What, *Name), Allowed.Contains(Name));
		}
		for (const FString& Name : Allowed)
		{
			TestTrue(*FString::Printf(TEXT("%s 목록 줄이 아직 살아 있다(바뀌었으면 지운다): %s"), What, *Name), Found.Contains(Name));
		}
	};
	Compare(TEXT("컴포넌트"), FoundComponents, AllowedComponents());
	Compare(TEXT("표 행"), FoundRows, AllowedRows());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
