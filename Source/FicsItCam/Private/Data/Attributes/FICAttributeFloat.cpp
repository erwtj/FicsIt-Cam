#include "FicsItCam/Public/Data/Attributes/FICAttributeFloat.h"

#include "Editor/Data/FICEditorAttributeBase.h"
#include "FicsItCam/Public/FICUtils.h"

TMap<FICFrame, TSharedRef<FFICKeyframe>> FFICFloatAttribute::GetKeyframes() {
	TMap<FICFrame, TSharedRef<FFICKeyframe>> OutKeyframes;
	for (const TTuple<FICFrame, FFICFloatKeyframe>& Keyframe : Keyframes) OutKeyframes.Add(Keyframe.Key, MakeShared<FFICFloatKeyframeTrampoline>(this, Keyframe.Key));
	return OutKeyframes;
}

EFICKeyframeType FFICFloatAttribute::GetAllowedKeyframeTypes() const {
	return FIC_KF_ALL;
}

TSharedRef<FFICKeyframe> FFICFloatAttribute::AddKeyframe(FICFrame Time) {
	if (Keyframes.Contains(Time)) return MakeShared<FFICFloatKeyframeTrampoline>(this, Time);
	FFICFloatKeyframe Keyframe;
	Keyframe.KeyframeType = FIC_KF_EASE;
	Keyframe.Value = GetValue(Time);
	SetKeyframe(Time, Keyframe);
	return MakeShared<FFICFloatKeyframeTrampoline>(this, Time);
}

void FFICFloatAttribute::RemoveKeyframe(FICFrame Time) {
	Keyframes.Remove(Time);
	OnUpdateBroadcast();
}

void FFICFloatAttribute::MoveKeyframe(FICFrame From, FICFrame To) {
	if (From == To) return;
	FFICFloatKeyframe* FromKeyframe = Keyframes.Find(From);
	if (!FromKeyframe) return;
	SetKeyframe(To, *FromKeyframe);
	RemoveKeyframe(From);
}

void FFICFloatAttribute::RecalculateKeyframe(FICFrame Time) {
	FFICFloatKeyframe* CurrentKeyframe = Keyframes.Find(Time);
	if (!CurrentKeyframe) return;

	RecalculateKeyframe(Time, CurrentKeyframe);

	OnUpdateBroadcast();
}

void FFICFloatAttribute::RecalculateAllKeyframes() {
	Keyframes.KeySort([](const FICFrame& A, const FICFrame& B) { return A < B; });

	for (size_t i = 0; i < Keyframes.Num(); ++i) {
		auto& [frame, keyframe] = Keyframes.Get(FSetElementId::FromInteger(i));
		if (i > 0) {
			const auto& [prevFrame, prevKeyframe] = Keyframes.Get(FSetElementId::FromInteger(i-1));
			if (i+1 < Keyframes.Num()) {
				const auto& [nextFrame, nextKeyframe] = Keyframes.Get(FSetElementId::FromInteger(i+1));
				RecalculateKeyframe(frame, &keyframe, prevFrame, prevKeyframe.Value, nextFrame, nextKeyframe.Value);
			} else {
				RecalculateKeyframe(frame, &keyframe, prevFrame, prevKeyframe.Value, 0, {});
			}
		} else {
			if (i+1 < Keyframes.Num()) {
				const auto& [nextFrame, nextKeyframe] = Keyframes.Get(FSetElementId::FromInteger(i+1));
				RecalculateKeyframe(frame, &keyframe, 0, {}, nextFrame, nextKeyframe.Value);
			} else {
				RecalculateKeyframe(frame, &keyframe, 0, {}, 0, {});
			}
		}
	}

	OnUpdateBroadcast();
}

void FFICFloatAttribute::RecalculateKeyframe(FICFrame Time, FFICFloatKeyframe* Keyframe) {
	FICFrame PTime;
	TOptional<float> PK;
	if (auto keyframe = StaticCastSharedPtr<FFICFloatKeyframeTrampoline>(GetPrevKeyframe(Time, PTime))) {
		if (auto kf = keyframe.Get()->GetKeyframe()) {
			PK = kf->Value;
		}
	}
	int64 NTime;
	TOptional<float> NK;
	if (auto keyframe = StaticCastSharedPtr<FFICFloatKeyframeTrampoline>(GetNextKeyframe(Time, NTime))) {
		if (auto kf = keyframe.Get()->GetKeyframe()) {
			NK = kf->Value;
		}
	}

	RecalculateKeyframe(Time, Keyframe, PTime, PK, NTime, NK);
}

void FFICFloatAttribute::RecalculateKeyframe(FICFrame Time, FFICFloatKeyframe* CurrentKeyframe, FICFrame PTime, TOptional<float> PK, FICFrame NTime, TOptional<float> NK) {
	if (CurrentKeyframe->KeyframeType & (FIC_KF_CUSTOM | FIC_KF_LINEAR | FIC_KF_MIRROR | FIC_KF_STEP) & ~FIC_KF_HANDLES) return;
	float Factor = 1.0/3.0;
	//Factor = 0.5;
	if (PK) {
		float PKval = *PK;
		float PKTimeDiff = Time - PTime;
		float PKValueDiff = CurrentKeyframe->Value - PKval;
		if (NK) {
			float NKval = *NK;
			float NKTimeDiff = NTime - Time;
			float NKValueDiff = NKval - CurrentKeyframe->Value;
			float KValueDiff = (PKValueDiff + NKValueDiff) / 2.0;
			float KTimeDiff = NTime - PTime;

			//FMath::Clamp(K->Value + PKValueDiff, FMath::Min(PK->Value, NK->Value), FMath::Max(PK->Value, NK->Value)) - K->Value;

			CurrentKeyframe->OutTanTime = NKTimeDiff * Factor;
			CurrentKeyframe->InTanTime = PKTimeDiff * Factor;

			float Ratio = PKTimeDiff / KTimeDiff;
			float Pitch = (PKValueDiff/PKTimeDiff) * (1-Ratio) + (NKValueDiff/NKTimeDiff) * Ratio;
			
			if (CurrentKeyframe->KeyframeType == FIC_KF_EASE) {
				if ((CurrentKeyframe->Value < PKval && CurrentKeyframe->Value < NKval) || (CurrentKeyframe->Value > PKval && CurrentKeyframe->Value > NKval)) {
					CurrentKeyframe->InTanValue = CurrentKeyframe->OutTanValue = 0;
				} else {
					CurrentKeyframe->InTanValue = FMath::Clamp(CurrentKeyframe->Value - Pitch * CurrentKeyframe->InTanTime, FMath::Min(PKval, NKval), FMath::Max(PKval, NKval)) - CurrentKeyframe->Value;
					CurrentKeyframe->OutTanValue = FMath::Clamp(CurrentKeyframe->Value + Pitch * CurrentKeyframe->OutTanTime, FMath::Min(PKval, NKval), FMath::Max(PKval, NKval)) - CurrentKeyframe->Value;
					float InPitch = -CurrentKeyframe->InTanValue/CurrentKeyframe->InTanTime;
					float OutPitch = CurrentKeyframe->OutTanValue/CurrentKeyframe->OutTanTime;
					Pitch = FMath::Abs(InPitch) < FMath::Abs(OutPitch) ? InPitch : OutPitch;
					CurrentKeyframe->InTanValue = Pitch * CurrentKeyframe->InTanTime;
					CurrentKeyframe->OutTanValue = Pitch * CurrentKeyframe->OutTanTime;
				}
			} else if (CurrentKeyframe->KeyframeType == FIC_KF_EASEINOUT) {
				CurrentKeyframe->OutTanValue = 0;
				CurrentKeyframe->InTanValue = 0;
			}
		} else {
			if (CurrentKeyframe->KeyframeType == FIC_KF_EASE) {
				CurrentKeyframe->InTanTime = CurrentKeyframe->InTanValue = 0;
			} else if (CurrentKeyframe->KeyframeType == FIC_KF_EASEINOUT) {
				CurrentKeyframe->InTanTime = PKTimeDiff * Factor;
				CurrentKeyframe->InTanValue = 0;
			}
		}
	} else {
		if (NK) {
			float NKval = *NK;
			float NKTimeDiff = NTime - Time;
			float NKValueDiff = NKval - CurrentKeyframe->Value;
		
			if (CurrentKeyframe->KeyframeType == FIC_KF_EASE) {
				CurrentKeyframe->OutTanTime = CurrentKeyframe->OutTanValue = 0;
			} else if (CurrentKeyframe->KeyframeType == FIC_KF_EASEINOUT) {
				CurrentKeyframe->OutTanTime = NKTimeDiff * Factor;
				CurrentKeyframe->OutTanValue = 0;
			}
		}
	}
}

FICValue FFICFloatAttribute::GetFloatValue(FICFrameFloat Time) {
	return GetValue(Time);
}

bool FFICFloatAttribute::HasKeyframe(FICFrame Time) const {
	return Keyframes.Contains(Time);
}

void FFICFloatAttribute::CopyFrom(TSharedRef<FFICAttribute> InAttrib) {
	FOnUpdate OnUpdateBuf = OnUpdate;
	if (InAttrib->GetAttributeType() == GetAttributeType()) {
		*this = *StaticCastSharedRef<FFICFloatAttribute>(InAttrib);
	}
	OnUpdate = OnUpdateBuf;
	OnUpdateBroadcast();
}

TSharedRef<FFICAttribute> FFICFloatAttribute::CreateCopy() {
	return MakeShared<FFICFloatAttribute>(*this);
}

TSharedRef<FFICEditorAttributeBase> FFICFloatAttribute::CreateEditorAttribute() {
	return MakeShared<TFICEditorAttribute<FFICFloatAttribute>>(*this);
}

FFICFloatKeyframe* FFICFloatAttribute::SetKeyframe(FICFrame Time, FFICFloatKeyframe Keyframe) {
	FFICFloatKeyframe* KF = &Keyframes.FindOrAdd(Time);
	*KF = Keyframe;
	OnUpdateBroadcast();
	return KF;
}

float FFICFloatAttribute::GetValue(FICFrameFloat Time) {
	FICFrameFloat Time1 = TNumericLimits<FICFrameFloat>::Lowest();
	FFICFloatKeyframe KF1;
	FICFrameFloat Time2 = TNumericLimits<FICFrameFloat>::Max();
	FFICFloatKeyframe KF2;
	for (const TPair<FICFrame, FFICFloatKeyframe>& Keyframe : Keyframes) {
		if (Keyframe.Key <= Time) {
			if (Time1 < Keyframe.Key) {
				Time1 = Keyframe.Key;
				KF1 = Keyframe.Value;
			}
		} else if (Keyframe.Key > Time) {
			if (Time2 > Keyframe.Key) {
				Time2 = Keyframe.Key;
				KF2 = Keyframe.Value;
			}
		}
	}

	float Interpolated = FallBackValue;
	if (Time1 > TNumericLimits<float>::Lowest()) {
		if (Time2 < TNumericLimits<float>::Max()) {
			float Factor = (Time - Time1) / (Time2 - Time1);
			if (KF1.KeyframeType == FIC_KF_STEP) {
				Interpolated = KF1.Value;
			} else if (KF1.KeyframeType == FIC_KF_LINEAR) {
				Interpolated = FMath::Lerp(KF1.Value, KF2.Value, Factor);
			} else {
				Interpolated = UFICUtils::BezierInterpolate({Time1, KF1.Value}, {Time1 + KF1.OutTanTime, KF1.Value + KF1.OutTanValue},
                 {Time2 - KF2.InTanTime, KF2.Value - KF2.InTanValue}, {Time2, KF2.Value}, Time);
			}
		} else {
			return KF1.Value;
		}
	} else {
		if (Time2 < TNumericLimits<float>::Max()) {
			return KF2.Value;
		}
	}
	return Interpolated;
}