// ArmorPaint plugin for UE4
// Based on plugin template, Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ArmorPaint.h"
#include "ArmorPaintStyle.h"
#include "ArmorPaintCommands.h"
#include "ArmorPaintSettings.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Engine/Selection.h"
#include "Templates/Casts.h"
#include "Interfaces/IPluginManager.h"
#include "Runtime/Engine/Classes/EditorFramework/AssetImportData.h"
#include "MeshDescriptionOperations.h"
#include "MeshDescription.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"

#include "LevelEditor.h"

static const FName ArmorPaintTabName("ArmorPaint");

#define LOCTEXT_NAMESPACE "FArmorPaintModule"

void RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "ArmorPaint",
			LOCTEXT("RuntimeSettingsName", "ArmorPaint"),
			LOCTEXT("RuntimeSettingsDescription", "Configure ArmorPaint settings"),
			GetMutableDefault<UArmorPaintSettings>()
		);
	}
}

void FArmorPaintModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	RegisterSettings();

	FArmorPaintStyle::Initialize();
	FArmorPaintStyle::ReloadTextures();

	FArmorPaintCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FArmorPaintCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FArmorPaintModule::PluginButtonClicked),
		FCanExecuteAction());

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FArmorPaintModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FArmorPaintModule::AddToolbarExtension));

		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}
}

void FArmorPaintModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FArmorPaintStyle::Shutdown();

	FArmorPaintCommands::Unregister();
}

void FArmorPaintModule::PluginButtonClicked()
{
	/*FString OutFolderName;
	void* ParentWindowWindowHandle = nullptr;
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
	if (MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid())
	{
		ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
	}
	FDesktopPlatformModule::Get()->OpenDirectoryDialog(ParentWindowWindowHandle, TEXT("Select folder where ArmorPaint is located"), TEXT(""), OutFolderName);*/

	FString OutFolderName = GetDefault<UArmorPaintSettings>()->ArmorPaintPath.Path;

	FString path_base = OutFolderName;
	FString path_tmp = path_base / TEXT("data") / TEXT("mesh_tmp.obj");
	FString path_exe = path_base / TEXT("ArmorPaint.exe");
	FString path_game = FPaths::ProjectContentDir().LeftChop(1);
	path_base = path_base.Replace(TEXT("/"), TEXT("\\"));
	path_tmp = path_tmp.Replace(TEXT("/"), TEXT("\\"));
	path_exe = path_exe.Replace(TEXT("/"), TEXT("\\"));

	if (!FPaths::FileExists(path_exe)) {
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Please configure folder where ArmorPaint is located at 'Project Settings - Plugins - ArmorPaint'")));
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	AActor* Actor = Cast<AActor>(SelectedActors->GetSelectedObject(0));

	if (!Actor) return;

	TArray<UStaticMeshComponent*> Components;
	Actor->GetComponents<UStaticMeshComponent>(Components);
	UStaticMeshComponent* StaticMeshComponent = Components[0];
	UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

	// Open source file
	const FAssetImportInfo& AssetImportInfo = StaticMesh->AssetImportData->SourceData;
	bool found = false;
	if (AssetImportInfo.SourceFiles.Num() > 0) {
		// Ensure absolute path
		FString SourceFilePath = AssetImportInfo.SourceFiles[0].RelativeFilename;
		SourceFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*SourceFilePath);
		SourceFilePath = SourceFilePath.Replace(TEXT("/"), TEXT("\\"));
		if (FPaths::FileExists(SourceFilePath)) {
			FString params = TEXT("\"") + SourceFilePath + TEXT("\"") + TEXT(" ") + TEXT("\"") + path_game + TEXT("\"");// +TEXT(" ") + TEXT("m_paint_plane");
			FPlatformProcess::CreateProc(*path_exe, *params, true, false, false, nullptr, 0, nullptr, nullptr);
			found = true;
		}
	}

	if (!found)
	{
		// UE4 obj exporter
		{
			// Currently, we only export LOD 0 of the static mesh. In the future, we could potentially export all available LODs
			const FStaticMeshLODResources& RenderData = StaticMesh->GetLODForExport(0);
			uint32 Count = RenderData.GetNumTriangles();

			// Create a new filename for the lightmap coordinate OBJ file (by adding "_UV1" to the end of the filename)
			FString Filename = path_tmp;

			// Open a second archive here so we can export lightmap coordinates at the same time we export the regular mesh
			FArchive* File = IFileManager::Get().CreateFileWriter(*Filename);

			TArray<FVector> Verts;				// The verts in the mesh
			TArray<FVector2D> UVs;				// UV coords from channel 0
			TArray<FVector> Normals;			// Normals
			TArray<uint32> SmoothingMasks;		// Complete collection of the smoothing groups from all triangles
			TArray<uint32> UniqueSmoothingMasks;	// Collection of the unique smoothing groups (used when writing out the face info into the OBJ file so we can group by smoothing group)

			File->Logf(TEXT("# UnrealEd OBJ exporter\r\n"));

			FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
			if (MeshDescription != nullptr)
			{
				uint32 TriangleCount = MeshDescription->Triangles().Num();
				if (Count == TriangleCount)
				{
					SmoothingMasks.AddZeroed(TriangleCount);

					// FStaticMeshOperations::ConvertHardEdgesToSmoothGroup(*MeshDescription, SmoothingMasks);
					FMeshDescriptionOperations::ConvertHardEdgesToSmoothGroup(*MeshDescription, SmoothingMasks);
					for (uint32 SmoothValue : SmoothingMasks)
					{
						UniqueSmoothingMasks.AddUnique(SmoothValue);
					}
				}
			}

			//This can happen in case the base LOD is reduce or the recompute normals has generate a different number of cases
			//the count will be lower and in such a case the smooth group of the original mesh description cannot be used since it is out of sync with the render data.
			if (SmoothingMasks.Num() != Count)
			{
				//Create one SmoothGroup with 0 values
				SmoothingMasks.Empty(Count);
				SmoothingMasks.AddZeroed(Count);
				UniqueSmoothingMasks.Empty(1);
				UniqueSmoothingMasks.Add(0);
			}

			// Collect all the data about the mesh
			Verts.Reserve(3 * Count);
			UVs.Reserve(3 * Count);
			Normals.Reserve(3 * Count);

			FIndexArrayView Indices = RenderData.IndexBuffer.GetArrayView();

			for (uint32 tri = 0; tri < Count; tri++)
			{
				uint32 Index1 = Indices[(tri * 3) + 0];
				uint32 Index2 = Indices[(tri * 3) + 1];
				uint32 Index3 = Indices[(tri * 3) + 2];

				FVector Vertex1 = RenderData.VertexBuffers.PositionVertexBuffer.VertexPosition(Index1);		//(FStaticMeshFullVertex*)(RawVertexData + Index1 * VertexStride);
				FVector Vertex2 = RenderData.VertexBuffers.PositionVertexBuffer.VertexPosition(Index2);
				FVector Vertex3 = RenderData.VertexBuffers.PositionVertexBuffer.VertexPosition(Index3);

				// Vertices
				Verts.Add(Vertex1);
				Verts.Add(Vertex2);
				Verts.Add(Vertex3);

				// UVs from channel 0
				UVs.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, 0));
				UVs.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, 0));
				UVs.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index3, 0));

				// Normals
				Normals.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Index1));
				Normals.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Index2));
				Normals.Add(RenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(Index3));
			}

			// Write out the vertex data
			File->Logf(TEXT("\r\n"));

			for (int32 v = 0; v < Verts.Num(); ++v)
			{
				// Transform to Lightwave's coordinate system
				File->Logf(TEXT("v %f %f %f\r\n"), Verts[v].X, Verts[v].Z, Verts[v].Y);
			}

			// Write out the UV data
			File->Logf(TEXT("\r\n"));

			for (int32 uv = 0; uv < UVs.Num(); ++uv)
			{
				File->Logf(TEXT("vt %f %f\r\n"), UVs[uv].X, UVs[uv].Y);
			}

			// Write object header
			File->Logf(TEXT("\r\n"));
			File->Logf(TEXT("g UnrealEdObject\r\n"));
			File->Logf(TEXT("\r\n"));

			// Write out the face windings, sectioned by unique smoothing groups
			int32 SmoothingGroup = 0;

			for (int32 sm = 0; sm < UniqueSmoothingMasks.Num(); ++sm)
			{
				File->Logf(TEXT("s %i\r\n"), SmoothingGroup);
				SmoothingGroup++;

				for (int32 tri = 0; tri < RenderData.GetNumTriangles(); tri++)
				{
					if (SmoothingMasks[tri] == UniqueSmoothingMasks[sm])
					{
						int idx = 1 + (tri * 3);
						File->Logf(TEXT("f %d/%d %d/%d %d/%d\r\n"), idx, idx, idx + 1, idx + 1, idx + 2, idx + 2);
					}
				}
			}

			// Write out footer
			File->Logf(TEXT("\r\n"));
			File->Logf(TEXT("g\r\n"));

			// Clean up and finish
			delete File;
		}

		path_tmp = TEXT("\"") + path_tmp + TEXT("\"");
		FPlatformProcess::CreateProc(*path_exe, *path_tmp, true, false, false, nullptr, 0, nullptr, nullptr);
	}

	//UMaterialInterface* material = StaticMeshComponent->GetMaterial(0);
	//material->GetName();
}

void FArmorPaintModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FArmorPaintCommands::Get().PluginAction);
}

void FArmorPaintModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FArmorPaintCommands::Get().PluginAction);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArmorPaintModule, ArmorPaint)
