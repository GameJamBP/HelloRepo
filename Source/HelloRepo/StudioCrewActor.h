#pragma once

#include "CoreMinimal.h"
#include "Animation/SkeletalMeshActor.h"
#include "StudioCrewActor.generated.h"

class UStaticMeshComponent;

/**
 * Crew skeletal mesh actor with a prop mesh natively attached to a hand bone socket.
 */
UCLASS(Blueprintable)
class HELLOREPO_API AStudioCrewActor : public ASkeletalMeshActor
{
	GENERATED_BODY()

public:
	AStudioCrewActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Studio")
	TObjectPtr<UStaticMeshComponent> HandPropMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Studio")
	FName PropAttachSocketName = TEXT("Socket_Hand_R");
};
