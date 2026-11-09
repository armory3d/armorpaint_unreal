// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArmorPaint.h"
#include "ArmorPaintStyle.h"
#include "ArmorPaintCommands.h"
#include "ArmorPaintSettings.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Components/StaticMeshComponent.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "IDesktopPlatform.h"
#include "ISettingsModule.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "MeshDescription.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshResources.h"
#include "ToolMenus.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogArmorPaint, Log, All);

#define LOCTEXT_NAMESPACE "FArmorPaintModule"

namespace ArmorPaint
{
	// Texture types written by ArmorPaint's "unreal" export preset and the asset name suffix used for each
	struct FTextureType
	{
		const TCHAR* Type;
		const TCHAR* Suffix;
	};
	static const FTextureType TextureTypes[] = {
		{ TEXT("base"), TEXT("BaseColor") },
		{ TEXT("nor"), TEXT("Normal") },
		{ TEXT("orm"), TEXT("ORM") },
	};

	// Unreal works in centimeters, export meshes in meters
	static constexpr float ObjScale = 0.01f;

	static constexpr double ExportTimeout = 120.0;

	static FString ProjectsDir()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("ArmorPaint"));
	}

	static FString ArmPath(const FString& Name)
	{
		return ProjectsDir() / Name / Name + TEXT(".arm");
	}

	// ArmorPaint names exported textures <project>_<type>.png
	static FString TexturePath(const FString& Name, const TCHAR* Type)
	{
		return ProjectsDir() / Name / FString::Printf(TEXT("%s_%s.png"), *Name, Type);
	}

	static FString ContentPath(const FString& Name)
	{
		return TEXT("/Game/ArmorPaint/") + Name;
	}

	// The name is used both for files and asset names, keep it to characters valid in both
	static FString SanitizeName(const FString& Label)
	{
		FString Name;
		for (TCHAR C : Label)
		{
			Name.AppendChar((FChar::IsAlnum(C) && C < 128) || C == TEXT('-') ? C : TEXT('_'));
		}
		return Name.IsEmpty() ? FString(TEXT("Object")) : Name;
	}

	static FString Quote(const FString& Path)
	{
		FString Result = Path;
		FPaths::MakePlatformFilename(Result);
		return TEXT("\"") + Result + TEXT("\"");
	}

	static void Notify(const FText& Text, bool bSuccess = true)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.0f;
		TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	// Floats are formatted by hand: printf style %f uses the decimal separator of the C runtime locale
	static void AppendFloat(FString& Out, float Value)
	{
		const double V = FMath::IsFinite(Value) ? (double)Value : 0.0;
		const int64 Scaled = (int64)FMath::RoundHalfFromZero(FMath::Abs(V) * 1000000.0);
		if (V < 0.0 && Scaled != 0)
		{
			Out.AppendChar(TEXT('-'));
		}
		Out.Appendf(TEXT("%lld.%06lld"), Scaled / 1000000, Scaled % 1000000);
	}

	static void AppendObjLine(FString& Out, const TCHAR* Prefix, std::initializer_list<float> Values)
	{
		Out += Prefix;
		for (float Value : Values)
		{
			Out.AppendChar(TEXT(' '));
			AppendFloat(Out, Value);
		}
		Out.AppendChar(TEXT('\n'));
	}

	static bool SaveUtf8(const FString& Text, const FString& Path)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}

using namespace ArmorPaint;

void FArmorPaintModule::StartupModule()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "ArmorPaint",
			LOCTEXT("RuntimeSettingsName", "ArmorPaint"),
			LOCTEXT("RuntimeSettingsDescription", "Configure ArmorPaint settings"),
			GetMutableDefault<UArmorPaintSettings>()
		);
	}

	FArmorPaintStyle::Initialize();
	FArmorPaintStyle::ReloadTextures();

	FArmorPaintCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FArmorPaintCommands::Get().PluginAction,
		FExecuteAction::CreateRaw(this, &FArmorPaintModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FArmorPaintModule::RegisterMenus));

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FArmorPaintModule::Tick), 0.5f);

	if (FSlateApplication::IsInitialized())
	{
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddRaw(this, &FArmorPaintModule::OnApplicationActivationStateChanged);
	}
}

void FArmorPaintModule::ShutdownModule()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	}

	// ArmorPaint keeps running, its project is picked up on the next editor run
	for (FPending& P : Pending)
	{
		FPlatformProcess::CloseProc(P.Process);
	}
	Pending.Empty();

	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "ArmorPaint");
	}

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FArmorPaintStyle::Shutdown();

	FArmorPaintCommands::Unregister();
}

void FArmorPaintModule::PluginButtonClicked()
{
	AActor* Actor = GEditor ? GEditor->GetSelectedActors()->GetTop<AActor>() : nullptr;
	UStaticMeshComponent* Component = nullptr;
	if (Actor)
	{
		TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
		for (UStaticMeshComponent* C : Components)
		{
			if (C->GetStaticMesh())
			{
				Component = C;
				break;
			}
		}
	}
	if (!Component)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoSelection", "Select an actor with a Static Mesh component to paint."));
		return;
	}

	const FString Name = SanitizeName(Actor->GetActorLabel());
	const FString Arm = ArmPath(Name);
	for (const FPending& P : Pending)
	{
		if (P.ArmPath == Arm)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("AlreadyPainting", "'{0}' is already open in ArmorPaint."), FText::FromString(Name)));
			return;
		}
	}

	const FString Exe = GetExecutable(true);
	if (Exe.IsEmpty())
	{
		return;
	}

	FString Args;
	if (FPaths::FileExists(Arm))
	{
		Args = Quote(Arm);
	}
	else
	{
		// New project: open the mesh and save the project to its .arm path right away,
		// so that saving on close goes there without asking for a location
		const FString TempDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir() / TEXT("ArmorPaint"));
		const FString ObjPath = TempDir / Name + TEXT(".obj");
		if (!ExportMeshToObj(Component->GetStaticMesh(), ObjPath, Name))
		{
			return;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Arm), true);
		FString ArmLiteral = Arm;
		FPaths::MakePlatformFilename(ArmLiteral);
		ArmLiteral = ArmLiteral.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		const FString ScriptPath = TempDir / Name + TEXT("_link.c");
		const FString Script = FString::Printf(TEXT("void main() {\n\tproject_filepath_set(\"%s\");\n\tproject_save(0);\n}\n"), *ArmLiteral);
		if (!SaveUtf8(Script, ScriptPath))
		{
			UE_LOG(LogArmorPaint, Error, TEXT("Failed to write %s"), *ScriptPath);
			return;
		}
		Args = Quote(ObjPath) + TEXT(" --script ") + Quote(ScriptPath);
	}

	FProcHandle Process;
	if (!StartArmorPaint(Exe, Args, false, Process))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("StartFailed", "Failed to start ArmorPaint at:\n{0}"), FText::FromString(Exe)));
		return;
	}

	Pending.Add({ Process, Name, Arm, Component });
	UE_LOG(LogArmorPaint, Log, TEXT("Painting '%s', textures will be applied when ArmorPaint is closed."), *Name);
	Notify(FText::Format(LOCTEXT("Painting", "Painting '{0}' in ArmorPaint. Save and close ArmorPaint to apply the textures."), FText::FromString(Name)));
}

FString FArmorPaintModule::GetExecutable(bool bPrompt)
{
	auto ExeIn = [](const FString& Dir) -> FString
	{
#if PLATFORM_WINDOWS
		return Dir / TEXT("ArmorPaint.exe");
#elif PLATFORM_MAC
		return Dir / TEXT("ArmorPaint.app/Contents/MacOS/ArmorPaint");
#else
		return Dir / TEXT("ArmorPaint");
#endif
	};

	UArmorPaintSettings* Settings = GetMutableDefault<UArmorPaintSettings>();
	const FString Configured = Settings->ArmorPaintPath.Path;
	if (!Configured.IsEmpty())
	{
		const FString Exe = ExeIn(FPaths::ConvertRelativePathToFull(Configured));
		if (FPaths::FileExists(Exe))
		{
			return Exe;
		}
	}
	if (!bPrompt)
	{
		UE_LOG(LogArmorPaint, Error, TEXT("ArmorPaint not found, configure its folder at 'Project Settings - Plugins - ArmorPaint'."));
		return FString();
	}

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	FString Folder;
	if (!DesktopPlatform || !DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		LOCTEXT("SelectFolder", "Select folder where ArmorPaint is located").ToString(),
		Configured, Folder))
	{
		return FString();
	}

	const FString Exe = ExeIn(FPaths::ConvertRelativePathToFull(Folder));
	if (!FPaths::FileExists(Exe))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("NotFound", "ArmorPaint was not found at:\n{0}\n\nSelect the folder where ArmorPaint is located, it can also be set at 'Project Settings - Plugins - ArmorPaint'."), FText::FromString(Exe)));
		return FString();
	}

	Settings->ArmorPaintPath.Path = Folder;
	Settings->TryUpdateDefaultConfigFile();
	return Exe;
}

bool FArmorPaintModule::StartArmorPaint(const FString& Exe, const FString& Args, bool bHidden, FProcHandle& OutProcess)
{
	OutProcess = FPlatformProcess::CreateProc(*Exe, *Args, true, bHidden, bHidden, nullptr, 0, *FPaths::GetPath(Exe), nullptr);
	if (!OutProcess.IsValid())
	{
		UE_LOG(LogArmorPaint, Error, TEXT("Failed to start %s %s"), *Exe, *Args);
		return false;
	}
	return true;
}

bool FArmorPaintModule::ExportMeshToObj(UStaticMesh* StaticMesh, const FString& ObjPath, const FString& Name)
{
	// Unreal is left-handed Z-up, OBJ is right-handed Y-up: swap Y and Z, which also keeps the winding order front facing.
	// Unreal UVs start at the top of the texture, OBJ UVs at the bottom.
	FString Obj;
	Obj.Appendf(TEXT("o %s\n"), *Name);

	// Prefer the source mesh: render data may be a reduced Nanite fallback mesh
	const FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(0);
	if (MeshDescription && MeshDescription->Triangles().Num() > 0)
	{
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		if (!UVs.IsValid() || UVs.GetNumChannels() == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("NoUVs", "'{0}' has no UVs, it can not be painted."), FText::FromString(StaticMesh->GetName())));
			return false;
		}

		// Normals are left empty when the mesh computes them on build, let ArmorPaint compute them in that case
		bool bHasNormals = Normals.IsValid();
		for (const FVertexInstanceID Instance : MeshDescription->VertexInstances().GetElementIDs())
		{
			if (!bHasNormals)
			{
				break;
			}
			bHasNormals = !Normals[Instance].IsNearlyZero();
		}

		TArray<int32> VertexIndex;
		VertexIndex.SetNumZeroed(MeshDescription->Vertices().GetArraySize());
		int32 Index = 1;
		for (const FVertexID Vertex : MeshDescription->Vertices().GetElementIDs())
		{
			VertexIndex[Vertex.GetValue()] = Index++;
			const FVector3f P = Positions[Vertex] * ObjScale;
			AppendObjLine(Obj, TEXT("v"), { P.X, P.Z, P.Y });
		}

		TArray<int32> InstanceIndex;
		InstanceIndex.SetNumZeroed(MeshDescription->VertexInstances().GetArraySize());
		Index = 1;
		for (const FVertexInstanceID Instance : MeshDescription->VertexInstances().GetElementIDs())
		{
			InstanceIndex[Instance.GetValue()] = Index++;
			const FVector2f UV = UVs.Get(Instance, 0);
			AppendObjLine(Obj, TEXT("vt"), { UV.X, 1.0f - UV.Y });
			if (bHasNormals)
			{
				const FVector3f N = Normals[Instance];
				AppendObjLine(Obj, TEXT("vn"), { N.X, N.Z, N.Y });
			}
		}

		for (const FTriangleID Triangle : MeshDescription->Triangles().GetElementIDs())
		{
			Obj += TEXT("f");
			for (const FVertexInstanceID Instance : MeshDescription->GetTriangleVertexInstances(Triangle))
			{
				const int32 V = VertexIndex[MeshDescription->GetVertexInstanceVertex(Instance).GetValue()];
				const int32 I = InstanceIndex[Instance.GetValue()];
				if (bHasNormals)
				{
					Obj.Appendf(TEXT(" %d/%d/%d"), V, I, I);
				}
				else
				{
					Obj.Appendf(TEXT(" %d/%d"), V, I);
				}
			}
			Obj += TEXT("\n");
		}
	}
	else if (StaticMesh->HasValidRenderData())
	{
		const FStaticMeshLODResources& LOD = StaticMesh->GetLODForExport(0);
		const FStaticMeshVertexBuffers& Buffers = LOD.VertexBuffers;
		const uint32 VertexCount = Buffers.PositionVertexBuffer.GetNumVertices();
		for (uint32 i = 0; i < VertexCount; i++)
		{
			const FVector3f P = Buffers.PositionVertexBuffer.VertexPosition(i) * ObjScale;
			AppendObjLine(Obj, TEXT("v"), { P.X, P.Z, P.Y });
		}
		for (uint32 i = 0; i < VertexCount; i++)
		{
			const FVector2f UV = Buffers.StaticMeshVertexBuffer.GetVertexUV(i, 0);
			AppendObjLine(Obj, TEXT("vt"), { UV.X, 1.0f - UV.Y });
		}
		for (uint32 i = 0; i < VertexCount; i++)
		{
			const FVector3f N = Buffers.StaticMeshVertexBuffer.VertexTangentZ(i);
			AppendObjLine(Obj, TEXT("vn"), { N.X, N.Z, N.Y });
		}
		const FIndexArrayView Indices = LOD.IndexBuffer.GetArrayView();
		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			const uint32 A = Indices[i] + 1;
			const uint32 B = Indices[i + 1] + 1;
			const uint32 C = Indices[i + 2] + 1;
			Obj.Appendf(TEXT("f %u/%u/%u %u/%u/%u %u/%u/%u\n"), A, A, A, B, B, B, C, C, C);
		}
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("NoMesh", "Mesh data of '{0}' is not available."), FText::FromString(StaticMesh->GetName())));
		return false;
	}

	if (!SaveUtf8(Obj, ObjPath))
	{
		UE_LOG(LogArmorPaint, Error, TEXT("Failed to write %s"), *ObjPath);
		return false;
	}
	return true;
}

// Waits for ArmorPaint instances started by the plugin to close
bool FArmorPaintModule::Tick(float DeltaTime)
{
	if (bUpdating || Pending.Num() == 0)
	{
		return true;
	}

	TArray<FPending> Closed;
	for (int32 i = Pending.Num() - 1; i >= 0; i--)
	{
		if (!FPlatformProcess::IsProcRunning(Pending[i].Process))
		{
			FPlatformProcess::CloseProc(Pending[i].Process);
			Closed.Add(Pending[i]);
			Pending.RemoveAt(i);
		}
	}

	for (const FPending& P : Closed)
	{
		if (!FPaths::FileExists(P.ArmPath))
		{
			UE_LOG(LogArmorPaint, Log, TEXT("%s was not saved, no textures to apply."), *P.ArmPath);
			continue;
		}
		UpdateProject(P.Name, P.Component.Get());
	}
	return true;
}

// Picks up projects saved while the editor was not tracking ArmorPaint, such as after the editor was restarted
void FArmorPaintModule::OnApplicationActivationStateChanged(bool bActive)
{
	if (!bActive || bUpdating)
	{
		return;
	}

	TArray<FString> Folders;
	IFileManager::Get().FindFiles(Folders, *(ProjectsDir() / TEXT("*")), false, true);
	for (const FString& Name : Folders)
	{
		const FString Arm = ArmPath(Name);
		if (!FPaths::FileExists(Arm) || Pending.ContainsByPredicate([&Arm](const FPending& P) { return P.ArmPath == Arm; }))
		{
			continue;
		}
		const FString Base = TexturePath(Name, TEXT("base"));
		if (!FPaths::FileExists(Base) || IFileManager::Get().GetTimeStamp(*Arm) > IFileManager::Get().GetTimeStamp(*Base))
		{
			UpdateProject(Name, nullptr);
		}
	}
}

void FArmorPaintModule::UpdateProject(const FString& Name, UStaticMeshComponent* Component)
{
	TGuardValue<bool> Guard(bUpdating, true);

	if (!ExportTextures(ArmPath(Name)))
	{
		Notify(FText::Format(LOCTEXT("ExportFailed", "ArmorPaint: texture export of '{0}' failed, see the Output Log."), FText::FromString(Name)), false);
		return;
	}

	TMap<FString, UTexture2D*> Textures;
	if (!ImportTextures(Name, Textures))
	{
		Notify(FText::Format(LOCTEXT("ImportFailed", "ArmorPaint: texture import of '{0}' failed, see the Output Log."), FText::FromString(Name)), false);
		return;
	}

	UMaterialInterface* Material = GetOrCreateMaterial(Name, Textures);
	if (Material && Component)
	{
		ApplyMaterial(Component, Material);
	}

	UE_LOG(LogArmorPaint, Log, TEXT("Textures of '%s' updated in %s"), *Name, *ContentPath(Name));
	Notify(FText::Format(LOCTEXT("Updated", "ArmorPaint: textures of '{0}' updated."), FText::FromString(Name)));
}

bool FArmorPaintModule::ExportTextures(const FString& Arm)
{
	if (!FPaths::FileExists(Arm))
	{
		return false;
	}
	const FString Exe = GetExecutable(false);
	if (Exe.IsEmpty())
	{
		return false;
	}

	// Remove previous textures so that a failed export is detected
	const FString Name = FPaths::GetBaseFilename(Arm);
	for (const FTextureType& T : TextureTypes)
	{
		IFileManager::Get().Delete(*TexturePath(Name, T.Type), false, true, true);
	}

	FScopedSlowTask SlowTask(1, FText::Format(LOCTEXT("Exporting", "Exporting textures from {0}"), FText::FromString(FPaths::GetCleanFilename(Arm))));
	SlowTask.MakeDialog();
	SlowTask.EnterProgressFrame(1);

	const FString Args = Quote(Arm) + TEXT(" --export-textures png unreal ") + Quote(FPaths::GetPath(Arm)) + TEXT(" --background");
	FProcHandle Process;
	if (!StartArmorPaint(Exe, Args, true, Process))
	{
		return false;
	}
	const double StartTime = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(Process))
	{
		if (FPlatformTime::Seconds() - StartTime > ExportTimeout)
		{
			FPlatformProcess::TerminateProc(Process);
			FPlatformProcess::CloseProc(Process);
			UE_LOG(LogArmorPaint, Error, TEXT("Texture export from %s timed out."), *Arm);
			return false;
		}
		FPlatformProcess::Sleep(0.05f);
	}
	FPlatformProcess::CloseProc(Process);

	if (!FPaths::FileExists(TexturePath(Name, TEXT("base"))))
	{
		UE_LOG(LogArmorPaint, Error, TEXT("Texture export from %s failed."), *Arm);
		return false;
	}
	UE_LOG(LogArmorPaint, Log, TEXT("Textures exported from %s"), *Arm);
	return true;
}

// Imports the exported textures, or reimports them into the existing assets
bool FArmorPaintModule::ImportTextures(const FString& Name, TMap<FString, UTexture2D*>& OutTextures)
{
	TArray<UAssetImportTask*> Tasks;
	TArray<const FTextureType*> TaskTypes;
	for (const FTextureType& T : TextureTypes)
	{
		const FString Png = TexturePath(Name, T.Type);
		if (!FPaths::FileExists(Png))
		{
			continue;
		}
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = Png;
		Task->DestinationPath = ContentPath(Name);
		Task->DestinationName = FString::Printf(TEXT("T_%s_%s"), *Name, T.Suffix);
		Task->bReplaceExisting = true;
		Task->bReplaceExistingSettings = false;
		Task->bAutomated = true;
		Task->bSave = false;
		Tasks.Add(Task);
		TaskTypes.Add(&T);
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.ImportAssetTasks(Tasks);

	TArray<UPackage*> Packages;
	for (int32 i = 0; i < Tasks.Num(); i++)
	{
		const TArray<UObject*>& Objects = Tasks[i]->GetObjects();
		UTexture2D* Texture = Objects.Num() > 0 ? Cast<UTexture2D>(Objects[0]) : nullptr;
		if (!Texture)
		{
			UE_LOG(LogArmorPaint, Error, TEXT("Failed to import %s"), *Tasks[i]->Filename);
			continue;
		}

		const FString Type = TaskTypes[i]->Type;
		const bool bNormal = Type == TEXT("nor");
		const bool bMasks = Type == TEXT("orm");
		const TextureCompressionSettings Compression = bNormal ? TC_Normalmap : bMasks ? TC_Masks : TC_Default;
		const bool bSRGB = !bNormal && !bMasks;
		if (Texture->CompressionSettings != Compression || (bool)Texture->SRGB != bSRGB)
		{
			Texture->Modify();
			Texture->CompressionSettings = Compression;
			Texture->SRGB = bSRGB;
			Texture->PostEditChange();
		}

		OutTextures.Add(Type, Texture);
		Packages.Add(Texture->GetPackage());
	}

	UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
	return OutTextures.Contains(TEXT("base"));
}

// Creates a material with the textures connected, an existing one keeps its graph and picks up the reimported textures
UMaterialInterface* FArmorPaintModule::GetOrCreateMaterial(const FString& Name, const TMap<FString, UTexture2D*>& Textures)
{
	const FString AssetName = TEXT("M_") + Name;
	const FString PackagePath = ContentPath(Name);
	const FString ObjectPath = PackagePath / AssetName + TEXT(".") + AssetName;
	if (UMaterialInterface* Existing = LoadObject<UMaterialInterface>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return Existing;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UMaterial* Material = Cast<UMaterial>(AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory));
	if (!Material)
	{
		UE_LOG(LogArmorPaint, Error, TEXT("Failed to create material %s"), *ObjectPath);
		return nullptr;
	}

	auto AddTexture = [Material, &Textures](const TCHAR* Type, int32 Y) -> UMaterialExpressionTextureSample*
	{
		UTexture2D* const* Texture = Textures.Find(Type);
		if (!Texture)
		{
			return nullptr;
		}
		UMaterialExpressionTextureSample* Sample = Cast<UMaterialExpressionTextureSample>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSample::StaticClass(), -400, Y));
		Sample->Texture = *Texture;
		Sample->AutoSetSampleType();
		return Sample;
	};

	if (UMaterialExpressionTextureSample* Base = AddTexture(TEXT("base"), 0))
	{
		UMaterialEditingLibrary::ConnectMaterialProperty(Base, TEXT("RGB"), MP_BaseColor);
	}
	// orm: R occlusion, G roughness, B metallic
	if (UMaterialExpressionTextureSample* Orm = AddTexture(TEXT("orm"), 250))
	{
		UMaterialEditingLibrary::ConnectMaterialProperty(Orm, TEXT("R"), MP_AmbientOcclusion);
		UMaterialEditingLibrary::ConnectMaterialProperty(Orm, TEXT("G"), MP_Roughness);
		UMaterialEditingLibrary::ConnectMaterialProperty(Orm, TEXT("B"), MP_Metallic);
	}
	if (UMaterialExpressionTextureSample* Normal = AddTexture(TEXT("nor"), 500))
	{
		UMaterialEditingLibrary::ConnectMaterialProperty(Normal, TEXT("RGB"), MP_Normal);
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);
	Material->MarkPackageDirty();
	UEditorLoadingAndSavingUtils::SavePackages({ Material->GetPackage() }, false);
	return Material;
}

// Assigns the material to all slots of the painted component, ArmorPaint paints the whole mesh as one
void FArmorPaintModule::ApplyMaterial(UStaticMeshComponent* Component, UMaterialInterface* Material)
{
	const int32 NumSlots = FMath::Max(1, Component->GetNumMaterials());
	bool bChanged = false;
	for (int32 i = 0; i < NumSlots; i++)
	{
		bChanged |= Component->GetMaterial(i) != Material;
	}
	if (!bChanged)
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ApplyMaterial", "Apply ArmorPaint Material"));
	Component->Modify();
	for (int32 i = 0; i < NumSlots; i++)
	{
		Component->SetMaterial(i, Material);
	}
}

void FArmorPaintModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FArmorPaintCommands::Get().PluginAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("PluginTools");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FArmorPaintCommands::Get().PluginAction));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FArmorPaintModule, ArmorPaint)
