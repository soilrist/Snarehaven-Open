// 마을 이름 표(`DT_OutpostTable`) 2층 검사 — 거점 14곳이 전부 이름이 있고, 지도 원본·설계서와 같은가.
// 보는 것 넷: ① 행 이름 집합 = `tools/unreal/data/world_map.json` 거점 id 집합(이름 빠짐·남는 행 0)
// ② 자리 XCm·YCm 이 json 과 같다(지도를 다시 굽고 표를 안 만든 사고) ③ 이름이 비지 않고 서로 다르다
// ④ 들어서는 반지름이 DESIGN 마을 이름 표(`EnterRadiusCm`)와 같다. 숫자는 여기 적지 않고 문서·json 에서 읽는다.
// 아래 자기검사는 이 검사가 정말 잡는지 — 일부러 깨뜨린 행으로 문제가 나오는지 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.OutpostNames; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/OutpostRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SnareOutpostBanner.h"

namespace
{
	const TCHAR* OutpostTablePath = TEXT("/Game/Game/Data/World/DT_OutpostTable.DT_OutpostTable");

	struct FMapOutpost
	{
		double X = 0.0;
		double Y = 0.0;
	};

	bool ReadMapOutposts(TMap<FName, FMapOutpost>& Out)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("tools/unreal/data/world_map.json"))))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Root->TryGetArrayField(TEXT("outposts"), List))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			FMapOutpost Item;
			Item.X = Obj->GetNumberField(TEXT("x"));
			Item.Y = Obj->GetNumberField(TEXT("y"));
			Out.Add(FName(*Obj->GetStringField(TEXT("id"))), Item);
		}
		return true;
	}

	/** DESIGN 마을 이름 표의 `EnterRadiusCm` 줄에서 첫 숫자. 못 읽으면 -1. */
	double ReadDesignRadius(const FString& Doc)
	{
		const FRegexPattern Pattern(TEXT("`EnterRadiusCm`\\s*\\|\\s*\\*\\*([0-9]+(?:\\.[0-9]+)?)"));
		FRegexMatcher Matcher(Pattern, Doc);
		return Matcher.FindNext() ? FCString::Atod(*Matcher.GetCaptureGroup(1)) : -1.0;
	}

	TArray<FString> CollectOutpostIssues(const TMap<FName, FOutpostRow>& Rows, const TMap<FName, FMapOutpost>& Map, double DesignRadius)
	{
		TArray<FString> Issues;
		if (Map.Num() == 0 || DesignRadius <= 0.0)
		{
			Issues.Add(TEXT("비교할 원본(json·설계서 반지름)을 못 읽었다"));
			return Issues;
		}
		TSet<FString> Seen;
		for (const TPair<FName, FMapOutpost>& Pair : Map)
		{
			const FOutpostRow* Row = Rows.Find(Pair.Key);
			if (Row == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("%s: 이름 행 없음"), *Pair.Key.ToString()));
				continue;
			}
			const FString Name = Row->DisplayName.ToString();
			if (Name.TrimStartAndEnd().IsEmpty())
			{
				Issues.Add(FString::Printf(TEXT("%s: 이름이 비었다"), *Pair.Key.ToString()));
			}
			else if (Seen.Contains(Name))
			{
				Issues.Add(FString::Printf(TEXT("%s: 이름 %s 가 다른 마을과 같다"), *Pair.Key.ToString(), *Name));
			}
			Seen.Add(Name);
			if (!FMath::IsNearlyEqual(Row->XCm, static_cast<float>(Pair.Value.X), 0.5f) || !FMath::IsNearlyEqual(Row->YCm, static_cast<float>(Pair.Value.Y), 0.5f))
			{
				Issues.Add(FString::Printf(TEXT("%s: 자리가 지도와 다르다 (표 %.0f,%.0f / 지도 %.0f,%.0f)"),
					*Pair.Key.ToString(), Row->XCm, Row->YCm, Pair.Value.X, Pair.Value.Y));
			}
			if (!FMath::IsNearlyEqual(Row->EnterRadiusCm, static_cast<float>(DesignRadius), 0.5f))
			{
				Issues.Add(FString::Printf(TEXT("%s: 반지름 %.0f 이 설계서 %.0f 와 다르다"), *Pair.Key.ToString(), Row->EnterRadiusCm, DesignRadius));
			}
		}
		for (const TPair<FName, FOutpostRow>& Pair : Rows)
		{
			if (!Map.Contains(Pair.Key))
			{
				Issues.Add(FString::Printf(TEXT("%s: 지도에 없는 남는 행"), *Pair.Key.ToString()));
			}
		}
		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutpostNameTest, "Snarehaven.World.OutpostNames.TableMatchesMapAndDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOutpostNameTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, OutpostTablePath);
	if (!TestNotNull(TEXT("DT_OutpostTable 로드"), Table))
	{
		return false;
	}

	TMap<FName, FMapOutpost> Map;
	TestTrue(TEXT("world_map.json 읽기"), ReadMapOutposts(Map));
	TestEqual(TEXT("거점 수"), Map.Num(), 14);

	FString Doc;
	TestTrue(TEXT("DESIGN.md 읽기"), FFileHelper::LoadFileToString(Doc, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md"))));
	const double DesignRadius = ReadDesignRadius(Doc);
	TestTrue(TEXT("설계서 EnterRadiusCm 읽기"), DesignRadius > 0.0);

	TMap<FName, FOutpostRow> Rows;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		Rows.Add(Pair.Key, *reinterpret_cast<const FOutpostRow*>(Pair.Value));
	}

	for (const FString& Issue : CollectOutpostIssues(Rows, Map, DesignRadius))
	{
		AddError(Issue);
	}

	// 자기검사 — 일부러 깬 사본에서 문제가 나와야 한다 (아무것도 못 잡는 검사는 통과해 봐야 소용없다).
	if (Rows.Num() > 0 && Map.Num() > 0 && DesignRadius > 0.0)
	{
		const FName First = Map.CreateConstIterator().Key();

		TMap<FName, FOutpostRow> Missing = Rows;
		Missing.Remove(First);
		TestTrue(TEXT("자기검사: 행이 빠지면 문제"), CollectOutpostIssues(Missing, Map, DesignRadius).Num() > 0);

		TMap<FName, FOutpostRow> Blank = Rows;
		Blank[First].DisplayName = FText::GetEmpty();
		TestTrue(TEXT("자기검사: 빈 이름이면 문제"), CollectOutpostIssues(Blank, Map, DesignRadius).Num() > 0);

		TMap<FName, FOutpostRow> Moved = Rows;
		Moved[First].XCm += 100.f;
		TestTrue(TEXT("자기검사: 자리가 어긋나면 문제"), CollectOutpostIssues(Moved, Map, DesignRadius).Num() > 0);

		TMap<FName, FOutpostRow> Extra = Rows;
		Extra.Add(FName(TEXT("ghost_town")), Rows[First]);
		TestTrue(TEXT("자기검사: 지도에 없는 행이면 문제"), CollectOutpostIssues(Extra, Map, DesignRadius).Num() > 0);
	}
	return true;
}

// 마을 이름 띠 — 어느 마을 안인지·들어선 순간·60초 안 재표시 없음. 표는 시험용 가짜(밸런스 숫자와 무관).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOutpostBannerTest, "Snarehaven.World.OutpostNames.EnterBanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenOutpostBannerTest::RunTest(const FString& Parameters)
{
	UDataTable* const Table = NewObject<UDataTable>();
	Table->RowStruct = FOutpostRow::StaticStruct();
	FOutpostRow A;
	A.XCm = 0.f; A.YCm = 0.f; A.EnterRadiusCm = 1000.f;
	FOutpostRow B;
	B.XCm = 5000.f; B.YCm = 0.f; B.EnterRadiusCm = 1000.f;
	Table->AddRow(TEXT("a"), A);
	Table->AddRow(TEXT("b"), B);

	TestEqual(TEXT("가운데는 a"), SnareOutpostBanner::FindEntered(Table, FVector(0, 0, 0)), FName(TEXT("a")));
	TestEqual(TEXT("반지름 끝 안쪽도 a(높이는 안 봄)"), SnareOutpostBanner::FindEntered(Table, FVector(999, 0, 9000)), FName(TEXT("a")));
	TestEqual(TEXT("반지름 밖은 없음"), SnareOutpostBanner::FindEntered(Table, FVector(1001, 0, 0)), NAME_None);
	TestEqual(TEXT("b 안"), SnareOutpostBanner::FindEntered(Table, FVector(5200, 300, 0)), FName(TEXT("b")));
	TestEqual(TEXT("표 없으면 없음"), SnareOutpostBanner::FindEntered(nullptr, FVector::ZeroVector), NAME_None);

	SnareOutpostBanner::FTracker Tracker;
	FName Show;
	const double Gap = SnareOutpostBanner::RepeatGapSeconds;
	TestFalse(TEXT("밖에서 밖은 안 띄움"), Tracker.Update(NAME_None, 0.0, Show));
	TestTrue(TEXT("a 에 들어서면 띄움"), Tracker.Update(FName(TEXT("a")), 1.0, Show));
	TestEqual(TEXT("띄울 id = a"), Show, FName(TEXT("a")));
	TestFalse(TEXT("안에 서 있으면 다시 안 띄움"), Tracker.Update(FName(TEXT("a")), 2.0, Show));
	TestFalse(TEXT("나감은 띄우지 않음"), Tracker.Update(NAME_None, 3.0, Show));
	TestFalse(TEXT("간격 안에 다시 들어서면 안 띄움"), Tracker.Update(FName(TEXT("a")), 1.0 + Gap - 1.0, Show));
	Tracker.Update(NAME_None, 4.0, Show);
	TestTrue(TEXT("간격이 지난 뒤 들어서면 다시 띄움"), Tracker.Update(FName(TEXT("a")), 1.0 + Gap + 1.0, Show));
	TestTrue(TEXT("마을을 곧장 건너가면 새 마을 이름"), Tracker.Update(FName(TEXT("b")), 1.0 + Gap + 2.0, Show));
	TestEqual(TEXT("띄울 id = b"), Show, FName(TEXT("b")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
