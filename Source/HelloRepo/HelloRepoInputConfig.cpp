#include "HelloRepoInputConfig.h"

#include "HelloRepo.h"
#include "InputAction.h"

const UInputAction* UHelloRepoInputConfig::FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	if (InputTag.IsValid())
	{
		for (const FHelloRepoInputAction& Action : NativeInputActions)
		{
			if (Action.InputAction && Action.InputTag == InputTag)
			{
				return Action.InputAction;
			}
		}
	}

	if (bLogNotFound)
	{
		UE_LOG(LogHelloRepo, Error, TEXT("No input action for tag [%s] in input config [%s]."),
			*InputTag.ToString(), *GetNameSafe(this));
	}
	return nullptr;
}
