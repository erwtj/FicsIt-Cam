#include "Command/Scene/FICCommandImport.h"

#include "Data/FICScene.h"
#include "Data/Objects/FICCamera.h"
#include "Dom/JsonObject.h"
#include "FICUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace {
	bool TryGetInt64Field(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int64& OutValue) {
		double Tmp = 0.0;
		if (!Object->TryGetNumberField(FieldName, Tmp)) return false;
		OutValue = FMath::RoundToInt64(Tmp);
		return true;
	}

	EFICKeyframeType ParseKeyframeType(const FString& InType) {
		if (InType.Equals(TEXT("linear"), ESearchCase::IgnoreCase)) return FIC_KF_LINEAR;
		if (InType.Equals(TEXT("step"), ESearchCase::IgnoreCase)) return FIC_KF_STEP;
		if (InType.Equals(TEXT("easeinout"), ESearchCase::IgnoreCase) || InType.Equals(TEXT("ease_in_out"), ESearchCase::IgnoreCase)) return FIC_KF_EASEINOUT;
		if (InType.Equals(TEXT("mirror"), ESearchCase::IgnoreCase)) return FIC_KF_MIRROR;
		if (InType.Equals(TEXT("custom"), ESearchCase::IgnoreCase)) return FIC_KF_CUSTOM;
		return FIC_KF_EASE;
	}

	void ClearFloatAttribute(FFICFloatAttribute& Attribute) {
		TArray<FICFrame> Frames;
		for (const TPair<FICFrame, TSharedRef<FFICKeyframe>>& Pair : Attribute.GetKeyframes()) {
			Frames.Add(Pair.Key);
		}
		for (FICFrame Frame : Frames) {
			Attribute.RemoveKeyframe(Frame);
		}
	}

	bool ImportFloatChannel(const TSharedPtr<FJsonObject>& KeyframesObject, const TCHAR* FieldName, FFICFloatAttribute& Attribute, FString& OutError) {
		const TArray<TSharedPtr<FJsonValue>>* JsonKeyframes = nullptr;
		if (!KeyframesObject->TryGetArrayField(FieldName, JsonKeyframes)) {
			return true;
		}

		ClearFloatAttribute(Attribute);
		if (JsonKeyframes->Num() < 1) return true;

		TArray<TPair<FICFrame, double>> Defaults;
		for (const TSharedPtr<FJsonValue>& JsonValue : *JsonKeyframes) {
			const TSharedPtr<FJsonObject>* KeyframeObject = nullptr;
			if (!JsonValue->TryGetObject(KeyframeObject)) {
				OutError = FString::Printf(TEXT("Channel '%s' contains a non-object keyframe."), FieldName);
				return false;
			}

			int64 Frame = 0;
			double Value = 0.0;
			if (!TryGetInt64Field(*KeyframeObject, TEXT("frame"), Frame) || !(*KeyframeObject)->TryGetNumberField(TEXT("value"), Value)) {
				OutError = FString::Printf(TEXT("Channel '%s' has keyframe missing 'frame' or 'value'."), FieldName);
				return false;
			}

			FFICFloatKeyframe Keyframe;
			Keyframe.Value = Value;

			FString TypeName;
			if ((*KeyframeObject)->TryGetStringField(TEXT("type"), TypeName)) {
				Keyframe.KeyframeType = ParseKeyframeType(TypeName);
			}

			double InTanValue = 0.0;
			double InTanTime = 0.0;
			double OutTanValue = 0.0;
			double OutTanTime = 0.0;
			if ((*KeyframeObject)->TryGetNumberField(TEXT("in_tan_value"), InTanValue)) Keyframe.InTanValue = InTanValue;
			if ((*KeyframeObject)->TryGetNumberField(TEXT("in_tan_time"), InTanTime)) Keyframe.InTanTime = InTanTime;
			if ((*KeyframeObject)->TryGetNumberField(TEXT("out_tan_value"), OutTanValue)) Keyframe.OutTanValue = OutTanValue;
			if ((*KeyframeObject)->TryGetNumberField(TEXT("out_tan_time"), OutTanTime)) Keyframe.OutTanTime = OutTanTime;

			Attribute.SetKeyframe(Frame, Keyframe);
			Defaults.Add({Frame, Value});
		}

		Defaults.Sort([](const TPair<FICFrame, double>& A, const TPair<FICFrame, double>& B) {
			return A.Key < B.Key;
		});
		Attribute.SetDefaultValue(Defaults[0].Value);
		Attribute.RecalculateAllKeyframes();
		return true;
	}

	bool TryGetJsonObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field, const TSharedPtr<FJsonObject>*& OutObject) {
		return Parent->TryGetObjectField(Field, OutObject);
	}
}

EExecutionStatus UFICCommandImport::ExecuteCommand(UCommandSender* InSender, TArray<FString> InArgs) {
	CheckArgCount(2)

	const FString SceneName = InArgs[0];
	if (!UFICUtils::IsValidFICObjectName(SceneName)) {
		InSender->SendChatMessage(FString::Printf(TEXT("'%s' is no valid scene name!"), *SceneName), FColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	AFICSubsystem* SubSys = AFICSubsystem::GetFICSubsystem(InSender);
	if (SubSys->FindSceneByName(SceneName)) {
		InSender->SendChatMessage(FString::Printf(TEXT("Scene '%s' already exists!"), *SceneName), FColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	FString Path;
	for (int32 ArgIndex = 1; ArgIndex < InArgs.Num(); ++ArgIndex) {
		if (!Path.IsEmpty()) Path += TEXT(" ");
		Path += InArgs[ArgIndex];
	}
	FPaths::NormalizeFilename(Path);
	if (!FPaths::FileExists(Path)) {
		InSender->SendChatMessage(FString::Printf(TEXT("File not found: %s"), *Path), FColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *Path)) {
		InSender->SendChatMessage(FString::Printf(TEXT("Unable to read file: %s"), *Path), FColor::Red);
		return EExecutionStatus::UNCOMPLETED;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) {
		InSender->SendChatMessage(TEXT("Invalid JSON in .ficjson file."), FColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}

	AFICScene* Scene = SubSys->CreateScene(SceneName);
	if (!Scene) {
		InSender->SendChatMessage(FString::Printf(TEXT("Failed to create scene '%s'."), *SceneName), FColor::Red);
		return EExecutionStatus::UNCOMPLETED;
	}

	auto AbortImport = [InSender, SubSys, Scene](const FString& Message, EExecutionStatus Status) {
		SubSys->DeleteScene(Scene);
		InSender->SendChatMessage(Message, FColor::Red);
		return Status;
	};

	UFICCamera* Camera = nullptr;
	for (UObject* SceneObject : Scene->GetSceneObjects()) {
		if (UFICCamera* SceneCamera = Cast<UFICCamera>(SceneObject)) {
			Camera = SceneCamera;
			break;
		}
	}

	if (!Camera) {
		return AbortImport(TEXT("Created scene has no camera object."), EExecutionStatus::UNCOMPLETED);
	}

	const TSharedPtr<FJsonObject>* SceneObject = nullptr;
	if (Root->TryGetObjectField(TEXT("scene"), SceneObject)) {
		const TSharedPtr<FJsonObject>* RangeObject = nullptr;
		if (TryGetJsonObject(*SceneObject, TEXT("animation_range"), RangeObject)) {
			int64 Begin = Scene->AnimationRange.Begin;
			int64 End = Scene->AnimationRange.End;
			TryGetInt64Field(*RangeObject, TEXT("begin"), Begin);
			TryGetInt64Field(*RangeObject, TEXT("end"), End);
			Scene->AnimationRange.SetRange(Begin, End);
		}

		int64 FPS = Scene->FPS;
		if (TryGetInt64Field(*SceneObject, TEXT("fps"), FPS)) Scene->FPS = FMath::Max<int64>(1, FPS);

		const TSharedPtr<FJsonObject>* ResolutionObject = nullptr;
		if (TryGetJsonObject(*SceneObject, TEXT("resolution"), ResolutionObject)) {
			int64 Width = Scene->ResolutionWidth;
			int64 Height = Scene->ResolutionHeight;
			if (TryGetInt64Field(*ResolutionObject, TEXT("width"), Width)) Scene->ResolutionWidth = FMath::Max<int64>(1, Width);
			if (TryGetInt64Field(*ResolutionObject, TEXT("height"), Height)) Scene->ResolutionHeight = FMath::Max<int64>(1, Height);
		}

		const TSharedPtr<FJsonObject>* SensorObject = nullptr;
		if (TryGetJsonObject(*SceneObject, TEXT("sensor_dimension"), SensorObject)) {
			double SensorWidth = Scene->SensorDimension.X;
			double SensorHeight = Scene->SensorDimension.Y;
			if ((*SensorObject)->TryGetNumberField(TEXT("width"), SensorWidth)) Scene->SensorDimension.X = FMath::Max(0.0, SensorWidth);
			if ((*SensorObject)->TryGetNumberField(TEXT("height"), SensorHeight)) Scene->SensorDimension.Y = FMath::Max(0.0, SensorHeight);
		}

		bool UseCinematic = Scene->bUseCinematic;
		bool BulletTime = Scene->bBulletTime;
		bool Looping = Scene->bLooping;
		if ((*SceneObject)->TryGetBoolField(TEXT("use_cinematic"), UseCinematic)) Scene->bUseCinematic = UseCinematic;
		if ((*SceneObject)->TryGetBoolField(TEXT("bullet_time"), BulletTime)) Scene->bBulletTime = BulletTime;
		if ((*SceneObject)->TryGetBoolField(TEXT("looping"), Looping)) Scene->bLooping = Looping;
	}

	const TSharedPtr<FJsonObject>* CameraObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("camera"), CameraObject)) {
		return AbortImport(TEXT("Missing 'camera' object in .ficjson."), EExecutionStatus::BAD_ARGUMENTS);
	}

	FString CameraName;
	if ((*CameraObject)->TryGetStringField(TEXT("name"), CameraName) && !CameraName.IsEmpty()) {
		Camera->SceneObjectName = CameraName;
	}

	bool ActiveDefault = true;
	if ((*CameraObject)->TryGetBoolField(TEXT("active_default"), ActiveDefault)) {
		Camera->Active.SetDefaultValue(ActiveDefault);
	}

	const TSharedPtr<FJsonObject>* KeyframesObject = nullptr;
	if (!(*CameraObject)->TryGetObjectField(TEXT("keyframes"), KeyframesObject)) {
		return AbortImport(TEXT("Missing 'camera.keyframes' object in .ficjson."), EExecutionStatus::BAD_ARGUMENTS);
	}

	FString ImportError;
	if (!ImportFloatChannel(*KeyframesObject, TEXT("pos_x"), Camera->Position.X, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("pos_y"), Camera->Position.Y, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("pos_z"), Camera->Position.Z, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("rot_pitch"), Camera->Rotation.Pitch, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("rot_yaw"), Camera->Rotation.Yaw, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("rot_roll"), Camera->Rotation.Roll, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("fov"), Camera->FOV, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("aperture"), Camera->Aperture, ImportError) ||
		!ImportFloatChannel(*KeyframesObject, TEXT("focus_distance"), Camera->FocusDistance, ImportError)) {
		return AbortImport(FString::Printf(TEXT("Import failed: %s"), *ImportError), EExecutionStatus::BAD_ARGUMENTS);
	}

	InSender->SendChatMessage(FString::Printf(TEXT("Imported scene '%s' from %s"), *SceneName, *Path), FColor::Green);
	return EExecutionStatus::COMPLETED;
}
