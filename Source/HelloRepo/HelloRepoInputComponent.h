#pragma once

#include "EnhancedInputComponent.h"
#include "HelloRepoInputConfig.h"
#include "HelloRepoInputComponent.generated.h"

/** Binds authored input actions by their gameplay tags. */
UCLASS(Config = Input)
class HELLOREPO_API UHelloRepoInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	/** Returns the binding so its owner can retain its handle for cleanup. */
	template<class UserClass, typename FuncType>
	FEnhancedInputActionEventBinding* BindNativeAction(const UHelloRepoInputConfig* InputConfig,
		const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object,
		FuncType Func, bool bLogNotFound = true)
	{
		if (InputConfig)
		{
			if (const UInputAction* Action = InputConfig->FindNativeInputActionForTag(InputTag, bLogNotFound))
			{
				return &BindAction(Action, TriggerEvent, Object, Func);
			}
		}
		return nullptr;
	}
};
