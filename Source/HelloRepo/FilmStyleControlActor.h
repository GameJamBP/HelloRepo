#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FilmStyleControlActor.generated.h"

class AFilmStyleSetting;

UCLASS(Blueprintable)
class HELLOREPO_API AFilmStyleControlActor : public AActor
{
	GENERATED_BODY()

public:
	AFilmStyleControlActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Control")
	TObjectPtr<AFilmStyleSetting> FilmStyleSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "1"))
	float TraceDistance = 100000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Control")
	int32 ActiveCutIndex = -1;

private:
	bool bWasPressed = false;
	void ScrollCut(int32 Direction);
};
