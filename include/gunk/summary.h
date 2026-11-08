#ifndef POKEDIAMOND_GUNK_SUMMARY_H
#define POKEDIAMOND_GUNK_SUMMARY_H

#include "global.h"
#include "pokemon.h"

// Gunk's: A on the summary's Skills page cycles the numbers through stats, IVs and EVs.
// Replaces the summary's mon-data loader calls (sub_0207AB0C) to cache the IVs and EVs.
void Gunk_SummaryLoadMon(u8 *summary, struct Pokemon *mon, u8 *monData);
// Called first by the summary's input handler (sub_0207A310); TRUE when it handled A on the Skills page.
BOOL Gunk_SummaryHandleInput(u8 *summary);
// Replaces the Skills page draw (sub_0207E200).
void Gunk_SummaryDrawSkills(u8 *summary);

#endif // POKEDIAMOND_GUNK_SUMMARY_H
