// Copyright 2023 PICO Inc. All Rights Reserved.

#include "PICO_HMDModule.h"
#include "PICOOpenXRRuntimeSettings.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/ConfigCacheIni.h"
#include "ShaderCore.h"

namespace
{
	TAutoConsoleVariable<int32> CVarPICOFoveationLevel(
		TEXT("xr.PICO.FBFoveationLevel"),
		2,
		TEXT("PICO FB foveation level.\n")
		TEXT("0 = None, 1 = Low, 2 = Medium, 3 = High.\n"),
		ECVF_RenderThreadSafe);

	int32 GetFBFoveationLevelFromConfig()
	{
		FString FoveationLevelValue;
		if (GConfig != nullptr)
		{
			GConfig->GetString(
				TEXT("/Script/PICOOpenXRRuntimeSettings.PICOOpenXRRuntimeSettings"),
				TEXT("FBFoveationLevel"),
				FoveationLevelValue,
				GEngineIni);
		}

		if (FoveationLevelValue.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			return static_cast<int32>(EFBFoveationLevelPICO::None);
		}
		if (FoveationLevelValue.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
		{
			return static_cast<int32>(EFBFoveationLevelPICO::Low);
		}
		if (FoveationLevelValue.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
		{
			return static_cast<int32>(EFBFoveationLevelPICO::Medium);
		}
		if (FoveationLevelValue.Equals(TEXT("High"), ESearchCase::IgnoreCase))
		{
			return static_cast<int32>(EFBFoveationLevelPICO::High);
		}

		int32 ParsedFoveationLevel = 0;
		if (LexTryParseString(ParsedFoveationLevel, *FoveationLevelValue))
		{
			return FMath::Clamp(ParsedFoveationLevel, 0, 3);
		}
		return static_cast<int32>(EFBFoveationLevelPICO::Medium);
	}
}

void FPICOOpenXRHMDModule::StartupModule()
{
	const bool bUseEngineBuiltInFBFoveation = UPICOOpenXRRuntimeSettings::GetBoolConfigByKey(TEXT("bUseEngineBuiltInFBFoveation"));
	const int32 FoveationLevel = GetFBFoveationLevelFromConfig();
	if (GConfig != nullptr)
	{
		GConfig->SetBool(
			TEXT("/Script/OpenXRHMD.OpenXRHMDSettings"),
			TEXT("bIsFBFoveationEnabled"),
			bUseEngineBuiltInFBFoveation,
			GEngineIni);
	}
	CVarPICOFoveationLevel.AsVariable()->Set(FoveationLevel, ECVF_SetByPluginHighPriority);

	HMDPICOExtension.Register();

#if PLATFORM_ANDROID
	if (IConsoleVariable* DisableOpenXROnAndroidWithoutOculusCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("xr.DisableOpenXROnAndroidWithoutOculus")))
	{
		const bool bDisableOpenXROnAndroidWithoutOculus = UPICOOpenXRRuntimeSettings::GetBoolConfigByKey(TEXT("bDisableOpenXROnAndroidWithoutOculus"));
		DisableOpenXROnAndroidWithoutOculusCVar->Set(bDisableOpenXROnAndroidWithoutOculus, ECVF_SetByPluginHighPriority);
	}
#endif

	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("PICOOpenXR"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/Plugin/PICOOpenXR"), PluginShaderDir);
}

void FPICOOpenXRHMDModule::ShutdownModule()
{
	HMDPICOExtension.Unregister();
}

IMPLEMENT_MODULE(FPICOOpenXRHMDModule, PICOOpenXRHMD)
