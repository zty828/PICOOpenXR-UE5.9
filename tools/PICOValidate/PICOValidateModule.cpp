// Editor-side validation commands for the PICOOpenXR plugin ported to UE 5.9.
//
//  PICODumpReflectionSurface [OutPath]  - dump every reflected /Script/PICO* type via TFieldIterator
//  PICOShaderCheck          [OutPath]  - resolve the plugin's global shaders against the active RHI
//
// This module intentionally depends on nothing that drags in the Kismet /
// BlueprintEditor graph: BlueprintEditorModule.cpp:214 does check(GEditor), which
// aborts any standalone (-ExecCmds) or commandlet run.
#include "PICOValidateModule.h"

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Shader.h"
#include "GlobalShader.h"
#include "RHI.h"
#include "RHIGlobals.h"

namespace PICOSurfaceDump
{
	static FString PropertyFlagsToString(const FProperty* Prop)
	{
		TArray<FString> Flags;
		if (Prop->HasAnyPropertyFlags(CPF_BlueprintVisible))  { Flags.Add(TEXT("BPVisible")); }
		if (Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) { Flags.Add(TEXT("BPReadOnly")); }
		if (Prop->HasAnyPropertyFlags(CPF_BlueprintAssignable)) { Flags.Add(TEXT("BPAssignable")); }
		if (Prop->HasAnyPropertyFlags(CPF_Edit))              { Flags.Add(TEXT("Edit")); }
		if (Prop->HasAnyPropertyFlags(CPF_EditConst))         { Flags.Add(TEXT("EditConst")); }
		if (Prop->HasAnyPropertyFlags(CPF_Config))            { Flags.Add(TEXT("Config")); }
		if (Prop->HasAnyPropertyFlags(CPF_Transient))         { Flags.Add(TEXT("Transient")); }
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated))        { Flags.Add(TEXT("Deprecated")); }
		return FString::Join(Flags, TEXT(","));
	}

	static FString FunctionFlagsToString(const UFunction* Func)
	{
		TArray<FString> Flags;
		if (Func->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { Flags.Add(TEXT("BPCallable")); }
		if (Func->HasAnyFunctionFlags(FUNC_BlueprintPure))     { Flags.Add(TEXT("BPPure")); }
		if (Func->HasAnyFunctionFlags(FUNC_BlueprintEvent))    { Flags.Add(TEXT("BPEvent")); }
		if (Func->HasAnyFunctionFlags(FUNC_Native))            { Flags.Add(TEXT("Native")); }
		if (Func->HasAnyFunctionFlags(FUNC_Static))            { Flags.Add(TEXT("Static")); }
		if (Func->HasAnyFunctionFlags(FUNC_Net))               { Flags.Add(TEXT("Net")); }
		return FString::Join(Flags, TEXT(","));
	}

	static bool IsPICOObject(const UObject* Obj)
	{
		if (Obj == nullptr) { return false; }
		const FString PackageName = Obj->GetOutermost()->GetName();
		if (!PackageName.StartsWith(TEXT("/Script/PICO"))) { return false; }
		const FString Name = Obj->GetName();
		return !Name.StartsWith(TEXT("SKEL_")) && !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("TRASH_")) && !Name.StartsWith(TEXT("Default__"));
	}

	void DumpSurface(const TArray<FString>& Args)
	{
		TArray<FString> Lines;
		int32 NumClasses = 0, NumFuncs = 0, NumProps = 0, NumStructs = 0, NumStructProps = 0;
		int32 NumEnums = 0, NumEnumValues = 0, NumCdoFailures = 0;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!IsPICOObject(Class)) { continue; }
			++NumClasses;

			Lines.Add(FString::Printf(TEXT("CLASS|%s|%s|%s|%d"), *Class->GetName(), *Class->GetPathName(),
				Class->GetSuperClass() ? *Class->GetSuperClass()->GetName() : TEXT("None"),
				Class->HasAnyClassFlags(CLASS_Abstract) ? 1 : 0));

			if (!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				const bool bCdoOk = Class->GetDefaultObject(false) != nullptr;
				if (!bCdoOk) { ++NumCdoFailures; }
				Lines.Add(FString::Printf(TEXT("CDO|%s|%d|%d"), *Class->GetName(), bCdoOk ? 1 : 0, Class->GetStructureSize()));
			}

			for (TFieldIterator<UFunction> FuncIt(Class, EFieldIterationFlags::None); FuncIt; ++FuncIt)
			{
				UFunction* Func = *FuncIt;
				if (Func->GetOwnerClass() != Class) { continue; }
				++NumFuncs;
				Lines.Add(FString::Printf(TEXT("FUNC|%s|%s|%s|%d"), *Class->GetName(), *Func->GetName(),
					*FunctionFlagsToString(Func), (int32)Func->NumParms));
			}

			for (TFieldIterator<FProperty> PropIt(Class, EFieldIterationFlags::None); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (Prop->GetOwner<UClass>() != Class) { continue; }
				++NumProps;
				Lines.Add(FString::Printf(TEXT("PROP|%s|%s|%s|%s"), *Class->GetName(), *Prop->GetName(),
					*Prop->GetCPPType(), *PropertyFlagsToString(Prop)));
			}
		}

		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			if (!IsPICOObject(Struct)) { continue; }
			++NumStructs;
			Lines.Add(FString::Printf(TEXT("STRUCT|%s|%s|%d"), *Struct->GetName(), *Struct->GetPathName(),
				(Struct->StructFlags & STRUCT_IsPlainOldData) != 0 ? 1 : 0));

			for (TFieldIterator<FProperty> PropIt(Struct, EFieldIterationFlags::None); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (Prop->GetOwner<UScriptStruct>() != Struct) { continue; }
				++NumStructProps;
				Lines.Add(FString::Printf(TEXT("SPROP|%s|%s|%s|%s"), *Struct->GetName(), *Prop->GetName(),
					*Prop->GetCPPType(), *PropertyFlagsToString(Prop)));
			}
		}

		for (TObjectIterator<UEnum> It; It; ++It)
		{
			UEnum* Enum = *It;
			if (!IsPICOObject(Enum)) { continue; }
			++NumEnums;
			const bool bBlueprintType = Enum->GetBoolMetaData(TEXT("BlueprintType"));
			Lines.Add(FString::Printf(TEXT("ENUM|%s|%s|%d|%d"), *Enum->GetName(), *Enum->GetPathName(),
				bBlueprintType ? 1 : 0, Enum->NumEnums()));

			for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
			{
				const FString EntryName = Enum->GetNameStringByIndex(Index);
				if (EntryName.EndsWith(TEXT("_MAX")) || EntryName.EndsWith(TEXT("_Max")))
				{
					continue;
				}
				++NumEnumValues;
				Lines.Add(FString::Printf(TEXT("EVAL|%s|%s|%d"), *Enum->GetName(), *EntryName,
					(int32)Enum->GetValueByIndex(Index)));
			}
		}

		Lines.Sort();
		Lines.Add(FString::Printf(
			TEXT("SUMMARY|classes=%d|funcs=%d|props=%d|structs=%d|structprops=%d|enums=%d|enumvalues=%d|cdo_failures=%d"),
			NumClasses, NumFuncs, NumProps, NumStructs, NumStructProps, NumEnums, NumEnumValues, NumCdoFailures));

		const FString Path = Args.Num() > 0 ? Args[0] : (FPaths::ProjectSavedDir() / TEXT("PICOSurface.txt"));
		const bool bSaved = FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Path);
		UE_LOG(LogTemp, Display, TEXT("PICOSurface: %d lines -> %s (saved=%d)"), Lines.Num(), *Path, bSaved ? 1 : 0);
	}
}

namespace PICOShaderCheck
{
	// The plugin's IMPLEMENT_SHADER_TYPE registrations live in PICOOpenXRHMD's private
	// PICO_Shaders.cpp, so the types are resolved by registered name - a hit proves that
	// module was loaded and its static registration ran inside THIS process.
	static const TCHAR* KnownShaderTypes[] =
	{
		TEXT("FPICOCubemapPS"),
		TEXT("FDisplayMappingPixelShader"),
	};

	static void Dump(const TArray<FString>& Args)
	{
		TArray<FString> Lines;
		int32 NumTypes = 0, NumCompiled = 0, NumMissing = 0, NumUnregistered = 0;

		for (const TCHAR* Name : KnownShaderTypes)
		{
			const FShaderType* Type = FindShaderTypeByName(FHashedName(Name));
			if (Type == nullptr)
			{
				++NumUnregistered;
				Lines.Add(FString::Printf(TEXT("KNOWN_TYPE|%s|NOT_REGISTERED"), Name));
				continue;
			}
			++NumTypes;
			Lines.Add(FString::Printf(TEXT("KNOWN_TYPE|%s|REGISTERED|%s|%s"), Name,
				Type->GetShaderFilename() ? Type->GetShaderFilename() : TEXT("?"),
				Type->GetFunctionName() ? Type->GetFunctionName() : TEXT("?")));
		}

		// Every shader type whose source file belongs to the plugin.
		const TArray<const FShaderType*> PluginTypes = FShaderType::GetShaderTypesByFilenameFilter(
			[](const TCHAR* Filename)
			{
				return Filename != nullptr && (FCString::Strstr(Filename, TEXT("PICOShaders")) != nullptr
					|| FCString::Strstr(Filename, TEXT("/Plugin/PICOOpenXR")) != nullptr);
			});
		Lines.Add(FString::Printf(TEXT("PLUGIN_SHADER_TYPES|%d"), PluginTypes.Num()));
		for (const FShaderType* Type : PluginTypes)
		{
			Lines.Add(FString::Printf(TEXT("STYPE|%s|%s|%s"), Type->GetName(),
				Type->GetShaderFilename() ? Type->GetShaderFilename() : TEXT("?"),
				Type->GetFunctionName() ? Type->GetFunctionName() : TEXT("?")));
		}

		// Resolve them against the global shader map of the RHI that is actually running.
		const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		if (ShaderMap == nullptr)
		{
			Lines.Add(TEXT("GLOBAL_SHADERMAP|null"));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("GLOBAL_SHADERMAP|ok|featurelevel=%d|platform=%d"),
				(int32)FeatureLevel, (int32)GMaxRHIShaderPlatform));

			for (const TCHAR* Name : KnownShaderTypes)
			{
				FShaderType* Type = FindShaderTypeByName(FHashedName(Name));
				if (Type == nullptr) { continue; }
				const bool bHas = ShaderMap->HasShader(Type, 0);
				if (bHas) { ++NumCompiled; } else { ++NumMissing; }
				Lines.Add(FString::Printf(TEXT("SHADER|%s|%s"), Name, bHas ? TEXT("COMPILED") : TEXT("MISSING")));
			}
			for (const FShaderType* ConstType : PluginTypes)
			{
				FShaderType* Type = const_cast<FShaderType*>(ConstType);
				const bool bHas = ShaderMap->HasShader(Type, 0);
				if (bHas) { ++NumCompiled; } else { ++NumMissing; }
				Lines.Add(FString::Printf(TEXT("SHADER|%s|%s"), Type->GetName(), bHas ? TEXT("COMPILED") : TEXT("MISSING")));
			}
		}

		Lines.Add(FString::Printf(TEXT("SUMMARY|known=%d|unregistered=%d|compiled=%d|missing=%d"),
			NumTypes, NumUnregistered, NumCompiled, NumMissing));

		const FString Path = Args.Num() > 0 ? Args[0] : (FPaths::ProjectSavedDir() / TEXT("PICOShaderCheck.txt"));
		const bool bSaved = FFileHelper::SaveStringToFile(FString::Join(Lines, TEXT("\n")) + TEXT("\n"), *Path);
		UE_LOG(LogTemp, Display, TEXT("PICOShaderCheck: known=%d compiled=%d missing=%d saved=%d -> %s"),
			NumTypes, NumCompiled, NumMissing, bSaved ? 1 : 0, *Path);
	}
}

void FPICOValidateModule::StartupModule()
{
	DumpSurfaceCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PICODumpReflectionSurface"),
		TEXT("Dump every reflected /Script/PICO* type to a text file. Usage: PICODumpReflectionSurface [OutPath]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PICOSurfaceDump::DumpSurface));

	ShaderCheckCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("PICOShaderCheck"),
		TEXT("Resolve the PICO plugin's global shaders against the active RHI. Usage: PICOShaderCheck [OutPath]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&PICOShaderCheck::Dump));
}

void FPICOValidateModule::ShutdownModule()
{
	if (DumpSurfaceCommand) { IConsoleManager::Get().UnregisterConsoleObject(DumpSurfaceCommand); DumpSurfaceCommand = nullptr; }
	if (ShaderCheckCommand) { IConsoleManager::Get().UnregisterConsoleObject(ShaderCheckCommand); ShaderCheckCommand = nullptr; }
}

IMPLEMENT_MODULE(FPICOValidateModule, PICOValidate);
