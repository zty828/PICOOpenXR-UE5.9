// Copyright 2023 PICO Inc. All Rights Reserved.

#include "PICOOpenXRRuntimeSettings.h"

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "Engine/RendererSettings.h"
#include "Misc/ConfigUtilities.h"
#include "OpenXRHMDSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY(LogPICOOpenXRSettings);

namespace
{
	const TCHAR* PICOOpenXRRuntimeSettingsSection = TEXT("/Script/PICOOpenXRRuntimeSettings.PICOOpenXRRuntimeSettings");
	const TCHAR* AndroidDeviceProfileSection = TEXT("Android DeviceProfile");
	const TCHAR* FBFoveationLevelCVarName = TEXT("xr.OpenXRFBFoveationLevel");

	// UE 5.9 added a hard gate in the engine's OpenXR plugin: on Android the OpenXR HMD module
	// refuses to initialise unless the app was packaged as an OpenXR "immersive HMD" application,
	// i.e. AndroidRuntimeSettings::bPackageForOpenXRImmersive (UE 5.8 had no such requirement and
	// silently relied on the runtime being present). A project migrated from 5.8 to 5.9 would
	// therefore lose OpenXR - and with it every PICO feature - on device, while still running fine
	// in the editor. Keep the flag enabled the same way this class already maintains the
	// "pvr.app.type" activity meta-data, but never override a value the project set on purpose.
	void EnsureOpenXRImmersivePackagingEnabled()
	{
		const FString SectionName = TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
		const FString SettingName = TEXT("bPackageForOpenXRImmersive");
		const FString DefaultEnginePath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini")));

		if (!IFileManager::Get().FileExists(*DefaultEnginePath))
		{
			return;
		}

		FString ExistingValue;
		GConfig->LoadFile(DefaultEnginePath);
		if (GConfig->GetString(*SectionName, *SettingName, ExistingValue, DefaultEnginePath))
		{
			// Already configured (possibly disabled on purpose) - leave it untouched.
			GConfig->UnloadFile(DefaultEnginePath);
			return;
		}

		GConfig->SetString(*SectionName, *SettingName, TEXT("True"), DefaultEnginePath);
		GConfig->Flush(false);
		GConfig->UnloadFile(DefaultEnginePath);
		UE_LOG(LogPICOOpenXRSettings, Display, TEXT("PICO OpenXR: enabled bPackageForOpenXRImmersive in %s (required by UE5.9 to keep OpenXR active on Android)."), *DefaultEnginePath);
	}

	void SyncFBFoveationLevelToDeviceProfile(int32 InLevel)
	{
		const FString DeviceProfilesPath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultDeviceProfiles.ini")));
		FConfigFile DeviceProfiles;
		if (IFileManager::Get().FileExists(*DeviceProfilesPath))
		{
			DeviceProfiles.Read(DeviceProfilesPath);
		}

		TArray<FString> CVars;
		DeviceProfiles.GetArray(AndroidDeviceProfileSection, TEXT("CVars"), CVars);

		const FString FoveationAssignment = FString::Printf(TEXT("%s=%d"), FBFoveationLevelCVarName, FMath::Clamp(InLevel, 0, 3));
		bool bFoundFoveationLevel = false;
		for (FString& CVar : CVars)
		{
			if (CVar.StartsWith(FString(FBFoveationLevelCVarName) + TEXT("="), ESearchCase::IgnoreCase))
			{
				CVar = FoveationAssignment;
				bFoundFoveationLevel = true;
				break;
			}
		}

		if (!bFoundFoveationLevel)
		{
			CVars.Add(FoveationAssignment);
		}

		DeviceProfiles.SetArray(AndroidDeviceProfileSection, TEXT("+CVars"), CVars);
		DeviceProfiles.Write(DeviceProfilesPath, false);
	}

#if WITH_EDITOR
	void ApplyChangedPropertyToConsoleVariable(UPICOOpenXRRuntimeSettings* Settings, FProperty* PropertyThatChanged)
	{
		if (!Settings || !PropertyThatChanged)
		{
			return;
		}

		const FString& CVarName = PropertyThatChanged->GetMetaData(TEXT("ConsoleVariable"));
		if (CVarName.IsEmpty())
		{
			return;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar || (CVar->GetFlags() & ECVF_ReadOnly) != 0)
		{
			return;
		}

		if (FByteProperty* ByteProperty = CastField<FByteProperty>(PropertyThatChanged))
		{
			if (ByteProperty->Enum != nullptr)
			{
				CVar->Set(ByteProperty->GetPropertyValue_InContainer(Settings), ECVF_SetByPluginHighPriority);
			}
		}
		else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(PropertyThatChanged))
		{
			FNumericProperty* UnderlyingProp = EnumProperty->GetUnderlyingProperty();
			void* PropertyAddress = EnumProperty->ContainerPtrToValuePtr<void>(Settings);
			CVar->Set((int32)UnderlyingProp->GetSignedIntPropertyValue(PropertyAddress), ECVF_SetByPluginHighPriority);
		}
		else if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(PropertyThatChanged))
		{
			CVar->Set((int32)BoolProperty->GetPropertyValue_InContainer(Settings), ECVF_SetByPluginHighPriority);
		}
		else if (FIntProperty* IntProperty = CastField<FIntProperty>(PropertyThatChanged))
		{
			CVar->Set(IntProperty->GetPropertyValue_InContainer(Settings), ECVF_SetByPluginHighPriority);
		}
		else if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(PropertyThatChanged))
		{
			CVar->Set(FloatProperty->GetPropertyValue_InContainer(Settings), ECVF_SetByPluginHighPriority);
		}
		else if (FStrProperty* StringProperty = CastField<FStrProperty>(PropertyThatChanged))
		{
			CVar->Set(*StringProperty->GetPropertyValue_InContainer(Settings), ECVF_SetByPluginHighPriority);
		}
		else if (FNameProperty* NameProperty = CastField<FNameProperty>(PropertyThatChanged))
		{
			CVar->Set(*NameProperty->GetPropertyValue_InContainer(Settings).ToString(), ECVF_SetByPluginHighPriority);
		}
	}
#endif
}

IMPLEMENT_MODULE(FDefaultModuleImpl, PICOOpenXRRuntimeSettings);

UPICOOpenXRRuntimeSettings::UPICOOpenXRRuntimeSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPICOOpenXRRuntimeSettings::ApplyCVarSettingsFromProjectSettings()
{
	UE::ConfigUtilities::ApplyCVarSettingsFromIni(PICOOpenXRRuntimeSettingsSection, *GEngineIni, ECVF_SetByProjectSetting);
}

void UPICOOpenXRRuntimeSettings::EnsureFBFoveationLevelDeviceProfile()
{
	const FString DeviceProfilesPath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultDeviceProfiles.ini")));
	if (!IFileManager::Get().FileExists(*DeviceProfilesPath))
	{
		const UPICOOpenXRRuntimeSettings* Settings = GetDefault<UPICOOpenXRRuntimeSettings>();
		SyncFBFoveationLevelToDeviceProfile(Settings != nullptr ? static_cast<int32>(Settings->FBFoveationLevel) : static_cast<int32>(EFBFoveationLevelPICO::Medium));
	}
}

void UPICOOpenXRRuntimeSettings::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITOR
	Super::TryUpdateDefaultConfigFile();

	if (GConfig)
	{
		const FString Value = FString("<meta-data android:name=\"") + "pvr.app.type" + "\" android:value=\"" + "vr" + "\" />";
		const FString MetaDataName = TEXT("android:name=\"pvr.app.type\"");
		const FString SettingName = TEXT("ExtraActivitySettings");
		const FString SectionName = TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");
		const FString DefaultEnginePath = FConfigCacheIni::NormalizeConfigIniPath(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini")));
		FString ExistingValue;
		bool bHasMatchingSetting = false;
		GConfig->LoadFile(DefaultEnginePath);
		GConfig->GetString(*SectionName, *SettingName, ExistingValue, DefaultEnginePath);
		if (!ExistingValue.IsEmpty())
		{
			TArray<FString> ExistingSettings;
			ExistingValue.ParseIntoArray(ExistingSettings, TEXT("\\n"), true);

			for (FString ExistingSetting : ExistingSettings)
			{
				ExistingSetting.TrimStartAndEndInline();
				if (ExistingSetting.Contains(MetaDataName))
				{
					bHasMatchingSetting = true;
					break;
				}
			}
		}

		if (!bHasMatchingSetting)
		{
			if (!ExistingValue.IsEmpty())
			{
				if (!ExistingValue.EndsWith(TEXT("\\n")))
				{
					ExistingValue += TEXT("\\n");
				}
				ExistingValue += Value;
			}
			else
			{
				ExistingValue = Value;
			}

			GConfig->SetString(*SectionName, *SettingName, *ExistingValue, DefaultEnginePath);
		}
		GConfig->Flush(false);
		GConfig->UnloadFile(DefaultEnginePath);
	}

	EnsureOpenXRImmersivePackagingEnabled();

	ToggleOcclusionCulling();
#endif
}

bool UPICOOpenXRRuntimeSettings::GetBoolConfigByKey(const FString& InKeyName)
{
	if(const FConfigSection* Section = GConfig->GetSection(TEXT("/Script/PICOOpenXRRuntimeSettings.PICOOpenXRRuntimeSettings"), false, GEngineIni))
	{
		for(FConfigSectionMap::TConstIterator It(*Section); It; ++It)
		{
			const FString& KeyString = It.Key().GetPlainNameString(); 
			const FString& ValueString = It.Value().GetValue();

			if (KeyString==InKeyName)
			{
				const FString& NewValueString = UE::ConfigUtilities::ConvertValueFromHumanFriendlyValue(*ValueString);

				if (NewValueString == TEXT("1"))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UPICOOpenXRRuntimeSettings::ToggleOcclusionCulling()
{
	URendererSettings* RendererSettings = GetMutableDefault<URendererSettings>();
	RendererSettings->bOcclusionCulling = !bDisableOcclusionCulling;
	RendererSettings->UpdateSinglePropertyInConfigFile(RendererSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, bOcclusionCulling)), RendererSettings->GetDefaultConfigFilename());
}

#if WITH_EDITOR
void UPICOOpenXRRuntimeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	GConfig->Flush(false);
	ApplyChangedPropertyToConsoleVariable(this, PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty : PropertyChangedEvent.Property);

	FString Dst = FPaths::ProjectDir() / TEXT("pico_splash.png");
	if (!(bUsingOSSplash && IFileManager::Get().Copy(*Dst, *OSSplashScreen.FilePath, true) == COPY_OK))
	{
		IFileManager::Get().Delete(*Dst, true);
	}

	static const FName EnableSemanticsAlignWithVertex = GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bSemanticsAlignWithVertex);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == EnableSemanticsAlignWithVertex)
	{
		if (bSemanticsAlignWithVertex == true)
		{
			bSemanticsAlignWithTriangle = false;
			UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bSemanticsAlignWithTriangle)), GetDefaultConfigFilename());
		}
	}

	static const FName EnableSemanticsAlignWithTriangle = GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bSemanticsAlignWithTriangle);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == EnableSemanticsAlignWithTriangle)
	{
		if (bSemanticsAlignWithTriangle == true)
		{
			bSemanticsAlignWithVertex = false;
			UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bSemanticsAlignWithVertex)), GetDefaultConfigFilename());
		}
	}

	static const FName EyeTrackingEnabled = GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bEyeTrackingEnabled);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == EyeTrackingEnabled)
	{
		if (!bEyeTrackingEnabled && bEnableETFR)
		{
			bEnableETFR = false;
			UpdateSinglePropertyInConfigFile(GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bEnableETFR)), GetDefaultConfigFilename());
		}
	}

	static const FName UseEngineBuiltInFBFoveation = GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, bUseEngineBuiltInFBFoveation);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == UseEngineBuiltInFBFoveation)
	{
		if (UOpenXRHMDSettings* OpenXRHMDSettings = GetMutableDefault<UOpenXRHMDSettings>())
		{
			OpenXRHMDSettings->bIsFBFoveationEnabled = bUseEngineBuiltInFBFoveation;
			OpenXRHMDSettings->UpdateSinglePropertyInConfigFile(
				OpenXRHMDSettings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UOpenXRHMDSettings, bIsFBFoveationEnabled)),
				OpenXRHMDSettings->GetDefaultConfigFilename());
		}
	}

	static const FName FBFoveationLevelProperty = GET_MEMBER_NAME_CHECKED(UPICOOpenXRRuntimeSettings, FBFoveationLevel);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == FBFoveationLevelProperty)
	{
		const int32 FoveationLevel = static_cast<int32>(FBFoveationLevel);
		SyncFBFoveationLevelToDeviceProfile(FoveationLevel);
	}

	ToggleOcclusionCulling();
}

#endif
