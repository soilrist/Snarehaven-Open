// ROADMAP M8-01 2층 검사 — 이벤트 이름표(`DT_AnalyticsEventTable`)가 문서·코드와 맞는지.
// 보는 것 셋: ① 표 ↔ `docs/ANALYTICS.md` 표 A (이름·보내는 쪽·칸이 양쪽 다 같은가)
//             ② 코드가 보내는 이름(`WriteGameEvent(TEXT("..."))`)이 전부 표에 있는가
//             ③ 표 자체가 성한가 (`EventID` 중복·빈 칸)
//
// **이 검사는 이벤트 이름을 하나도 들고 있지 않다** — 아홉 개를 여기 적으면 사본이 셋이 되어
// (문서·표·검사) 검사 자신이 막으려던 "한쪽만 고치기" 를 저지른다. 문서에서 읽어 표와 맞댈 뿐이다.
// (M7-02 `Snarehaven.Gacha.Disclosure.TableMatchesDocument` 와 같은 짜임.)
//
// **지금 이벤트를 보내는 코드는 0줄이다** — ② 는 오늘 0개를 훑고, M8-02(통로)·M8-03~06(부르는 자리)이
// 붙는 순간부터 문다. 0개를 "통과" 로 세지 않으려고 몇 개를 봤는지 로그에 남긴다 (Verification.md).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Analytics; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AnalyticsEventRow.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* AnalyticsTablePath = TEXT("/Game/Game/Data/Analytics/DT_AnalyticsEventTable.DT_AnalyticsEventTable");

	// 문서 표 A 의 한 줄에서 읽어낸 것. 표가 원본이라 이 구조체가 기대값이 된다.
	struct FDocEvent
	{
		EAnalyticsEventSender Sender = EAnalyticsEventSender::Client;
		TArray<FName> PayloadKeys;
	};

	// 문서의 "보내는 쪽" 칸은 사람 말이다 — `클라이언트 (**호스트 권위**)` 처럼.
	// 굵게 표시된 낱말 하나로 가른다. 넷 중 아무것도 아니면 맨 뒤(Client)로 떨어진다.
	EAnalyticsEventSender SenderFromDocCell(const FString& Cell)
	{
		if (Cell.Contains(TEXT("호스트 권위"))) { return EAnalyticsEventSender::HostAuthority; }
		if (Cell.Contains(TEXT("요청자 본인"))) { return EAnalyticsEventSender::Requester; }
		if (Cell.Contains(TEXT("관찰만"))) { return EAnalyticsEventSender::Observer; }
		return EAnalyticsEventSender::Client;
	}

	const TCHAR* SenderName(const EAnalyticsEventSender Sender)
	{
		switch (Sender)
		{
		case EAnalyticsEventSender::HostAuthority: return TEXT("HostAuthority");
		case EAnalyticsEventSender::Requester:     return TEXT("Requester");
		case EAnalyticsEventSender::Observer:      return TEXT("Observer");
		default:                                   return TEXT("Client");
		}
	}

	// 칸 목록을 한 줄로 이어 붙인다 — 어긋났을 때 "어떻게 다른지" 가 오류 줄에 그대로 보이게.
	FString JoinNames(const TArray<FName>& Names)
	{
		TArray<FString> Parts;
		for (const FName& Name : Names)
		{
			Parts.Add(Name.ToString());
		}
		return FString::Join(Parts, TEXT(", "));
	}

	// 백틱으로 감싼 밑줄 이름들만 긁는다 (`pet_item_id` · `drop_count`). 칸 설명에 붙은 괄호말은 안 걸린다.
	TArray<FName> ParseBacktickedNames(const FString& Cell)
	{
		TArray<FName> Names;
		const FRegexPattern Pattern(TEXT("`([a-z0-9_]+)`"));
		FRegexMatcher Matcher(Pattern, Cell);
		while (Matcher.FindNext())
		{
			Names.Add(FName(*Matcher.GetCaptureGroup(1)));
		}
		return Names;
	}

	// `docs/ANALYTICS.md` 표 A 를 읽는다. 줄 단위로 보는 이유는 `|` 가 줄을 넘어 짝지어지면
	// 엉뚱한 칸끼리 붙기 때문이다 (M7-02 가 겪은 것과 같은 함정).
	// 표 A 만 걸리는 근거: 칸이 여섯(자른 조각 여덟)이고 첫 칸이 백틱 이름 하나뿐인 줄은 표 A 밖에 없다.
	TMap<FString, FDocEvent> ParseDocumentEvents(const FString& Doc)
	{
		TMap<FString, FDocEvent> Events;

		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);

		const FRegexPattern EventNamePattern(TEXT("^\\s*`([a-z0-9_]+)`\\s*$"));
		for (const FString& Line : Lines)
		{
			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT("|"), false);
			if (Cells.Num() != 8)
			{
				continue;
			}

			FRegexMatcher NameMatcher(EventNamePattern, Cells[1]);
			if (!NameMatcher.FindNext())
			{
				continue;
			}

			FDocEvent Event;
			Event.PayloadKeys = ParseBacktickedNames(Cells[3]);
			Event.Sender = SenderFromDocCell(Cells[4]);
			Events.Add(NameMatcher.GetCaptureGroup(1), Event);
		}

		return Events;
	}

	// `Source/` 아래 `.cpp`·`.h` 를 전부 읽어 (경로, 내용) 으로 돌려준다. 훑는 검사 둘이 같은 목록을 쓴다.
	TArray<TPair<FString, FString>> LoadSourceFiles(const FString& SourceDir)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*.cpp"), true, false);
		IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*.h"), true, false, false);

		TArray<TPair<FString, FString>> Loaded;
		for (const FString& File : Files)
		{
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *File))
			{
				Loaded.Emplace(File, MoveTemp(Text));
			}
		}
		return Loaded;
	}

	// 통로를 부르는 자리를 이름별로 센다. 나가는 길은 M8-02 의 `WriteGameEvent` 하나로 정해져 있어서
	// (`docs/ANALYTICS.md` 표 B) 그 부름만 훑으면 된다 — 몇 개를 봤는지는 검사마다 로그에 남긴다. **정규식을 한 곳에만 두는 이유:** 훑는 규칙이 둘로 갈리면
	// 한쪽만 고쳐져 "보내는데 안 세는" 자리가 생긴다. (패턴 글자에 `\\(` 가 들어 있어 이 파일 자신은 안 걸린다.)
	// **이름을 쪼개 이어 붙이는 이유:** 이 검사는 `Source/` 아래 글자를 훑는다 — 통째로 적어 두면
	// 검사기가 **제 소스에서** 그 글자를 세어 자기 자신을 부르는 자리로 본다 (루프#110 함정).
	FString EventCallNeedle()
	{
		return FString(TEXT("WriteGame")) + TEXT("Event(TEXT(\"");
	}

	TMap<FString, int32> CountEventCallsByID(const FString& Text)
	{
		TMap<FString, int32> Counts;
		const FRegexPattern CallPattern(TEXT("WriteGameEvent\\(\\s*TEXT\\(\"([a-z0-9_]*)\""));
		FRegexMatcher Matcher(CallPattern, Text);
		while (Matcher.FindNext())
		{
			++Counts.FindOrAdd(Matcher.GetCaptureGroup(1));
		}
		return Counts;
	}

	// 코드가 실제로 보내는 이름들(중복 없이). 표에 없는 이름을 골라내는 쪽이 이걸 쓴다.
	TArray<FString> FindEventIDsSentByCode(const TArray<TPair<FString, FString>>& Sources)
	{
		TArray<FString> EventIDs;
		for (const TPair<FString, FString>& Source : Sources)
		{
			for (const TPair<FString, int32>& Call : CountEventCallsByID(Source.Value))
			{
				EventIDs.AddUnique(Call.Key);
			}
		}

		return EventIDs;
	}

	// ── M8-02: 나가는 길이 하나뿐인가 ─────────────────────────────────────────────

	/**
	 * PlayFab 이벤트 API 를 부르는 자리. 이 이름이 나가는 유일한 문이다 (`docs/ANALYTICS.md` 표 B).
	 * **이름을 쪼개 이어 붙이는 이유:** 이 검사는 `Source/` 아래 글자를 훑는다 — 통째로 적어 두면
	 * 검사기가 **제 소스에서** 그 글자를 세어 "나가는 길이 둘" 이라고 자기 자신에게 운다.
	 */
	FString EventApiCallNeedle()
	{
		return FString(TEXT("->Write")) + TEXT("PlayerEvent(");
	}

	// **부르는 자리만** 센다. `->` 를 앞에 두는 이유 — 주석·문서·문자열에 적힌 같은 낱말까지 세면
	// 설명문 한 줄이 검사를 울린다. 실제로 호출하려면 API 포인터를 거쳐야 한다.
	int32 CountEventApiCallSites(const FString& Text)
	{
		const FString Needle = EventApiCallNeedle();
		int32 Count = 0;
		int32 From = 0;
		while (true)
		{
			const int32 At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			if (At == INDEX_NONE)
			{
				return Count;
			}
			++Count;
			From = At + Needle.Len();
		}
	}

	// `<서명> … {` 부터 짝이 맞는 `}` 까지 잘라낸다. 몸통 안에서 무엇을 건드리는지 보려는 것이라
	// 주석·문자열도 그대로 담는다 — 놓치는 쪽보다 과하게 우는 쪽이 낫다.
	FString ExtractFunctionBody(const FString& Text, const FString& Signature)
	{
		const int32 SignatureAt = Text.Find(Signature, ESearchCase::CaseSensitive);
		if (SignatureAt == INDEX_NONE)
		{
			return FString();
		}

		const int32 OpenAt = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SignatureAt);
		if (OpenAt == INDEX_NONE)
		{
			return FString();
		}

		int32 Depth = 0;
		for (int32 At = OpenAt; At < Text.Len(); ++At)
		{
			if (Text[At] == TEXT('{'))
			{
				++Depth;
			}
			else if (Text[At] == TEXT('}'))
			{
				--Depth;
				if (Depth == 0)
				{
					return Text.Mid(OpenAt, At - OpenAt + 1);
				}
			}
		}

		return FString();
	}

	// 통로가 값을 건드리면 분석이 지급 통로가 된다 (CLAUDE.md 8 · DESIGN 4.1). **읽기도 막는다** —
	// 잔액·인벤토리를 읽어 이벤트에 실으면 그 수가 백엔드가 준 것인지 클라이언트가 만든 것인지 가릴 수 없다.
	TArray<FString> FindForbiddenTouches(const FString& Body)
	{
		static const TCHAR* const Forbidden[] = {
			TEXT("PlayerSnapshot"), TEXT("InventorySlots"), TEXT("CoinBalance"),
			TEXT("GemBalance"), TEXT("SaveLocalCache")
		};

		TArray<FString> Found;
		for (const TCHAR* const Word : Forbidden)
		{
			if (Body.Contains(Word, ESearchCase::CaseSensitive))
			{
				Found.Add(Word);
			}
		}
		return Found;
	}

	// 표에 없는 이름을 골라낸다. 바로 AddError 하지 않고 돌려주는 이유 — 자기검사가 에셋 없이
	// "이 검사가 정말 잡는가" 를 다시 볼 수 있게 (SnarehavenZoneTableTest 와 같은 짜임).
	TArray<FString> CollectUnknownEventIDs(const TSet<FString>& KnownEventIDs, const TArray<FString>& SentEventIDs)
	{
		TArray<FString> Unknown;
		for (const FString& Sent : SentEventIDs)
		{
			if (!KnownEventIDs.Contains(Sent))
			{
				Unknown.Add(Sent);
			}
		}
		return Unknown;
	}

	// ── M8-03: 호스트 권위 이벤트가 권위 자리에서만 나가는가 ──────────────────────────

	// 아래 `ExtractAuthorityBodies` 가 쓴다 — 몸통은 M8-05a 자리(밑)에 있다. 사본을 만들지 않으려고 미리 알린다.
	TArray<FString> ExtractAllFunctionBodies(const FString& Text);

	/**
	 * **권위 자리**의 몸통을 모은다. 권위는 두 얼굴로 드러난다:
	 *  ① 이름이 `…OnAuthority` — 부르는 길 자체가 이미 호스트다 (`AttackOnceOnAuthority`).
	 *  ② 몸통이 `if (!HasAuthority())` 로 스스로 문을 막는다 — 그 줄 뒤는 전부 호스트만 온다
	 *     (`AZonePortalActor::TravelToTargetZone`, M8-06). 이름만 보면 **실제로 막아 둔 자리**를
	 *     검사가 거짓으로 잡아 이름 짓기가 검사에 끌려간다.
	 * 정의는 열 0 에서 시작하고 `::` 를 달고 있어서 들여쓴 `return AttackOnceOnAuthority();` 같은
	 * **부름**과 갈린다 — 부름을 정의로 잘못 보면 바로 뒤 함수의 몸통이 통째로 "권위 안" 으로
	 * 세어져 검사가 눈을 감는다. 두 얼굴에 같이 걸리는 함수는 `AddUnique` 로 **한 번만** 센다
	 * (두 번 세면 몸통 안의 부름 수가 파일 전체의 부름 수보다 커져 검사가 거짓으로 운다).
	 */
	TArray<FString> ExtractAuthorityBodies(const FString& Text)
	{
		static const FString Needle = TEXT("OnAuthority(");

		TArray<FString> Bodies;
		int32 From = 0;
		while (true)
		{
			const int32 At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			if (At == INDEX_NONE)
			{
				break;
			}
			From = At + Needle.Len();

			int32 LineStart = 0;
			if (const int32 NewLineAt = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, At);
				NewLineAt != INDEX_NONE)
			{
				LineStart = NewLineAt + 1;
			}

			const FString Head = Text.Mid(LineStart, At - LineStart);
			if (Head.IsEmpty() || FChar::IsWhitespace(Head[0]) || !Head.Contains(TEXT("::")))
			{
				continue;
			}

			const FString Body = ExtractFunctionBody(Text.Mid(LineStart), Needle);
			if (!Body.IsEmpty())
			{
				Bodies.AddUnique(Body);
			}
		}

		// ② 이름이 뭐든 **스스로 막은** 몸통. 여는 문지기가 있으면 그 뒤는 게스트가 못 지나간다.
		static const FString Gate = TEXT("if (!HasAuthority())");
		for (const FString& Body : ExtractAllFunctionBodies(Text))
		{
			if (Body.Contains(Gate, ESearchCase::CaseSensitive))
			{
				Bodies.AddUnique(Body);
			}
		}

		return Bodies;
	}

	// 호스트 권위 이벤트인데 권위 몸통 **밖에서** 나가는 자리를 사람이 읽을 줄로 돌려준다.
	// 하나라도 나오면 같은 사건이 게스트에서 한 번 더 세어질 수 있다 (DESIGN 4.1 권위 2단).
	TArray<FString> FindUngatedHostAuthorityCalls(const FString& Text, const TSet<FString>& HostAuthorityEvents)
	{
		FString Gated;
		for (const FString& Body : ExtractAuthorityBodies(Text))
		{
			Gated += Body;
		}
		const TMap<FString, int32> GatedCounts = CountEventCallsByID(Gated);

		TArray<FString> Problems;
		for (const TPair<FString, int32>& Call : CountEventCallsByID(Text))
		{
			if (!HostAuthorityEvents.Contains(Call.Key))
			{
				continue;
			}

			const int32 InsideAuthority = GatedCounts.FindRef(Call.Key);
			if (InsideAuthority != Call.Value)
			{
				Problems.Add(FString::Printf(TEXT("%s - 부르는 자리 %d개 중 권위 몸통 안은 %d개"),
					*Call.Key, Call.Value, InsideAuthority));
			}
		}
		return Problems;
	}

	// 표에서 **보내는 쪽**이 같은 이벤트 이름들. **검사에 이름을 적지 않으려고** 표에서 읽는다.
	TSet<FString> ReadEventIDsBySender(const UDataTable& Table, const EAnalyticsEventSender Sender)
	{
		TSet<FString> EventIDs;
		for (const FName& RowName : Table.GetRowNames())
		{
			const FAnalyticsEventRow* const Row =
				Table.FindRow<FAnalyticsEventRow>(RowName, TEXT("AnalyticsSenderLookup"));
			if (Row != nullptr && Row->Sender == Sender && !Row->EventID.IsNone())
			{
				EventIDs.Add(Row->EventID.ToString());
			}
		}
		return EventIDs;
	}

	// ── M8-04: 이벤트 칸이 표와 같은가 ────────────────────────────────────────────

	/**
	 * 이벤트 한 건이 칸을 채우는 **구간**. 이 저장소는 자리마다 `TMap<FName, FString> Payload;` 로
	 * 그릇을 새로 만들고 바로 이어 `WriteGameEvent` 를 부른다 — 그 사이가 이 이벤트의 칸이다.
	 * **함수 몸통 단위로 세지 않는 이유:** 한 함수가 이벤트 둘을 보내는 자리가 이미 있다
	 * (`TryCaptureTargetOnAuthority` 의 성공·실패) — 몸통으로 묶으면 칸이 섞여 보인다.
	 */
	FString ExtractPayloadSection(const FString& Text, int32 CallAt)
	{
		static const FString Declaration = TEXT("TMap<FName, FString> Payload");
		const int32 DeclAt = Text.Find(Declaration, ESearchCase::CaseSensitive, ESearchDir::FromEnd, CallAt);
		if (DeclAt == INDEX_NONE)
		{
			return FString();
		}

		// 앞선 이벤트의 구간을 삼키지 않게 — 가장 가까운 선언이 이전 부름보다 뒤에 있어야 내 것이다.
		const int32 PreviousCallAt =
			Text.Find(EventCallNeedle(), ESearchCase::CaseSensitive, ESearchDir::FromEnd, CallAt);
		if (PreviousCallAt != INDEX_NONE && PreviousCallAt > DeclAt)
		{
			return FString();
		}

		return Text.Mid(DeclAt, CallAt - DeclAt);
	}

	// 구간이 이벤트 칸에 적은 이름들. 값이 아니라 **칸 이름**을 본다 — 표가 정하는 것이 그것이다.
	TArray<FString> CollectPayloadKeys(const FString& Section)
	{
		TArray<FString> Keys;
		const FRegexPattern KeyPattern(TEXT("Payload\\.Add\\(\\s*TEXT\\(\"([a-z0-9_]*)\""));
		FRegexMatcher Matcher(KeyPattern, Section);
		while (Matcher.FindNext())
		{
			Keys.AddUnique(Matcher.GetCaptureGroup(1));
		}
		return Keys;
	}

	/**
	 * 칸에 **싣는 값** 쪽 금지 낱말. 칸 이름이 표와 같아도 그 자리에 재고·잔액을 적으면
	 * 클라이언트가 수확량의 출처가 된다 (`docs/ANALYTICS.md` 표 C 반려 기준 · CLAUDE.md 8).
	 */
	TArray<FString> FindLedgerWordsInPayload(const FString& Section)
	{
		static const TCHAR* const Forbidden[] = {
			TEXT("Stock"), TEXT("Coin"), TEXT("Gem"), TEXT("Balance"), TEXT("Refund")
		};

		TArray<FString> Found;
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			if (!Line.Contains(TEXT("Payload.Add("), ESearchCase::CaseSensitive))
			{
				continue;
			}
			for (const TCHAR* const Word : Forbidden)
			{
				if (Line.Contains(Word, ESearchCase::CaseSensitive))
				{
					Found.AddUnique(Word);
				}
			}
		}
		return Found;
	}

	// 이벤트를 부르는 자리마다 "표가 정한 칸 == 실제로 적은 칸" 인지 본다. 본 자리 수를 같이 세는
	// 이유는 0개를 조용히 통과로 세지 않으려는 것이다 (Verification.md).
	TArray<FString> FindPayloadMismatches(const FString& Text,
		const TMap<FString, TArray<FString>>& KeysByEventID, int32& InOutCallSites)
	{
		TArray<FString> Problems;
		const FRegexPattern CallPattern(TEXT("WriteGameEvent\\(\\s*TEXT\\(\"([a-z0-9_]*)\""));
		FRegexMatcher Matcher(CallPattern, Text);
		while (Matcher.FindNext())
		{
			++InOutCallSites;
			const FString EventID = Matcher.GetCaptureGroup(1);

			const TArray<FString>* const Expected = KeysByEventID.Find(EventID);
			if (Expected == nullptr)
			{
				Problems.Add(FString::Printf(TEXT("%s - 표에 없는 이름이다"), *EventID));
				continue;
			}

			const FString Section = ExtractPayloadSection(Text, Matcher.GetMatchBeginning());
			const TArray<FString> Written = CollectPayloadKeys(Section);

			for (const FString& Key : *Expected)
			{
				if (!Written.Contains(Key))
				{
					Problems.Add(FString::Printf(TEXT("%s - 표의 칸 '%s' 를 안 적었다"), *EventID, *Key));
				}
			}
			for (const FString& Key : Written)
			{
				if (!Expected->Contains(Key))
				{
					Problems.Add(FString::Printf(TEXT("%s - 표에 없는 칸 '%s' 를 적었다"), *EventID, *Key));
				}
			}
			for (const FString& Word : FindLedgerWordsInPayload(Section))
			{
				Problems.Add(FString::Printf(TEXT("%s - 칸에 '%s' 를 싣는다 (원장은 백엔드가 쓴다)"),
					*EventID, *Word));
			}
		}
		return Problems;
	}

	// ── M8-05a: 관찰 이벤트가 캐시를 건드리지 않는가 ────────────────────────────────

	/**
	 * 파일 안의 **함수 정의 몸통**을 전부. 정의 줄은 열 0 에서 시작하고 `::` 와 `(` 를 단다
	 * (`ExtractAuthorityBodies` 와 같은 판정) — 들여쓴 줄을 정의로 보면 **부름**을 정의로 잘못 읽어
	 * 엉뚱한 몸통이 잘린다. 겹쳐 잘린 몸통이 몇 개 더 생기는 것은 해가 없다(볼 자리가 늘 뿐이다).
	 */
	TArray<FString> ExtractAllFunctionBodies(const FString& Text)
	{
		TArray<FString> Bodies;
		int32 From = 0;
		while (true)
		{
			const int32 At = Text.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
			if (At == INDEX_NONE)
			{
				return Bodies;
			}
			From = At + 2;

			int32 LineStart = 0;
			if (const int32 NewLineAt = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, At);
				NewLineAt != INDEX_NONE)
			{
				LineStart = NewLineAt + 1;
			}

			const int32 LineEnd = Text.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At);
			const FString Line = Text.Mid(LineStart, (LineEnd == INDEX_NONE ? Text.Len() : LineEnd) - LineStart);
			if (Line.IsEmpty() || FChar::IsWhitespace(Line[0]) || !Line.Contains(TEXT("("))
				|| Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("/")))
			{
				continue;
			}

			const FString Body = ExtractFunctionBody(Text.Mid(LineStart), TEXT("("));
			if (!Body.IsEmpty())
			{
				Bodies.AddUnique(Body);
			}
		}
	}

	// 그 몸통이 보내는 이벤트 중 관찰(`Observer`) 인 것의 수. 0 이면 볼 것이 없는 몸통이다.
	int32 CountObserverCalls(const FString& Body, const TSet<FString>& ObserverEvents)
	{
		int32 Count = 0;
		for (const TPair<FString, int32>& Call : CountEventCallsByID(Body))
		{
			if (ObserverEvents.Contains(Call.Key))
			{
				Count += Call.Value;
			}
		}
		return Count;
	}

	/**
	 * 관찰 이벤트를 **보내는 함수**가 캐시를 건드리는 자리. 보내는 쪽이 `Observer` 라는 것은
	 * "본 것을 옮길 뿐" 이라는 뜻이다 — 같은 함수가 잔액·인벤토리를 읽거나 쓰면 그 수가 백엔드가
	 * 준 것인지 그 함수가 만든 것인지 가릴 수 없다 (`docs/ANALYTICS.md` 표 A·C · CLAUDE.md 8).
	 * @param OutSeenCalls 몸통 안에서 실제로 본 관찰 부름 수 — 0개를 조용히 통과로 세지 않으려고 센다
	 */
	TArray<FString> FindObserverCallsTouchingCache(const FString& Text, const TSet<FString>& ObserverEvents,
		int32& OutSeenCalls)
	{
		TArray<FString> Problems;
		for (const FString& Body : ExtractAllFunctionBodies(Text))
		{
			const int32 ObserverCalls = CountObserverCalls(Body, ObserverEvents);
			if (ObserverCalls == 0)
			{
				continue;
			}
			OutSeenCalls += ObserverCalls;

			for (const FString& Touch : FindForbiddenTouches(Body))
			{
				Problems.AddUnique(FString::Printf(
					TEXT("관찰 이벤트를 보내는 함수가 '%s' 를 건드린다 (관찰은 옮기기만 한다)"), *Touch));
			}
		}
		return Problems;
	}

	// 표의 `PayloadKeys` 를 이벤트 이름별로. **검사에 칸 이름을 적지 않으려고** 표에서 읽는다.
	TMap<FString, TArray<FString>> ReadPayloadKeysByEventID(const UDataTable& Table)
	{
		TMap<FString, TArray<FString>> KeysByEventID;
		for (const FName& RowName : Table.GetRowNames())
		{
			const FAnalyticsEventRow* const Row =
				Table.FindRow<FAnalyticsEventRow>(RowName, TEXT("AnalyticsPayloadTest"));
			if (Row == nullptr || Row->EventID.IsNone())
			{
				continue;
			}

			TArray<FString>& Keys = KeysByEventID.Add(Row->EventID.ToString());
			for (const FName& Key : Row->PayloadKeys)
			{
				Keys.AddUnique(Key.ToString());
			}
		}
		return KeysByEventID;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsEventTableTest,
	"Snarehaven.Analytics.EventTable.MatchesDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsEventTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/ANALYTICS.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("이벤트 문서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const TMap<FString, FDocEvent> DocEvents = ParseDocumentEvents(Doc);

	// 문서 모양이 바뀌어 한 줄도 못 읽으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
	if (!TestTrue(TEXT("문서 표 A 에서 이벤트 줄을 하나 이상 읽었다"), DocEvents.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("docs/ANALYTICS.md 표 A 에서 이벤트 %d개를 읽었다"), DocEvents.Num()));

	UDataTable* EventTable = LoadObject<UDataTable>(nullptr, AnalyticsTablePath);
	if (!TestNotNull(TEXT("DT_AnalyticsEventTable 로드"), EventTable))
	{
		return false;
	}

	const TArray<FName> RowNames = EventTable->GetRowNames();
	TestEqual(TEXT("문서의 이벤트 수와 표의 행 수가 같다"), RowNames.Num(), DocEvents.Num());

	// ① 표 → 문서: 표의 행이 문서에도 같은 내용으로 적혀 있나. 그러면서 표 자체도 본다(빈 칸·중복).
	TSet<FString> MatchedDocNames;
	TSet<FString> KnownEventIDs;
	for (const FName& RowName : RowNames)
	{
		const FAnalyticsEventRow* Row = EventTable->FindRow<FAnalyticsEventRow>(RowName, TEXT("AnalyticsEventTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			continue;
		}

		const FString EventID = Row->EventID.ToString();
		if (!TestFalse(*FString::Printf(TEXT("[%s] EventID 가 비지 않았다"), *RowName.ToString()), Row->EventID.IsNone()))
		{
			continue;
		}
		TestFalse(*FString::Printf(TEXT("[%s] EventID '%s' 가 표 안에서 중복되지 않는다"), *RowName.ToString(), *EventID),
			KnownEventIDs.Contains(EventID));
		KnownEventIDs.Add(EventID);

		TestTrue(*FString::Printf(TEXT("[%s] 칸 목록이 비지 않았다"), *EventID), Row->PayloadKeys.Num() > 0);

		const FDocEvent* DocEvent = DocEvents.Find(EventID);
		if (!TestNotNull(*FString::Printf(TEXT("문서 표 A 에 '%s' 줄이 있다"), *EventID), DocEvent))
		{
			continue;
		}

		AddInfo(FString::Printf(TEXT("[%s] 보내는 쪽 %s · 칸 %d개"),
			*EventID, SenderName(Row->Sender), Row->PayloadKeys.Num()));

		TestTrue(*FString::Printf(TEXT("'%s' 보내는 쪽이 문서와 같다 (문서 %s / 표 %s)"),
			*EventID, SenderName(DocEvent->Sender), SenderName(Row->Sender)),
			Row->Sender == DocEvent->Sender);
		TestEqual(*FString::Printf(TEXT("'%s' 칸 목록이 문서와 같다"), *EventID),
			JoinNames(Row->PayloadKeys), JoinNames(DocEvent->PayloadKeys));

		MatchedDocNames.Add(EventID);
	}

	// ② 문서 → 표: 있는 것만 훑으면 한쪽에만 있는 이벤트를 못 본다 (루프#91 이 목장 생산에서 겪은 구멍).
	for (const TPair<FString, FDocEvent>& DocEvent : DocEvents)
	{
		TestTrue(*FString::Printf(TEXT("문서의 '%s' 이벤트가 표에도 있다"), *DocEvent.Key),
			MatchedDocNames.Contains(DocEvent.Key));
	}

	// ③ 코드 → 표: 보내는 이름이 전부 표에 있나 (M8-01 합격 기준).
	const TArray<FString> SentEventIDs = FindEventIDsSentByCode(LoadSourceFiles(FPaths::ProjectDir() / TEXT("Source")));
	AddInfo(FString::Printf(TEXT("코드가 보내는 이벤트 이름 %d개를 훑었다 (0개면 아직 보내는 코드가 없다는 뜻 - M8-02~06)"),
		SentEventIDs.Num()));
	for (const FString& Unknown : CollectUnknownEventIDs(KnownEventIDs, SentEventIDs))
	{
		AddError(FString::Printf(TEXT("코드가 보내는 '%s' 가 DT_AnalyticsEventTable 에 없다"), *Unknown));
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·문서를 건드리지 않고 값을 손으로 지어 먹인다 — 검사 전용 이름이라 실제 표에 기대지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsEventCheckSelfTest,
	"Snarehaven.Analytics.EventTable.CheckCatchesUnknownEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsEventCheckSelfTest::RunTest(const FString& Parameters)
{
	// ① 표에 없는 이름을 보내면 잡는가. 멀쩡한 쪽까지 물면 검사가 쓸모없어지므로 그쪽부터 본다.
	const TSet<FString> Known = { TEXT("test_event_a"), TEXT("test_event_b") };
	TestEqual(TEXT("표에 있는 이름만 보내면 문제 0건"),
		CollectUnknownEventIDs(Known, { TEXT("test_event_a"), TEXT("test_event_b") }).Num(), 0);
	TestEqual(TEXT("표에 없는 이름을 하나 보내면 문제 1건"),
		CollectUnknownEventIDs(Known, { TEXT("test_event_a"), TEXT("test_event_nowhere") }).Num(), 1);
	TestEqual(TEXT("이름을 아예 안 적고 보내면(빈 이름) 문제 1건"),
		CollectUnknownEventIDs(Known, { TEXT("") }).Num(), 1);

	// ② 문서 읽기가 살아 있는가. 표 A 와 같은 꼴의 줄을 지어 먹인다.
	const FString FakeDoc =
		TEXT("| 이벤트 이름 | 언제 | 칸(payload) | 보내는 쪽 | 지금 그 자리 | 조각 |\n")
		TEXT("|---|---|---|---|---|---|\n")
		TEXT("| `test_killed` | 언제 | `test_key_a` - `test_key_b` | 클라이언트 (**호스트 권위**) | 없음 | M8-03 |\n")
		TEXT("| `test_requested` | 언제 | `test_key_c` | 클라이언트 (요청자 본인) | 없음 | M8-04 |\n")
		TEXT("| 이건 이벤트 줄이 아니다 | 셋 | 뿐이다 |\n");

	const TMap<FString, FDocEvent> Parsed = ParseDocumentEvents(FakeDoc);
	if (!TestEqual(TEXT("표 A 꼴 줄 둘만 읽는다 (머리줄·구분줄·칸 수가 다른 줄은 안 읽는다)"), Parsed.Num(), 2))
	{
		return false;
	}

	const FDocEvent* Killed = Parsed.Find(TEXT("test_killed"));
	if (TestNotNull(TEXT("`test_killed` 줄을 읽었다"), Killed))
	{
		TestTrue(TEXT("호스트 권위로 읽는다"), Killed->Sender == EAnalyticsEventSender::HostAuthority);
		TestEqual(TEXT("칸 둘을 순서대로 읽는다"), JoinNames(Killed->PayloadKeys),
			FString(TEXT("test_key_a, test_key_b")));
	}

	const FDocEvent* Requested = Parsed.Find(TEXT("test_requested"));
	if (TestNotNull(TEXT("`test_requested` 줄을 읽었다"), Requested))
	{
		TestTrue(TEXT("요청자 본인으로 읽는다"), Requested->Sender == EAnalyticsEventSender::Requester);
	}

	return true;
}


// ROADMAP M8-02 2층 검사 — 이벤트가 나가는 길이 `WriteGameEvent` 하나뿐이고, 그 함수가 값을 안 건드리는가.
// 길이 둘이 되면 "무엇을 보내는가" 를 한 곳에서 볼 수 없고, M8-01 의 이름표 대조에도 새는 길이 생긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsChannelTest,
	"Snarehaven.Analytics.Channel.SinglePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsChannelTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> Sources = LoadSourceFiles(FPaths::ProjectDir() / TEXT("Source"));

	// 한 파일도 못 읽으면 "부르는 자리 0개" 가 되어 조용히 통과한다 — 그게 제일 위험하다.
	if (!TestTrue(TEXT("Source/ 아래 소스 파일을 하나 이상 읽었다"), Sources.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("소스 %d개를 훑었다"), Sources.Num()));

	int32 TotalCallSites = 0;
	TArray<FString> FilesWithCallSite;
	for (const TPair<FString, FString>& Source : Sources)
	{
		const int32 CallSites = CountEventApiCallSites(Source.Value);
		if (CallSites > 0)
		{
			TotalCallSites += CallSites;
			FilesWithCallSite.Add(FPaths::GetCleanFilename(Source.Key));
		}
	}

	TestEqual(*FString::Printf(TEXT("이벤트가 나가는 자리가 하나뿐이다 (찾은 곳: %s)"),
		FilesWithCallSite.Num() > 0 ? *FString::Join(FilesWithCallSite, TEXT(", ")) : TEXT("없음")),
		TotalCallSites, 1);
	TestEqual(TEXT("그 자리는 SnareBackendSubsystem.cpp 다"),
		FString::Join(FilesWithCallSite, TEXT(", ")), FString(TEXT("SnareBackendSubsystem.cpp")));

	const FString BackendPath =
		FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private/Backend/SnareBackendSubsystem.cpp");
	FString Backend;
	if (!TestTrue(*FString::Printf(TEXT("통로 파일을 읽는다 (%s)"), *BackendPath),
		FFileHelper::LoadFileToString(Backend, *BackendPath)))
	{
		return false;
	}

	const FString Body = ExtractFunctionBody(Backend, TEXT("USnareBackendSubsystem::WriteGameEvent"));
	if (!TestTrue(TEXT("WriteGameEvent 몸통을 잘라냈다"), !Body.IsEmpty()))
	{
		return false;
	}

	// 파일 안에 하나 있다는 것만으로는 부족하다 — 그 하나가 **이 함수 안**이어야 통로가 하나다.
	TestEqual(TEXT("그 자리가 WriteGameEvent 몸통 안에 있다"), CountEventApiCallSites(Body), 1);

	const TArray<FString> Touches = FindForbiddenTouches(Body);
	for (const FString& Touch : Touches)
	{
		AddError(FString::Printf(
			TEXT("WriteGameEvent 가 '%s' 를 건드린다 - 분석은 관찰만 한다 (CLAUDE.md 8)"), *Touch));
	}
	AddInfo(FString::Printf(TEXT("WriteGameEvent 몸통 %d글자 - 잔액·인벤토리를 건드리는 낱말 %d개"),
		Body.Len(), Touches.Num()));

	return true;
}


// 위 검사가 "코드가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 실제 파일을 건드리지 않고 지어낸 소스 글자를 먹인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsChannelCheckSelfTest,
	"Snarehaven.Analytics.Channel.CheckCatchesSecondPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsChannelCheckSelfTest::RunTest(const FString& Parameters)
{
	// 지어낸 소스도 **이름을 이어 붙여** 만든다 — 통째로 적으면 이 파일이 진짜 검사에 걸린다.
	const FString Call = FString(TEXT("Api")) + EventApiCallNeedle();

	// ① 부르는 자리 세기. 멀쩡한 쪽까지 물면 검사가 쓸모없어지므로 그쪽부터 본다.
	TestEqual(TEXT("부르는 자리가 없으면 0"), CountEventApiCallSites(TEXT("아무것도 없다")), 0);
	TestEqual(TEXT("부르는 자리 하나면 1"), CountEventApiCallSites(Call + TEXT("Request);")), 1);
	TestEqual(TEXT("다른 곳에서 한 번 더 부르면 2"),
		CountEventApiCallSites(Call + TEXT("A);\n") + Call + TEXT("B);")), 2);
	TestEqual(TEXT("주석·문자열에 적힌 같은 낱말은 안 센다 (`->` 가 없다)"),
		CountEventApiCallSites(FString(TEXT("// Write")) + TEXT("PlayerEvent 로 나간다")), 0);

	// ② 몸통 잘라내기 — 안쪽 중괄호(람다·for)에 속지 않아야 몸통 밖의 부름이 안 섞인다.
	const FString FakeSource =
		TEXT("void Other() { ") + Call + TEXT("Outside); }\n")
		+ TEXT("bool UFake::WriteGameEvent(const FName EventID)\n")
		+ TEXT("{\n")
		+ TEXT("\tfor (int32 I = 0; I < 2; ++I) { Body.Add(I); }\n")
		+ TEXT("\t") + Call + TEXT("Request);\n")
		+ TEXT("\treturn true;\n")
		+ TEXT("}\n")
		+ TEXT("void After() { ") + Call + TEXT("Later); }\n");

	const FString Body = ExtractFunctionBody(FakeSource, TEXT("UFake::WriteGameEvent"));
	if (!TestTrue(TEXT("몸통을 잘라냈다"), !Body.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("몸통 안의 부름만 센다 (앞뒤 함수의 부름은 안 센다)"), CountEventApiCallSites(Body), 1);
	TestFalse(TEXT("몸통이 다음 함수까지 먹지 않았다"), Body.Contains(TEXT("Later")));
	TestEqual(TEXT("없는 함수를 찾으면 빈 글자"), ExtractFunctionBody(FakeSource, TEXT("UFake::NotHere")), FString());

	// ③ 값 건드림 잡기.
	TestEqual(TEXT("관찰만 하는 몸통은 문제 0건"),
		FindForbiddenTouches(TEXT("{ Request.EventName = EventID.ToString(); }")).Num(), 0);
	TestEqual(TEXT("잔액을 이벤트에 실으면 문제 1건"),
		FindForbiddenTouches(TEXT("{ Body.Add(TEXT(\"balance\"), PlayerSnapshot.CoinBalance); }")).Num(), 2);

	return true;
}

// ROADMAP M8-03 2층 검사 — 호스트 권위 이벤트는 **권위 자리에서만** 나가는가.
// 게스트도 같은 이벤트를 보내면 한 사건이 두 번 세어진다 (DESIGN 4.1 권위 2단 · `docs/ANALYTICS.md` 표 A).
// 보는 규칙 하나: **표에서 `HostAuthority` 로 적힌 이벤트를 부르는 자리는 전부 `…OnAuthority` 함수 몸통 안이다.**
// (M8-06 이 `zone_travelled` 를 다른 이름의 함수에서 보내면 이 검사가 운다 — 그때 자리 이름을 맞추거나
//  검사를 넓힌다. 조용히 통과시키지 않는 쪽이 옳다.)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsCoreLoopTest,
	"Snarehaven.Analytics.CoreLoop.SentOnHostAuthorityOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsCoreLoopTest::RunTest(const FString& Parameters)
{
	UDataTable* const EventTable = LoadObject<UDataTable>(nullptr, AnalyticsTablePath);
	if (!TestNotNull(TEXT("DT_AnalyticsEventTable 로드"), EventTable))
	{
		return false;
	}

	const TSet<FString> HostAuthorityEvents =
		ReadEventIDsBySender(*EventTable, EAnalyticsEventSender::HostAuthority);
	if (!TestTrue(TEXT("표에 호스트 권위 이벤트가 있다"), HostAuthorityEvents.Num() > 0))
	{
		return false;
	}

	const FString SourceDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"));
	const TArray<TPair<FString, FString>> Sources = LoadSourceFiles(SourceDir);
	if (!TestTrue(TEXT("훑을 소스 파일이 있다"), Sources.Num() > 0))
	{
		return false;
	}

	int32 GatedCallSites = 0;
	for (const TPair<FString, FString>& Source : Sources)
	{
		const TArray<FString> Problems = FindUngatedHostAuthorityCalls(Source.Value, HostAuthorityEvents);
		for (const FString& Problem : Problems)
		{
			AddError(FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(Source.Key), *Problem));
		}

		for (const TPair<FString, int32>& Call : CountEventCallsByID(Source.Value))
		{
			if (HostAuthorityEvents.Contains(Call.Key))
			{
				GatedCallSites += Call.Value;
			}
		}
	}

	// 부르는 자리가 0개면 "볼 게 없어 통과" 다 — 빈 검사를 통과로 세지 않는다 (Verification.md).
	TestTrue(TEXT("호스트 권위 이벤트를 보내는 자리를 실제로 봤다"), GatedCallSites > 0);
	AddInfo(FString::Printf(TEXT("소스 %d개에서 호스트 권위 이벤트 %d개 이름, 부르는 자리 %d개를 봤다"),
		Sources.Num(), HostAuthorityEvents.Num(), GatedCallSites));

	return true;
}


// 위 검사가 "코드가 멀쩡해서" 통과할 때도 로직이 살아 있는지 본다 (역검증). 지어낸 소스만 먹인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsCoreLoopCheckSelfTest,
	"Snarehaven.Analytics.CoreLoop.CheckCatchesUngatedCall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsCoreLoopCheckSelfTest::RunTest(const FString& Parameters)
{
	// 지어낸 부름도 **이름을 이어 붙여** 만든다 — 통째로 적으면 위 검사가 이 파일을 진짜 자리로 센다.
	auto FakeCall = [](const TCHAR* EventID)
	{
		return FString(TEXT("Backend->WriteGame")) + TEXT("Event(TEXT(\"") + EventID + TEXT("\"), Payload);");
	};

	const TSet<FString> HostAuthorityEvents = { TEXT("fake_host_event") };

	const FString Gated =
		TEXT("bool UFake::AttackOnceOnAuthority()\n{\n\t") + FakeCall(TEXT("fake_host_event")) + TEXT("\n}\n");
	TestEqual(TEXT("권위 몸통 안에서 보내면 문제 0건"),
		FindUngatedHostAuthorityCalls(Gated, HostAuthorityEvents).Num(), 0);

	const FString Ungated =
		TEXT("bool UFake::AttackOnce()\n{\n\t") + FakeCall(TEXT("fake_host_event")) + TEXT("\n}\n");
	TestEqual(TEXT("권위 문턱 밖에서 보내면 문제 1건"),
		FindUngatedHostAuthorityCalls(Ungated, HostAuthorityEvents).Num(), 1);

	// 들여쓴 **부름**을 정의로 잘못 보면 바로 뒤 함수가 통째로 "권위 안" 이 된다 — 그 함정을 본다.
	const FString CallLooksLikeDefinition =
		FString(TEXT("bool UFake::AttackOnce()\n{\n\treturn AttackOnceOnAuthority();\n}\n\n"))
		+ TEXT("void UFake::Elsewhere()\n{\n\t") + FakeCall(TEXT("fake_host_event")) + TEXT("\n}\n");
	TestEqual(TEXT("들여쓴 부름은 권위 몸통으로 세지 않는다"),
		FindUngatedHostAuthorityCalls(CallLooksLikeDefinition, HostAuthorityEvents).Num(), 1);

	// 요청자·관찰 이벤트(M8-04~06)는 권위 자리 밖이 정상이라 건드리지 않는다.
	const FString RequesterCall =
		TEXT("void UFake::Queue()\n{\n\t") + FakeCall(TEXT("fake_request_event")) + TEXT("\n}\n");
	TestEqual(TEXT("호스트 권위가 아닌 이벤트는 문턱 밖에서도 문제 0건"),
		FindUngatedHostAuthorityCalls(RequesterCall, HostAuthorityEvents).Num(), 0);

	// M8-06: 이름이 `…OnAuthority` 가 아니어도 **스스로 막은** 몸통은 권위 자리다
	// (`AZonePortalActor::TravelToTargetZone`). 이름만 보면 막아 둔 자리를 거짓으로 잡는다.
	const FString SelfGated =
		FString(TEXT("bool UFake::Travel()\n{\n\tif (!HasAuthority())\n\t{\n\t\treturn false;\n\t}\n\t"))
		+ FakeCall(TEXT("fake_host_event")) + TEXT("\n}\n");
	TestEqual(TEXT("스스로 막은 몸통 안에서 보내면 문제 0건"),
		FindUngatedHostAuthorityCalls(SelfGated, HostAuthorityEvents).Num(), 0);

	// 두 얼굴에 같이 걸리는 함수를 **두 번 세면** 몸통 안의 부름이 파일 전체보다 많아져 거짓으로 운다.
	const FString BothFaces =
		FString(TEXT("bool UFake::TravelOnAuthority()\n{\n\tif (!HasAuthority())\n\t{\n\t\treturn false;\n\t}\n\t"))
		+ FakeCall(TEXT("fake_host_event")) + TEXT("\n}\n");
	TestEqual(TEXT("이름과 문지기에 같이 걸려도 몸통은 하나"), ExtractAuthorityBodies(BothFaces).Num(), 1);
	TestEqual(TEXT("같이 걸린 함수도 문제 0건"),
		FindUngatedHostAuthorityCalls(BothFaces, HostAuthorityEvents).Num(), 0);

	// 몸통 잘라내기가 앞뒤 함수를 먹지 않는지 — 위 셋이 기대는 바탕이다.
	const TArray<FString> Bodies = ExtractAuthorityBodies(Gated + TEXT("void After() { Later(); }\n"));
	if (TestEqual(TEXT("권위 함수 몸통을 하나 잘라냈다"), Bodies.Num(), 1))
	{
		TestFalse(TEXT("몸통이 다음 함수까지 먹지 않았다"), Bodies[0].Contains(TEXT("Later")));
	}

	return true;
}

// ROADMAP M8-04 2층 검사 — 이벤트가 **표가 정한 칸만** 싣는가.
// 목장 두 줄의 합격 기준("클라이언트가 수확량·코인을 이벤트 칸에 적는 줄 0")을 이 한 규칙이 덮는다 —
// `coin`·`stock` 같은 칸은 표(`DT_AnalyticsEventTable`)에 없으니 생기는 순간 운다
// (`docs/ANALYTICS.md` 표 C 반려 기준 · CLAUDE.md 8). 목장만이 아니라 **보내는 자리 전부**를 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsPayloadTest,
	"Snarehaven.Analytics.Payload.MatchesTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsPayloadTest::RunTest(const FString& Parameters)
{
	UDataTable* const EventTable = LoadObject<UDataTable>(nullptr, AnalyticsTablePath);
	if (!TestNotNull(TEXT("DT_AnalyticsEventTable 로드"), EventTable))
	{
		return false;
	}

	const TMap<FString, TArray<FString>> KeysByEventID = ReadPayloadKeysByEventID(*EventTable);
	if (!TestTrue(TEXT("표에서 칸 목록을 읽었다"), KeysByEventID.Num() > 0))
	{
		return false;
	}

	const FString SourceDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"));
	const TArray<TPair<FString, FString>> Sources = LoadSourceFiles(SourceDir);
	if (!TestTrue(TEXT("훑을 소스 파일이 있다"), Sources.Num() > 0))
	{
		return false;
	}

	int32 CallSites = 0;
	for (const TPair<FString, FString>& Source : Sources)
	{
		for (const FString& Problem : FindPayloadMismatches(Source.Value, KeysByEventID, CallSites))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(Source.Key), *Problem));
		}
	}

	// 보내는 자리가 0개면 "볼 게 없어 통과" 다 — 빈 검사를 통과로 세지 않는다 (Verification.md).
	TestTrue(TEXT("이벤트를 보내는 자리를 실제로 봤다"), CallSites > 0);
	AddInfo(FString::Printf(TEXT("소스 %d개에서 표의 이벤트 %d개, 칸을 맞대 본 자리 %d개"),
		Sources.Num(), KeysByEventID.Num(), CallSites));

	return true;
}


// 위 검사가 "코드가 멀쩡해서" 통과할 때도 로직이 살아 있는지 본다 (역검증). 지어낸 소스만 먹인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsPayloadCheckSelfTest,
	"Snarehaven.Analytics.Payload.CheckCatchesWrongKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsPayloadCheckSelfTest::RunTest(const FString& Parameters)
{
	// 지어낸 부름도 **이름을 이어 붙여** 만든다 — 통째로 적으면 위 검사가 이 파일을 진짜 자리로 센다.
	auto FakeSite = [](const TCHAR* Adds)
	{
		return FString(TEXT("void UFake::Queue()\n{\n\tTMap<FName, FString> Payload;\n")) + Adds
			+ TEXT("\tBackend->") + EventCallNeedle() + TEXT("fake_request\"), Payload);\n}\n");
	};

	TMap<FString, TArray<FString>> KeysByEventID;
	KeysByEventID.Add(TEXT("fake_request"), { TEXT("slot_index") });

	int32 CallSites = 0;
	const FString Good = FakeSite(TEXT("\tPayload.Add(TEXT(\"slot_index\"), FString::FromInt(SlotIndex));\n"));
	TestEqual(TEXT("표의 칸만 적으면 문제 0건"),
		FindPayloadMismatches(Good, KeysByEventID, CallSites).Num(), 0);
	TestEqual(TEXT("본 자리를 셌다"), CallSites, 1);

	const FString Extra = FakeSite(
		TEXT("\tPayload.Add(TEXT(\"slot_index\"), FString::FromInt(SlotIndex));\n")
		TEXT("\tPayload.Add(TEXT(\"harvest_count\"), FString::FromInt(7));\n"));
	TestEqual(TEXT("표에 없는 칸을 적으면 문제 1건"),
		FindPayloadMismatches(Extra, KeysByEventID, CallSites).Num(), 1);

	const FString Missing = FakeSite(TEXT(""));
	TestEqual(TEXT("표의 칸을 안 적으면 문제 1건"),
		FindPayloadMismatches(Missing, KeysByEventID, CallSites).Num(), 1);

	// 칸 이름은 맞는데 **값**이 원장이면 — 수확량이 클라이언트 캐시에서 나오는 길이다.
	const FString Ledger = FakeSite(TEXT("\tPayload.Add(TEXT(\"slot_index\"), FString::FromInt(Slot.Stock));\n"));
	TestEqual(TEXT("칸에 재고를 실으면 문제 1건"),
		FindPayloadMismatches(Ledger, KeysByEventID, CallSites).Num(), 1);

	// 한 함수가 이벤트 둘을 보내도 칸이 섞이지 않는지 — 실제 포획 성공·실패가 그 모양이다.
	KeysByEventID.Add(TEXT("fake_second"), { TEXT("reason") });
	const FString TwoSites = Good
		+ FString(TEXT("void UFake::Other()\n{\n\tTMap<FName, FString> Payload;\n"))
		+ TEXT("\tPayload.Add(TEXT(\"reason\"), TEXT(\"none\"));\n")
		+ TEXT("\tBackend->") + EventCallNeedle() + TEXT("fake_second\"), Payload);\n}\n");
	TestEqual(TEXT("자리 둘이 각자 제 칸만 보면 문제 0건"),
		FindPayloadMismatches(TwoSites, KeysByEventID, CallSites).Num(), 0);

	// 그릇 선언이 앞 이벤트 뒤에 없으면 **앞 구간을 삼키지 않고** 빈 구간으로 본다.
	TestTrue(TEXT("그릇 선언이 없으면 구간이 비어 있다"),
		ExtractPayloadSection(FString(TEXT("Backend->")) + EventCallNeedle() + TEXT("fake_request\"));"),
			9).IsEmpty());

	return true;
}

// ROADMAP M8-05a 2층 검사 — 관찰(`Observer`) 이벤트는 **캐시를 건드리지 않는 함수**에서 나가는가.
// `currency_balance_changed` 는 잔액이 바뀐 것을 **보고만** 하는 이벤트다. 같은 함수가 잔액을 쓰면
// 분석이 재화를 바꾸는 통로가 되고(CLAUDE.md 8), 읽기만 해도 실린 수의 출처를 가릴 수 없다.
// 보는 규칙 하나: **표에서 `Observer` 로 적힌 이벤트를 부르는 함수 몸통에는 잔액·인벤토리 낱말이 0개다.**
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsObserverTest,
	"Snarehaven.Analytics.Observer.SentWithoutTouchingCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsObserverTest::RunTest(const FString& Parameters)
{
	UDataTable* const EventTable = LoadObject<UDataTable>(nullptr, AnalyticsTablePath);
	if (!TestNotNull(TEXT("DT_AnalyticsEventTable 로드"), EventTable))
	{
		return false;
	}

	const TSet<FString> ObserverEvents = ReadEventIDsBySender(*EventTable, EAnalyticsEventSender::Observer);
	if (!TestTrue(TEXT("표에 관찰 이벤트가 있다"), ObserverEvents.Num() > 0))
	{
		return false;
	}

	const TArray<TPair<FString, FString>> Sources = LoadSourceFiles(FPaths::ProjectDir() / TEXT("Source"));
	if (!TestTrue(TEXT("훑을 소스 파일이 있다"), Sources.Num() > 0))
	{
		return false;
	}

	int32 SeenInBodies = 0;
	int32 SeenInFiles = 0;
	for (const TPair<FString, FString>& Source : Sources)
	{
		SeenInFiles += CountObserverCalls(Source.Value, ObserverEvents);
		for (const FString& Problem : FindObserverCallsTouchingCache(Source.Value, ObserverEvents, SeenInBodies))
		{
			AddError(FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(Source.Key), *Problem));
		}
	}

	// 파일에는 있는데 몸통에서 못 본 부름이 있으면 **검사가 그 자리를 안 본 것**이다 — 조용히 넘기지 않는다.
	TestEqual(TEXT("파일에서 센 관찰 부름을 함수 몸통에서도 전부 봤다"), SeenInBodies, SeenInFiles);
	TestTrue(TEXT("관찰 이벤트를 보내는 자리를 실제로 봤다"), SeenInBodies > 0);
	AddInfo(FString::Printf(TEXT("소스 %d개에서 관찰 이벤트 %d개 이름, 부르는 자리 %d개를 봤다"),
		Sources.Num(), ObserverEvents.Num(), SeenInBodies));

	return true;
}


// 위 검사가 "코드가 멀쩡해서" 통과할 때도 로직이 살아 있는지 본다 (역검증). 지어낸 소스만 먹인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsObserverCheckSelfTest,
	"Snarehaven.Analytics.Observer.CheckCatchesCacheTouch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsObserverCheckSelfTest::RunTest(const FString& Parameters)
{
	// 지어낸 부름도 **이름을 이어 붙여** 만든다 — 통째로 적으면 위 검사가 이 파일을 진짜 자리로 센다.
	auto FakeFunction = [](const TCHAR* Head, const TCHAR* Extra)
	{
		return FString(TEXT("void UFake::")) + Head + TEXT("()\n{\n") + Extra
			+ TEXT("\t") + EventCallNeedle() + TEXT("fake_observed\"), Payload);\n}\n");
	};

	TSet<FString> ObserverEvents;
	ObserverEvents.Add(TEXT("fake_observed"));

	// ① 몸통 잘라내기 — 들여쓴 **부름**을 정의로 보면 엉뚱한 몸통이 잘린다.
	const FString Indented =
		FString(TEXT("void UFake::Outer()\n{\n\tUFake::Inner();\n\treturn;\n}\n"));
	TestEqual(TEXT("정의 하나면 몸통 하나"), ExtractAllFunctionBodies(Indented).Num(), 1);
	TestEqual(TEXT("정의가 없으면 몸통 0개"),
		ExtractAllFunctionBodies(TEXT("\tUFake::Inner();\n")).Num(), 0);

	// ② 관찰 부름 세기.
	const FString Clean = FakeFunction(TEXT("Report"), TEXT("\tPayload.Add(TEXT(\"currency\"), Code);\n"));
	TestEqual(TEXT("관찰 이벤트를 보내면 1"), CountObserverCalls(Clean, ObserverEvents), 1);
	TestEqual(TEXT("표에 없는 이름은 안 센다"), CountObserverCalls(Clean, TSet<FString>()), 0);

	// ③ 캐시를 건드리는 자리 잡기.
	int32 SeenCalls = 0;
	TestEqual(TEXT("옮기기만 하는 함수는 문제 0건"),
		FindObserverCallsTouchingCache(Clean, ObserverEvents, SeenCalls).Num(), 0);
	TestEqual(TEXT("본 자리를 셌다"), SeenCalls, 1);

	const FString Writes = FakeFunction(TEXT("Report"), TEXT("\tPlayerSnapshot.CoinBalance = 100;\n"));
	TestEqual(TEXT("같은 함수가 잔액을 쓰면 문제가 잡힌다"),
		FindObserverCallsTouchingCache(Writes, ObserverEvents, SeenCalls).Num(), 2);

	const FString Reads = FakeFunction(TEXT("Report"), TEXT("\tconst int32 Amount = PlayerSnapshot.GemBalance;\n"));
	TestEqual(TEXT("읽기만 해도 잡힌다 (실린 수의 출처가 흐려진다)"),
		FindObserverCallsTouchingCache(Reads, ObserverEvents, SeenCalls).Num(), 2);

	return true;
}


// ROADMAP M8-07 2층 검사 — 출시 서류에 **신고한** 이벤트 목록(`docs/STORE_READINESS.md` 표 A)이
// 실제로 **보내는** 목록(`docs/ANALYTICS.md` 표 A)과 같은가.
// 사람 눈이 아니라 검사가 보는 이유: 이벤트를 하나 더하고 서류를 안 고치면 **없다고 신고한 것을 보내는** 상태가 된다.
// **이 검사도 이름을 하나도 들고 있지 않다** (M8-01 과 같은 이유 — 사본이 늘면 검사 자신이 "한쪽만 고치기" 를 저지른다).
namespace
{
	// 서류의 이벤트 목록은 이 표시 줄 **다음 줄**에 백틱 이름으로만 적는다. 표시 줄에는 문서 경로·검사 이름이
	// 같이 백틱으로 들어가는데(밑줄 이름 규칙에 안 걸리지만) 줄을 갈라 두면 그 줄에 문장이 늘어도 안 섞인다.
	const TCHAR* DisclosureMarker = TEXT("분석 이벤트 목록");

	TArray<FName> ParseDisclosedEvents(const FString& Doc)
	{
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);

		for (int32 Index = 0; Index + 1 < Lines.Num(); ++Index)
		{
			if (Lines[Index].Contains(DisclosureMarker))
			{
				return ParseBacktickedNames(Lines[Index + 1]);
			}
		}
		return TArray<FName>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsDisclosureTest,
	"Snarehaven.Analytics.Disclosure.StoreReadinessMatchesDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsDisclosureTest::RunTest(const FString& Parameters)
{
	const FString EventDocPath = FPaths::ProjectDir() / TEXT("docs/ANALYTICS.md");
	const FString StoreDocPath = FPaths::ProjectDir() / TEXT("docs/STORE_READINESS.md");

	FString EventDoc;
	FString StoreDoc;
	if (!TestTrue(*FString::Printf(TEXT("이벤트 문서를 읽는다 (%s)"), *EventDocPath),
			FFileHelper::LoadFileToString(EventDoc, *EventDocPath))
		|| !TestTrue(*FString::Printf(TEXT("출시 서류를 읽는다 (%s)"), *StoreDocPath),
			FFileHelper::LoadFileToString(StoreDoc, *StoreDocPath)))
	{
		return false;
	}

	TSet<FString> Sent;
	for (const TPair<FString, FDocEvent>& DocEvent : ParseDocumentEvents(EventDoc))
	{
		Sent.Add(DocEvent.Key);
	}

	TSet<FString> Disclosed;
	for (const FName& Name : ParseDisclosedEvents(StoreDoc))
	{
		Disclosed.Add(Name.ToString());
	}

	// 한쪽이라도 0개면 "맞댈 게 없어 통과" 가 된다 — 문서 모양이 바뀐 것이지 맞는 것이 아니다.
	if (!TestTrue(TEXT("ANALYTICS 표 A 에서 이벤트를 하나 이상 읽었다"), Sent.Num() > 0)
		|| !TestTrue(TEXT("STORE_READINESS 표 A 의 이벤트 목록을 하나 이상 읽었다"), Disclosed.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("보내는 목록 %d개, 신고한 목록 %d개를 맞대 봤다"), Sent.Num(), Disclosed.Num()));

	// 양방향으로 본다 — 한쪽만 훑으면 서류에만 있는 이름(안 보내는 것을 신고)을 못 본다.
	for (const FString& EventID : Sent)
	{
		TestTrue(*FString::Printf(TEXT("보내는 '%s' 가 출시 서류에도 신고돼 있다"), *EventID),
			Disclosed.Contains(EventID));
	}
	for (const FString& EventID : Disclosed)
	{
		TestTrue(*FString::Printf(TEXT("서류에 신고한 '%s' 가 ANALYTICS 표 A 에도 있다"), *EventID),
			Sent.Contains(EventID));
	}

	return true;
}

// #0204 2층 — 이벤트 칸 이름에 PlayFab **예약 필드**가 없는가. `source` 칸 하나 때문에 `currency_balance_changed` 가
// 통째로 거절됐다 (`play_0201.log` "Field source is a reserved PlayFab field"). 대소문자를 가리지 않는다.
// 표 칸과 코드가 부르는 칸 둘 다 본다 — 코드 한 줄만 되돌려도 운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnalyticsReservedFieldTest,
	"Snarehaven.Analytics.Payload.NoReservedField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnalyticsReservedFieldTest::RunTest(const FString& Parameters)
{
	// PlayStream 이벤트 몸통에서 덮어쓸 수 없는 이름들 (PlayFab 이 이벤트에 스스로 채우는 칸, docs/ANALYTICS.md).
	static const TCHAR* const Reserved[] = {
		TEXT("Source"), TEXT("SourceType"), TEXT("EventName"), TEXT("EventNamespace"), TEXT("EventId"), TEXT("Entity"),
		TEXT("EntityId"), TEXT("EntityType"), TEXT("Timestamp"), TEXT("History"), TEXT("CustomTags"), TEXT("Reserved"),
		TEXT("PlayFabEnvironment") };
	auto IsReserved = [](const FString& Key)
	{
		for (const TCHAR* const Name : Reserved)
		{
			if (Key.Equals(Name, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	UDataTable* const EventTable = LoadObject<UDataTable>(nullptr, AnalyticsTablePath);
	if (!TestNotNull(TEXT("DT_AnalyticsEventTable 로드"), EventTable))
	{
		return false;
	}

	int32 TableKeys = 0;
	for (const TPair<FString, TArray<FString>>& Event : ReadPayloadKeysByEventID(*EventTable))
	{
		for (const FString& Key : Event.Value)
		{
			++TableKeys;
			TestFalse(*FString::Printf(TEXT("[표 %s] 칸 %s 은 PlayFab 예약 이름이 아니다"), *Event.Key, *Key), IsReserved(Key));
		}
	}

	int32 CodeKeys = 0;
	for (const TPair<FString, FString>& Source : LoadSourceFiles(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"))))
	{
		for (const FString& Key : CollectPayloadKeys(Source.Value))
		{
			++CodeKeys;
			TestFalse(*FString::Printf(TEXT("[%s] 칸 %s 은 PlayFab 예약 이름이 아니다"),
				*FPaths::GetCleanFilename(Source.Key), *Key), IsReserved(Key));
		}
	}

	// 본 칸이 0 이면 빈 검사다 (Verification.md).
	TestTrue(TEXT("표와 코드에서 칸을 실제로 봤다"), TableKeys > 0 && CodeKeys > 0);
	AddInfo(FString::Printf(TEXT("표 칸 %d개 · 코드 칸 %d개를 예약 이름 %d개와 맞대 봤다"), TableKeys, CodeKeys, UE_ARRAY_COUNT(Reserved)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
