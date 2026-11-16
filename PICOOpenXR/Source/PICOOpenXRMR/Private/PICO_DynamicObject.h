// Copyright PICO Technology Co., Ltd. All rights reserved.
// This plugin incorporates portions of the Unreal® Engine. Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PICO_MR.h"

class FPICOOpenXRMRModule;

class FDynamicObjectExtensionPICO : public FMixedRealityPICO
{
public:
	FDynamicObjectExtensionPICO();
	virtual ~FDynamicObjectExtensionPICO() override {}

	void GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions);
	void GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions);
	void PostGetSystem(XrInstance InInstance, XrSystemId InSystem);
	virtual void PostCreateSession(XrSession InSession) override;
	virtual void OnDestroySession(XrSession InSession) override;

	static FDynamicObjectExtensionPICO* GetInstance();

	virtual bool CreateProvider(const FSenseDataProviderCreateInfoBasePICO& CreateInfo, EResultPICO& OutResult) override;

	bool RequestDynamicObjects(const FPICOPollFutureDelegate& Delegate, EResultPICO& OutResult);
	bool GetDynamicObjectInfos(const XrFutureEXT& FutureHandle, TArray<FDynamicObjectInfoPICO>& DynamicObjectInfos, EResultPICO& OutResult);

	void ClearDynamicObjectProviderBuffer();

	bool IsKeyboardSupported() const { return bSupportsDynamicObjectKeyboard; }
	bool IsMouseSupported() const { return bSupportsDynamicObjectMouse; }
	bool IsPicoKeyboardSupported() const { return bSupportsDynamicObjectPicoKeyboard; }

private:
	bool IsContainsInLastUpdate(const FSpatialUUIDPICO& UUID);
	int64_t GetLastUpdateTimeByUUID(const FSpatialUUIDPICO& UUID);

	TMap<FSpatialUUIDPICO, FDynamicObjectInfoPICO> CachedUUIDToInfoMap;

	mutable FCriticalSection CriticalSection;

	bool bSupportsDynamicObjectTrackingEXT = false;
	bool bSupportsDynamicObjectTracking = false;
	bool bSupportsDynamicObjectKeyboardEXT = false;
	bool bSupportsDynamicObjectKeyboard = false;
	bool bSupportsDynamicObjectMouseEXT = false;
	bool bSupportsDynamicObjectMouse = false;
	bool bSupportsDynamicObjectPicoKeyboardEXT = false;
	bool bSupportsDynamicObjectPicoKeyboard = false;
};
