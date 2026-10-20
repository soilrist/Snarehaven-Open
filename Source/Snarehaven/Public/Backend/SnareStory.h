#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// 메인 스토리(`docs/STORY.md`, 5장 25단계)를 화면에 옮기는 순수 함수 — 서버 `GetStory` 답을 읽고
// 왼쪽 위 「지금 할 일」 한 줄을 만든다. 판정은 서버(CloudScript `AdvanceStory`)이고 여기는 읽기만 한다 (CLAUDE.md 8).
namespace SnareStory
{
	/** 서버 `STORY_STEPS` 와 같은 단계 수. */
	constexpr int32 TotalSteps = 25;
	constexpr int32 StepsPerChapter = 5;

	/** `GetStory` 답에서 화면에 필요한 칸만 뽑은 것. */
	struct FStoryView
	{
		int32 Step = 0;			// 끝낸 단계 수 (0~25)
		bool bDone = false;
		int32 Chapter = 1;		// 1~5
		int32 CurrentStep = 0;	// 지금 단계 (1~25), 끝났으면 0
		FString Kind;			// give / count / ranch / report
		int32 Have = 0;
		int32 Need = 0;
		TArray<TPair<FName, int32>> Needs;	// give 단계의 건넬 재료
		TArray<FString> Titles;
	};

	/** `GetStory` 답을 읽는다. 오류 답·`step` 없음이면 false (화면은 줄을 접는다). */
	SNAREHAVEN_API bool ParseView(const TSharedPtr<FJsonObject>& Response, FStoryView& Out);

	/** 의뢰인 머리 위 표식 — 없음 / 「!」 받을 것(건넬 재료가 아직 모자람) / 「?」 보고할 것(말 걸기·재료를 다 모음). */
	enum class EGiverMark : uint8 { None, Bang, Question };

	/** 지금 단계가 의뢰인 솔방울과 이어진 단계인가에 따라 표식을 고른다. 세기·목장 단계와 끝난 계정은 없음. */
	SNAREHAVEN_API EGiverMark GiverMark(const FStoryView& View);

	/** 스토리에 말을 거는 사람 — 표식(「!」「?」)이 붙을 NPC. 갈고리는 월드에 아직 없다(⑤). */
	enum class EStoryNpc : uint8 { None, Solbangul, Mollae, Hook };

	/** 단계(1~25)의 대사·건넬 상대. 범위 밖이면 None. 대사 말하는 사람과 같다 (`StepDialogue`). */
	SNAREHAVEN_API EStoryNpc TargetNpc(int32 Step);

	/** 지금 단계의 표식 상대 — 캐릭터가 단계가 바뀔 때 적고 미니맵·월드 지도가 읽는다(없음이면 지도 표식 접힘). 화면 상태일 뿐 판정이 아니다. */
	SNAREHAVEN_API void SetCurrentTargetNpc(EStoryNpc Npc);
	SNAREHAVEN_API EStoryNpc GetCurrentTargetNpc();

	/** 세기·목장 단계(서버가 숫자로 판정)가 이미 채워졌나 — 그러면 클라가 `AdvanceStory` 를 한 번 부른다. 판정은 서버가 다시 한다. */
	SNAREHAVEN_API bool ShouldAutoAdvance(const FStoryView& View);

	/** 보고·가기 단계(`report`)인가 — 대사를 다 읽으면 클라가 `AdvanceStory` 에 `report: true` 로 알린다. 판정은 서버가 다시 한다. */
	SNAREHAVEN_API bool ShouldReport(const FStoryView& View);

	/** 건네기 단계(`give`)인가 — 대사를 다 읽으면 클라가 `AdvanceStory` 를 `report` 없이 부른다(서버가 재료를 가져가고 판정). */
	SNAREHAVEN_API bool ShouldGive(const FStoryView& View);

	/** 표식 글자 — 없음은 빈 글. */
	SNAREHAVEN_API FText GiverMarkText(EGiverMark Mark);

	/** 장 제목 (1~5), 범위 밖이면 빈 글. */
	SNAREHAVEN_API FText ChapterTitle(int32 Chapter);

	/** 끝낸 단계 수로 본 마지막으로 끝낸 장 (0~5). 장 끝 카드를 띄울지 가리는 데 쓴다. */
	SNAREHAVEN_API int32 CompletedChapter(const FStoryView& View);

	/** 등대 불빛 고리 켜진 수 (0~5) — 끝낸 장 수 그대로. 새 재화가 아니라 장 진행을 그림으로 보일 뿐 (STORY.md 5절). */
	SNAREHAVEN_API int32 LighthouseRingsLit(const FStoryView& View);

	/** 장 끝 보상 코인 — 서버 `STORY_CHAPTER_COINS` 와 같은 표 (STORY.md 5절). 범위 밖이면 0. 화면 글일 뿐 지급은 서버가 한다. */
	SNAREHAVEN_API int32 ChapterCoins(int32 Chapter);

	/** 장 끝 꾸미기 이름 (STORY.md 5절). 범위 밖이면 빈 글. */
	SNAREHAVEN_API FText ChapterRewardItemName(int32 Chapter);

	/** 단계(1~25)의 목표 글 — 재료 건네기 단계는 재료 줄을 `{0}` 자리에 끼운다. 범위 밖이면 빈 글. */
	SNAREHAVEN_API FText StepGoal(int32 Step);

	/** 대사 판 한 줄 — 말하는 사람과 말. */
	struct FDialogueLine
	{
		FText Speaker;
		FText Text;
	};

	/** 단계(1~25)를 받을 때 대사 판에 띄울 대사 1~3줄 (`docs/STORY.md` 4절). 범위 밖이면 빈 배열. */
	SNAREHAVEN_API TArray<FDialogueLine> StepDialogue(int32 Step);

	/** 가기 단계(14·16)에서 목표 지점에 닿았을 때의 발견 대사. `StepDialogue` 는 단계 시작 때 「가 보게」로 쓴다. 다른 단계는 빈 배열. */
	SNAREHAVEN_API TArray<FDialogueLine> ArrivalDialogue(int32 Step);

	/**
	 * 왼쪽 위 한 줄: `지금 할 일: 손도끼 만들기 (0/1) · 1장 2/5`. 개수가 1이면 괄호를 뺀다.
	 * 스토리를 다 끝냈으면 `올가미지기가 되었다 — 등대가 켜졌다`. 답 전(View 없음)은 호출하지 않는다.
	 */
	SNAREHAVEN_API FText FormatLine(const FStoryView& View, TFunctionRef<FText(FName)> DisplayName);

	/** 안개뿔이 서 있어야 하는 단계인가 — 지금 단계가 22(등대 언덕 오르기)·23(안개뿔 포획)일 때만. 이미 잡았으면(24~) 안 선다. */
	SNAREHAVEN_API bool ShouldStandFogbuck(int32 CurrentStep);

	/** 가기·말 걸기 단계의 도착 조건 종류 — 대사 판·보고는 이 조건이 참일 때만 (결함 10-19 「안 가도 끝난다」). */
	enum class EArrival : uint8 { None, NearGiver, InRegion, AtLighthouse, AtCamp, CagesBroken, NearMollae, LightPressed, NearHook };

	struct FArrival
	{
		EArrival Kind = EArrival::None;
		float RadiusCm = 0.f;
		FName RegionId;	// InRegion 일 때 존 표 ZoneID
	};

	/** 단계(1~25)의 도착 조건. 월드에 목표가 아직 없는 단계(모래·덫)는 None — ⑤ 에서 채운다. */
	SNAREHAVEN_API FArrival ArrivalFor(int32 Step);

	/** 조건이 참인가. `GiverDistanceCm` 은 가장 가까운 의뢰인까지 (없으면 큰 수), `RegionId` 는 서 있는 존 (없으면 None). */
	SNAREHAVEN_API bool IsArrived(const FArrival& Arrival, float GiverDistanceCm, FName RegionId, float LighthouseDistanceCm, float CampDistanceCm = TNumericLimits<float>::Max(), bool bCagesBroken = false, float MollaeDistanceCm = TNumericLimits<float>::Max(), bool bLighthouseLit = false, float HookDistanceCm = TNumericLimits<float>::Max());

	/** 상황 버튼에 「불 켜기」 를 띄우나 — 지금 단계가 누르기 도착(`LightPressed`, 25단계)이고 아직 안 눌렀고 탑 반경(`FArrival::RadiusCm`) 안일 때만. */
	SNAREHAVEN_API bool CanPressLighthouseLight(int32 CurrentStep, float LighthouseDistanceCm, bool bAlreadyPressed);

	/** 이 클라(호스트)가 마지막으로 받은 지금 단계. 답을 받기 전엔 0. 스포너가 읽기만 하고, 판정은 서버가 한다. */
	SNAREHAVEN_API void SetKnownCurrentStep(int32 CurrentStep);
	SNAREHAVEN_API int32 GetKnownCurrentStep();

	/** 등대 불빛 빛줄기 한 바퀴 (초) — 명세 story-lighthouse-light.md 3절. */
	constexpr double LighthouseTurnSeconds = 20.0;
	/** 등대 빛줄기가 닿는 거리 (cm). */
	constexpr float LighthouseReachCm = 5000.f;
	/** 등대 탑을 세우는 자리 (설원 뭍 해안 25m 안쪽 — 칸 가운데 22400,-7400 은 바다라 #1283 에서 옮김). */
	constexpr double LighthouseSpotX = 19416.0;
	constexpr double LighthouseSpotY = -4736.0;

	/** 회색 작살단 캠프 자리 (늪 뭍 해안 25m 안쪽 — `islands.land_point(-22600,-25600,zone='swamp',margin=2500)`). 4장 1단계 「캠프 찾기」 목표. */
	constexpr double CampSpotX = -22098.0;
	constexpr double CampSpotY = -30374.0;

	/** 장 끝 연출 — 카메라가 등대를 한 번 비춘다 (STORY.md 4절). 다가가기·돌아오기 섞기와 머무는 시간 (초). */
	constexpr float LighthouseGlanceBlendSeconds = 1.0f;
	constexpr float LighthouseGlanceHoldSeconds = 2.5f;
	/** 비추는 카메라 자리 — 탑에서 플레이어 쪽으로 이만큼 떨어져, 탑 밑동 위 눈높이에서 탑 중간 높이를 본다 (cm).
	 *  게임 카메라 화각 30도 그대로 약 40도 내려다본다 — 수평으로 보면 하늘이 검게 나오고(#1419 첫 캡처) 탑 키 1337 이 폰 화면 세로의 6할쯤 들어오는 거리. */
	constexpr float LighthouseGlanceDistanceCm = 5300.f;
	constexpr float LighthouseGlanceEyeHeightCm = 5100.f;
	constexpr float LighthouseGlanceLookHeightCm = 650.f;

	/** 장 끝 연출 카메라 자리·방향 — 탑 밑동에서 플레이어 쪽 수평 방향으로 띄워 탑을 본다. 둘이 같은 자리면 -X 쪽. */
	SNAREHAVEN_API FTransform LighthouseGlancePose(const FVector& TowerBase, const FVector& PlayerLocation);

	/** 장 끝(등대 비추기 → 카드)과 대사 판의 차례 — 한 화면에 하나씩 (감독 보정 10-20 17:15: 카드와 대사가 겹치면 한 번 눌러 무엇이 닫힐지 모른다).
	 *  열려 있는지는 위젯·카메라가 정답이라 부를 때 넘겨 받고, 이 틀은 미룬 것만 쥔다. 같은 순간 둘 다 오면 부르는 쪽이 대사부터 청한다 → 대사를 다 닫은 뒤 등대 → 카드. 장 1~5 같은 길. */
	struct SNAREHAVEN_API FPopupQueue
	{
		/** 대사가 열려 있어 미룬 장 끝 (0 = 없음). */
		int32 WaitingChapter = 0;
		bool bWaitingChapterPersist = true;
		/** 장 끝이 열려 있어 미룬 대사가 있나. */
		bool bDialogueWaiting = false;

		/** 장 끝을 청한다 — 지금 시작해도 되면 true. 대사가 열려 있으면 미뤄 두고 false, 장 끝이 이미 열려 있으면 버리고 false. */
		bool RequestChapterEnd(int32 Chapter, bool bPersist, bool bDialogueOpen, bool bChapterEndOpen);
		/** 대사 판을 청한다 — 지금 띄워도 되면 true. 장 끝이 열려 있으면 미뤄 두고 false. */
		bool RequestDialogue(bool bChapterEndOpen);
		/** 대사 판이 닫혔다 — 미룬 장 끝이 있으면 그 장(꺼냄), 없으면 0. */
		int32 TakeChapterAfterDialogue(bool& bOutPersist);
		/** 장 끝(카드)이 닫혔다 — 미룬 대사를 다시 볼지 (꺼냄). */
		bool TakeDialogueAfterChapterEnd();
	};

	/** 5장 끝 점화 — 등대 비추기 카메라가 탑에 닿은 뒤 불이 0 에서 다 켜질 때까지 (초). 낮이어도 연출 동안은 켠다. */
	constexpr float LighthouseIgnitionRampSeconds = 1.5f;

	/** 점화 연출 길이 (초) — 등대 비추기와 같다(가기 섞기 + 머물기 + 돌아오기 섞기). 이 뒤엔 밤 규칙(`LighthouseBrightness`)만. */
	constexpr float LighthouseIgnitionSeconds() { return LighthouseGlanceBlendSeconds * 2.f + LighthouseGlanceHoldSeconds; }

	/** 점화 밝기 0~1 — 시작 뒤 경과 초. 카메라가 가는 동안(섞기) 0, 닿으면 `LighthouseIgnitionRampSeconds` 에 걸쳐 1, 연출이 끝나면(또는 음수) 0. */
	SNAREHAVEN_API float LighthouseIgnitionBrightness(float SecondsSinceStart);

	/** 점화 동안 빛줄기에 더하는 돌림 (도) — 카메라가 머무는 동안 한 바퀴(0→360), 그 전 0·그 뒤 360. 끝에 360 = 0 이라 밤 규칙 방향으로 끊김 없이 이어진다. */
	SNAREHAVEN_API float LighthouseIgnitionExtraYaw(float SecondsSinceStart);

	/** 불빛 밝기 0~1 — 25단계를 끝낸 계정(bDone)만 밤(Night 0~1)에 비례해 켜고, 아니면 늘 0. */
	SNAREHAVEN_API float LighthouseBrightness(bool bDone, float Night);

	/** 빛줄기 방향(도, 0~360) — 20초에 한 바퀴. 음수 시각도 주기로 접는다. */
	SNAREHAVEN_API float LighthouseBeamYaw(double Seconds);

	/** 이 클라가 마지막으로 받은 「25단계 끝냈나」. 답 전엔 false. 등대 액터가 읽기만 한다. */
	SNAREHAVEN_API void SetKnownDone(bool bDone);
	SNAREHAVEN_API bool GetKnownDone();
}
