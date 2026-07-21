#ifndef OPENUA_CRASHDIAG_H_INCLUDED
#define OPENUA_CRASHDIAG_H_INCLUDED

#include <cstdint>

namespace CrashDiag
{

bool Initialize(bool enabled);
bool IsEnabled();

// Labels passed here must have static storage duration (normally string literals).
void Breadcrumb(const char *phase);
void SetLastOperation(const char *operation);
void UpdateRuntimeState(int32_t screenMode, int32_t levelId, uint32_t timeStamp, uint32_t dTime);

void ShutdownComplete();

}

#endif
