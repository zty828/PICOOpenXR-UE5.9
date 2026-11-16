// Copyright 2024 PICO Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PICO_CameraFunctionLibrary.h"
#include "TimerManager.h"
#include "PICO_CameraPreviewActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;
class UAndroidPermissionCallbackProxy;

/**
 * Runtime test actor for the PICO Camera API.
 *
 * Place it in a level, package to Android, and run on a PICO headset. The actor
 * requests android.permission.CAMERA, creates a camera capture session with a
 * supported resolution, and displays the CPU camera buffer on a plane.
 */
UCLASS()
class PICOOPENXRHMD_API APICOCameraPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	APICOCameraPreviewActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera")
	bool bAutoStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera")
	bool bUseFirstAvailableCamera = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera", meta = (EditCondition = "!bUseFirstAvailableCamera"))
	int64 CameraID = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera")
	FIntPoint PreferredResolution = FIntPoint(1024, 768);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera")
	EPICOCameraImageFPS PreferredFPS = EPICOCameraImageFPS::FPS_30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PermissionGrantedStartDelaySeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera|Display")
	TObjectPtr<UMaterialInterface> DisplayMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICO|Camera|Display")
	FName TextureParameterName = TEXT("CameraTexture");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO|Camera")
	FString LastStatus;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO|Camera")
	bool bCaptureRunning = false;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO|Camera|Display")
	TObjectPtr<USceneComponent> PreviewRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PICO|Camera|Display")
	TObjectPtr<UStaticMeshComponent> DisplayPlane;

private:
	void StartAfterPermission();
	void StartAfterPermissionDelayElapsed();
	bool SelectCameraAndConfig(FPICOCameraCaptureConfig& OutConfig);
	void CreatePreviewResources(const FIntPoint& Resolution);
	void ShutdownCamera();
	void SetStatus(const FString& Status);
	void LogDebugStatus(const FString& Status) const;

	UFUNCTION()
	void OnCameraPermissionResult(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);

	UFUNCTION()
	void OnCameraDeviceCreated();

	UFUNCTION()
	void OnCaptureSessionCreated();

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CameraRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DisplayMID;

	UPROPERTY(Transient)
	TObjectPtr<UAndroidPermissionCallbackProxy> PermissionProxy;

	FPICOCameraDeviceHandle DeviceHandle;
	FPICOCameraCaptureSessionHandle SessionHandle;
	FPICOCameraCaptureConfig CaptureConfig;
	FTimespan LastCaptureTime;
	FTimerHandle PermissionGrantedStartDelayTimerHandle;
	bool bDeviceCreateSuccess = false;
	bool bSessionCreateSuccess = false;
	int64 FramesAcquired = 0;
	int64 RenderTargetUpdateFailures = 0;
	int32 ConsecutiveNoImageFrames = 0;
	float SecondsSinceNoImageLog = 0.0f;
};
