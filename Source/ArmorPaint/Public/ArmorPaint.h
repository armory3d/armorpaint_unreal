// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UTexture2D;

// Paints the selected objects in ArmorPaint and brings the painted textures back.
// The selection gets a project at <Project>/ArmorPaint/<Name>/<Name>.arm, starting from the objects'
// current materials. Objects painted together share one texture set, their UV maps should not overlap.
// When ArmorPaint closes, textures are imported into /Game/ArmorPaint/<Name> and applied through a material.
class FArmorPaintModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	struct FPending
	{
		FProcHandle Process;
		FString Name;
		FString ArmPath;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Components;
	};

	struct FPaintTarget
	{
		FString Name;
		UStaticMeshComponent* Component;
	};

	void PluginButtonClicked();
	void UnpaintButtonClicked();
	TSharedRef<class SWidget> MakeToolbarMenu();
	void RegisterMenus();
	bool Tick(float DeltaTime);
	void OnApplicationActivationStateChanged(bool bActive);

	FString GetExecutable(bool bPrompt);
	bool StartArmorPaint(const FString& Exe, const FString& Args, bool bHidden, FProcHandle& OutProcess);
	bool ExportMeshesToObj(const TArray<FPaintTarget>& Targets, const FString& ObjPath);
	bool BakeTextures(const TArray<FPaintTarget>& Targets, const FString& Dir, TMap<FString, FString>& OutPaths);
	FString LinkScript(const FString& ArmPath, const TMap<FString, FString>& Baked);
	void SaveOriginalMaterials(const FString& Name, const TArray<FPaintTarget>& Targets);
	void RestoreOriginalMaterials(const FString& Name, const TArray<UStaticMeshComponent*>& Components);
	bool ExportTextures(const FString& ArmPath);
	bool ImportTextures(const FString& Name, TMap<FString, UTexture2D*>& OutTextures);
	UMaterialInterface* GetOrCreateMaterial(const FString& Name, const TMap<FString, UTexture2D*>& Textures);
	void ApplyMaterial(const TArray<UStaticMeshComponent*>& Components, UMaterialInterface* Material);
	void UpdateProject(const FString& Name, const TArray<UStaticMeshComponent*>& Components);

	TSharedPtr<class FUICommandList> PluginCommands;
	TArray<FPending> Pending;
	FTSTicker::FDelegateHandle TickerHandle;
	FDelegateHandle ActivationHandle;
	bool bUpdating = false;
};
