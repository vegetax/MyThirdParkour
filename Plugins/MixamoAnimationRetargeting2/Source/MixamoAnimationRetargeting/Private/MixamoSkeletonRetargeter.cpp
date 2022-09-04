// Copyright 2022 UNAmedia. All Rights Reserved.

#include "MixamoSkeletonRetargeter.h"
#include "MixamoToolkitPrivatePCH.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#include "IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "Solvers/IKRig_PBIKSolver.h"

#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "PackageTools.h"

#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Misc/ScopedSlowTask.h"

#include "SkeletonPoser.h"
#include "SkeletonMatcher.h"

#include "Editor.h"
#include "SMixamoToolkitWidget.h"
#include "ComponentReregisterContext.h"
#include "Components/SkinnedMeshComponent.h"



#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"


// Define it to ignore the UE5 mannequin as a valid retarget source
#define MAR_IGNORE_UE5_MANNEQUIN


// Define it to disable the automatic addition of the Root Bone (needed to support UE4 Root Animations).
//#define MAR_ADDROOTBONE_DISABLE_
#ifdef MAR_ADDROOTBONE_DISABLE_
#	pragma message ("***WARNING*** Feature \"AddRootBone\" disabled.")
#endif

// Define it to disable the advance chains setup of the IK Retarger assets
//#define MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_
#ifdef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_
#	pragma message ("***WARNING*** IK Retargeter is not using the advanced chains setup.")
#endif

// Define it to disable the IK solvers setup of the IK Retarger assets
//#define MAR_IKRETARGETER_IKSOLVERS_DISABLE_
#ifdef MAR_IKRETARGETER_IKSOLVERS_DISABLE_
#	pragma message ("***WARNING*** IK Retargeter is not using the IK solvers setup.")
#endif

//#define MAR_UPPERARMS_PRESERVECS_EXPERIMENTAL_ENABLE_
#ifdef MAR_UPPERARMS_PRESERVECS_EXPERIMENTAL_ENABLE_
#	pragma message ("***WARNING*** Preserving the Component Space bones of the upper arm bones is an experimental feature.")
#endif



namespace
{



/**
Index of the last Mixamo bone, in kUEMannequinToMixamo_BoneNamesMapping arrays,
used to determine if a skeleton is from Mixamo.

Given the pair N-th, then index i = N * 2 + 1.
*/
constexpr int IndexLastCheckedMixamoBone = 22 * 2 + 1;
/**
Index of the last UE Mannequin bone, in kUEMannequinToMixamo_BoneNamesMapping arrays,
used to determine if a skeleton is the UE Mannequin.

Given the pair N-th, then index i = N * 2 + 1.
*/
constexpr int IndexLastCheckedUEMannequinBone = 22 * 2;



/**
Mapping of "UE Mannequin" skeleton bones to the corresponding "Mixamo" skeleton bones names.

NOTES:
- includes the added "root" bone (by default it's missing in Mixamo skeletons and it's added by the plugin).
- the first N pairs [ N = (IndexLastCheckedMixamoBone + 1) / 2 ] are used to
	determine if a skeleton is from Mixamo.
*/
static const char* const kUEMannequinToMixamo_BoneNamesMapping[] = {
	// UE Mannequin bone name		MIXAMO bone name
	"root",					"root",
	"pelvis", 				"Hips",
	"spine_01", 			"Spine",
	"spine_02", 			"Spine1",
	"spine_03", 			"Spine2",
	"neck_01", 				"Neck",
	"head", 				"head",
	"clavicle_l", 			"LeftShoulder",
	"upperarm_l", 			"LeftArm",
	"lowerarm_l", 			"LeftForeArm",
	"hand_l", 				"LeftHand",
	"clavicle_r", 			"RightShoulder",
	"upperarm_r", 			"RightArm",
	"lowerarm_r", 			"RightForeArm",
	"hand_r", 				"RightHand",
	"thigh_l", 				"LeftUpLeg",
	"calf_l", 				"LeftLeg",
	"foot_l", 				"LeftFoot",
	"ball_l", 				"LeftToeBase",
	"thigh_r", 				"RightUpLeg",
	"calf_r",				"RightLeg",
	"foot_r", 				"RightFoot",
	"ball_r", 				"RightToeBase",
	// From here, ignored to determine if a skeleton is from Mixamo.
	// From here, ignored to determine if a skeleton is from UE Mannequin.
	"index_01_l", 			"LeftHandIndex1",
	"index_02_l",			"LeftHandIndex2",
	"index_03_l", 			"LeftHandIndex3",
	"middle_01_l", 			"LeftHandMiddle1",
	"middle_02_l", 			"LeftHandMiddle2",
	"middle_03_l", 			"LeftHandMiddle3",
	"pinky_01_l", 			"LeftHandPinky1",
	"pinky_02_l", 			"LeftHandPinky2",
	"pinky_03_l", 			"LeftHandPinky3",
	"ring_01_l", 			"LeftHandRing1",
	"ring_02_l", 			"LeftHandRing2",
	"ring_03_l", 			"LeftHandRing3",
	"thumb_01_l", 			"LeftHandThumb1",
	"thumb_02_l", 			"LeftHandThumb2",
	"thumb_03_l", 			"LeftHandThumb3",
	"index_01_r", 			"RightHandIndex1",
	"index_02_r", 			"RightHandIndex2",
	"index_03_r", 			"RightHandIndex3",
	"middle_01_r", 			"RightHandMiddle1",
	"middle_02_r", 			"RightHandMiddle2",
	"middle_03_r", 			"RightHandMiddle3",
	"pinky_01_r", 			"RightHandPinky1",
	"pinky_02_r", 			"RightHandPinky2",
	"pinky_03_r", 			"RightHandPinky3",
	"ring_01_r", 			"RightHandRing1",
	"ring_02_r", 			"RightHandRing2",
	"ring_03_r", 			"RightHandRing3",
	"thumb_01_r", 			"RightHandThumb1",
	"thumb_02_r", 			"RightHandThumb2",
	"thumb_03_r", 			"RightHandThumb3",
	// Un-mapped bones (at the moment). Here for reference.
	//"lowerarm_twist_01_l", 	nullptr,
	//"upperarm_twist_01_l", 	nullptr,
	//"lowerarm_twist_01_r", 	nullptr,
	//"upperarm_twist_01_r", 	nullptr,
	//"calf_twist_01_l", 		nullptr,
	//"thigh_twist_01_l", 	nullptr,
	//"calf_twist_01_r", 		nullptr,
	//"thigh_twist_01_r", 	nullptr,
	//"ik_foot_root",			nullptr,
	//"ik_foot_l",			nullptr,
	//"ik_foot_r",			nullptr,
	//"ik_hand_root",			nullptr,
	//"ik_hand_gun",			nullptr,
	//"ik_hand_l",			nullptr,
	//"ik_hand_r",			nullptr,
};

// UE5 mannequin bones in addition of the old mannequin.
// not all additional bones were included here (e.g fingers, etc)
static const char* const kUE5MannequinAdditionalBones[] =
{
	"spine_04",
	"spine_05",
	"neck_02",
	"lowerarm_twist_02_l",
	"lowerarm_twist_02_r",
	"upperarm_twist_02_l",
	"upperarm_twist_02_r",
	"thigh_twist_02_l",
	"thigh_twist_02_r",
	"calf_twist_02_l",
	"calf_twist_02_r"
};

constexpr int32 kUEMannequinToMixamo_BoneNamesMapping_Num = sizeof(kUEMannequinToMixamo_BoneNamesMapping) / sizeof(decltype(kUEMannequinToMixamo_BoneNamesMapping[0]));
static_assert (kUEMannequinToMixamo_BoneNamesMapping_Num % 2 == 0, "An event number of entries is expected");

static_assert (IndexLastCheckedMixamoBone % 2 == 1, "Mixamo indexes are odd numbers");
static_assert (IndexLastCheckedMixamoBone >= 1, "First valid Mixamo index is 1");
static_assert (IndexLastCheckedMixamoBone < kUEMannequinToMixamo_BoneNamesMapping_Num, "Index out of bounds");

static_assert (IndexLastCheckedUEMannequinBone % 2 == 0, "UE Mannequin indexes are even numbers");
static_assert (IndexLastCheckedUEMannequinBone >= 0, "First valid UE Mannequin index is 0");
static_assert (IndexLastCheckedUEMannequinBone < kUEMannequinToMixamo_BoneNamesMapping_Num, "Index out of bounds");



/**
Names of bones in the Mixamo skeleton that must preserve their Component Space transform when re-posed
to match the UE Mannequin skeleton base pose.
*/
static const TArray<FName> Mixamo_PreserveComponentSpacePose_BoneNames = {
	"Head",
	"LeftToeBase",
	"RightToeBase"

#ifdef MAR_UPPERARMS_PRESERVECS_EXPERIMENTAL_ENABLE_
	,"RightShoulder"
	,"RightArm"
	,"LeftShoulder"
	,"LeftArm"
#endif
};

/**
Names of bones in the UE Mannequin skeleton that must preserve their Component Space transform when re-posed
to match the Mixamo skeleton base pose.
*/
static const TArray<FName> UEMannequin_PreserveComponentSpacePose_BoneNames = {
	"head",
	"ball_r",
	"ball_l"

#ifdef MAR_UPPERARMS_PRESERVECS_EXPERIMENTAL_ENABLE_
	,"clavicle_r"
	,"upperarm_r"
	,"clavicle_l"
	,"upperarm_l"
#endif
};



/**
Names of bones in the Mixamo skeleton that must preserve their Component Space transform (relative to the parent)
when re-posed to match the Mixamo skeleton base pose.
*/
static const TArray<FName> Mixamo_ForceNewComponentSpacePose_BoneNames = {
};

/**
Names of bones in the UE Mannequin skeleton that must preserve their Component Space transform (relative to the parent)
when re-posed to match the Mixamo skeleton base pose.
*/
static const TArray<FName> UEMannequin_ForceNewComponentSpacePose_BoneNames = {
	"upperarm_l",
	"upperarm_r",
	"lowerarm_l",
	"lowerarm_r",
	"thigh_l",
	"thigh_r",
	"calf_l",
	"calf_r"
};



static const FName RootBoneName("root");



#ifndef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_
/**
Mapping of "UE Mannequin" chain names to the corresponding "Mixamo" chain names.
*/
static const char* const kUEMannequinToMixamo_ChainNamesMapping[] = {
	"Root", "Root",
	"Spine", "Spine",
	"Head", "Head",
	"LeftClavicle", "LeftClavicle",
	"RightClavicle", "RightClavicle",
	"LeftArm", "LeftArm",
	"RightArm", "RightArm",
	"LeftLeg", "LeftLeg",
	"RightLeg", "RightLeg",
	"LeftIndex", "LeftIndex",
	"RightIndex", "RightIndex",
	"LeftMiddle", "LeftMiddle",
	"RightMiddle", "RightMiddle",
	"LeftPinky", "LeftPinky",
	"RightPinky", "RightPinky",
	"LeftRing", "LeftRing",
	"RightRing", "RightRing",
	"LeftThumb", "LeftThumb",
	"RightThumb", "RightThumb"
};

constexpr int32 kUEMannequinToMixamo_ChainNamesMapping_Num = sizeof(kUEMannequinToMixamo_ChainNamesMapping) / sizeof(decltype(kUEMannequinToMixamo_ChainNamesMapping[0]));
static_assert (kUEMannequinToMixamo_ChainNamesMapping_Num % 2 == 0, "An event number of entries is expected");
#endif


/**
List of "chain names" (relative the the UE Mannequin names) that must not be configured in the IKRetarget asset.
*/
static const TArray<FName> UEMannequin_SkipChains_ChainNames = {
#ifdef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_
	TEXT("root"),
	TEXT("pelvis")
#endif
};


/**
List of "chain names" (relative the the UE Mannequin names) for which the "Drive IK Goal" must be configured.
*/
static const TArray<FName> UEMannequin_DriveIKGoal_ChainNames = {
	"LeftArm",
	"RightArm",
	"LeftLeg",
	"RightLeg"
};


/**
List of "chain names" (relative the the UE Mannequin names) for which the "one to one" must be set as FK Rotation mode.
*/
static const TArray<FName> UEMannequin_OneToOneFKRotationMode_ChainNames = {
	"LeftIndex",
	"RightIndex",
	"LeftMiddle",
	"RightMiddle",
	"LeftPinky",
	"RightPinky",
	"LeftRing",
	"RightRing",
	"LeftThumb",
	"RightThumb",
};



/// Returns the "cleaned" name of a skeleton asset.
FString GetBaseSkeletonName(const USkeleton* Skeleton)
{
	check(Skeleton != nullptr);
	FString Name = Skeleton->GetName();
	Name.RemoveFromStart(TEXT("SK_"));
	Name.RemoveFromEnd(TEXT("Skeleton"));
	Name.RemoveFromEnd(TEXT("_"));
	return Name;
}

/// Returns a nicer name for the IKRig asset associated to Skeleton.
FString GetRigName(const USkeleton* Skeleton)
{
	return FString::Printf(TEXT("IK_%s"), *GetBaseSkeletonName(Skeleton));
}


/// Returns a nicer name for the IKRetargeter asset used to retarget from ReferenceSkeleton to Skeleton.
FString GetRetargeterName(const USkeleton* ReferenceSkeleton, const USkeleton* Skeleton)
{
	return FString::Printf(TEXT("RTG_%s_%s"), *GetBaseSkeletonName(ReferenceSkeleton), *GetBaseSkeletonName(Skeleton));
}



// See FSkeletonPoser::ComputeComponentSpaceTransform().
// TODO: this is used by ConfigureBoneLimitsLocalToBS(); if we'll keep this function, it could be helpful to make the above one public and use it.
FTransform ComputeComponentSpaceTransform(const FReferenceSkeleton & RefSkeleton, int32 BoneIndex)
{
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	const TArray<FTransform>& RelTransforms = RefSkeleton.GetRefBonePose();
	FTransform T = RelTransforms[BoneIndex];
	int32 i = RefSkeleton.GetParentIndex(BoneIndex);
	while (i != INDEX_NONE)
	{
		T *= RelTransforms[i];
		i = RefSkeleton.GetParentIndex(i);
	}

	return T;
}



/**
Configure the bone preferred angle converting from the input "Local Space" to the Skeleton Bone Space.

Local space is constructed with the forward vector pointing to the bone direction (the direction pointing the child bone), 
the input right vector (BoneLimitRightCS) and the Up vector as the cross product of the two.

Preferred angles are then remapped from these axis to the **matching** skeleton bone space axis.

Bone name is in Settings.
*/
void ConfigureBonePreferredAnglesLocalToBS(
	const USkeleton* Skeleton,
	UIKRig_PBIKBoneSettings* Settings,
	FName ChildBoneName,
	FVector PreferredAnglesLS,
	FVector BoneLimitRightCS
)
{
	check(Skeleton != nullptr);
	if (Settings == nullptr)
	{
		return;
	}
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(Settings->Bone);
	// Skip if required data are missing.
	if (BoneIndex == INDEX_NONE)
	{
		return;
	}

	const int32 ChildBoneIndex = RefSkeleton.FindBoneIndex(ChildBoneName);
	if (ChildBoneIndex == INDEX_NONE)
	{
		return;
	}

	check(RefSkeleton.GetParentIndex(ChildBoneIndex) == BoneIndex);

	auto GetMatchingAxis = [](const TArray<FVector>& axis, const FVector& refAxis, bool& bInverted)
	{
		float ProjOnDir = FVector::DotProduct(axis[0], refAxis);
		int bestIdx = 0;
		for(int i = 1; i < 3; ++i)
		{
			float dot = FVector::DotProduct(axis[i], refAxis);
			if (FMath::Abs(dot) > FMath::Abs(ProjOnDir))
			{
				ProjOnDir = dot;
				bestIdx = i;
			}
		}

		bInverted = ProjOnDir < 0;
		return bestIdx;
	};

	const FTransform BoneCS = ComputeComponentSpaceTransform(RefSkeleton, BoneIndex);
	const FTransform ChildBoneCS = ComputeComponentSpaceTransform(RefSkeleton, ChildBoneIndex);
	const FQuat& BoneR = BoneCS.GetRotation();

	const FVector XAxisCS = BoneCS.GetUnitAxis(EAxis::X);
	const FVector YAxisCS = BoneCS.GetUnitAxis(EAxis::Y);
	const FVector ZAxisCS = BoneCS.GetUnitAxis(EAxis::Z);

	const FVector ParentToChildDirCS = (ChildBoneCS.GetTranslation() - BoneCS.GetTranslation()).GetSafeNormal();

	// NOTE: [XBoneLimitCS, YBoneLimitCS, ZBoneLimitCS] could be NOT an orthonormal basis!
	const FVector XBoneLimitCS = ParentToChildDirCS;
	const FVector YBoneLimitCS = BoneLimitRightCS;
	const FVector ZBoneLimitCS = FVector::CrossProduct(XBoneLimitCS, YBoneLimitCS);
	check(!ZBoneLimitCS.IsNearlyZero());

	bool bInverted[3];
	int remappedAxis[3];

	remappedAxis[0] = GetMatchingAxis({ XAxisCS , YAxisCS , ZAxisCS }, XBoneLimitCS, bInverted[0]);
	remappedAxis[1] = GetMatchingAxis({ XAxisCS , YAxisCS , ZAxisCS }, YBoneLimitCS, bInverted[1]);
	remappedAxis[2] = GetMatchingAxis({ XAxisCS , YAxisCS , ZAxisCS }, ZBoneLimitCS, bInverted[2]);

	for (int i = 0; i < 3; ++i)
	{
		int axisIdx = remappedAxis[i];
		float angle = PreferredAnglesLS[i];
		float sign = 1.0f;

		if (bInverted[i])
			sign = -1.0f;
		if(((i == 2) ^ (axisIdx == 2)) == true)
			sign *= -1.0f;

		Settings->PreferredAngles[axisIdx] = angle * sign;
	}

	Settings->bUsePreferredAngles = true;
}

} // namespace *unnamed*



FMixamoSkeletonRetargeter::FMixamoSkeletonRetargeter()
	: UEMannequinToMixamo_BoneNamesMapping(kUEMannequinToMixamo_BoneNamesMapping, kUEMannequinToMixamo_BoneNamesMapping_Num),
#ifdef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_
	  UEMannequinToMixamo_ChainNamesMapping(kUEMannequinToMixamo_BoneNamesMapping, kUEMannequinToMixamo_BoneNamesMapping_Num)
#else
	  UEMannequinToMixamo_ChainNamesMapping(kUEMannequinToMixamo_ChainNamesMapping, kUEMannequinToMixamo_ChainNamesMapping_Num)
#endif
{
}



/**
Retarget all the Skeletons (Mixamo skeletons) to a UE Mannequin skeleton that the user will interactively select.
*/
void FMixamoSkeletonRetargeter::RetargetToUE4Mannequin(TArray<USkeleton *> Skeletons) const
{
	if (Skeletons.Num() <= 0)
	{
		return;
	}

	// Get the UE4 "Mannequin" skeleton.
	USkeleton * ReferenceSkeleton = AskUserForTargetSkeleton();
	if (ReferenceSkeleton == nullptr)
	{
		// We hadn't found a suitable skeleton.
		UE_LOG(LogMixamoToolkit, Error, TEXT("No suitable Skeleton selected. Retargeting aborted."));
		return;
	}

	TArray<UObject*> AssetsToOverwrite;
	for (USkeleton* Skeleton : Skeletons)
	{
		EnumerateAssetsToOverwrite(Skeleton, ReferenceSkeleton, AssetsToOverwrite);
	}
	if (AssetsToOverwrite.Num() && !AskUserOverridingAssetsConfirmation(AssetsToOverwrite))
	{
		UE_LOG(LogMixamoToolkit, Error, TEXT("Files overwritten denied. Retargeting aborted by the user."));
		return;
	}

	// Process all input skeletons.
	FScopedSlowTask Progress(Skeletons.Num(), LOCTEXT("FMixamoSkeletonRetargeter_ProgressTitle", "Retargeting of Mixamo assets"));
	Progress.MakeDialog();
	const FScopedTransaction Transaction(LOCTEXT("FMixamoSkeletonRetargeter_RetargetSkeletons", "Retargeting of Mixamo assets"));
	for (USkeleton * Skeleton : Skeletons)
	{
		Progress.EnterProgressFrame(1, FText::FromName(Skeleton->GetFName()));
		Retarget(Skeleton, ReferenceSkeleton);
	}
}



/// Return true if Skeleton is a Mixamo skeleton.
bool FMixamoSkeletonRetargeter::IsMixamoSkeleton(const USkeleton * Skeleton) const
{
	// We consider a Skeleton "coming from Mixamo" if it has at least X% of the expected bones.
	const float MINIMUM_MATCHING_PERCENTAGE = .75f;

	// Convert the array of expected bone names (TODO: cache it...).
	TArray<FName> BoneNames;
	UEMannequinToMixamo_BoneNamesMapping.GetDestination(BoneNames);
	// Look for and count the known Mixamo bones (see comments on IndexLastCheckedMixamoBone and UEMannequinToMixamo_BonesMapping).
	constexpr int32 NumBones = (IndexLastCheckedMixamoBone + 1) / 2;
	BoneNames.SetNum(NumBones);
	
	FSkeletonMatcher SkeletonMatcher(BoneNames, MINIMUM_MATCHING_PERCENTAGE);
	return SkeletonMatcher.IsMatching(Skeleton);
}



/// Return true if AssetData is NOT a UE Mannequin skeleton asset.
bool FMixamoSkeletonRetargeter::OnShouldFilterNonUEMannequinSkeletonAsset(const FAssetData& AssetData) const
{
	// To check the skeleton bones, unfortunately we've to load the asset.
	USkeleton* Skeleton = Cast<USkeleton>(AssetData.GetAsset());
	return Skeleton != nullptr ? !IsUEMannequinSkeleton(Skeleton) : true;
}



/// Return true if Skeleton is a UE Mannequin skeleton.
bool FMixamoSkeletonRetargeter::IsUEMannequinSkeleton(const USkeleton* Skeleton) const
{
	// We consider a Skeleton "being the UE Mannequin" if it has at least X% of the expected bones.
	const float MINIMUM_MATCHING_PERCENTAGE = .75f;

	// Convert the array of expected bone names (TODO: cache it...).
	TArray<FName> BoneNames;
	UEMannequinToMixamo_BoneNamesMapping.GetSource(BoneNames);
	// Look for and count the known UE Mannequin bones (see comments on IndexLastCheckedUEMannequinBone and UEMannequinToMixamo_BonesMapping).
	constexpr int32 NumBones = (IndexLastCheckedUEMannequinBone + 2) / 2;
	BoneNames.SetNum(NumBones);
	
	FSkeletonMatcher SkeletonMatcher(BoneNames, MINIMUM_MATCHING_PERCENTAGE);
	if (SkeletonMatcher.IsMatching(Skeleton))
	{
#ifdef MAR_IGNORE_UE5_MANNEQUIN
		TArray<FName> UE5BoneNames;
		for (int i = 0; i < sizeof(kUE5MannequinAdditionalBones) / sizeof(char*); ++i)
		{
			UE5BoneNames.Add(kUE5MannequinAdditionalBones[i]);
		}

		const float MINIMUM_MATCHING_PERCENTAGE_UE5 = .25f;
		FSkeletonMatcher SkeletonMatcherUE5(UE5BoneNames, MINIMUM_MATCHING_PERCENTAGE_UE5);

		// return false if the skeleton was recognized as an UE5 mannequin!
		return SkeletonMatcherUE5.IsMatching(Skeleton) == false;
#else
		return true;
#endif
	}

	return false;
}



/**
Process Skeleton to support retargeting to ReferenceSkeleton.

Usually this requires to process all the Skeletal Meshes based on Skeleton.
*/
void FMixamoSkeletonRetargeter::Retarget(USkeleton* Skeleton, const USkeleton * ReferenceSkeleton) const
{
	check(Skeleton != nullptr);
	check(ReferenceSkeleton != nullptr);

	UE_LOG(LogMixamoToolkit, Log, TEXT("Retargeting Mixamo skeleton '%s'"), *Skeleton->GetName());

	// Check for a skeleton retargeting on itself.
	if (Skeleton == ReferenceSkeleton)
	{
		UE_LOG(LogMixamoToolkit, Warning, TEXT("Skipping retargeting of Mixamo skeleton '%s' on itself"), *Skeleton->GetName());
		return;
	}

	// Check for invalid root bone (root bone not at position 0)
	if (HasFakeRootBone(Skeleton))
	{
		UE_LOG(LogMixamoToolkit, Warning, TEXT("Skipping retargeting of Mixamo skeleton '%s'; invalid 'root' bone at index != 0"), *Skeleton->GetName());
		return;
	}

	// Get all USkeletalMesh assets using Skeleton (i.e. the Mixamo skeleton).
	TArray<USkeletalMesh *> SkeletalMeshes;
	GetAllSkeletalMeshesUsingSkeleton(Skeleton, SkeletalMeshes);

	/*
		Retargeting uses the SkeletalMesh's reference skeleton, as it counts for mesh proportions.
		If you need to use the original Skeleton, you have to ensure Skeleton pose has the same proportions
		of the skeletal mesh we are retargeting calling:
			Skeleton->GetPreviewMesh(true)->UpdateReferencePoseFromMesh(SkeletonMesh);
	*/
	// Add the root bone if needed. This: fixes a offset glitch in the animations, is generally useful.
#ifndef MAR_ADDROOTBONE_DISABLE_
	AddRootBone(Skeleton, SkeletalMeshes);
#endif

	// Be sure that the Skeleton has a preview mesh!
	// without it, retargeting an animation will fail
	if (SkeletalMeshes.Num() > 0)
		SetPreviewMesh(Skeleton, SkeletalMeshes[0]);

	// Create the IKRig assets, one for each input skeleton.
	UIKRigDefinition* MixamoRig = CreateMixamoIKRig(Skeleton);
	UIKRigDefinition* UEMannequinRig = CreateUEMannequinIKRig(ReferenceSkeleton);

	// Create the IKRetarget asset to retarget from the UE Mannequin to Mixamo.
	const FString SkeletonBasePackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
	const FStaticNamesMapper MixamoToUEMannequin_ChainNamesMapping = UEMannequinToMixamo_ChainNamesMapping.GetInverseMapper();
	UIKRetargeter* IKRetargeter_UEMannequinToMixamo = CreateIKRetargeter(
		SkeletonBasePackagePath,
		GetRetargeterName(ReferenceSkeleton, Skeleton),
		UEMannequinRig,
		MixamoRig,
		MixamoToUEMannequin_ChainNamesMapping,
		UEMannequinToMixamo_ChainNamesMapping.MapNames(UEMannequin_SkipChains_ChainNames),
		UEMannequinToMixamo_ChainNamesMapping.MapNames(UEMannequin_DriveIKGoal_ChainNames),
		UEMannequinToMixamo_ChainNamesMapping.MapNames(UEMannequin_OneToOneFKRotationMode_ChainNames)
	);

	// Set-up the translation retargeting modes, to avoid artifacts when retargeting the animations.
	SetupTranslationRetargetingModes(Skeleton);
	// Retarget the base pose of the Mixamo skeletal meshes to match the "UE4_Mannequin_Skeleton" one.
	RetargetBasePose(
		SkeletalMeshes,
		ReferenceSkeleton,
		Mixamo_PreserveComponentSpacePose_BoneNames,
		Mixamo_ForceNewComponentSpacePose_BoneNames,
		UEMannequinToMixamo_BoneNamesMapping.GetInverseMapper(),
		/*bApplyPoseToRetargetBasePose=*/true,
		UIKRetargeterController::GetController(IKRetargeter_UEMannequinToMixamo)
	);

	// = Setup the Mixamo to UE Mannequin retargeting.
	
	// Get all USkeletalMesh assets using ReferenceSkeleton (i.e. the UE Mannequin skeleton).
	TArray<USkeletalMesh*> UEMannequinSkeletalMeshes;
	GetAllSkeletalMeshesUsingSkeleton(ReferenceSkeleton, UEMannequinSkeletalMeshes);

	// Create the IKRetarget asset to retarget from Mixamo to the UE Mannequin.
	UIKRetargeter* IKRetargeter_MixamoToUEMannequin = CreateIKRetargeter(
		SkeletonBasePackagePath,
		GetRetargeterName(Skeleton, ReferenceSkeleton),
		MixamoRig,
		UEMannequinRig,
		UEMannequinToMixamo_ChainNamesMapping,
		UEMannequin_SkipChains_ChainNames,
		UEMannequin_DriveIKGoal_ChainNames,
		UEMannequin_OneToOneFKRotationMode_ChainNames
	);

	// Retarget the base pose of the UE Mannequin skeletal meshes to match the Mixamo skeleton one.
	// Only in the IK Retargeter asset, do not change the UE Mannueqin Skeletal Meshes.
	RetargetBasePose(
		UEMannequinSkeletalMeshes,
		Skeleton,
		UEMannequin_PreserveComponentSpacePose_BoneNames,
		UEMannequin_ForceNewComponentSpacePose_BoneNames,
		UEMannequinToMixamo_BoneNamesMapping,
		/*bApplyPoseToRetargetBasePose=*/false,
		UIKRetargeterController::GetController(IKRetargeter_MixamoToUEMannequin)
	);

	/*
	// Open a content browser showing the specified assets (technically all the content of the directories containing them...).

	TArray<UObject*> ObjectsToSync;
	ObjectsToSync.Add(IKRetargeter_UEMannequinToMixamo);
	ObjectsToSync.Add(IKRetargeter_MixamoToUEMannequin);
	ObjectsToSync.Add(MixamoRig);
	ObjectsToSync.Add(UEMannequinRig);
	GEditor->SyncBrowserToObjects(ObjectsToSync);
	*/
}



/// Return true if Skeleton has a bone named "root" and it's not at position 0; return false otherwise.
bool FMixamoSkeletonRetargeter::HasFakeRootBone(const USkeleton* Skeleton) const
{
	check(Skeleton != nullptr);
	const int32 rootBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(RootBoneName);
	return rootBoneIndex != INDEX_NONE && rootBoneIndex != 0;
}



/// Add the "root" bone to Skeleton and all its SkeletalMeshes.
void FMixamoSkeletonRetargeter::AddRootBone(USkeleton* Skeleton, TArray<USkeletalMesh *> SkeletalMeshes) const
{
	// Skip if the mesh has already a bone named "root".
	if (Skeleton->GetReferenceSkeleton().FindBoneIndex(RootBoneName) != INDEX_NONE)
	{
		return;
	}

	//=== Add the root bone to all the Skeletal Meshes using Skeleton.
	// We'll have to fix the Skeletal Meshes to account for the added root bone.

	// When going out of scope, it'll re-register components with the scene.
	TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;

	// Add the root bone to *all* skeletal meshes in SkeletalMeshes.
	for (int iMesh = 0; iMesh < SkeletalMeshes.Num(); ++ iMesh)
	{
		USkeletalMesh * SkeletonMesh = SkeletalMeshes[iMesh];
		ensure(SkeletonMesh != nullptr);	// @TODO: manage the nullptr case.
		check(SkeletonMesh->GetSkeleton() == Skeleton);

		SkeletonMesh->Modify();

		SkeletonMesh->ReleaseResources();
		SkeletonMesh->ReleaseResourcesFence.Wait();

		// Add the root bone to the skeletal mesh's reference skeleton.
		AddRootBone(SkeletonMesh->GetSkeleton(), &SkeletonMesh->GetRefSkeleton());
		// Fix-up bone transforms and reset RetargetBasePose.
		SkeletonMesh->GetRetargetBasePose().Empty();
		SkeletonMesh->CalculateInvRefMatrices();	// @BUG: UE4 Undo system fails to undo the CalculateInvRefMatrices() effect.

		// As we added a new parent bone, fix "old" Skeletal Mesh indices.
		uint32 LODIndex = 0;
		for (FSkeletalMeshLODModel & LODModel : SkeletonMesh->GetImportedModel()->LODModels)
		{
			// == Fix the list of bones used by LODModel.

			// Increase old ActiveBoneIndices by 1, to compensate the new root bone.
			for (FBoneIndexType & i : LODModel.ActiveBoneIndices)
			{
				++i;
			}
			// Add the new root bone to the ActiveBoneIndices.
			LODModel.ActiveBoneIndices.Insert(0, 0);

			// Increase old RequiredBones by 1, to compensate the new root bone.
			for (FBoneIndexType & i : LODModel.RequiredBones)
			{
				++i;
			}
			// Add the new root bone to the RequiredBones.
			LODModel.RequiredBones.Insert(0, 0);

			// Updated the bone references used by the SkinWeightProfiles
			for (auto & Kvp : LODModel.SkinWeightProfiles)
			{
				FImportedSkinWeightProfileData & SkinWeightProfile = LODModel.SkinWeightProfiles.FindChecked(Kvp.Key);

				// Increase old InfluenceBones by 1, to compensate the new root bone.
				for (FRawSkinWeight & w : SkinWeightProfile.SkinWeights)
				{
					for (int i = 0; i < MAX_TOTAL_INFLUENCES; ++ i)
					{
						if (w.InfluenceWeights[i] > 0)
						{
							++ w.InfluenceBones[i];
						}
					}
				}

				// Increase old BoneIndex by 1, to compensate the new root bone.
				for (SkeletalMeshImportData::FVertInfluence & v: SkinWeightProfile.SourceModelInfluences)
				{
					if (v.Weight > 0)
					{
						++ v.BoneIndex;
					}
				}
			}

			// == Fix the mesh LOD sections.

			// Since UE4.24, newly imported Skeletal Mesh asset (UASSET) are serialized with additional data
			// and are processed differently. On the post-edit change of the asset, the editor automatically
			// re-builds all the sections starting from the stored raw mesh, if available.
			// This is made to properly re-apply the reduction settings after changes.
			// In this case, we must update the bones in the raw mesh and the editor will rebuild LODModel.Sections.
			if (SkeletonMesh->IsLODImportedDataBuildAvailable(LODIndex) && !SkeletonMesh->IsLODImportedDataEmpty(LODIndex))
			{
				FSkeletalMeshImportData RawMesh;
				SkeletonMesh->LoadLODImportedData(LODIndex, RawMesh);

				// Increase old ParentIndex by 1, to compensate the new root bone.
				int32 NumRootChildren = 0;
				for (SkeletalMeshImportData::FBone & b : RawMesh.RefBonesBinary)
				{
					if (b.ParentIndex == INDEX_NONE)
					{
						NumRootChildren += b.NumChildren;
					}
					++ b.ParentIndex;
				}
				// Add the new root bone to the RefBonesBinary.
				check(NumRootChildren > 0);
				const SkeletalMeshImportData::FJointPos NewRootPos = { FTransform3f::Identity, 1.f, 100.f, 100.f, 100.f };
				const SkeletalMeshImportData::FBone NewRoot = { RootBoneName.ToString(), 0, NumRootChildren, INDEX_NONE, NewRootPos };
				RawMesh.RefBonesBinary.Insert(NewRoot, 0);

				// Increase old BoneIndex by 1, to compensate the new root bone.
				// Influences stores the pairs (vertex, bone), no need to add new items.
				for (SkeletalMeshImportData::FRawBoneInfluence & b : RawMesh.Influences)
				{
					++ b.BoneIndex;
				}

				if (RawMesh.MorphTargets.Num() > 0)
				{
					UE_LOG(LogMixamoToolkit, Warning, TEXT("MorphTargets are not supported."));
				}

				if (RawMesh.AlternateInfluences.Num() > 0)
				{
					UE_LOG(LogMixamoToolkit, Warning, TEXT("AlternateInfluences are not supported."));
				}

				SkeletonMesh->SaveLODImportedData(LODIndex, RawMesh);
			}
			else
			{
				// For Skeletal Mesh assets (UASSET) using a pre-UE4.24 format (or missing the raw mesh data),
				// we must manually update the LODModel.Sections to keep them synchronized with the new added root bone.
				for (FSkelMeshSection & LODSection : LODModel.Sections)
				{
					// Increase old BoneMap indices by 1, to compensate the new root bone.
					for (FBoneIndexType & i : LODSection.BoneMap)
					{
						++i;
					}
					// No need to add the new root bone to BoneMap, as no vertices would use it.
					//
					// No need to update LODSection.SoftVertices[] items as FSoftSkinVertex::InfluenceBones
					// contains indices over LODSection.BoneMap, that does't changed items positions.
				}
			}

            ++LODIndex;
		}

		SkeletonMesh->PostEditChange();
		SkeletonMesh->InitResources();

		// Use the modified skeletal mesh to recreate the Skeleton bones structure, so it'll contains also the new root bone.
		// NOTE: this would invalidate the animations.
		Skeleton->Modify();
		if (iMesh == 0)
		{
			// Use the first mesh to re-create the base bone tree...
			Skeleton->RecreateBoneTree(SkeletonMesh);
		}
		else
		{
			// ...and then merge into Skeleton any new bone from SkeletonMesh.
			Skeleton->MergeAllBonesToBoneTree(SkeletonMesh);
		}
	}
}



/**
Add the "root" bone to a Skeletal Mesh's Reference Skeleton (RefSkeleton).
RefSkeleton must be based on Skeleton.
*/
void FMixamoSkeletonRetargeter::AddRootBone(const USkeleton * Skeleton, FReferenceSkeleton * RefSkeleton) const
{
	check(Skeleton != nullptr);
	check(RefSkeleton != nullptr);
	checkf(RefSkeleton->FindBoneIndex(RootBoneName) == INDEX_NONE, TEXT("The reference skeleton has already a \"root\" bone."));

	//=== Create a new FReferenceSkeleton with the root bone added.
	FReferenceSkeleton NewRefSkeleton;
	{
		// Destructor rebuilds the ref-skeleton.
		FReferenceSkeletonModifier RefSkeletonModifier(NewRefSkeleton, Skeleton);

		// Add the new root bone.
		const FMeshBoneInfo Root(RootBoneName, RootBoneName.ToString(), INDEX_NONE);
		RefSkeletonModifier.Add(Root, FTransform::Identity);

		// Copy and update existing bones indexes to get rid of the added root bone.
		for (int32 i = 0; i < RefSkeleton->GetRawBoneNum(); ++i)
		{
			FMeshBoneInfo info = RefSkeleton->GetRawRefBoneInfo()[i];
			info.ParentIndex += 1;
			const FTransform & pose = RefSkeleton->GetRawRefBonePose()[i];
			RefSkeletonModifier.Add(info, pose);
		}
	}

	// Set the new Reference Skeleton.
	*RefSkeleton = NewRefSkeleton;
}



/**
Setup the "Translation Retargeting" options for Skeleton (that is expected to be a Mixamo skeleton).

This options are used by Unreal Engine to retarget animations using Skeleton
(and NOT to retarget animations using a different skeleton asset, this is done considering the retargeting pose instead).
The reason is that skeletal meshes using the same skeleton can have different sizes and proportions,
these options allow Unreal Engine to adapt an animation authored for a specific skeletal mesh to
a skeletal mesh with different proportions (but based on the same skeleton).

See:
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimationRetargeting/index.html#settingupretargeting
- https://docs.unrealengine.com/latest/INT/Engine/Animation/RetargetingDifferentSkeletons/#retargetingadjustments
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimHowTo/Retargeting/index.html#retargetingusingthesameskeleton
*/
void FMixamoSkeletonRetargeter::SetupTranslationRetargetingModes(USkeleton* Skeleton) const
{
	check(Skeleton != nullptr);

	const FReferenceSkeleton & RefSkeleton = Skeleton->GetReferenceSkeleton();
	Skeleton->Modify();

	// Convert all bones, starting from the root one, to "Skeleton".
	// This will ensure that all bones use the skeleton's static translation.
	const int32 RootIndex = 0;
#ifndef MAR_ADDROOTBONE_DISABLE_
	checkf(RefSkeleton.FindBoneIndex(RootBoneName) == RootIndex, TEXT("Root bone at index 0"));
#endif
	Skeleton->SetBoneTranslationRetargetingMode(RootIndex, EBoneTranslationRetargetingMode::Skeleton, true);
	// Set the Pelvis bone (in Mixamo it's called "Hips") to AnimationScaled.
	// This will make sure that the bone sits at the right height and is still animated.
	const int32 PelvisIndex = RefSkeleton.FindBoneIndex(TEXT("Hips"));
	if (PelvisIndex != INDEX_NONE)
	{
		Skeleton->SetBoneTranslationRetargetingMode(PelvisIndex, EBoneTranslationRetargetingMode::AnimationScaled);
	}
	// Find the Root bone, any IK bones, any Weapon bones you may be using or other marker-style bones and set them to Animation.
	// This will make sure that bone's translation comes from the animation data itself and is unchanged.
	// @TODO: do it for IK bones.
	Skeleton->SetBoneTranslationRetargetingMode(RootIndex, EBoneTranslationRetargetingMode::Animation);
}



/**
Configure the "retarget pose" of SkeletalMeshes to match the "reference pose" of ReferenceSkeleton.

This is the pose needed by Unreal Engine to properly retarget animations involving different skeletons.
Animations are handled as additive bone transformations respect to the base pose of the skeletal mesh
for which they have been authored.

The new "retarget base pose" is then stored/applied accordingly to the inputs.

@param SkeletalMeshes	Skeletal Meshes for which a "retarget pose" must be configured.
@param ReferenceSkeleton	The skeleton with the "reference pose" that must be matched.
	Here we use the term "reference" to indicate the skeleton we want to match,
	do not confuse it with the "reference skeleton" term used by Unreal Engine to indicate the actual
	and concrete skeleton used by a Skeletal Mesh or Skeleton asset.
@param PreserveCSBonesNames	The bone names, in SkeletalMeshes's reference skeletons, that must preserve their Component Space transform.
@param ForceNewCSBoneNames	The bone names, in SkeletalMeshes's reference skeletons, that must be forcefully "posed" also if they control multiple child bones.
@param EditToReference_BoneNamesMapping Mapping of bone names from the edited skeleton (ie. from SkeletalMeshes's reference skeletons) to
	ReferenceSkeleton
@param bApplyPoseToRetargetBasePose If true, the computed "retarget pose" is applied to the "Retarget Base Pose" of the processed Skeletal Mesh.
@param Controller The IK Retargeter Controller to use to store the computed "retarget poses" in the processed Skeletal Mesh. Can be nullptr.
*/
void FMixamoSkeletonRetargeter::RetargetBasePose(
	TArray<USkeletalMesh *> SkeletalMeshes,
	const USkeleton * ReferenceSkeleton,
	const TArray<FName>& PreserveCSBonesNames,
	const TArray<FName>& ForceNewCSBoneNames,
	const FStaticNamesMapper & EditToReference_BoneNamesMapping,
	bool bApplyPoseToRetargetBasePose,
	UIKRetargeterController* Controller
) const
{
	check(ReferenceSkeleton != nullptr);
	check(Controller);
	checkf(bApplyPoseToRetargetBasePose || Controller != nullptr, TEXT("Computed retarget pose must be saved/applied somewhere"));

	// @NOTE: UE4 mannequin skeleton must have same pose & proportions as of its skeletal mesh.
	FSkeletonPoser poser(ReferenceSkeleton, ReferenceSkeleton->GetReferenceSkeleton().GetRefBonePose());

	// Retarget all Skeletal Meshes using Skeleton.
	for (USkeletalMesh * Mesh : SkeletalMeshes)
	{
		Controller->AddRetargetPose(Mesh->GetFName());

		// Some of Mixamo's bones need a different rotation respect to UE4 mannequin reference pose.
		// An analytics solution would be preferred, but (for now) preserving the CS pose of their
		// children bones works quite well.
		TArray<FTransform> MeshBonePose;
		poser.PoseBasedOnMappedBoneNames(Mesh, PreserveCSBonesNames, ForceNewCSBoneNames, EditToReference_BoneNamesMapping, MeshBonePose);
		if (bApplyPoseToRetargetBasePose)
		{
			FSkeletonPoser::ApplyPoseToRetargetBasePose(Mesh, MeshBonePose);
		}
		if (Controller != nullptr)
		{
			FSkeletonPoser::ApplyPoseToIKRetargetPose(Mesh, Controller, MeshBonePose);
		}

		//@todo Add IK bones if possible.
	}

	// Ensure the Controller is set with the pose of the rendered preview mesh.
	if (Controller != nullptr)
	{
		if (USkeletalMesh* PreviewMesh = Controller->GetTargetPreviewMesh())
		{
			Controller->SetCurrentRetargetPose(PreviewMesh->GetFName());
		}
		else
		{
			Controller->SetCurrentRetargetPose(Controller->GetAsset()->GetDefaultPoseName());
		}
	}
}



/**
Ask to the user the Skeleton to use as "reference" for the retargeting.

I.e. the one to which we want to retarget the currently processed skeleton.
*/
USkeleton * FMixamoSkeletonRetargeter::AskUserForTargetSkeleton() const
{
	TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
		.Title(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_WindowTitle", "Select retargeting skeleton"))
		.ClientSize(FVector2D(500, 600))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.HasCloseButton(false);

	TSharedRef<SRiggedSkeletonPicker> RiggedSkeletonPicker = SNew(SRiggedSkeletonPicker)
		.Title(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_Title", "Select a Skeleton asset to use as retarget source."))
		.Description(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_Description", "For optimal results, it should be the standard Unreal Engine mannequin skeleton."))
		.OnShouldFilterAsset(SRiggedSkeletonPicker::FOnShouldFilterAsset::CreateRaw(this, &FMixamoSkeletonRetargeter::OnShouldFilterNonUEMannequinSkeletonAsset));

	WidgetWindow->SetContent(RiggedSkeletonPicker);
	GEditor->EditorAddModalWindow(WidgetWindow);

	return RiggedSkeletonPicker->GetSelectedSkeleton();
}


bool FMixamoSkeletonRetargeter::AskUserOverridingAssetsConfirmation(const TArray<UObject*>& AssetsToOverwrite) const
{
	TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
		.Title(LOCTEXT("FMixamoSkeletonRetargeter_AskUserOverridingAssetsConfirmation_WindowTitle", "Overwrite confirmation"))
		.ClientSize(FVector2D(400, 450))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.HasCloseButton(false);

	TSharedRef<SOverridingAssetsConfirmationDialog> ConfirmationDialog 
		= SNew(SOverridingAssetsConfirmationDialog)
		.AssetsToOverwrite(AssetsToOverwrite);

	WidgetWindow->SetContent(ConfirmationDialog);
	GEditor->EditorAddModalWindow(WidgetWindow);

	return ConfirmationDialog->HasConfirmed();
}


/// Return the FAssetData of all the skeletal meshes based on Skeleton.
void FMixamoSkeletonRetargeter::GetAllSkeletalMeshesUsingSkeleton(const USkeleton * Skeleton, TArray<FAssetData> & SkeletalMeshes) const
{
	SkeletalMeshes.Empty();

	FARFilter Filter;
	Filter.ClassNames.Add(USkeletalMesh::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	const FString SkeletonString = FAssetData(Skeleton).GetExportTextName();
	Filter.TagsAndValues.Add(USkeletalMesh::GetSkeletonMemberName(), SkeletonString);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, SkeletalMeshes);
}



/**
Return the Skeletal Mesh assets of all the skeletal meshes based on Skeleton.

This will load all the returned Skeletal Meshes.
*/
void FMixamoSkeletonRetargeter::GetAllSkeletalMeshesUsingSkeleton(const USkeleton* Skeleton, TArray<USkeletalMesh *>& SkeletalMeshes) const
{
	TArray<FAssetData> Assets;
	GetAllSkeletalMeshesUsingSkeleton(Skeleton, Assets);
	SkeletalMeshes.Reset(Assets.Num());
	for (FAssetData& Asset : Assets)
	{
		// This will load the asset if needed.
		SkeletalMeshes.Emplace(CastChecked<USkeletalMesh>(Asset.GetAsset()));
	}
}



/// If Skeleton doesn't already have a Preview Mesh, then set it to PreviewMesh.
void FMixamoSkeletonRetargeter::SetPreviewMesh(USkeleton * Skeleton, USkeletalMesh * PreviewMesh) const
{
	check(Skeleton != nullptr);

	if (Skeleton->GetPreviewMesh() == nullptr && PreviewMesh != nullptr)
		Skeleton->SetPreviewMesh(PreviewMesh);
}



void FMixamoSkeletonRetargeter::EnumerateAssetsToOverwrite(const USkeleton* Skeleton, const USkeleton* ReferenceSkeleton, TArray<UObject*>& AssetsToOverride) const
{
	auto AddIfExists = [&AssetsToOverride](const FString& PackagePath, const FString& AssetName)
	{
		const FString LongPackageName = PackagePath / AssetName;

		UObject* Package = StaticFindObject(UObject::StaticClass(), nullptr, *LongPackageName);

		if (!Package)
		{
			Package = LoadPackage(nullptr, *LongPackageName, LOAD_NoWarn);
		}

		if (Package)
		{
			if (UObject* Obj = FindObject<UObject>(Package, *AssetName))
			{
				AssetsToOverride.AddUnique(Obj);
			}
		}
	};

	{
		const FString PackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
		AddIfExists(PackagePath, GetRigName(Skeleton));
	}
	{
		const FString PackagePath = FPackageName::GetLongPackagePath(ReferenceSkeleton->GetPackage()->GetName());
		AddIfExists(PackagePath, GetRigName(ReferenceSkeleton));
	}
	{
		const FString PackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
		AddIfExists(PackagePath, GetRetargeterName(Skeleton, ReferenceSkeleton));
	}
	{
		const FString PackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
		AddIfExists(PackagePath, GetRetargeterName(ReferenceSkeleton, Skeleton));
	}
}



/**
Create an IK Rig asset for Skeleton (it's Preview Mesh).
*/
UIKRigDefinition* FMixamoSkeletonRetargeter::CreateIKRig(
	const FString & PackagePath,
	const FString & AssetName,
	const USkeleton* Skeleton
) const
{
	check(Skeleton);

	const FString LongPackageName = PackagePath / AssetName;
	UPackage* Package = UPackageTools::FindOrCreatePackageForAssetType(FName(*LongPackageName), UIKRigDefinition::StaticClass());
	check(Package);

	UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(Package, FName(*AssetName), RF_Standalone | RF_Public | RF_Transactional);

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(IKRig);
	// Mark the package dirty...
	Package->MarkPackageDirty();

	// imports the skeleton data into the IK Rig
	UIKRigController* Controller = UIKRigController::GetIKRigController(IKRig);
	Controller->SetSkeletalMesh(Skeleton->GetPreviewMesh());

	return IKRig;
}



/**
Create an IK Rig asset for a Mixamo Skeleton (it's Preview Mesh).
*/
UIKRigDefinition* FMixamoSkeletonRetargeter::CreateMixamoIKRig(const USkeleton* Skeleton) const
{
	check(Skeleton);

	const FString PackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
	UIKRigDefinition* IKRig = CreateIKRig(PackagePath, GetRigName(Skeleton), Skeleton);

	// imports the skeleton data into the IK Rig
	UIKRigController* Controller = UIKRigController::GetIKRigController(IKRig);
	Controller->SetSkeletalMesh(Skeleton->GetPreviewMesh());

	static const FName RetargetRootBone(TEXT("Hips"));

#ifndef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	Controller->AddRetargetChain(TEXT("Root"), TEXT("root"), TEXT("root"));
	Controller->AddRetargetChain(TEXT("Spine"), TEXT("Spine"), TEXT("Spine2"));
	Controller->AddRetargetChain(TEXT("Head"), TEXT("Neck"), TEXT("head"));
	static const FName LeftClavicleChainName(TEXT("LeftClavicle"));
	Controller->AddRetargetChain(LeftClavicleChainName, TEXT("LeftShoulder"), TEXT("LeftShoulder"));
	static const FName LeftArmChainName(TEXT("LeftArm"));
	static const FName LeftHandBoneName(TEXT("LeftHand"));
	Controller->AddRetargetChain(LeftArmChainName, TEXT("LeftArm"), LeftHandBoneName);
	static const FName RightClavicleChainName(TEXT("RightClavicle"));
	Controller->AddRetargetChain(RightClavicleChainName, TEXT("RightShoulder"), TEXT("RightShoulder"));
	static const FName RightArmChainName(TEXT("RightArm"));
	static const FName RightHandBoneName(TEXT("RightHand"));
	Controller->AddRetargetChain(RightArmChainName, TEXT("RightArm"), RightHandBoneName);
	static const FName LeftLegChainName(TEXT("LeftLeg"));
	static const FName LeftToeBaseBoneName(TEXT("LeftToeBase"));
	Controller->AddRetargetChain(LeftLegChainName, TEXT("LeftUpLeg"), LeftToeBaseBoneName);
	static const FName RightLegChainName(TEXT("RightLeg"));
	static const FName RightToeBaseBoneName(TEXT("RightToeBase"));
	Controller->AddRetargetChain(RightLegChainName, TEXT("RightUpLeg"), RightToeBaseBoneName);
	Controller->AddRetargetChain(TEXT("LeftIndex"), TEXT("LeftHandIndex1"), TEXT("LeftHandIndex3"));
	Controller->AddRetargetChain(TEXT("RightIndex"), TEXT("RightHandIndex1"), TEXT("RightHandIndex3"));
	Controller->AddRetargetChain(TEXT("LeftMiddle"), TEXT("LeftHandMiddle1"), TEXT("LeftHandMiddle3"));
	Controller->AddRetargetChain(TEXT("RightMiddle"), TEXT("RightHandMiddle1"), TEXT("RightHandMiddle3"));
	Controller->AddRetargetChain(TEXT("LeftPinky"), TEXT("LeftHandPinky1"), TEXT("LeftHandPinky3"));
	Controller->AddRetargetChain(TEXT("RightPinky"), TEXT("RightHandPinky1"), TEXT("RightHandPinky3"));
	Controller->AddRetargetChain(TEXT("LeftRing"), TEXT("LeftHandRing1"), TEXT("LeftHandRing3"));
	Controller->AddRetargetChain(TEXT("RightRing"), TEXT("RightHandRing1"), TEXT("RightHandRing3"));
	Controller->AddRetargetChain(TEXT("LeftThumb"), TEXT("LeftHandThumb1"), TEXT("LeftHandThumb3"));
	Controller->AddRetargetChain(TEXT("RightThumb"), TEXT("RightHandThumb1"), TEXT("RightHandThumb3"));

#ifndef MAR_IKRETARGETER_IKSOLVERS_DISABLE_

	const int32 SolverIndex = Controller->AddSolver(UIKRigPBIKSolver::StaticClass());
	UIKRigPBIKSolver* Solver = CastChecked<UIKRigPBIKSolver>(Controller->GetSolver(SolverIndex));
	Solver->SetRootBone(RetargetRootBone);
	//Solver->RootBehavior = EPBIKRootBehavior::PinToInput;

	// Hips bone settings
	static const FName HipsBoneName(TEXT("Hips"));
	Controller->AddBoneSetting(HipsBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* HipsBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(HipsBoneName, SolverIndex));
	HipsBoneSettings->RotationStiffness = 0.99f;

	// Spine bone settings
	static const FName SpineBoneName(TEXT("Spine"));
	Controller->AddBoneSetting(SpineBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* SpineBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(SpineBoneName, SolverIndex));
	SpineBoneSettings->RotationStiffness = 0.7f;

	// Spine1 bone settings
	static const FName Spine1BoneName(TEXT("Spine1"));
	Controller->AddBoneSetting(Spine1BoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* Spine1BoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(Spine1BoneName, SolverIndex));
	Spine1BoneSettings->RotationStiffness = 0.8f;

	// Spine2 bone settings
	static const FName Spine2BoneName(TEXT("Spine2"));
	Controller->AddBoneSetting(Spine2BoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* Spine2BoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(Spine2BoneName, SolverIndex));
	Spine2BoneSettings->RotationStiffness = 0.95f;

	// Left Shoulder bone settings
	static const FName LeftShoulderBoneName(TEXT("LeftShoulder"));
	Controller->AddBoneSetting(LeftShoulderBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LeftShoulderBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftShoulderBoneName, SolverIndex));
	LeftShoulderBoneSettings->RotationStiffness = 0.99f;

	// Left Hand goal
	static const FName LeftHandGoalName(TEXT("LeftHand_Goal"));
	UIKRigEffectorGoal* LeftHandGoal = Controller->AddNewGoal(LeftHandGoalName, LeftHandBoneName);
	LeftHandGoal->bExposePosition = true;
	LeftHandGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*LeftHandGoal, SolverIndex);
	Controller->SetRetargetChainGoal(LeftArmChainName, LeftHandGoalName);
	UIKRig_FBIKEffector* LeftHandGoalSettings = CastChecked<UIKRig_FBIKEffector>(Controller->GetGoalSettingsForSolver(LeftHandGoalName, SolverIndex));
	LeftHandGoalSettings->PullChainAlpha = 0.f;

	// Right Shoulder bone settings
	static const FName RightShoulderBoneName(TEXT("RightShoulder"));
	Controller->AddBoneSetting(RightShoulderBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * RightShoulderBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightShoulderBoneName, SolverIndex));
	RightShoulderBoneSettings->RotationStiffness = 0.99f;

	// Right Hand goal
	static const FName RightHandGoalName(TEXT("RightHand_Goal"));
	UIKRigEffectorGoal* RightHandGoal = Controller->AddNewGoal(RightHandGoalName, RightHandBoneName);
	RightHandGoal->bExposePosition = true;
	RightHandGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*RightHandGoal, SolverIndex);
	Controller->SetRetargetChainGoal(RightArmChainName, RightHandGoalName);
	UIKRig_FBIKEffector* RightHandGoalSettings = CastChecked<UIKRig_FBIKEffector>(Controller->GetGoalSettingsForSolver(RightHandGoalName, SolverIndex));
	RightHandGoalSettings->PullChainAlpha = 0.f;

	// Left forearm settings
	static const FName LeftForeArmBoneName(TEXT("LeftForeArm"));
	Controller->AddBoneSetting(LeftForeArmBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LeftForeArmBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftForeArmBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		LeftForeArmBoneSettings,
		FName("LeftHand"),
		{0, -90, 0},
		FVector::UpVector
	);

	// Right forearm settings
	static const FName RightForeArmBoneName(TEXT("RightForeArm"));
	Controller->AddBoneSetting(RightForeArmBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* RightForeArmBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightForeArmBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		RightForeArmBoneSettings,
		FName("RightHand"),
		{ 0, 90, 0 },
		FVector::UpVector
	);

	// Left Up Leg bone settings
	static const FName LeftUpLegBoneName(TEXT("LeftUpLeg"));
	Controller->AddBoneSetting(LeftUpLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LeftUpLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftUpLegBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		LeftUpLegBoneSettings,
		FName("LeftLeg"),
		{ 0, -90, 0 },
		FVector::ForwardVector
	);

	// Left Leg bone settings
	static const FName LeftLegBoneName(TEXT("LeftLeg"));
	Controller->AddBoneSetting(LeftLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * LeftLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftLegBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		LeftLegBoneSettings,
		FName("LeftFoot"),
		{ 0, 90, 0 },
		FVector::ForwardVector
	);

	// Left Foot goal
	static const FName LeftFootGoalName(TEXT("LeftFoot_Goal"));
	UIKRigEffectorGoal* LeftFootGoal = Controller->AddNewGoal(LeftFootGoalName, LeftToeBaseBoneName);
	LeftFootGoal->bExposePosition = true;
	LeftFootGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*LeftFootGoal, SolverIndex);
	Controller->SetRetargetChainGoal(LeftLegChainName, LeftFootGoalName);

	static const FName RightUpLegBoneName(TEXT("RightUpLeg"));
	Controller->AddBoneSetting(RightUpLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* RightUpLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightUpLegBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		RightUpLegBoneSettings,
		FName("RightLeg"),
		{ 0, -90, 0 },
		FVector::ForwardVector
	);

	// Right Leg bone settings
	static const FName RightLegBoneName(TEXT("RightLeg"));
	Controller->AddBoneSetting(RightLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * RightLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightLegBoneName, SolverIndex));
	ConfigureBonePreferredAnglesLocalToBS(
		Skeleton,
		RightLegBoneSettings,
		FName("RightFoot"),
		{ 0, 90, 0 },
		FVector::ForwardVector
	);

	// Right Foot goal
	static const FName RightFootGoalName(TEXT("RightFoot_Goal"));
	UIKRigEffectorGoal* RightFootGoal = Controller->AddNewGoal(RightFootGoalName, RightToeBaseBoneName);
	RightFootGoal->bExposePosition = true;
	RightFootGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*RightFootGoal, SolverIndex);
	Controller->SetRetargetChainGoal(RightLegChainName, RightFootGoalName);

#endif // MAR_IKRETARGETER_IKSOLVERS_DISABLE_
	
#else // MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	TArray<FName> BoneNames;
	UEMannequinToMixamo_BoneNamesMapping.GetDestination(BoneNames);
	for (const FName& BoneName : BoneNames)
	{
		Controller->AddRetargetChain(/*ChainName=*/BoneName, /*StartBoneName=*/BoneName, /*EndBoneName=*/BoneName);
	}

#endif // MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	Controller->SetRetargetRoot(RetargetRootBone);

	return IKRig;
}



/**
Create an IK Rig asset for a UE Mannequin Skeleton (it's Preview Mesh).
*/
UIKRigDefinition* FMixamoSkeletonRetargeter::CreateUEMannequinIKRig(const USkeleton* Skeleton) const
{
	check(Skeleton != nullptr);

	const FString PackagePath = FPackageName::GetLongPackagePath(Skeleton->GetPackage()->GetName());
	UIKRigDefinition* IKRig = CreateIKRig(PackagePath, GetRigName(Skeleton), Skeleton);

	// imports the skeleton data into the IK Rig
	UIKRigController* Controller = UIKRigController::GetIKRigController(IKRig);
	Controller->SetSkeletalMesh(Skeleton->GetPreviewMesh());

	static const FName RetargetRootBone(TEXT("pelvis"));

#ifndef MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	Controller->AddRetargetChain(TEXT("Root"), TEXT("root"), TEXT("root"));
	Controller->AddRetargetChain(TEXT("Spine"), TEXT("spine_01"), TEXT("spine_03"));
	Controller->AddRetargetChain(TEXT("Head"), TEXT("neck_01"), TEXT("head"));
	static const FName LeftClavicleChainName(TEXT("LeftClavicle"));
	Controller->AddRetargetChain(LeftClavicleChainName, TEXT("clavicle_l"), TEXT("clavicle_l"));
	static const FName LeftArmChainName(TEXT("LeftArm"));
	static const FName LeftHandBoneName(TEXT("hand_l"));
	Controller->AddRetargetChain(LeftArmChainName, TEXT("upperarm_l"), LeftHandBoneName);
	static const FName RightClavicleChainName(TEXT("RightClavicle"));
	Controller->AddRetargetChain(RightClavicleChainName, TEXT("clavicle_r"), TEXT("clavicle_r"));
	static const FName RightArmChainName(TEXT("RightArm"));
	static const FName RightHandBoneName(TEXT("hand_r"));
	Controller->AddRetargetChain(RightArmChainName, TEXT("upperarm_r"), RightHandBoneName);
	static const FName LeftLegChainName(TEXT("LeftLeg"));
	static const FName LeftBallBoneName(TEXT("ball_l"));
	Controller->AddRetargetChain(TEXT("LeftLeg"), TEXT("thigh_l"), LeftBallBoneName);
	static const FName RightLegChainName(TEXT("RightLeg"));
	static const FName RightBallBoneName(TEXT("ball_r"));
	Controller->AddRetargetChain(RightLegChainName, TEXT("thigh_r"), RightBallBoneName);
	Controller->AddRetargetChain(TEXT("LeftIndex"), TEXT("index_01_l"), TEXT("index_03_l"));
	Controller->AddRetargetChain(TEXT("RightIndex"), TEXT("index_01_r"), TEXT("index_03_r"));
	Controller->AddRetargetChain(TEXT("LeftMiddle"), TEXT("middle_01_l"), TEXT("middle_03_l"));
	Controller->AddRetargetChain(TEXT("RightMiddle"), TEXT("middle_01_r"), TEXT("middle_03_r"));
	Controller->AddRetargetChain(TEXT("LeftPinky"), TEXT("pinky_01_l"), TEXT("pinky_03_l"));
	Controller->AddRetargetChain(TEXT("RightPinky"), TEXT("pinky_01_r"), TEXT("pinky_03_r"));
	Controller->AddRetargetChain(TEXT("LeftRing"), TEXT("ring_01_l"), TEXT("ring_03_l"));
	Controller->AddRetargetChain(TEXT("RightRing"), TEXT("ring_01_r"), TEXT("ring_03_r"));
	Controller->AddRetargetChain(TEXT("LeftThumb"), TEXT("thumb_01_l"), TEXT("thumb_03_l"));
	Controller->AddRetargetChain(TEXT("RightThumb"), TEXT("thumb_01_r"), TEXT("thumb_03_r"));

#ifndef MAR_IKRETARGETER_IKSOLVERS_DISABLE_

	const int32 SolverIndex = Controller->AddSolver(UIKRigPBIKSolver::StaticClass());
	UIKRigPBIKSolver* Solver = CastChecked<UIKRigPBIKSolver>(Controller->GetSolver(SolverIndex));
	Solver->SetRootBone(RetargetRootBone);
	//Solver->RootBehavior = EPBIKRootBehavior::PinToInput;

	// Pelvis bone settings
	static const FName PelvisBoneName(TEXT("pelvis"));
	Controller->AddBoneSetting(PelvisBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* HipsBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(PelvisBoneName, SolverIndex));
	HipsBoneSettings->RotationStiffness = 1.0f;

	// Spine_01 bone settings
	static const FName Spine1BoneName(TEXT("spine_01"));
	Controller->AddBoneSetting(Spine1BoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* Spine1BoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(Spine1BoneName, SolverIndex));
	Spine1BoneSettings->RotationStiffness = 0.784f;

	// Spine_02 bone settings
	static const FName Spine2BoneName(TEXT("spine_02"));
	Controller->AddBoneSetting(Spine2BoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* Spine2BoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(Spine2BoneName, SolverIndex));
	Spine2BoneSettings->RotationStiffness = 0.928f;

	// Spine_03 bone settings
	static const FName Spine3BoneName(TEXT("spine_03"));
	Controller->AddBoneSetting(Spine3BoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* Spine3BoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(Spine3BoneName, SolverIndex));
	Spine3BoneSettings->RotationStiffness = 0.936f;

	// Clavicle Left bone settings
	static const FName ClavicleLeftBoneName(TEXT("clavicle_l"));
	Controller->AddBoneSetting(ClavicleLeftBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* ClavicleLeftBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(ClavicleLeftBoneName, SolverIndex));
	ClavicleLeftBoneSettings->RotationStiffness = 1.0f;

	// Left Lower arm bone settings
	static const FName LowerArmLeftBoneName(TEXT("lowerarm_l"));
	Controller->AddBoneSetting(LowerArmLeftBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LowerArmLeftBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LowerArmLeftBoneName, SolverIndex));
	LowerArmLeftBoneSettings->bUsePreferredAngles = true;
	LowerArmLeftBoneSettings->PreferredAngles = { 0, 0, 90 };

	// Left Hand goal
	static const FName LeftHandGoalName(TEXT("hand_l_Goal"));
	UIKRigEffectorGoal* LeftHandGoal = Controller->AddNewGoal(LeftHandGoalName, LeftHandBoneName);
	LeftHandGoal->bExposePosition = true;
	LeftHandGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*LeftHandGoal, SolverIndex);
	Controller->SetRetargetChainGoal(LeftArmChainName, LeftHandGoalName);
	UIKRig_FBIKEffector* LeftHandGoalSettings = CastChecked<UIKRig_FBIKEffector>(Controller->GetGoalSettingsForSolver(LeftHandGoalName, SolverIndex));
	LeftHandGoalSettings->PullChainAlpha = 0.f;

	// Clavicle Right bone settings
	static const FName ClavicleRightBoneName(TEXT("clavicle_r"));
	Controller->AddBoneSetting(ClavicleRightBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * ClavicleRightBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(ClavicleRightBoneName, SolverIndex));
	ClavicleRightBoneSettings->RotationStiffness = 1.0f;

	// Right Lower arm bone settings
	static const FName LowerArmRightBoneName(TEXT("lowerarm_r"));
	Controller->AddBoneSetting(LowerArmRightBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LowerArmRightBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LowerArmRightBoneName, SolverIndex));
	LowerArmRightBoneSettings->bUsePreferredAngles = true;
	LowerArmRightBoneSettings->PreferredAngles = { 0, 0, 90 };

	// Right Hand goal
	static const FName RightHandGoalName(TEXT("hand_r_Goal"));
	UIKRigEffectorGoal* RightHandGoal = Controller->AddNewGoal(RightHandGoalName, RightHandBoneName);
	RightHandGoal->bExposePosition = true;
	RightHandGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*RightHandGoal, SolverIndex);
	Controller->SetRetargetChainGoal(RightArmChainName, RightHandGoalName);
	UIKRig_FBIKEffector* RightHandGoalSettings = CastChecked<UIKRig_FBIKEffector>(Controller->GetGoalSettingsForSolver(RightHandGoalName, SolverIndex));
	RightHandGoalSettings->PullChainAlpha = 0.f;

	// Left Leg bone settings
	static const FName LeftLegBoneName(TEXT("calf_l"));
	Controller->AddBoneSetting(LeftLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * LeftLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftLegBoneName, SolverIndex));
	LeftLegBoneSettings->bUsePreferredAngles = true;
	LeftLegBoneSettings->PreferredAngles = { 0, 0, 90 };
	
	// Left Foot goal
	static const FName LeftFootGoalName(TEXT("foot_l_Goal"));
	UIKRigEffectorGoal* LeftFootGoal = Controller->AddNewGoal(LeftFootGoalName, LeftBallBoneName);
	LeftFootGoal->bExposePosition = true;
	LeftFootGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*LeftFootGoal, SolverIndex);
	Controller->SetRetargetChainGoal(LeftLegChainName, LeftFootGoalName);

	// Right Leg bone settings
	static const FName RightLegBoneName(TEXT("calf_r"));
	Controller->AddBoneSetting(RightLegBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings * RightLegBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightLegBoneName, SolverIndex));
	RightLegBoneSettings->bUsePreferredAngles = true;
	RightLegBoneSettings->PreferredAngles = { 0, 0, 90 };

	// Right Foot goal
	static const FName RightFootGoalName(TEXT("foot_r_Goal"));
	UIKRigEffectorGoal* RightFootGoal = Controller->AddNewGoal(RightFootGoalName, RightBallBoneName);
	RightFootGoal->bExposePosition = true;
	RightFootGoal->bExposeRotation = true;
	Controller->ConnectGoalToSolver(*RightFootGoal, SolverIndex);
	Controller->SetRetargetChainGoal(RightLegChainName, RightFootGoalName);

	// Left Thigh bone settings
	static const FName LeftThighBoneName(TEXT("thigh_l"));
	Controller->AddBoneSetting(LeftThighBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* LeftThighBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(LeftThighBoneName, SolverIndex));
	LeftThighBoneSettings->bUsePreferredAngles = true;
	LeftThighBoneSettings->PreferredAngles = { 0, 0, -90 };

	// Right Thigh bone settings
	static const FName RightThighBoneName(TEXT("thigh_r"));
	Controller->AddBoneSetting(RightThighBoneName, SolverIndex);
	UIKRig_PBIKBoneSettings* RightThighBoneSettings = CastChecked<UIKRig_PBIKBoneSettings>(Controller->GetSettingsForBone(RightThighBoneName, SolverIndex));
	RightThighBoneSettings->bUsePreferredAngles = true;
	RightThighBoneSettings->PreferredAngles = { 0, 0, -90 };

#endif // MAR_IKRETARGETER_IKSOLVERS_DISABLE_

#else // MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	TArray<FName> BoneNames;
	UEMannequinToMixamo_BoneNamesMapping.GetSource(BoneNames);
	for (const FName& BoneName : BoneNames)
	{
		Controller->AddRetargetChain(/*ChainName=*/BoneName, /*StartBoneName=*/BoneName, /*EndBoneName=*/BoneName);
	}

#endif // MAR_IKRETARGETER_ADVANCED_CHAINS_DISABLE_

	Controller->SetRetargetRoot(RetargetRootBone);

	return IKRig;
}



/**
Create an IK Retargeter asset from Source Rig to Target Rig.

@param TargetToSource_ChainNamesMapping Mapper of chain names from the TargetRig to the SourceRig.
@param BoneChainsToSkip Set of IK Rig chain names (relative to TargetRig) for which a "retarget chain" must not be configured.
@param TargetBoneChainsDriveIKGoalToSource Set of IK Rig chain names (relative to TargetRig) for which a the "Drive IK Goal" must be configured.
*/
UIKRetargeter* FMixamoSkeletonRetargeter::CreateIKRetargeter(
	const FString & PackagePath,
	const FString & AssetName,
	UIKRigDefinition* SourceRig,
	UIKRigDefinition* TargetRig,
	const FStaticNamesMapper & TargetToSource_ChainNamesMapping,
	const TArray<FName> & TargetBoneChainsToSkip,
	const TArray<FName> & TargetBoneChainsDriveIKGoal,
	const TArray<FName>& TargetBoneChainsOneToOneRotationMode
) const
{
	/*
	// Using the FAssetToolsModule (add AssetTools to Build.cs and #include "AssetToolsModule.h"):

	FAssetToolsModule& AssetToolsModule = FAssetToolsModule::GetModule();
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetToolsModule.Get().CreateUniqueAssetName(PackagePath / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);
	const FString UniquePackagePath = FPackageName::GetLongPackagePath(UniquePackageName);

	UIKRetargeter* Retargeter = CastChecked<UIKRetargeter>(AssetToolsModule.Get().CreateAsset(UniqueAssetName, UniquePackagePath, UIKRetargeter::StaticClass(), nullptr));
	*/

	const FString LongPackageName = PackagePath / AssetName;
	UPackage* Package = UPackageTools::FindOrCreatePackageForAssetType(FName(*LongPackageName), UIKRetargeter::StaticClass());
	check(Package);

	UIKRetargeter* Retargeter = NewObject<UIKRetargeter>(Package, FName(*AssetName), RF_Standalone | RF_Public | RF_Transactional);

	// Notify the asset registry
	FAssetRegistryModule::AssetCreated(Retargeter);
	// Mark the package dirty...
	Package->MarkPackageDirty();

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	Controller->SetSourceIKRig(SourceRig);

	// Controller->SetTargetIKRig(TargetRig) is BUGGED, do not set the TargetIKRig! Set it with reflection.
	FObjectPropertyBase* TargetIKRigProperty = CastFieldChecked<FObjectPropertyBase>(
		UIKRetargeter::StaticClass()->FindPropertyByName(UIKRetargeter::GetTargetIKRigPropertyName()));
	if (ensure(TargetIKRigProperty))
	{
		void* ptr = TargetIKRigProperty->ContainerPtrToValuePtr<void>(Retargeter);
		check(ptr);
		TargetIKRigProperty->SetObjectPropertyValue(ptr, TargetRig);
	}

	Controller->CleanChainMapping();
	for (URetargetChainSettings* ChainMap : Controller->GetChainMappings())
	{
		// Check if we need to explicitly skip an existing bone chain.
		if (TargetBoneChainsToSkip.Contains(ChainMap->TargetChain))
		{
			continue;
		}
		// Search the mapped bone name.
		const FName SourceChainName = TargetToSource_ChainNamesMapping.MapName(ChainMap->TargetChain);
		// Skip if the targte chain name is not mapped.
		if (SourceChainName.IsNone())
		{
			continue;
		}

		// Add the Target->Source chain name association.
		Controller->SetSourceChainForTargetChain(ChainMap, SourceChainName);

		//= Configure the ChainMap settings

		// this is needed for root motion
		if (SourceChainName == FName("Root"))
		{
			ChainMap->TranslationMode = ERetargetTranslationMode::GloballyScaled;
		}

#ifndef MAR_IKRETARGETER_IKSOLVERS_DISABLE_
		// Configure the DriveIKGoal setting.
		ChainMap->DriveIKGoal = TargetBoneChainsDriveIKGoal.Contains(ChainMap->TargetChain);

		if (TargetBoneChainsOneToOneRotationMode.Contains(ChainMap->TargetChain))
		{
			ChainMap->RotationMode = ERetargetRotationMode::OneToOne;
		}
#endif
	}

	return Retargeter;
}


#undef LOCTEXT_NAMESPACE
