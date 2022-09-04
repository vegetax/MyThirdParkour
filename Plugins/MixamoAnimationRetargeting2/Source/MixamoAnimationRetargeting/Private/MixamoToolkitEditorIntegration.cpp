// Copyright 2022 UNAmedia. All Rights Reserved.

#include "MixamoToolkitEditorIntegration.h"
#include "MixamoToolkitPrivatePCH.h"

#include "MixamoAnimationRootMotionSolver.h"

#include "MixamoToolkitCommands.h"
#include "MixamoToolkitStyle.h"

#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"

//#include "LevelEditor.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"



#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"



void FMixamoToolkitEditorIntegration::Register()
{
	// Register RetargetMixamoSkeleton action into the Content Browser contextual menu.
	// The contextual menu is built at run-time using the specified delegate.
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		TArray<FContentBrowserMenuExtender_SelectedAssets> & CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateSP(this, &FMixamoToolkitEditorIntegration::MakeContentBrowserContextMenuExtender));
	}
}



void FMixamoToolkitEditorIntegration::Unregister()
{
}



/** Run the RetargetMixamoSkeleton action on the passed assets. */
void FMixamoToolkitEditorIntegration::ExecuteAction_RetargetMixamoSkeletons() const
{
	// Get all USkeleton objects to process.
	TArray<USkeleton *> Skeletons;
	for (const FAssetData& Asset : ContentBrowserSelectedAssets)
	{
		if (CanExecuteAction_RetargetMixamoSkeleton(Asset))
		{
			USkeleton * o = CastChecked<USkeleton> (Asset.GetAsset());
			if (o != nullptr)
			{
				Skeletons.Add(o);
			}
		}
	}

	FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->RetargetToUE4Mannequin(Skeletons);
}



/** Returns if the RetargetMixamoSkeleton action can run. */
bool FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeleton(const FAssetData & Asset) const
{
	// Check the asset type.
	if (Asset.AssetClass != USkeleton::StaticClass()->GetFName())
	{
		return false;
	}
	// Check the asset content.
	// NOTE: this will load the asset if needed.
	if (!FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->IsMixamoSkeleton(Cast<USkeleton> (Asset.GetAsset())))
	{
		return false;
	}
	
	return true;
}



/** Returns if the RetargetMixamoSkeleton action can run on selected assets (editor will gray-out it otherwise). */
bool FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeletons() const
{
	// Return true if any of SelectedAssets can be processed.
	return ContentBrowserSelectedAssets.ContainsByPredicate(
		[this](const FAssetData & asset)
	{
		return CanExecuteAction_RetargetMixamoSkeleton(asset);
	}
	);
}



void FMixamoToolkitEditorIntegration::ExecuteAction_ExtractRootMotion() const
{
    // Get all USkeleton objects to process.
    USkeleton * Skeleton(nullptr);
    for (const FAssetData& Asset : ContentBrowserSelectedAssets)
    {
        if (CanExecuteAction_ExtractRootMotion(Asset))
        {
            USkeleton * o = CastChecked<USkeleton>(Asset.GetAsset());
            if (o != nullptr)
            {
                Skeleton = o;
                break;
            }
        }
    }

    FMixamoAnimationRetargetingModule::Get().GetMixamoAnimationRootMotionSolver()->LaunchProcedureFlow(Skeleton);
}



bool FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion(const FAssetData & Asset) const
{
    // Check the asset type.
    if (Asset.AssetClass != USkeleton::StaticClass()->GetFName())
    {
        return false;
    }

    USkeleton* Skeleton = Cast<USkeleton>(Asset.GetAsset());
    return FMixamoAnimationRetargetingModule::Get().GetMixamoAnimationRootMotionSolver()->CanExecuteProcedure(Skeleton);
}



bool FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion() const
{
    if (ContentBrowserSelectedAssets.Num() != 1)
        return false;

    // Return true if any of SelectedAssets can be processed.
    return ContentBrowserSelectedAssets.ContainsByPredicate(
        [this](const FAssetData & asset)
    {
        return CanExecuteAction_ExtractRootMotion(asset);
    }
    );
}



/** Called when the ContentBrowser asks for extenders on selected assets. */
TSharedRef<FExtender> FMixamoToolkitEditorIntegration::MakeContentBrowserContextMenuExtender(const TArray<FAssetData> & NewSelectedAssets)
{
	ContentBrowserSelectedAssets = NewSelectedAssets;

	TSharedRef<FExtender> Extender(new FExtender());

	// Enable the action on supported asset types, use CanExecuteAction_RetargetMixamoSkeleton() to check later
	// if the asset object can be affected.
	bool bAnySupportedAssets = false;
	for (const FAssetData& Asset: ContentBrowserSelectedAssets)
	{
		bAnySupportedAssets = bAnySupportedAssets || (Asset.AssetClass == USkeleton::StaticClass()->GetFName());
	}

	if (bAnySupportedAssets)
	{
		// Add the actions to the extender
		Extender->AddMenuExtension(
			"GetAssetActions",
			EExtensionHook::After,
			PluginCommands,
			// To use an intermediary sub-menu: FMenuExtensionDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextSubMenu)
			FMenuExtensionDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries)
		);
	}

	return Extender;
}



void FMixamoToolkitEditorIntegration::AddContentBrowserContextSubMenu(class FMenuBuilder& MenuBuilder) const
{
	// Add the submenu only if we can execute some actions.
	if (!CanExecuteAction_RetargetMixamoSkeletons())
	{
		return;
	}

	MenuBuilder.AddSubMenu(
		LOCTEXT("FMixamoToolkitEditorIntegration_ContentBrowser_SubMenuLabel", "Mixamo Asset Actions"),
		LOCTEXT("FMixamoToolkitEditorIntegration_ContentBrowser_SubMenuToolTip", "Other Mixamo Asset Actions"),
		FNewMenuDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries),
		FUIAction(),
		NAME_None,	// InExtensionHook
		EUserInterfaceActionType::Button,
		false,	// bInOpenSubMenuOnClick
		FSlateIcon(FMixamoToolkitStyle::GetStyleSetName(), "ContentBrowser.AssetActions")
	);
}



void FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries(class FMenuBuilder& MenuBuilder) const
{
	// Add the RetargetMixamoSkeleton action.
	TSharedRef< FUICommandInfo > cmd = FMixamoToolkitCommands::Get().RetargetMixamoSkeleton.ToSharedRef();
	MenuBuilder.AddMenuEntry(
		cmd->GetLabel(),
		cmd->GetDescription(),
		cmd->GetIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::ExecuteAction_RetargetMixamoSkeletons),
			FCanExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeletons)
		)
	);

	// Add the ExtractRootMotion action.
	cmd = FMixamoToolkitCommands::Get().ExtractRootMotion.ToSharedRef();
	MenuBuilder.AddMenuEntry(
		cmd->GetLabel(),
		cmd->GetDescription(),
		cmd->GetIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::ExecuteAction_ExtractRootMotion),
			FCanExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion)
		)
	);
}



#undef LOCTEXT_NAMESPACE
