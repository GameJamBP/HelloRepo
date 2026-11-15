#include "StudioCrewActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

AStudioCrewActor::AStudioCrewActor()
{
	HandPropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HandPropMesh"));
	HandPropMesh->SetupAttachment(GetSkeletalMeshComponent(), PropAttachSocketName);
	HandPropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AStudioCrewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (HandPropMesh && GetSkeletalMeshComponent())
	{
		HandPropMesh->AttachToComponent(
			GetSkeletalMeshComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			PropAttachSocketName
		);
	}
}
