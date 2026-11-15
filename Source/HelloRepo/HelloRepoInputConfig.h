#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "HelloRepoInputConfig.generated.h"

class UInputAction;

/** Maps an authored input action to the gameplay operation it represents. */
USTRUCT(BlueprintType)
struct HELLOREPO_API FHelloRepoInputAction
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UInputAction> InputAction = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (Categories = "InputTag"))
	FGameplayTag InputTag;
};

/** Action-to-tag configuration; key mappings remain in authored mapping contexts. */
UCLASS(BlueprintType, Const)
class HELLOREPO_API UHelloRepoInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Looks up an exact tag, without matching its parents or children. */
	UFUNCTION(BlueprintCallable, Category = "Input")
	const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input", meta = (TitleProperty = "InputTag"))
	TArray<FHelloRepoInputAction> NativeInputActions;
};
