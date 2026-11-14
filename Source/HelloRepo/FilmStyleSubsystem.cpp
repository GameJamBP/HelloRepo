#include "FilmStyleSubsystem.h"
#include "SelectableActorShell.h"

void UFilmStyleSubsystem::SetFilmMode(bool bInFilmMode)
{
	bFilmMode = bInFilmMode;
}

void UFilmStyleSubsystem::SetSelectActorData(const FSelectActorStateData& InData)
{
	SelectActorData = InData;
}

void UFilmStyleSubsystem::SetTemporaryActiveActor(ASelectableActorShell* Actor)
{
	if (TemporaryActiveActor.Get() == Actor)
	{
		return;
	}
	if (auto* Old = TemporaryActiveActor.Get())
	{
		Old->SetTemporarilyActive(false);
	}
	TemporaryActiveActor = Actor;
	if (IsValid(Actor))
	{
		Actor->SetTemporarilyActive(true);
	}
}
