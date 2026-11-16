// Copyright 2024 PICO Inc. All Rights Reserved.

#include "PICO_CameraPreviewActor.h"

#include "AndroidPermissionCallbackProxy.h"
#include "AndroidPermissionFunctionLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LatentActions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPICOCameraPreview, Log, All);

namespace
{
	constexpr const TCHAR* CameraPermission = TEXT("android.permission.CAMERA");

	FString JoinCameraIDs(const TArray<int64>& CameraIDs)
	{
		TArray<FString> Strings;
		Strings.Reserve(CameraIDs.Num());
		for (const int64 CameraID : CameraIDs)
		{
			Strings.Add(FString::Printf(TEXT("%lld"), CameraID));
		}
		return FString::Join(Strings, TEXT(", "));
	}

	FString JoinResolutions(const TArray<FIntPoint>& Resolutions)
	{
		TArray<FString> Strings;
		Strings.Reserve(Resolutions.Num());
		for (const FIntPoint& Resolution : Resolutions)
		{
			Strings.Add(FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}
		return FString::Join(Strings, TEXT(", "));
	}

	template <typename TEnum>
	FString JoinEnumValues(const TArray<TEnum>& Values)
	{
		TArray<FString> Strings;
		Strings.Reserve(Values.Num());
		for (const TEnum Value : Values)
		{
			Strings.Add(FString::FromInt(static_cast<int32>(Value)));
		}
		return FString::Join(Strings, TEXT(", "));
	}

	bool ContainsResolution(const TArray<FIntPoint>& Resolutions, const FIntPoint& Desired)
	{
		return Resolutions.ContainsByPredicate([Desired](const FIntPoint& Item)
		{
			return Item.X == Desired.X && Item.Y == Desired.Y;
		});
	}

	template <typename TEnum>
	TEnum PickPreferredOrFirst(const TArray<TEnum>& Values, TEnum Preferred)
	{
		return Values.Contains(Preferred) ? Preferred : (Values.Num() > 0 ? Values[0] : Preferred);
	}
}

APICOCameraPreviewActor::APICOCameraPreviewActor()
{
	PrimaryActorTick.bCanEverTick = true;

	PreviewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	RootComponent = PreviewRoot;

	DisplayPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DisplayPlane"));
	DisplayPlane->SetupAttachment(PreviewRoot);
	DisplayPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DisplayPlane->SetRelativeScale3D(FVector(1.0, 1.0, 1.0));
	DisplayPlane->SetRelativeRotation(FRotator(0.0, 90.0, 90.0));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		DisplayPlane->SetStaticMesh(PlaneMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PreviewMaterial(TEXT("/PICOOpenXR/Materials/M_PICOCameraPreview.M_PICOCameraPreview"));
	if (PreviewMaterial.Succeeded())
	{
		DisplayMaterial = PreviewMaterial.Object;
	}
}

void APICOCameraPreviewActor::BeginPlay()
{
	Super::BeginPlay();

	LogDebugStatus(FString::Printf(TEXT("BeginPlay: AutoStart=%s UseFirstAvailableCamera=%s CameraID=%lld PreferredResolution=%dx%d PreferredFPS=%d Material=%s TextureParam=%s"),
		bAutoStart ? TEXT("true") : TEXT("false"),
		bUseFirstAvailableCamera ? TEXT("true") : TEXT("false"),
		CameraID,
		PreferredResolution.X,
		PreferredResolution.Y,
		static_cast<int32>(PreferredFPS),
		DisplayMaterial ? *DisplayMaterial->GetPathName() : TEXT("<null>"),
		*TextureParameterName.ToString()));

	if (!bAutoStart)
	{
		SetStatus(TEXT("PICO Camera preview: auto start disabled"));
		return;
	}

#if PLATFORM_ANDROID
	const bool bHasCameraPermission = UAndroidPermissionFunctionLibrary::CheckPermission(CameraPermission);
	LogDebugStatus(FString::Printf(TEXT("BeginPlay: CheckPermission(%s)=%s"), CameraPermission, bHasCameraPermission ? TEXT("true") : TEXT("false")));
	if (!bHasCameraPermission)
	{
		PermissionProxy = UAndroidPermissionFunctionLibrary::AcquirePermissions({ CameraPermission });
		if (PermissionProxy)
		{
			PermissionProxy->OnPermissionsGrantedDynamicDelegate.AddDynamic(this, &APICOCameraPreviewActor::OnCameraPermissionResult);
			SetStatus(TEXT("PICO Camera preview: requesting CAMERA permission"));
			return;
		}

		SetStatus(TEXT("PICO Camera preview: failed to request CAMERA permission"));
		return;
	}
#endif

	StartAfterPermission();
}

void APICOCameraPreviewActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bCaptureRunning || !CameraRenderTarget)
	{
		return;
	}

	SecondsSinceNoImageLog += DeltaSeconds;
	FPICOCameraImageData ImageData;
	if (UPICOCameraFunctionLibrary::AcquireCameraImage(SessionHandle, LastCaptureTime, ImageData))
	{
		LastCaptureTime = ImageData.CaptureTime;
		++FramesAcquired;
		ConsecutiveNoImageFrames = 0;
		SecondsSinceNoImageLog = 0.0f;

		const bool bUpdatedRenderTarget = UPICOCameraFunctionLibrary::UpdateRenderTargetWithImageData(CameraRenderTarget, ImageData);
		if (!bUpdatedRenderTarget)
		{
			++RenderTargetUpdateFailures;
			UE_LOG(LogPICOCameraPreview, Warning, TEXT("UpdateRenderTargetWithImageData failed. Failures=%lld Image=%dx%d Stride=%d BPP=%d BufferBytes=%d CaptureTime=%s RenderTarget=%s"),
				RenderTargetUpdateFailures,
				ImageData.Width,
				ImageData.Height,
				ImageData.Stride,
				ImageData.BytesPerPixel,
				ImageData.Buffer.Num(),
				*ImageData.CaptureTime.ToString(),
				*GetNameSafe(CameraRenderTarget));
		}
		else if (FramesAcquired == 1 || FramesAcquired % 60 == 0)
		{
			UE_LOG(LogPICOCameraPreview, Log, TEXT("Frame acquired. Frames=%lld Image=%dx%d Stride=%d BPP=%d BufferBytes=%d CaptureTime=%s RT=%dx%d"),
				FramesAcquired,
				ImageData.Width,
				ImageData.Height,
				ImageData.Stride,
				ImageData.BytesPerPixel,
				ImageData.Buffer.Num(),
				*ImageData.CaptureTime.ToString(),
				CameraRenderTarget->SizeX,
				CameraRenderTarget->SizeY);
		}
	}
	else
	{
		++ConsecutiveNoImageFrames;
		if (SecondsSinceNoImageLog >= 2.0f)
		{
			UE_LOG(LogPICOCameraPreview, Warning, TEXT("AcquireCameraImage returned false for %.1fs. ConsecutiveNoImageFrames=%d SessionHandle=%lld LastCaptureTime=%s"),
				SecondsSinceNoImageLog,
				ConsecutiveNoImageFrames,
				SessionHandle.Handle,
				*LastCaptureTime.ToString());
			SecondsSinceNoImageLog = 0.0f;
		}
	}
}

void APICOCameraPreviewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownCamera();
	Super::EndPlay(EndPlayReason);
}

void APICOCameraPreviewActor::OnCameraPermissionResult(const TArray<FString>& Permissions, const TArray<bool>& GrantResults)
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("Permission callback received. Permissions=%d GrantResults=%d"), Permissions.Num(), GrantResults.Num());
	bool bGranted = false;
	for (int32 Index = 0; Index < Permissions.Num(); ++Index)
	{
		UE_LOG(LogPICOCameraPreview, Log, TEXT("Permission result [%d]: %s=%s"),
			Index,
			*Permissions[Index],
			GrantResults.IsValidIndex(Index) && GrantResults[Index] ? TEXT("granted") : TEXT("denied"));

		if (Permissions[Index] == CameraPermission && GrantResults.IsValidIndex(Index))
		{
			bGranted = GrantResults[Index];
			break;
		}
	}

#if PLATFORM_ANDROID
	bGranted = bGranted || UAndroidPermissionFunctionLibrary::CheckPermission(CameraPermission);
	UE_LOG(LogPICOCameraPreview, Log, TEXT("Permission callback final CheckPermission(%s)=%s"), CameraPermission, bGranted ? TEXT("true") : TEXT("false"));
#endif

	if (bGranted)
	{
		UWorld* World = GetWorld();
		if (World && PermissionGrantedStartDelaySeconds > 0.0f)
		{
			World->GetTimerManager().SetTimer(
				PermissionGrantedStartDelayTimerHandle,
				this,
				&APICOCameraPreviewActor::StartAfterPermissionDelayElapsed,
				PermissionGrantedStartDelaySeconds,
				false);
			SetStatus(FString::Printf(TEXT("PICO Camera preview: CAMERA permission granted, starting in %.1fs"), PermissionGrantedStartDelaySeconds));
		}
		else
		{
			StartAfterPermission();
		}
	}
	else
	{
		SetStatus(TEXT("PICO Camera preview: CAMERA permission denied"));
	}
}

void APICOCameraPreviewActor::StartAfterPermissionDelayElapsed()
{
	PermissionGrantedStartDelayTimerHandle.Invalidate();
	LogDebugStatus(TEXT("StartAfterPermissionDelayElapsed: permission callback delay finished"));
	StartAfterPermission();
}

void APICOCameraPreviewActor::StartAfterPermission()
{
	LogDebugStatus(TEXT("StartAfterPermission: checking camera image extension support"));

	const bool bCameraImageSupported = UPICOCameraFunctionLibrary::IsCameraImageSupported();
	UE_LOG(LogPICOCameraPreview, Log, TEXT("IsCameraImageSupported=%s"), bCameraImageSupported ? TEXT("true") : TEXT("false"));
	if (!bCameraImageSupported)
	{
		SetStatus(TEXT("PICO Camera preview: camera image extension is not supported"));
		return;
	}

	if (!SelectCameraAndConfig(CaptureConfig))
	{
		return;
	}

	CreatePreviewResources(CaptureConfig.Resolution);
	UE_LOG(LogPICOCameraPreview, Log, TEXT("CreateCameraDeviceAsync request. CameraID=%lld"), CameraID);

	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	LatentInfo.ExecutionFunction = GET_FUNCTION_NAME_CHECKED(APICOCameraPreviewActor, OnCameraDeviceCreated);
	LatentInfo.Linkage = 0;
	LatentInfo.UUID = 1001;

	UPICOCameraFunctionLibrary::CreateCameraDeviceAsync(this, CameraID, LatentInfo, bDeviceCreateSuccess, DeviceHandle);
	SetStatus(FString::Printf(TEXT("PICO Camera preview: creating device for camera %lld"), CameraID));
}

void APICOCameraPreviewActor::OnCameraDeviceCreated()
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("CreateCameraDeviceAsync completed. Success=%s DeviceHandle=%lld"),
		bDeviceCreateSuccess ? TEXT("true") : TEXT("false"),
		DeviceHandle.Handle);

	if (!bDeviceCreateSuccess || !DeviceHandle.IsValid())
	{
		SetStatus(TEXT("PICO Camera preview: CreateCameraDeviceAsync failed"));
		return;
	}

	FLatentActionInfo LatentInfo;
	LatentInfo.CallbackTarget = this;
	LatentInfo.ExecutionFunction = GET_FUNCTION_NAME_CHECKED(APICOCameraPreviewActor, OnCaptureSessionCreated);
	LatentInfo.Linkage = 0;
	LatentInfo.UUID = 1002;

	UPICOCameraFunctionLibrary::CreateCaptureSessionAsync(this, DeviceHandle, CaptureConfig, LatentInfo, bSessionCreateSuccess, SessionHandle);
	SetStatus(FString::Printf(TEXT("PICO Camera preview: creating session %dx%d format=%d transfer=%d model=%d fps=%d"),
		CaptureConfig.Resolution.X,
		CaptureConfig.Resolution.Y,
		static_cast<int32>(CaptureConfig.ImageFormat),
		static_cast<int32>(CaptureConfig.TransferType),
		static_cast<int32>(CaptureConfig.CameraModel),
		static_cast<int32>(CaptureConfig.FPS)));
}

void APICOCameraPreviewActor::OnCaptureSessionCreated()
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("CreateCaptureSessionAsync completed. Success=%s SessionHandle=%lld"),
		bSessionCreateSuccess ? TEXT("true") : TEXT("false"),
		SessionHandle.Handle);

	if (!bSessionCreateSuccess || !SessionHandle.IsValid())
	{
		SetStatus(TEXT("PICO Camera preview: CreateCaptureSessionAsync failed"));
		ShutdownCamera();
		return;
	}

	bCaptureRunning = UPICOCameraFunctionLibrary::BeginCameraCapture(SessionHandle);
	LastCaptureTime = FTimespan::Zero();
	FramesAcquired = 0;
	RenderTargetUpdateFailures = 0;
	ConsecutiveNoImageFrames = 0;
	SecondsSinceNoImageLog = 0.0f;
	UE_LOG(LogPICOCameraPreview, Log, TEXT("BeginCameraCapture returned %s for SessionHandle=%lld"),
		bCaptureRunning ? TEXT("true") : TEXT("false"),
		SessionHandle.Handle);
	SetStatus(bCaptureRunning ? TEXT("PICO Camera preview: capture started") : TEXT("PICO Camera preview: BeginCameraCapture failed"));
}

bool APICOCameraPreviewActor::SelectCameraAndConfig(FPICOCameraCaptureConfig& OutConfig)
{
	LogDebugStatus(TEXT("SelectCameraAndConfig: begin"));

	if (bUseFirstAvailableCamera)
	{
		FPICOCameraPropertyFilter Filter;
		TArray<int64> CameraIDs;
		const bool bEnumerated = UPICOCameraFunctionLibrary::EnumerateAvailableCameras(Filter, CameraIDs);
		UE_LOG(LogPICOCameraPreview, Log, TEXT("EnumerateAvailableCameras returned %s. Count=%d IDs=[%s]"),
			bEnumerated ? TEXT("true") : TEXT("false"),
			CameraIDs.Num(),
			*JoinCameraIDs(CameraIDs));
		if (!bEnumerated || CameraIDs.Num() == 0)
		{
			SetStatus(TEXT("PICO Camera preview: no available cameras"));
			return false;
		}

		CameraID = CameraIDs[0];
	}

	EPICOCameraFacing Facing = EPICOCameraFacing::None;
	EPICOCameraPosition Position = EPICOCameraPosition::None;
	EPICOCameraType CameraType = EPICOCameraType::None;
	bool bSupportsFacing = false;
	bool bSupportsPosition = false;
	bool bSupportsCameraType = false;
	const bool bGotProperties = UPICOCameraFunctionLibrary::GetCameraProperties(CameraID, Facing, Position, CameraType, bSupportsFacing, bSupportsPosition, bSupportsCameraType);
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetCameraProperties(CameraID=%lld)=%s Facing=%d SupportsFacing=%s Position=%d SupportsPosition=%s Type=%d SupportsType=%s"),
		CameraID,
		bGotProperties ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Facing),
		bSupportsFacing ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Position),
		bSupportsPosition ? TEXT("true") : TEXT("false"),
		static_cast<int32>(CameraType),
		bSupportsCameraType ? TEXT("true") : TEXT("false"));

	TArray<EPICOCameraCapabilityType> CapabilityTypes;
	const bool bGotCapabilities = UPICOCameraFunctionLibrary::GetSupportedCapabilityTypes(CameraID, CapabilityTypes);
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedCapabilityTypes=%s Count=%d Values=[%s]"),
		bGotCapabilities ? TEXT("true") : TEXT("false"),
		CapabilityTypes.Num(),
		*JoinEnumValues(CapabilityTypes));

	FPICOCameraSupportedResolutions SupportedResolutions;
	const bool bGotResolutions = UPICOCameraFunctionLibrary::GetSupportedResolutions(CameraID, SupportedResolutions);
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedResolutions=%s Count=%d Values=[%s] Preferred=%dx%d"),
		bGotResolutions ? TEXT("true") : TEXT("false"),
		SupportedResolutions.Resolutions.Num(),
		*JoinResolutions(SupportedResolutions.Resolutions),
		PreferredResolution.X,
		PreferredResolution.Y);
	if (!bGotResolutions || SupportedResolutions.Resolutions.Num() == 0)
	{
		SetStatus(TEXT("PICO Camera preview: no supported resolutions"));
		return false;
	}

	OutConfig.Resolution = ContainsResolution(SupportedResolutions.Resolutions, PreferredResolution)
		? PreferredResolution
		: SupportedResolutions.Resolutions[0];

	FPICOCameraSupportedFormats SupportedFormats;
	if (UPICOCameraFunctionLibrary::GetSupportedImageFormats(CameraID, SupportedFormats) && SupportedFormats.Formats.Num() > 0)
	{
		OutConfig.ImageFormat = PickPreferredOrFirst(SupportedFormats.Formats, EPICOCameraImageFormat::RGBA_8888);
	}
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedImageFormats Count=%d Values=[%s] Selected=%d"),
		SupportedFormats.Formats.Num(),
		*JoinEnumValues(SupportedFormats.Formats),
		static_cast<int32>(OutConfig.ImageFormat));

	FPICOCameraSupportedTransferTypes SupportedTransferTypes;
	if (UPICOCameraFunctionLibrary::GetSupportedTransferTypes(CameraID, SupportedTransferTypes) && SupportedTransferTypes.TransferTypes.Num() > 0)
	{
		OutConfig.TransferType = PickPreferredOrFirst(SupportedTransferTypes.TransferTypes, EPICOCameraDataTransferType::RawBuffer);
	}
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedTransferTypes Count=%d Values=[%s] Selected=%d"),
		SupportedTransferTypes.TransferTypes.Num(),
		*JoinEnumValues(SupportedTransferTypes.TransferTypes),
		static_cast<int32>(OutConfig.TransferType));

	FPICOCameraSupportedModels SupportedModels;
	if (UPICOCameraFunctionLibrary::GetSupportedModels(CameraID, SupportedModels) && SupportedModels.Models.Num() > 0)
	{
		OutConfig.CameraModel = PickPreferredOrFirst(SupportedModels.Models, EPICOCameraModel::Pinhole);
	}
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedModels Count=%d Values=[%s] Selected=%d"),
		SupportedModels.Models.Num(),
		*JoinEnumValues(SupportedModels.Models),
		static_cast<int32>(OutConfig.CameraModel));

	FPICOCameraSupportedFPS SupportedFPS;
	if (UPICOCameraFunctionLibrary::GetSupportedFPS(CameraID, SupportedFPS) && SupportedFPS.FPSValues.Num() > 0)
	{
		OutConfig.FPS = PickPreferredOrFirst(SupportedFPS.FPSValues, PreferredFPS);
	}
	UE_LOG(LogPICOCameraPreview, Log, TEXT("GetSupportedFPS Count=%d Values=[%s] Preferred=%d Selected=%d"),
		SupportedFPS.FPSValues.Num(),
		*JoinEnumValues(SupportedFPS.FPSValues),
		static_cast<int32>(PreferredFPS),
		static_cast<int32>(OutConfig.FPS));

	UE_LOG(LogPICOCameraPreview, Log, TEXT("Selected config: CameraID=%lld Resolution=%dx%d Format=%d Transfer=%d Model=%d FPS=%d"),
		CameraID,
		OutConfig.Resolution.X,
		OutConfig.Resolution.Y,
		static_cast<int32>(OutConfig.ImageFormat),
		static_cast<int32>(OutConfig.TransferType),
		static_cast<int32>(OutConfig.CameraModel),
		static_cast<int32>(OutConfig.FPS));

	return true;
}

void APICOCameraPreviewActor::CreatePreviewResources(const FIntPoint& Resolution)
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("CreatePreviewResources requested. Resolution=%dx%d ExistingRT=%s DisplayPlane=%s DisplayMaterial=%s"),
		Resolution.X,
		Resolution.Y,
		*GetNameSafe(CameraRenderTarget),
		*GetNameSafe(DisplayPlane),
		*GetNameSafe(DisplayMaterial));

	if (!CameraRenderTarget)
	{
		CameraRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("PICOCameraPreviewRenderTarget"));
	}

	CameraRenderTarget->RenderTargetFormat = RTF_RGBA8;
	CameraRenderTarget->ClearColor = FLinearColor::Black;
	CameraRenderTarget->bAutoGenerateMips = false;
	CameraRenderTarget->InitCustomFormat(Resolution.X, Resolution.Y, PF_R8G8B8A8, false);
	CameraRenderTarget->UpdateResourceImmediate(true);

	const float AspectRatio = Resolution.Y > 0 ? static_cast<float>(Resolution.X) / static_cast<float>(Resolution.Y) : 1.0f;
	const float PlaneHeightCm = 100.0f;
	const float PlaneWidthCm = PlaneHeightCm * AspectRatio;
	DisplayPlane->SetRelativeScale3D(FVector(PlaneWidthCm / 100.0f, PlaneHeightCm / 100.0f, 1.0f));

	if (DisplayMaterial)
	{
		DisplayMID = UMaterialInstanceDynamic::Create(DisplayMaterial, this);
		DisplayMID->SetTextureParameterValue(TextureParameterName, CameraRenderTarget);
		DisplayPlane->SetMaterial(0, DisplayMID);
		UE_LOG(LogPICOCameraPreview, Log, TEXT("Preview material assigned. MID=%s TextureParameter=%s RT=%s RTSize=%dx%d"),
			*GetNameSafe(DisplayMID),
			*TextureParameterName.ToString(),
			*GetNameSafe(CameraRenderTarget),
			CameraRenderTarget->SizeX,
			CameraRenderTarget->SizeY);
	}
	else
	{
		UE_LOG(LogPICOCameraPreview, Error, TEXT("DisplayMaterial is null. Camera image can be acquired but will not be visible on the plane."));
	}
}

void APICOCameraPreviewActor::ShutdownCamera()
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("ShutdownCamera begin. CaptureRunning=%s DeviceHandle=%lld SessionHandle=%lld Frames=%lld RTUpdateFailures=%lld"),
		bCaptureRunning ? TEXT("true") : TEXT("false"),
		DeviceHandle.Handle,
		SessionHandle.Handle,
		FramesAcquired,
		RenderTargetUpdateFailures);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PermissionGrantedStartDelayTimerHandle);
	}

	if (bCaptureRunning && SessionHandle.IsValid())
	{
		const bool bEnded = UPICOCameraFunctionLibrary::EndCameraCapture(SessionHandle);
		UE_LOG(LogPICOCameraPreview, Log, TEXT("EndCameraCapture returned %s"), bEnded ? TEXT("true") : TEXT("false"));
	}

	if (SessionHandle.IsValid())
	{
		const bool bDestroyedSession = UPICOCameraFunctionLibrary::DestroyCaptureSession(SessionHandle);
		UE_LOG(LogPICOCameraPreview, Log, TEXT("DestroyCaptureSession returned %s"), bDestroyedSession ? TEXT("true") : TEXT("false"));
		SessionHandle = FPICOCameraCaptureSessionHandle();
	}

	if (DeviceHandle.IsValid())
	{
		const bool bDestroyedDevice = UPICOCameraFunctionLibrary::DestroyCameraDevice(DeviceHandle);
		UE_LOG(LogPICOCameraPreview, Log, TEXT("DestroyCameraDevice returned %s"), bDestroyedDevice ? TEXT("true") : TEXT("false"));
		DeviceHandle = FPICOCameraDeviceHandle();
	}

	bCaptureRunning = false;
	PermissionProxy = nullptr;
}

void APICOCameraPreviewActor::SetStatus(const FString& Status)
{
	LastStatus = Status;
	UE_LOG(LogPICOCameraPreview, Log, TEXT("%s"), *Status);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 3.0f, FColor::Cyan, Status);
	}
}

void APICOCameraPreviewActor::LogDebugStatus(const FString& Status) const
{
	UE_LOG(LogPICOCameraPreview, Log, TEXT("%s"), *Status);
}
