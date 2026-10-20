// ROADMAP M12-03 2층 검사 — 사냥 HUD 미니맵이 `DT_ZoneTable` 의 칸을 **표대로** 그리는지.
// 보는 것: ① 표의 L_Hunt 칸 수만큼 칸 위젯을 세우고 색이 표·DESIGN 1.3 "미니맵" 표와 같은가
// ② 월드 → 지도 변환이 칸 경계에서 맞는가(경계 1cm 안쪽은 제 칸 그림 안, 1cm 바깥은 이웃 칸 그림 안),
// 방향(위 = +X, 오른쪽 = +Y)이 화면과 같은가 ③ 점과 존 이름이 존 규칙(`SnareZones`)과 같은가
// ④ 역검증 — 축을 바꾼·뒤집은 가짜 변환, 틀린 색을 넣으면 잡는가.
// **칸 숫자는 하나도 들고 있지 않다** — 칸은 표에서, 색 설계값은 문서에서 읽는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ui.Minimap; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UI/SnareMinimapHud.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"

namespace
{
	const FName MinimapLevel(TEXT("L_Hunt"));

	// 경계에서 한 발 안·밖을 재는 거리(cm). 검사 전용 값이다 (Verification.md 6).
	constexpr double BorderProbeCm = 1.0;

	using FMapper = TFunctionRef<FVector2D(const FBox2D&, const FVector&)>;

	struct FBuiltMinimap
	{
		UWidgetTree* Tree = nullptr;
		SnareMinimapHud::FPlaceholder Placeholder;
	};

	/** HUD 와 같은 함수로 자리표시 지도를 세운다 (월드 없이 — 위젯 트리만 있으면 된다). */
	FBuiltMinimap BuildMinimap(const TArray<const FZoneRow*>& Rows, const ASeaActor* Sea = nullptr)
	{
		FBuiltMinimap Out;
		Out.Tree = NewObject<UWidgetTree>(GetTransientPackage());
		UCanvasPanel* const Root = Out.Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("TestRoot"));
		Out.Tree->RootWidget = Root;
		Out.Placeholder = SnareMinimapHud::AddPlaceholder(*Out.Tree, *Root, Rows, FVector2D::ZeroVector, Sea);
		return Out;
	}

	const FZoneRow* FindOwner(const TArray<const FZoneRow*>& Rows, const FVector& World)
	{
		for (const FZoneRow* Row : Rows)
		{
			if (SnareZones::IsInsideRegion(*Row, World))
			{
				return Row;
			}
		}
		return nullptr;
	}

	/** 칸 위젯이 실제로 차지한 지도 안 사각형(테두리 뺀 좌표). 칸 사이 선 절반만큼 넓혀 잰다. */
	FBox2D GetCellRect(const UWidgetTree& Tree, const FZoneRow& Row)
	{
		const UImage* const Cell = Tree.FindWidget<UImage>(SnareMinimapHud::GetCellWidgetName(Row));
		const UCanvasPanelSlot* const Slot = Cell ? Cast<UCanvasPanelSlot>(Cell->Slot) : nullptr;
		if (Slot == nullptr)
		{
			return FBox2D(ForceInit);
		}
		const FVector2D Pad(SnareMinimapHud::CellGap * 0.5f + 0.01f);
		const FVector2D Min = Slot->GetPosition() - FVector2D(SnareMinimapHud::FrameBorder) - Pad;
		return FBox2D(Min, Min + Slot->GetSize() + Pad * 2.0);
	}

	/**
	 * ① 칸 네 변마다 1cm 안쪽 점은 제 칸 그림 안, 1cm 바깥 점은 (그 자리가 이웃 칸이면) 이웃 칸 그림 안인가
	 * ② 칸 둘의 위·아래·왼·오른이 월드 X·Y 순서와 맞는가(+X 가 위, +Y 가 오른쪽).
	 * 문제를 모아 돌려준다 — 역검증이 가짜 변환을 넣는다.
	 */
	TArray<FString> CollectMappingIssues(const UWidgetTree& Tree, const TArray<const FZoneRow*>& Rows,
		const FBox2D& Bounds, FMapper Mapper)
	{
		TArray<FString> Issues;
		const FVector2D Sides[] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
		for (const FZoneRow* Row : Rows)
		{
			for (const FVector2D& Side : Sides)
			{
				for (const double Offset : { -BorderProbeCm, BorderProbeCm })
				{
					const FVector World(Row->RegionCenterCm + Side * (Row->RegionHalfExtentCm + Offset), 0.0);
					const FZoneRow* const Owner = FindOwner(Rows, World);
					if (Owner == nullptr)
					{
						continue;   // 칸 밖(바다) — 지도 가장자리에 붙는 것은 아래 본 검사가 따로 본다
					}
					const FVector2D OnMap = Mapper(Bounds, World);
					if (!GetCellRect(Tree, *Owner).IsInside(OnMap))
					{
						Issues.Add(FString::Printf(TEXT("월드 %s 는 %s 칸인데 지도 %s 가 그 칸 그림 %s 밖이다"),
							*World.ToString(), *Owner->ZoneID.ToString(), *OnMap.ToString(),
							*GetCellRect(Tree, *Owner).ToString()));
					}
				}
			}
		}

		for (const FZoneRow* A : Rows)
		{
			for (const FZoneRow* B : Rows)
			{
				const FVector2D MapA = Mapper(Bounds, FVector(A->RegionCenterCm, 0.0));
				const FVector2D MapB = Mapper(Bounds, FVector(B->RegionCenterCm, 0.0));
				if (A->RegionCenterCm.X > B->RegionCenterCm.X && !(MapA.Y < MapB.Y))
				{
					Issues.Add(FString::Printf(TEXT("%s 가 %s 보다 +X 인데 지도에서 위가 아니다"),
						*A->ZoneID.ToString(), *B->ZoneID.ToString()));
				}
				if (A->RegionCenterCm.Y > B->RegionCenterCm.Y && !(MapA.X > MapB.X))
				{
					Issues.Add(FString::Printf(TEXT("%s 가 %s 보다 +Y 인데 지도에서 오른쪽이 아니다"),
						*A->ZoneID.ToString(), *B->ZoneID.ToString()));
				}
			}
		}
		return Issues;
	}

	/** DESIGN 1.3 "미니맵" 표 — `| 미니맵 칸 색 평원 (`zone.plains`) | `#52825D` | 근거 |` 꼴의 줄을 존 ID 별로. */
	TMap<FName, FColor> ParseDesignColors(const FString& Doc)
	{
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);
		const FRegexPattern Pattern(TEXT("^\\|[^|]*\\(`(zone\\.[a-z_]+)`\\)\\s*\\|\\s*`#([0-9A-Fa-f]{6})`"));
		TMap<FName, FColor> Out;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(Pattern, Line);
			if (Matcher.FindNext())
			{
				Out.Add(FName(*Matcher.GetCaptureGroup(1)), FColor::FromHex(Matcher.GetCaptureGroup(2)));
			}
		}
		return Out;
	}

	TArray<FString> CollectColorIssues(const TArray<const FZoneRow*>& Rows, const TMap<FName, FColor>& Design)
	{
		TArray<FString> Issues;
		if (Design.Num() == 0)
		{
			Issues.Add(TEXT("DESIGN 1.3 에서 미니맵 칸 색 줄을 하나도 못 읽었다"));
		}
		for (const FZoneRow* Row : Rows)
		{
			const FColor* const Want = Design.Find(Row->ZoneID);
			if (Want == nullptr || *Want != Row->MinimapColor)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 미니맵 색이 문서와 다르다 (문서 %s / 표 %s)"), *Row->ZoneID.ToString(),
					Want ? *Want->ToHex() : TEXT("없음"), *Row->MinimapColor.ToHex()));
			}
		}
		return Issues;
	}

	/**
	 * 칸 그리기 길(지도 그림이 없는 맵)의 변환 검사용 2×2 가짜 칸 — 새 사냥터 존은 정사각형이 아니라(틀이 겹친다)
	 * 표 행으로는 경계를 못 잰다(2026-09-25). 값은 검사 전용이다 (Verification.md 6).
	 */
	TArray<const FZoneRow*> MakeGridRows()
	{
		static TArray<FZoneRow> Grid;
		if (Grid.Num() == 0)
		{
			const TCHAR* const Ids[] = { TEXT("zone.test_nw"), TEXT("zone.test_ne"), TEXT("zone.test_sw"), TEXT("zone.test_se") };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				FZoneRow& Row = Grid.AddDefaulted_GetRef();
				Row.ZoneID = Ids[Index];
				Row.LevelName = MinimapLevel;
				Row.RegionCenterCm = FVector2D(Index < 2 ? 1000.0 : -1000.0, Index % 2 == 0 ? -1000.0 : 1000.0);
				Row.RegionHalfExtentCm = 1000.f;
				Row.MinimapColor = FColor(40 * Index, 80, 120);
			}
		}
		TArray<const FZoneRow*> Out;
		for (const FZoneRow& Row : Grid)
		{
			Out.Add(&Row);
		}
		return Out;
	}

	const ASeaActor* LoadHuntSea()
	{
		const UWorld* const Level = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
		if (Level != nullptr && Level->PersistentLevel != nullptr)
		{
			for (const AActor* const Actor : Level->PersistentLevel->Actors)
			{
				if (const ASeaActor* const Sea = Cast<ASeaActor>(Actor))
				{
					return Sea;
				}
			}
		}
		return nullptr;
	}

	TArray<const FZoneRow*> LoadHuntRows(FAutomationTestBase& Test)
	{
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
		Test.TestNotNull(TEXT("DT_ZoneTable 로드"), Table);
		return SnareMinimapHud::GetRegionRows(Table, MinimapLevel);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapCellsTest, "Snarehaven.Ui.Minimap.DrawsZoneCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMinimapCellsTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), Table))
	{
		return false;
	}

	// 칸 수는 표를 **따로** 세어 맞댄다 — 미니맵 쪽 행 고르기가 틀려도 같은 수가 나오지 않게.
	int32 TableCells = 0;
	Table->ForeachRow<FZoneRow>(TEXT("MinimapTest"), [&TableCells](const FName&, const FZoneRow& Row)
	{
		TableCells += (Row.LevelName == MinimapLevel && Row.RegionHalfExtentCm > 0.f) ? 1 : 0;
	});
	const TArray<const FZoneRow*> Rows = SnareMinimapHud::GetRegionRows(Table, MinimapLevel);
	AddInfo(FString::Printf(TEXT("L_Hunt 칸 행 %d개 · 미니맵이 고른 행 %d개"), TableCells, Rows.Num()));
	TestTrue(TEXT("L_Hunt 에 칸이 하나 이상"), TableCells > 0);
	TestEqual(TEXT("미니맵이 고른 행 = 표의 L_Hunt 칸 행"), Rows.Num(), TableCells);

	const FBuiltMinimap Built = BuildMinimap(Rows);
	TestEqual(TEXT("칸 위젯 수 = 표의 칸 수"), Built.Placeholder.CellCount, TableCells);
	TestNotNull(TEXT("플레이어 점이 있다"), Built.Placeholder.PlayerDot);
	TestNotNull(TEXT("존 이름 줄이 있다"), Built.Placeholder.ZoneNameText);

	for (const FZoneRow* Row : Rows)
	{
		const UImage* const Cell = Built.Tree->FindWidget<UImage>(SnareMinimapHud::GetCellWidgetName(*Row));
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 칸 위젯이 있다"), *Row->ZoneID.ToString()), Cell))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("[%s] 표에 색이 채워져 있다"), *Row->ZoneID.ToString()),
			Row->MinimapColor != FColor::Black);
		TestTrue(*FString::Printf(TEXT("[%s] 칸 색 = 표 색"), *Row->ZoneID.ToString()),
			Cell->GetBrush().TintColor.GetSpecifiedColor().Equals(FLinearColor(Row->MinimapColor)));
	}

	FString Doc;
	if (TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		const TMap<FName, FColor> Design = ParseDesignColors(Doc);
		AddInfo(FString::Printf(TEXT("DESIGN 1.3 미니맵 색 %d줄"), Design.Num()));
		for (const FString& Issue : CollectColorIssues(Rows, Design))
		{
			AddError(Issue);
		}
	}

	// 칸이 없는 맵(목장)은 아무것도 안 세운다 — 그래야 HUD 왼쪽 기둥이 원래 자리에 선다.
	const FBuiltMinimap Empty = BuildMinimap({});
	TestEqual(TEXT("칸 없는 맵은 칸 0"), Empty.Placeholder.CellCount, 0);
	TestEqual(TEXT("칸 없는 맵은 높이 0"), Empty.Placeholder.Height, 0.f);
	TestNull(TEXT("칸 없는 맵은 점도 없다"), Empty.Placeholder.PlayerDot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapMappingTest, "Snarehaven.Ui.Minimap.WorldToMapMatchesBorders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMinimapMappingTest::RunTest(const FString& Parameters)
{
	const TArray<const FZoneRow*> Rows = MakeGridRows();
	const FBuiltMinimap Built = BuildMinimap(Rows);
	const FBox2D Bounds = Built.Placeholder.Bounds;

	const TArray<FString> Issues = CollectMappingIssues(*Built.Tree, Rows, Bounds,
		[](const FBox2D& B, const FVector& W) { return SnareMinimapHud::WorldToMap(B, W); });
	for (const FString& Issue : Issues)
	{
		AddError(Issue);
	}
	AddInfo(FString::Printf(TEXT("칸 %d개 경계 점 검사 문제 %d건"), Rows.Num(), Issues.Num()));

	// 지도 모서리: 맨 위 왼쪽 = (+X 끝, -Y 끝), 맨 아래 오른쪽 = 지도 크기. 칸 밖(바다)은 가장자리에 붙는다.
	const FVector2D Extent = SnareMinimapHud::GetMapExtent(Bounds);
	TestTrue(TEXT("긴 변이 지도 크기다"), FMath::IsNearlyEqual(FMath::Max(Extent.X, Extent.Y), static_cast<double>(SnareMinimapHud::MapSize), 0.01));
	TestTrue(TEXT("(+X 끝, -Y 끝) = 지도 왼쪽 위"),
		SnareMinimapHud::WorldToMap(Bounds, FVector(Bounds.Max.X, Bounds.Min.Y, 0.0)).Equals(FVector2D::ZeroVector, 0.01));
	TestTrue(TEXT("(-X 끝, +Y 끝) = 지도 오른쪽 아래"),
		SnareMinimapHud::WorldToMap(Bounds, FVector(Bounds.Min.X, Bounds.Max.Y, 0.0)).Equals(Extent, 0.01));
	TestTrue(TEXT("칸 밖 먼 바다는 지도 왼쪽 위 가장자리에 붙는다"),
		SnareMinimapHud::WorldToMap(Bounds, FVector(Bounds.Max.X * 10.0 + 1.0, Bounds.Min.Y * 10.0 - 1.0, 0.0))
			.Equals(FVector2D::ZeroVector, 0.01));

	// 점과 존 이름: 칸 가운데에 서면 점 가운데가 그 칸 그림 가운데, 이름은 존 규칙이 고른 칸 이름이다.
	UImage* const Dot = Built.Placeholder.PlayerDot;
	const UCanvasPanelSlot* const DotSlot = Dot ? Cast<UCanvasPanelSlot>(Dot->Slot) : nullptr;
	if (TestNotNull(TEXT("점 슬롯"), DotSlot))
	{
		for (const FZoneRow* Row : Rows)
		{
			const FVector Center(Row->RegionCenterCm, 0.0);
			SnareMinimapHud::MoveDot(*Dot, Bounds, Center);
			const FVector2D DotCenter = DotSlot->GetPosition() + FVector2D(SnareMinimapHud::DotSize * 0.5f)
				- FVector2D(SnareMinimapHud::FrameBorder);
			TestTrue(*FString::Printf(TEXT("[%s] 점 가운데 = 칸 그림 가운데 (%s / %s)"), *Row->ZoneID.ToString(),
					*DotCenter.ToString(), *GetCellRect(*Built.Tree, *Row).GetCenter().ToString()),
				DotCenter.Equals(GetCellRect(*Built.Tree, *Row).GetCenter(), 0.01));

		}
	}
	TestEqual(TEXT("칸 밖이면 존 이름은 `-`"), SnareMinimapHud::FormatZoneName(nullptr).ToString(), FString(TEXT("-")));

	// 새 사냥터 — 바다가 든 지도 그림 한 장이 틀이고, 존 이름은 존 칸 코드로 고른 행 이름이다.
	const TArray<const FZoneRow*> HuntRows = LoadHuntRows(*this);
	const ASeaActor* const Sea = LoadHuntSea();
	if (!TestNotNull(TEXT("L_Hunt 바다"), Sea) || !TestNotNull(TEXT("지도 그림"), Sea->MapTexture.Get()))
	{
		return false;
	}
	const FBuiltMinimap Hunt = BuildMinimap(HuntRows, Sea);
	TestTrue(TEXT("지도 틀 = 바다가 든 지도 그림 틀"), Hunt.Placeholder.Bounds == Sea->GetMapBounds());
	TestNotNull(TEXT("지도 그림 위젯"), Hunt.Tree->FindWidget<UImage>(TEXT("MinimapPicture")));
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	for (const FZoneRow* Row : HuntRows)
	{
		FVector2D Probe;
		if (Row->FieldCode <= 0 || !TestTrue(*FString::Printf(TEXT("[%s] 지도에 제 존 뭍 칸"), *Row->ZoneID.ToString()),
			Sea->FindZonePoint(Row->FieldCode, Row->RegionCenterCm, 400.f, Row->RegionHalfExtentCm * 2.f, Probe)))
		{
			continue;
		}
		const FZoneRow* const At = SnareZones::FindRegionAt(Table, MinimapLevel, FVector(Probe, 0.0), Sea);
		TestEqual(*FString::Printf(TEXT("[%s] 존 이름 = 표 이름"), *Row->ZoneID.ToString()),
			SnareMinimapHud::FormatZoneName(At).ToString(), Row->DisplayName.ToString());
	}
	return true;
}

// 위 검사가 "변환이 멀쩡해서" 통과할 때도 살아 있는지 (역검증).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapSelfTest, "Snarehaven.Ui.Minimap.CheckCatchesBrokenMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMinimapSelfTest::RunTest(const FString& Parameters)
{
	const TArray<const FZoneRow*> Rows = MakeGridRows();
	const FBuiltMinimap Built = BuildMinimap(Rows);
	const FBox2D Bounds = Built.Placeholder.Bounds;

	// 가로·세로를 바꾼 변환 (화면을 90도 돌린 지도).
	const int32 Swapped = CollectMappingIssues(*Built.Tree, Rows, Bounds, [](const FBox2D& B, const FVector& W)
	{
		const FVector2D Real = SnareMinimapHud::WorldToMap(B, W);
		return FVector2D(Real.Y, Real.X);
	}).Num();
	// 위아래를 뒤집은 변환 (+X 가 아래).
	const int32 Mirrored = CollectMappingIssues(*Built.Tree, Rows, Bounds, [](const FBox2D& B, const FVector& W)
	{
		const FVector2D Real = SnareMinimapHud::WorldToMap(B, W);
		return FVector2D(Real.X, SnareMinimapHud::GetMapExtent(B).Y - Real.Y);
	}).Num();
	AddInfo(FString::Printf(TEXT("가짜 변환 문제: 축 바꿈 %d건 · 위아래 뒤집음 %d건"), Swapped, Mirrored));
	TestTrue(TEXT("축을 바꾼 변환을 잡는다"), Swapped > 0);
	TestTrue(TEXT("위아래를 뒤집은 변환을 잡는다"), Mirrored > 0);

	// 색: 문서와 한 칸이라도 다르면, 문서를 못 읽으면 잡는다.
	TMap<FName, FColor> Design;
	for (const FZoneRow* Row : Rows)
	{
		Design.Add(Row->ZoneID, Row->MinimapColor);
	}
	TestEqual(TEXT("맞는 색은 문제 0건"), CollectColorIssues(Rows, Design).Num(), 0);
	Design[Rows[0]->ZoneID] = FColor(1, 2, 3);
	TestEqual(TEXT("한 칸 색이 틀리면 1건"), CollectColorIssues(Rows, Design).Num(), 1);
	TestTrue(TEXT("문서를 못 읽으면 통과시키지 않는다"), CollectColorIssues(Rows, {}).Num() > 0);
	TestEqual(TEXT("문서 줄 모양을 읽는다"),
		ParseDesignColors(TEXT("| 미니맵 칸 색 가 (`zone.test_a`) | `#52825D` | 근거 |\n| 가 (`zone.test_b`) | 중심 (0, 0) cm |")).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapCoordTest, "Snarehaven.Ui.Minimap.SatelliteShowsCoordinates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 13.5 위성 통신기 — 지도 아래 좌표 줄은 처음엔 접혀 있고, 든 동안만 `북 N · 동 E`(m) 로 펴진다.
bool FSnarehavenMinimapCoordTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const FBuiltMinimap Built = BuildMinimap(SnareMinimapHud::GetRegionRows(Table, MinimapLevel));
	UTextBlock* const Coord = Built.Placeholder.CoordText;
	if (!TestNotNull(TEXT("좌표 줄이 있다"), Coord))
	{
		return false;
	}
	TestEqual(TEXT("처음엔 접혀 있다"), static_cast<int32>(Coord->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));
	TestEqual(TEXT("cm → m 반올림, X 가 북·Y 가 동"),
		SnareMinimapHud::FormatCoordinates(FVector(1234.0, -3456.0, 0.0)).ToString(), FString(TEXT("북 12 · 동 -35")));

	SnareMinimapHud::UpdateCoordinates(Coord, true, FVector(1234.0, -3456.0, 0.0));
	TestNotEqual(TEXT("들면 보인다"), static_cast<int32>(Coord->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));
	TestEqual(TEXT("들면 좌표를 적는다"), Coord->GetText().ToString(), FString(TEXT("북 12 · 동 -35")));
	SnareMinimapHud::UpdateCoordinates(Coord, false, FVector::ZeroVector);
	TestEqual(TEXT("내리면 접힌다"), static_cast<int32>(Coord->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));
	SnareMinimapHud::UpdateCoordinates(nullptr, true, FVector::ZeroVector);   // 칸 없는 맵 — 줄이 없어도 안 터진다
	TestNull(TEXT("칸 없는 맵은 좌표 줄도 없다"), BuildMinimap({}).Placeholder.CoordText);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapTreasureTest, "Snarehaven.Ui.Minimap.TreasureDotDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 13.8 보물 표식 (M13-15) — 금색 점은 칸 위·내 점 아래에 그려지고, 찾는 보물이 이 맵에 있으면 그 자리에 펴진다.
// 보물 칸이 다른 맵이면 점은 접고 남은 시간 줄만, 끝나면(0초) 둘 다 접는다.
bool FSnarehavenMinimapTreasureTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const TArray<const FZoneRow*> Rows = SnareMinimapHud::GetRegionRows(Table, MinimapLevel);
	const FBuiltMinimap Built = BuildMinimap(Rows);
	UImage* const Dot = Built.Placeholder.TreasureDot;
	UTextBlock* const Text = Built.Placeholder.TreasureText;
	if (!TestTrue(TEXT("칸이 있다"), Rows.Num() > 0) || !TestNotNull(TEXT("보물 점"), Dot) || !TestNotNull(TEXT("보물 줄"), Text))
	{
		return false;
	}
	const UPanelWidget* const Panel = Dot->GetParent();
	const UWidget* const Cell = Built.Tree->FindWidget(SnareMinimapHud::GetCellWidgetName(*Rows.Last()));
	if (TestNotNull(TEXT("점이 지도 판에 붙었다"), Panel) && TestNotNull(TEXT("마지막 칸"), Cell))
	{
		TestTrue(TEXT("보물 점은 칸 위에 그린다"), Panel->GetChildIndex(Dot) > Panel->GetChildIndex(Cell));
		TestTrue(TEXT("보물 점은 내 점 아래에 그린다"), Panel->GetChildIndex(Dot) < Panel->GetChildIndex(Built.Placeholder.PlayerDot));
	}
	const auto IsCollapsed = [](const UWidget* Widget)
	{
		return Widget->GetVisibility() == ESlateVisibility::Collapsed;
	};
	TestTrue(TEXT("처음엔 점이 접혀 있다"), IsCollapsed(Dot));

	const FVector Spot(Rows[0]->RegionCenterCm, 0.0);
	SnareMinimapHud::UpdateTreasureMarker(Dot, Text, Built.Placeholder.Bounds, &Spot, 300.0);
	TestFalse(TEXT("보물이 이 맵이면 점이 보인다"), IsCollapsed(Dot));
	TestFalse(TEXT("남은 시간 줄이 보인다"), IsCollapsed(Text));
	TestTrue(TEXT("줄 = 남은 시간"), Text->GetText().EqualTo(SnareMinimapHud::FormatTreasureRemaining(300.0)));
	const UCanvasPanelSlot* const DotSlot = Cast<UCanvasPanelSlot>(Dot->Slot);
	if (TestNotNull(TEXT("점 슬롯"), DotSlot))
	{
		const FVector2D DotCenter = DotSlot->GetPosition() + DotSlot->GetSize() * 0.5f - FVector2D(SnareMinimapHud::FrameBorder);
		TestTrue(*FString::Printf(TEXT("점 가운데 = 보물 칸 그림 가운데 (%s / %s)"), *DotCenter.ToString(),
				*GetCellRect(*Built.Tree, *Rows[0]).GetCenter().ToString()),
			DotCenter.Equals(GetCellRect(*Built.Tree, *Rows[0]).GetCenter(), 0.01));
	}

	SnareMinimapHud::UpdateTreasureMarker(Dot, Text, Built.Placeholder.Bounds, nullptr, 300.0);
	TestTrue(TEXT("다른 맵 보물이면 점은 접는다"), IsCollapsed(Dot));
	TestFalse(TEXT("다른 맵 보물이어도 줄은 보인다"), IsCollapsed(Text));
	SnareMinimapHud::UpdateTreasureMarker(Dot, Text, Built.Placeholder.Bounds, &Spot, 0.0);
	TestTrue(TEXT("끝난 보물은 점을 접는다"), IsCollapsed(Dot));
	TestTrue(TEXT("끝난 보물은 줄도 접는다"), IsCollapsed(Text));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMinimapTouchTest, "Snarehaven.Ui.Minimap.TouchOpensMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// INBOX 결함 10-11 23:05 — 미니맵을 손가락으로 누르면 큰 지도가 열린다. 누름 칸이 지도 틀을 덮고(존 이름 줄은 안 덮고),
// 눌리는 칸이어야 하며(Visible), 칸 없는 맵은 칸도 없다.
bool FSnarehavenMinimapTouchTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const FBuiltMinimap Built = BuildMinimap(SnareMinimapHud::GetRegionRows(Table, MinimapLevel));
	UButton* const Touch = Built.Placeholder.TouchButton;
	if (!TestNotNull(TEXT("미니맵 누름 칸"), Touch))
	{
		return false;
	}
	TestTrue(TEXT("눌리는 칸 (보이지 않아도 닿아야 한다)"), Touch->GetVisibility() == ESlateVisibility::Visible);
	const UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(Touch->Slot);
	if (TestNotNull(TEXT("누름 칸 자리"), Slot))
	{
		const FVector2D Frame = SnareMinimapHud::GetMapExtent(Built.Placeholder.Bounds) + FVector2D(SnareMinimapHud::FrameBorder * 2.f);
		TestTrue(TEXT("누름 칸 = 지도 틀 크기"), Slot->GetSize().Equals(Frame, 0.01));
		TestTrue(TEXT("존 이름 줄 위에서 끝난다"), Slot->GetPosition().Y + Slot->GetSize().Y <= Frame.Y + 0.01);
	}
	TestNull(TEXT("칸 없는 맵은 누름 칸도 없다"), BuildMinimap({}).Placeholder.TouchButton);
	return true;
}

#endif
