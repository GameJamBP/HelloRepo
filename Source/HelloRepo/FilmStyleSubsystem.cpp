#include "FilmStyleSubsystem.h"

void UFilmStyleSubsystem::SetFilmMode(bool bInFilmMode)
{
	bFilmMode = bInFilmMode;
}

void UFilmStyleSubsystem::SetSelectActorData(const FSelectActorStateData& InData)
{
	SelectActorData = InData;
}
