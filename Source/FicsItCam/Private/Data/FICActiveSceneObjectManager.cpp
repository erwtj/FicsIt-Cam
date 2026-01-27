#include "Data/FICActiveSceneObjectManager.h"

void FFICActiveSceneObjectManager::Initialize(AFICScene* InScene) {
	Scene = InScene;
}

void FFICActiveSceneObjectManager::UpdateActiveObjects(FICFrameFloat Frame) {
	if (!Scene) return;
	TMap<FString, UObject*> Active;
	if (Scene) for (UObject* SceneObject : Scene->GetSceneObjects()) {
		IFICSceneObjectActive* SceneObjectActive = Cast<IFICSceneObjectActive>(SceneObject);
		if (!SceneObjectActive) continue;
		FString Type = SceneObjectActive->GetActiveType();
		if (!Active.Contains(Type) && IsSceneObjectActive.IsBound()) {
			if (IsSceneObjectActive.Execute(SceneObject, Frame)) {
				Active.Add(Type, SceneObject);
				UObject** CurrentActive = ActiveSceneObjects.Find(Type);
				if (!CurrentActive) {
					SceneObjectActive->Activate();
				} else if (*CurrentActive != SceneObject) {
					Cast<IFICSceneObjectActive>(*CurrentActive)->Deactivate();
					SceneObjectActive->Activate();
				}
			}
		}
	}
	for (const TPair<FString, UObject*>& ActiveSceneObject : ActiveSceneObjects) {
		if (!Active.Contains(ActiveSceneObject.Key)) {
			IFICSceneObjectActive* active = Cast<IFICSceneObjectActive>(ActiveSceneObject.Value);
			if (active) active->Deactivate();
		}
	}
	ActiveSceneObjects = Active;
}

void FFICActiveSceneObjectManager::Shutdown() {
	for (const TPair<FString, UObject*>& Object : ActiveSceneObjects) {
		Cast<IFICSceneObjectActive>(Object.Value)->Deactivate();
	}
	ActiveSceneObjects.Empty();
}
