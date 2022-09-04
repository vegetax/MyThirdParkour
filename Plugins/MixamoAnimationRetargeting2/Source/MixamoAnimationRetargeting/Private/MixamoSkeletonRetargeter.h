// Copyright 2022 UNAmedia. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

#include "NamesMapper.h"



class UIKRigDefinition;
class UIKRetargeter;
class USkeleton;
class USkeletalMesh;
struct FReferenceSkeleton;



/**
Manage the retargeting of a Mixamo skeleton.

Further info:
- https://docs.unrealengine.com/latest/INT/Engine/Animation/Skeleton/
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimationRetargeting/index.html
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimHowTo/Retargeting/index.html
- https://docs.unrealengine.com/latest/INT/Engine/Animation/RetargetingDifferentSkeletons/
*/
class FMixamoSkeletonRetargeter
{
public:
	FMixamoSkeletonRetargeter();

	void RetargetToUE4Mannequin(TArray<USkeleton *> Skeletons) const;
	bool IsMixamoSkeleton(const USkeleton * Skeleton) const;

private:
	bool OnShouldFilterNonUEMannequinSkeletonAsset(const FAssetData& AssetData) const;
	bool IsUEMannequinSkeleton(const USkeleton * Skeleton) const;
	void Retarget(USkeleton* Skeleton, const USkeleton * ReferenceSkeleton) const;
	bool HasFakeRootBone(const USkeleton* Skeleton) const;
	void AddRootBone(USkeleton * Skeleton, TArray<USkeletalMesh *> SkeletalMeshes) const;
	void AddRootBone(const USkeleton * Skeleton, FReferenceSkeleton * RefSkeleton) const;
	void SetupTranslationRetargetingModes(USkeleton* Skeleton) const;
	void RetargetBasePose(
		TArray<USkeletalMesh *> SkeletalMeshes,
		const USkeleton * ReferenceSkeleton,
		const TArray<FName>& PreserveCSBonesNames,
		const TArray<FName>& ForceNewCSBoneNames,
		const FStaticNamesMapper & EditToReference_BoneNamesMapping,
		bool bApplyPoseToRetargetBasePose,
		class UIKRetargeterController* Controller
	) const;
	USkeleton * AskUserForTargetSkeleton() const;
	bool AskUserOverridingAssetsConfirmation(const TArray<UObject*>& AssetsToOverwrite) const;
	/// Valid within a single method's stack space.
	void GetAllSkeletalMeshesUsingSkeleton(const USkeleton * Skeleton, TArray<FAssetData> & SkeletalMeshes) const;
	void GetAllSkeletalMeshesUsingSkeleton(const USkeleton * Skeleton, TArray<USkeletalMesh *> & SkeletalMeshes) const;
	void SetPreviewMesh(USkeleton * Skeleton, USkeletalMesh * PreviewMesh) const;

	void EnumerateAssetsToOverwrite(const USkeleton* Skeleton, const USkeleton* ReferenceSkeleton, TArray<UObject*>& AssetsToOverride) const;

	UIKRigDefinition* CreateIKRig(
		const FString & PackagePath,
		const FString & AssetName,
		const USkeleton* Skeleton
	) const;
	UIKRigDefinition* CreateMixamoIKRig(const USkeleton* Skeleton) const;
	UIKRigDefinition* CreateUEMannequinIKRig(const USkeleton* Skeleton) const;
	UIKRetargeter* CreateIKRetargeter(
		const FString & PackagePath,
		const FString & AssetName,
		UIKRigDefinition* SourceRig,
		UIKRigDefinition* TargetRig,
		const FStaticNamesMapper & TargetToSource_ChainNamesMapping,
		const TArray<FName> & TargetBoneChainsToSkip,
		const TArray<FName> & TargetBoneChainsDriveIKGoal,
		const TArray<FName>& TargetBoneChainsOneToOneRotationMode
	) const;

private:
	const FStaticNamesMapper UEMannequinToMixamo_BoneNamesMapping;
	const FStaticNamesMapper UEMannequinToMixamo_ChainNamesMapping;
};
