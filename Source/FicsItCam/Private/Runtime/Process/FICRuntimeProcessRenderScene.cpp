#include "Runtime/Process/FICRuntimeProcessRenderScene.h"

#include "AkAudioModule.h"
#include "AudioDevice.h"
#include "CanvasTypes.h"
#include "EngineModule.h"
#include "FicsItCamModule.h"
#include "FICSubsystem.h"
#include "IImageWrapperModule.h"
#include "PlatformFileManager.h"
#include "Algo/Accumulate.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Editor/FICEditorSubsystem.h"
#include "GameFramework/WorldSettings.h"
#include "Widgets/SViewport.h"
#include "Widgets/Notifications/SProgressBar.h"

UE_DISABLE_OPTIMIZATION_SHIP
static void CaptureCallback(AkAudioBuffer& in_CaptureBuffer, AkOutputDeviceID /*in_idOutput*/, void* in_pCookie) {
	auto self = reinterpret_cast<UFICRuntimeProcessRenderScene*>(in_pCookie);

	if (!self->bStarted) return;

	size_t uSampleCount = static_cast<size_t>(in_CaptureBuffer.uValidFrames);

	if (!uSampleCount || !in_CaptureBuffer.GetInterleavedData())
		return;

	StaticCastSharedPtr<FSequenceMP4Exporter>(self->Exporter)->AddAudioFrame((float*)in_CaptureBuffer.GetInterleavedData(), in_CaptureBuffer.uValidFrames);
}
UE_ENABLE_OPTIMIZATION_SHIP

UFICRuntimeProcessRenderScene::~UFICRuntimeProcessRenderScene() {
	if (bStarted) Stop(nullptr);
}

void UFICRuntimeProcessRenderScene::Start(AFICRuntimeProcessorCharacter* InCharacter) {
	Super::Start(InCharacter);

	WwiseSoundEngineAPI = IWwiseSoundEngineAPI::Get();

	auto* Settings = GetWorld()->GetWorldSettings();
	PrevMinUndilatedFrameTime = Settings->MinUndilatedFrameTime;
	PrevMaxUndilatedFrameTime = Settings->MaxUndilatedFrameTime;
	if (Scene->bBulletTime) {
		Settings->MinUndilatedFrameTime = 0;
		Settings->MaxUndilatedFrameTime = 0;
	} else {
		Settings->MinUndilatedFrameTime = 1.0/(double)Scene->FPS;
		Settings->MaxUndilatedFrameTime = Settings->MinUndilatedFrameTime;
	}
	FrameProgress = Scene->AnimationRange.Begin;

	AudioSampleRate = WwiseSoundEngineAPI->GetSampleRate();

	// Audio Capture
	{
		//FAudioDeviceManager::Get()->GetActiveAudioDevice().GetAudioDevice()->) // TODO: Audio Capture?
		FAudioThread::StopAudioThread();

		AKRESULT ret = WwiseSoundEngineAPI->SetOfflineRendering(true);
		if (ret != AK_Success) {
			UE_LOG(LogFicsItCam, Warning, TEXT("Unable to set offline rendering: %i"), ret);
		}

		// RenderAudio() wont generate more audio samples
		WwiseSoundEngineAPI->SetOfflineRenderingFrameTime(0.0f);

		// Flush both messages from above
		WwiseSoundEngineAPI->RenderAudio();

		/*AkChannelConfig channelConfig;
		channelConfig.SetStandard(AK_SPEAKER_SETUP_STEREO);

		AkOutputSettings newSettings;
		newSettings.audioDeviceShareset = AK_INVALID_UNIQUE_ID;
		newSettings.idDevice = 0;
		newSettings.ePanningRule = AkPanningRule_Speakers;
		newSettings.channelConfig = channelConfig;

		WwiseSoundEngineAPI->ReplaceOutput(newSettings, 0, &m_defaultOutputDeviceId);*/
		WwiseSoundEngineAPI->RenderAudio(true);

		ret = WwiseSoundEngineAPI->RegisterCaptureCallback(&CaptureCallback, 0, this);
		if (ret != AK_Success) {
			UE_LOG(LogFicsItCam, Warning, TEXT("Unable to register capture callback: %i"), ret);
		}
	}

	FViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	DummyViewport = MakeShared<FFICRendererViewport>(ViewportClient, Scene->ResolutionWidth, Scene->ResolutionHeight);

	// Create Save Path
	FString FSP;
	// TODO: Get UFGSaveSystem::GetSaveDirectoryPath() working
	if (FSP.IsEmpty()) {
		FSP = FPaths::Combine(FPlatformProcess::UserSettingsDir(), FApp::GetProjectName(), TEXT("Saved/") TEXT("SaveGames/") TEXT("FicsItCam/"), Scene->SceneName);
	}
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*FSP)) PlatformFile.CreateDirectoryTree(*FSP);
	
	Path = FPaths::Combine(FSP, FDateTime::Now().ToString() + TEXT(".mp4"));

#if PLATFORM_WINDOWS
	Exporter = MakeShared<FSequenceMP4Exporter>(FIntPoint(Scene->ResolutionWidth, Scene->ResolutionHeight), Scene->FPS, Path, AudioSampleRate);
#else
	Exporter = MakeShared<FSequenceImageExporter>(Path, FIntPoint(Scene->ResolutionWidth, Scene->ResolutionHeight));
#endif
	Exporter->Init();

	GEngine->GameViewport->AddViewportWidgetContent(
		SAssignNew(Overlay, SVerticalBox)
		+SVerticalBox::Slot()
		.VAlign(VAlign_Bottom)
		.HAlign(HAlign_Fill)[
			SNew(SOverlay)
			+SOverlay::Slot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)[
				SNew(SProgressBar)
				.Percent_Lambda([this]() {
					return (float)(FrameProgress - Scene->AnimationRange.Begin) / (float)Scene->AnimationRange.Length();
				})
			]
			+SOverlay::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)[
				SNew(STextBlock)
				.Text_Lambda([this]() {
					int64 CurrentFrame = FrameProgress - Scene->AnimationRange.Begin;
					int64 FrameCount = Scene->AnimationRange.Length();
					float Percent = (float)CurrentFrame / (float)FrameCount;
					float ETASec = (float)(FrameCount - CurrentFrame) * (Algo::Accumulate(ETAStatistics, 0.0f)/ETAStatistics.Num());
					FString ETA = UFGBlueprintFunctionLibrary::SecondsToTimeString(ETASec);
					return FText::FromString(FString::Printf(TEXT("%.1f%% [%lld/%lld] - ETA: %s"), Percent*100, CurrentFrame, FrameCount, *ETA));
				})
			]
		]
	);

	bStarted = true;
}

void UFICRuntimeProcessRenderScene::Tick(AFICRuntimeProcessorCharacter* InCharacter, float DeltaSeconds) {
	if(GetWorld()->IsLevelStreamingRequestPending(GetWorld()->GetFirstPlayerController())) return;

	ETAStatistics.Insert(GetWorld()->DeltaRealTimeSeconds, 0);
	while (ETAStatistics.Num() > 60) ETAStatistics.Pop();

	Progress = (float)FrameProgress / (float)Scene->FPS;
	Super::Tick(InCharacter, DeltaSeconds);

	// Capture Image
	FlushRenderingCommands();
	
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	FCanvas Canvas(DummyViewport.Get(), NULL, ViewportClient->GetWorld(), ViewportClient->GetWorld()->GetFeatureLevel());
	ViewportClient->Draw(DummyViewport.Get(), &Canvas);
	Canvas.Flush_GameThread();

	// Store Image
	AFICSubsystem::GetFICSubsystem(this)->ExportRenderTarget(Exporter.ToSharedRef(), DummyViewport.ToSharedRef());

	FlushRenderingCommands();

	WwiseSoundEngineAPI->SetOfflineRenderingFrameTime(1.0 / Scene->FPS);
	//WwiseSoundEngineAPI->RenderAudio();
	
	++FrameProgress;
}

void UFICRuntimeProcessRenderScene::Stop(AFICRuntimeProcessorCharacter* InCharacter) {
	Super::Stop(InCharacter);

	if (!bStarted) return;

	WwiseSoundEngineAPI->UnregisterCaptureCallback(&CaptureCallback, 0, this);
	WwiseSoundEngineAPI->SetOfflineRendering(false);
	WwiseSoundEngineAPI->RenderAudio();

	if (Overlay) GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());

	Exporter->Finish();

	FAudioThread::StartAudioThread();
	
	auto* Settings = GetWorld()->GetWorldSettings();
	Settings->MinUndilatedFrameTime = PrevMinUndilatedFrameTime;
	Settings->MaxUndilatedFrameTime = PrevMaxUndilatedFrameTime;

	bStarted = false;
}

UFICRuntimeProcessRenderScene* UFICRuntimeProcessRenderScene::StartRenderScene(AFICScene* InScene) {
	AFICEditorSubsystem* EditSubSys = AFICEditorSubsystem::GetFICEditorSubsystem(InScene);
	if (InScene->IsSceneAlreadyInUse()) return nullptr;
	AFICSubsystem* SubSys = AFICSubsystem::GetFICSubsystem(InScene);
	UFICRuntimeProcessRenderScene* Process = NewObject<UFICRuntimeProcessRenderScene>(SubSys);
	Process->Scene = InScene;
	if (SubSys->CreateRuntimeProcess(AFICScene::GetSceneProcessKey(InScene->SceneName), Process, true)) {
		return Process;
	} else {
		return nullptr;
	}
}
