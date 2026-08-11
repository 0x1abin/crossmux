#pragma once

#ifdef WEREAD_BACKEND_RTOS
#include <WeReadRtos.h>
#else
#include <WeReadWebApi.h>
#endif

namespace WeReadBackend {

namespace Client = WeReadClient;
namespace Store = WeReadStore;

}  // namespace WeReadBackend
