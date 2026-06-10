#pragma once

#include "Command/FICCommand.h"
#include "FICCommandImport.generated.h"

UCLASS()
class UFICCommandImport : public UFICCommand {
	GENERATED_BODY()
public:
	UFICCommandImport() {
		bFinal = true;
		CommandName = TEXT("import");
		CommandSyntax = TEXT("/fic import <scene name> <path to .ficjson>");
	}

	virtual EExecutionStatus ExecuteCommand(UCommandSender* InSender, TArray<FString> InArgs) override;
};
