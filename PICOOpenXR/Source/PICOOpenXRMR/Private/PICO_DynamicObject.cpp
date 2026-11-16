// Copyright PICO Technology Co., Ltd. All rights reserved.
// This plugin incorporates portions of the Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.
// Copyright Epic Games, Inc. All Rights Reserved.


#include "PICO_DynamicObject.h"
#include "PICO_MRModule.h"
#include "IOpenXRHMDModule.h"
#include "IXRTrackingSystem.h"
#include "OpenXRCore.h"
#include "Engine/Engine.h"
#include "PICOOpenXRRuntimeSettings.h"
#include "openxr/pico_dynamic_object_pico_keyboard.h"

FDynamicObjectExtensionPICO::FDynamicObjectExtensionPICO()
{
}

FDynamicObjectExtensionPICO* FDynamicObjectExtensionPICO::GetInstance()
{
	return &FPICOOpenXRMRModule::Get().GetMRModuleExtension().GetDynamicObjectPICO();
}

void FDynamicObjectExtensionPICO::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add(XR_PICO_DYNAMIC_OBJECT_TRACKING_EXTENSION_NAME);
}

void FDynamicObjectExtensionPICO::GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	// Do NOT touch GetDefault<UPICOOpenXRRuntimeSettings>() here: this is invoked during
	// IOpenXRHMDModule::InitInstance (PostConfigInit) before UClass registration completes,
	// which triggers "load config data ... before the Class has been constructed" and
	// silently zero-initializes the CDO for ALL settings. Enumerate unconditionally and
	// gate on Settings in PostCreateSession where the CDO is safely available.
	OutExtensions.Add(XR_PICO_DYNAMIC_OBJECT_KEYBOARD_EXTENSION_NAME);
	OutExtensions.Add(XR_PICO_DYNAMIC_OBJECT_MOUSE_EXTENSION_NAME);
	OutExtensions.Add(XR_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_EXTENSION_NAME);
}

void FDynamicObjectExtensionPICO::PostGetSystem(XrInstance InInstance, XrSystemId InSystem)
{
	if (!FSpatialSensingExtensionPICO::GetInstance()->IsSupportsSpatialSensing())
	{
		return;
	}

	bSupportsDynamicObjectTrackingEXT = IOpenXRHMDModule::Get().IsExtensionEnabled(XR_PICO_DYNAMIC_OBJECT_TRACKING_EXTENSION_NAME);
	bSupportsDynamicObjectKeyboardEXT = IOpenXRHMDModule::Get().IsExtensionEnabled(XR_PICO_DYNAMIC_OBJECT_KEYBOARD_EXTENSION_NAME);
	bSupportsDynamicObjectMouseEXT = IOpenXRHMDModule::Get().IsExtensionEnabled(XR_PICO_DYNAMIC_OBJECT_MOUSE_EXTENSION_NAME);
	bSupportsDynamicObjectPicoKeyboardEXT = IOpenXRHMDModule::Get().IsExtensionEnabled(XR_PICO_DYNAMIC_OBJECT_PICO_KEYBOARD_EXTENSION_NAME);

	UE_LOG(LogMRPICO, Verbose, TEXT("DynamicObject EXTs: tracking=%d keyboard=%d mouse=%d picoKeyboard=%d"),
		bSupportsDynamicObjectTrackingEXT, bSupportsDynamicObjectKeyboardEXT, bSupportsDynamicObjectMouseEXT, bSupportsDynamicObjectPicoKeyboardEXT);

	if (!bSupportsDynamicObjectTrackingEXT)
	{
		return;
	}

	XrSystemDynamicObjectMousePropertiesPICO MouseProps = { XR_TYPE_SYSTEM_DYNAMIC_OBJECT_MOUSE_PROPERTIES_PICO };
	XrSystemDynamicObjectKeyboardPropertiesPICO KeyboardProps = { XR_TYPE_SYSTEM_DYNAMIC_OBJECT_KEYBOARD_PROPERTIES_PICO };
	XrSystemDynamicObjectPicoKeyboardPropertiesPICO PicoKeyboardProps = { XR_TYPE_SYSTEM_DYNAMIC_OBJECT_PICO_KEYBOARD_PROPERTIES_PICO };
	XrSystemDynamicObjectTrackingPropertiesPICO TrackingProps = { XR_TYPE_SYSTEM_DYNAMIC_OBJECT_TRACKING_PROPERTIES_PICO };

	void* NextChain = nullptr;
	if (bSupportsDynamicObjectPicoKeyboardEXT)
	{
		PicoKeyboardProps.next = NextChain;
		NextChain = &PicoKeyboardProps;
	}
	if (bSupportsDynamicObjectMouseEXT)
	{
		MouseProps.next = NextChain;
		NextChain = &MouseProps;
	}
	if (bSupportsDynamicObjectKeyboardEXT)
	{
		KeyboardProps.next = NextChain;
		NextChain = &KeyboardProps;
	}
	TrackingProps.next = NextChain;

	XrSystemProperties SPTracking{ XR_TYPE_SYSTEM_PROPERTIES, &TrackingProps };
	XR_ENSURE(xrGetSystemProperties(InInstance, InSystem, &SPTracking));
	bSupportsDynamicObjectTracking = TrackingProps.supportsDynamicObjectTracking == XR_TRUE;
	bSupportsDynamicObjectKeyboard = bSupportsDynamicObjectKeyboardEXT && KeyboardProps.supportsDynamicObjectKeyboard == XR_TRUE;
	bSupportsDynamicObjectMouse = bSupportsDynamicObjectMouseEXT && MouseProps.supportsDynamicObjectMouse == XR_TRUE;
	bSupportsDynamicObjectPicoKeyboard = bSupportsDynamicObjectPicoKeyboardEXT && PicoKeyboardProps.supportsDynamicObjectPicoKeyboard == XR_TRUE;

	UE_LOG(LogMRPICO, Log, TEXT("DynamicObject supports: tracking=%d keyboard=%d mouse=%d picoKeyboard=%d"),
		bSupportsDynamicObjectTracking, bSupportsDynamicObjectKeyboard, bSupportsDynamicObjectMouse, bSupportsDynamicObjectPicoKeyboard);
}

void FDynamicObjectExtensionPICO::PostCreateSession(XrSession InSession)
{
	FMixedRealityPICO::PostCreateSession(InSession);
	const UPICOOpenXRRuntimeSettings* Settings = GetDefault<UPICOOpenXRRuntimeSettings>();

	if (!Settings->bEnableDynamicObjectTracking)
	{
		return;
	}

	if (!bSupportsDynamicObjectTracking)
	{
		return;
	}

	FSenseDataProviderCreateInfoDynamicObjectPICO CreateInfo;
	if (bSupportsDynamicObjectKeyboard)
	{
		CreateInfo.TrackingTypes.Add(EDynamicObjectTypePICO::Keyboard);
	}
	if (bSupportsDynamicObjectMouse)
	{
		CreateInfo.TrackingTypes.Add(EDynamicObjectTypePICO::Mouse);
	}
	if (bSupportsDynamicObjectPicoKeyboard)
	{
		CreateInfo.TrackingTypes.Add(EDynamicObjectTypePICO::PicoKeyboard);
	}

	if (CreateInfo.TrackingTypes.Num() == 0)
	{
		UE_LOG(LogMRPICO, Warning, TEXT("DynamicObjectTracking enabled but no tracking type is runtime-supported. keyboard=%d mouse=%d picoKeyboard=%d"),
			bSupportsDynamicObjectKeyboard, bSupportsDynamicObjectMouse, bSupportsDynamicObjectPicoKeyboard);
		return;
	}

	EResultPICO OutResult = EResultPICO::XR_Error_Unknown_PICO;
	if (CreateProvider(CreateInfo, OutResult))
	{
		UE_LOG(LogMRPICO, Log, TEXT("CreateDynamicObjectProvider Success"));
	}
	else
	{
		UE_LOG(LogMRPICO, Error, TEXT("CreateDynamicObjectProvider failed OutResult:%d"), static_cast<int32>(OutResult));
	}
}

void FDynamicObjectExtensionPICO::OnDestroySession(XrSession InSession)
{
	FMixedRealityPICO::OnDestroySession(InSession);
	ClearDynamicObjectProviderBuffer();
}

bool FDynamicObjectExtensionPICO::CreateProvider(const FSenseDataProviderCreateInfoBasePICO& createInfo, EResultPICO& OutResult)
{
	check(createInfo.Type == EProviderTypePICO::Pico_Provider_Dynamic_Object);
	bool bResult = false;
	if (bSupportsDynamicObjectTracking)
	{
		if (Session == XR_NULL_HANDLE)
		{
			OutResult = EResultPICO::XR_Error_ValidationFailure;
			return false;
		}

		if (IsHandleValid())
		{
			UE_LOG(LogMRPICO, Error, TEXT("CreateDynamicObjectProvider already created"));
			OutResult = EResultPICO::XR_Error_LimitReached;
			return false;
		}

		const FSenseDataProviderCreateInfoDynamicObjectPICO& TypedInfo = static_cast<const FSenseDataProviderCreateInfoDynamicObjectPICO&>(createInfo);

		TArray<XrDynamicObjectTypePICO> TrackingTypes;
		TrackingTypes.Reserve(TypedInfo.TrackingTypes.Num());
		for (EDynamicObjectTypePICO TrackingType : TypedInfo.TrackingTypes)
		{
			switch (TrackingType)
			{
			case EDynamicObjectTypePICO::Keyboard:
				TrackingTypes.Add(XR_DYNAMIC_OBJECT_TYPE_KEYBOARD_PICO);
				break;
			case EDynamicObjectTypePICO::Mouse:
				TrackingTypes.Add(XR_DYNAMIC_OBJECT_TYPE_MOUSE_PICO);
				break;
			case EDynamicObjectTypePICO::PicoKeyboard:
				TrackingTypes.Add(XR_DYNAMIC_OBJECT_TYPE_PICO_KEYBOARD_PICO);
				break;
			case EDynamicObjectTypePICO::PicoTouchpad:
				TrackingTypes.Add(XR_DYNAMIC_OBJECT_TYPE_PICO_TOUCHPAD_PICO);
				break;
			default:
				break;
			}
		}

		if (TrackingTypes.Num() == 0)
		{
			OutResult = EResultPICO::XR_Error_ValidationFailure;
			return false;
		}

		Type = EProviderTypePICO::Pico_Provider_Dynamic_Object;

		XrSenseDataProviderCreateInfoDynamicObjectPICO cCreateInfo = {};
		cCreateInfo.type = XR_TYPE_SENSE_DATA_PROVIDER_CREATE_INFO_DYNAMIC_OBJECT_PICO;
		cCreateInfo.trackingTypeCount = TrackingTypes.Num();
		cCreateInfo.trackingTypes = TrackingTypes.GetData();

		FString TypesStr;
		for (XrDynamicObjectTypePICO T : TrackingTypes)
		{
			TypesStr += FString::Printf(TEXT("%d,"), static_cast<int32>(T));
		}
		UE_LOG(LogMRPICO, Log, TEXT("xrCreateSenseDataProviderPICO trackingTypes=[%s]"), *TypesStr);

		XrResult xrResult = FSpatialSensingExtensionPICO::GetInstance()->xrCreateSenseDataProviderPICO(
			Session,
			reinterpret_cast<XrSenseDataProviderCreateInfoBaseHeaderPICO*>(&cCreateInfo),
			&ProviderHandle);
		bResult = XR_SUCCEEDED(xrResult);

		ProviderHandle = bResult ? ProviderHandle : XR_NULL_HANDLE;
		OutResult = FSpatialSensingExtensionPICO::CastToPICOResult(xrResult);
	}

	return bResult;
}

bool FDynamicObjectExtensionPICO::RequestDynamicObjects(const FPICOPollFutureDelegate& Delegate, EResultPICO& OutResult)
{
	bool bResult = false;
	if (bSupportsDynamicObjectTracking && IsHandleValid())
	{
		XrFutureEXT Handle;
		XrSenseDataQueryInfoPICO cQueryInfo = {};
		cQueryInfo.type = XR_TYPE_SENSE_DATA_QUERY_INFO_PICO;
		cQueryInfo.filter = nullptr;

		XrResult xrResult = FSpatialSensingExtensionPICO::GetInstance()->xrQuerySenseDataAsyncPICO(ProviderHandle, &cQueryInfo, &Handle);
		bResult = XR_SUCCEEDED(xrResult);

		OutResult = FSpatialSensingExtensionPICO::CastToPICOResult(xrResult);
		if (bResult)
		{
			bResult = FSpatialSensingExtensionPICO::GetInstance()->AddPollFutureRequirement(Handle, Delegate);
		}
	}
	return bResult;
}

bool FDynamicObjectExtensionPICO::GetDynamicObjectInfos(const XrFutureEXT& FutureHandle, TArray<FDynamicObjectInfoPICO>& DynamicObjectInfos, EResultPICO& OutResult)
{
	if (!bSupportsDynamicObjectTracking)
	{
		return false;
	}

	FScopeLock Lock(&CriticalSection);
	FSenseDataQueryCompletionPICO QueryCompletion;
	if (!FSpatialSensingExtensionPICO::GetInstance()->QuerySenseDataComplete(ProviderHandle, FutureHandle, QueryCompletion, OutResult))
	{
		return false;
	}

	if (PXR_FAILURE(QueryCompletion.FutureResult))
	{
		OutResult = QueryCompletion.FutureResult;
		return false;
	}

	if (QueryCompletion.SnapShotHandle == XR_NULL_HANDLE)
	{
		OutResult = EResultPICO::XR_Error_HandleInvalid;
		return false;
	}

	FQueriedSenseDataPICO QueriedSenseData;
	if (!FSpatialSensingExtensionPICO::GetInstance()->GetQueriedSenseData(ProviderHandle, QueryCompletion.SnapShotHandle, QueriedSenseData, OutResult))
	{
		return false;
	}

	TSet<FSpatialUUIDPICO> CurrentUUIDSet;
	TSet<FSpatialUUIDPICO> RemovedUUIDSet;
	DynamicObjectInfos.Empty();

	for (auto EntityInfo : QueriedSenseData.QueriedSpatialEntityInfos)
	{
		if (!EntityInfo.entity)
		{
			continue;
		}

		CurrentUUIDSet.Add(EntityInfo.uuid.data);
		UE_LOG(LogMRPICO, Log, TEXT("DynamicObject Entity UUID=%s EntityHandle=%llu LastUpdateTime=%lld"),
			*BytesToHex(EntityInfo.uuid.data, PXR_UUID_SIZE),
			static_cast<unsigned long long>(EntityInfo.entity),
			static_cast<long long>(EntityInfo.lastUpdateTime));

		FDynamicObjectInfoPICO Info;
		Info.UUID = EntityInfo.uuid.data;
		Info.UpdateTime = static_cast<int64>(EntityInfo.lastUpdateTime);

		if (IsContainsInLastUpdate(EntityInfo.uuid.data))
		{
			if (GetLastUpdateTimeByUUID(EntityInfo.uuid.data) >= EntityInfo.lastUpdateTime)
			{
				CachedUUIDToInfoMap[EntityInfo.uuid.data].State = ESpatialMeshStatePICO::Stable;
				continue;
			}
			Info.State = ESpatialMeshStatePICO::Updated;
		}
		else
		{
			Info.State = ESpatialMeshStatePICO::Added;
		}

		TArray<ESpatialEntityComponentTypePICO> ComponentTypes;
		if (!FSpatialSensingExtensionPICO::GetInstance()->EnumerateSpatialEntityComponentTypes(QueryCompletion.SnapShotHandle, EntityInfo.entity, ComponentTypes, OutResult))
		{
			UE_LOG(LogMRPICO, Error, TEXT("EnumerateSpatialEntityComponentTypes Failed OutResult:%d"), static_cast<int32>(OutResult));
			continue;
		}

		{
			FString ComponentTypesStr;
			for (const auto ComponentType : ComponentTypes)
			{
				ComponentTypesStr.Append(FString::Printf(TEXT("%u,"), static_cast<uint32>(ComponentType)));
			}
			UE_LOG(LogMRPICO, Log, TEXT("DynamicObject Entity=%llu ComponentTypes=[%s]"),
				static_cast<unsigned long long>(EntityInfo.entity), *ComponentTypesStr);
		}

		if (!FSpatialSensingExtensionPICO::GetInstance()->GetSpatialEntityLocation(QueryCompletion.SnapShotHandle, EntityInfo.entity, Info.Pose, OutResult))
		{
			UE_LOG(LogMRPICO, Error, TEXT("GetSpatialEntityLocation Failed OutResult:%d"), static_cast<int32>(OutResult));
			continue;
		}

		const bool bHasDynamicObject = ComponentTypes.Contains(ESpatialEntityComponentTypePICO::DynamicObject);
		const bool bHasSphere = ComponentTypes.Contains(ESpatialEntityComponentTypePICO::Sphere);
		const bool bHasPeripheral = ComponentTypes.Contains(ESpatialEntityComponentTypePICO::Peripheral);
		const bool bHasBB3D = ComponentTypes.Contains(ESpatialEntityComponentTypePICO::Boundary_3D);

		if (bHasDynamicObject)
		{
			if (!FSpatialSensingExtensionPICO::GetInstance()->GetSpatialEntityDynamicObjectData(QueryCompletion.SnapShotHandle, EntityInfo.entity, Info.ObjectType, OutResult))
			{
				UE_LOG(LogMRPICO, Warning, TEXT("GetSpatialEntityDynamicObjectData Failed OutResult:%d"), static_cast<int32>(OutResult));
			}
		}

		if (bHasSphere)
		{
			FVector SphereCenter = FVector::ZeroVector;
			float SphereRadius = 0.f;
			if (FSpatialSensingExtensionPICO::GetInstance()->GetSpatialEntitySphere(QueryCompletion.SnapShotHandle, EntityInfo.entity, SphereCenter, SphereRadius, OutResult))
			{
				Info.SphereRadius = SphereRadius;
				Info.bHasSphere = true;
			}
			else
			{
				UE_LOG(LogMRPICO, Warning, TEXT("GetSpatialEntitySphere Failed OutResult:%d"), static_cast<int32>(OutResult));
			}
		}

		if (bHasPeripheral)
		{
			if (FSpatialSensingExtensionPICO::GetInstance()->GetSpatialEntityPeripheralData(
				QueryCompletion.SnapShotHandle,
				EntityInfo.entity,
				Info.Brand,
				Info.Model,
				Info.Category,
				OutResult))
			{
				Info.bHasPeripheral = true;
			}
			else
			{
				UE_LOG(LogMRPICO, Warning, TEXT("GetSpatialEntityPeripheralData Failed OutResult:%d"), static_cast<int32>(OutResult));
			}
		}

		if (bHasBB3D)
		{
			if (!FSpatialSensingExtensionPICO::GetInstance()->GetSpatialEntityBoundary3D(QueryCompletion.SnapShotHandle, EntityInfo.entity, Info.BoundingBox, OutResult))
			{
				UE_LOG(LogMRPICO, Warning, TEXT("GetSpatialEntityBoundary3D Failed OutResult:%d"), static_cast<int32>(OutResult));
			}
		}
		else if (Info.ObjectType == EDynamicObjectTypePICO::PicoKeyboard
			|| Info.ObjectType == EDynamicObjectTypePICO::PicoTouchpad)
		{
			// PICO first-party keyboard/touchpad do not expose the BoundingBox3D component;
			// fall back to fixed physical dimensions so downstream rendering has valid extents.
			// Touchpad shares the same physical footprint as the keyboard per extension design.
			// Axis convention follows the runtime BB3D layout observed on third-party keyboards:
			//   Width  -> long side, Height -> thickness (thin axis), Depth -> short side.
			static constexpr float kDefaultPicoKeyboardLength    = 0.273f;
			static constexpr float kDefaultPicoKeyboardShortSide = 0.122f;
			static constexpr float kDefaultPicoKeyboardThickness = 0.00652f;

			const float WorldToMetersScale = (GEngine && GEngine->XRSystem.IsValid())
				? GEngine->XRSystem->GetWorldToMetersScale()
				: 100.f;

			Info.BoundingBox.Center = FTransform::Identity;
			Info.BoundingBox.Extent.Width  = kDefaultPicoKeyboardLength    * WorldToMetersScale;
			Info.BoundingBox.Extent.Height = kDefaultPicoKeyboardThickness * WorldToMetersScale;
			Info.BoundingBox.Extent.Depth  = kDefaultPicoKeyboardShortSide * WorldToMetersScale;
		}

		CachedUUIDToInfoMap.Emplace(EntityInfo.uuid.data, MoveTemp(Info));
	}

	for (auto& Pair : CachedUUIDToInfoMap)
	{
		if (!CurrentUUIDSet.Contains(Pair.Key))
		{
			CachedUUIDToInfoMap[Pair.Key].State = ESpatialMeshStatePICO::Removed;
			RemovedUUIDSet.Add(Pair.Key);
		}
	}

	CachedUUIDToInfoMap.GenerateValueArray(DynamicObjectInfos);

	for (auto& RemovedUUID : RemovedUUIDSet)
	{
		CachedUUIDToInfoMap.Remove(RemovedUUID);
	}

	if (!FSpatialSensingExtensionPICO::GetInstance()->DestroySenseDataQueryResult(QueryCompletion.SnapShotHandle, OutResult))
	{
		UE_LOG(LogMRPICO, Error, TEXT("DestroySenseDataQueryResult Failed"));
		return false;
	}

	return true;
}

void FDynamicObjectExtensionPICO::ClearDynamicObjectProviderBuffer()
{
	FScopeLock Lock(&CriticalSection);
	CachedUUIDToInfoMap.Empty();
}

bool FDynamicObjectExtensionPICO::IsContainsInLastUpdate(const FSpatialUUIDPICO& UUID)
{
	return CachedUUIDToInfoMap.Contains(UUID);
}

int64_t FDynamicObjectExtensionPICO::GetLastUpdateTimeByUUID(const FSpatialUUIDPICO& UUID)
{
	if (IsContainsInLastUpdate(UUID))
	{
		return CachedUUIDToInfoMap[UUID].UpdateTime;
	}
	return 0;
}
