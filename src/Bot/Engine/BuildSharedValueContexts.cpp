#include "AiObjectContext.h"
#include "ValueContext.h"
#include "UBValueContext.h"

void AiObjectContext::BuildSharedValueContexts(SharedNamedObjectContextList<UntypedValue>& valueContexts)
{
    valueContexts.Add(new ValueContext());
    valueContexts.Add(new TbcDungeonUnderbogValueContext());
}
