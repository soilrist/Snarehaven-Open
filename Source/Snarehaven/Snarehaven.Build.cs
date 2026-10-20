using UnrealBuildTool;

public class Snarehaven : ModuleRules
{
	public Snarehaven(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 유니티 빌드(여러 .cpp 를 한 덩어리로 묶어 컴파일)를 끈다.
		// 2층 검사 파일들은 저마다 익명 네임스페이스에 같은 이름의 도우미(CreateTestWorld·TestMaxHP …)를
		// 두는데, 묶이면 한 번역 단위가 돼서 "redefinition" 으로 깨진다. 게다가 어떤 파일이 묶이는지는
		// **커밋 안 된 파일 목록**에 따라 달라져서(Adaptive Unity), 같은 코드가 어제는 되고 오늘은 안 됐다
		// (루프#25 에서 실제로 그랬다 — logs/build_editor_loop25.log). 모듈이 20여 파일이라 끄는 비용은 몇 초다.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"HTTP", "Json", "JsonUtilities",
			// AssetRegistry·EngineSettings: M0-05 검사(데이터 테이블 훑기·기본 맵/게임모드 확인)에서 쓴다.
			"AssetRegistry", "EngineSettings",
			// UMG·Slate: M1-08 사냥 HUD(UUserWidget 파생 + 위젯 트리를 C++ 이 만든다).
			"UMG", "Slate", "SlateCore",
			// M3-01a EOS 로그인 · M3-01d 에서 OSSv1(OnlineSubsystem·OnlineSubsystemUtils)을 뺐다 — 결정 004.
			// EOSShared 는 EOSSDK 를 public 으로 물고 있어서 eos_connect.h 가 같이 따라온다.
			"OnlineServicesInterface", "CoreOnline", "EOSShared", "EOSSDK"
		});

		// M2-01 백엔드(PlayFab). **Private 인 이유:** PlayFab 타입은 `USnareBackendSubsystem` 의 .cpp 안에만
		// 나오고 헤더에는 새어 나가지 않는다 — 나중에 백엔드를 갈아도 우리 헤더를 쓰는 쪽은 안 고친다.
		PrivateDependencyModuleNames.AddRange(new string[] {
			"PlayFabCpp", "PlayFabCommon",
			// ApplicationCore: M4-04 의 3층 검사 스위치(`-SnareTouch=`)가 터치 이벤트를 넣을 때
			// 엔진 가짜 터치와 **같은 장치 번호**를 쓰려고 `IPlatformInputDeviceMapper` 를 부른다.
			// Private 인 이유 — 우리 헤더에는 안 나오고 `.cpp` 한 군데서만 쓴다.
			"ApplicationCore",
			// RHI: M17-04 성능 로그(`-SnarePerfLog`)가 드로콜 수(`GNumDrawCallsRHI`)를 읽는다.
			"RHI",
			// OnlineSubsystemUtils·Sockets: 앱 복귀 때 리슨 서버 소켓(`UIpNetDriver::GetSocket`)이 죽었나 엿본다
			// (INBOX 18:20 폰 `SE_ENOTCONN`). OSSv1 로그인을 다시 쓰는 게 아니라 넷 드라이버 클래스만 본다 — 결정 004 그대로.
			"OnlineSubsystemUtils", "Sockets"
		});
	}
}
