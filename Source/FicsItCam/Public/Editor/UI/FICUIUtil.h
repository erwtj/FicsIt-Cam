#pragma once

#include "MultiBoxBuilder.h"
#include "Editor/FICEditorContext.h"

FMenuBuilder FICCreateKeyframeTypeChangeMenu(UFICEditorContext* Context, TFunction<TSet<TPair<FFICAttribute*, FICFrame>>()> Keyframes);
TOptional<FString> FICSaveSceneFileDialog(const FString& SceneName);
TOptional<FString> FICOpenSceneFileDialog();