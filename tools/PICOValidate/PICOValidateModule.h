#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

struct IConsoleCommand;

class FPICOValidateModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	IConsoleCommand* DumpSurfaceCommand = nullptr;
	IConsoleCommand* ShaderCheckCommand = nullptr;
};
