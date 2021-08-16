
#include "logging/logging.h"

#include "TrampolineBridge/ClosureTrampolineBridge/common-bridge-handler.h"

PUBLIC void intercept_routing_common_bridge_handler(RegisterContext *ctx, ClosureTrampolineEntry *entry) {
  DLOG(0, "Catch common bridge handler, carry data: %p, carry handler: %p", (HookEntry *)entry->carry_data,
       entry->carry_handler);

  typedef void (*intercept_routing_handler_t)(RegisterContext * ctx, ClosureTrampolineEntry * entry);
  intercept_routing_handler_t routing_handler = (intercept_routing_handler_t)entry->carry_handler;

#if __APPLE__ && __has_feature(ptrauth_calls)
  routing_handler =
      (typeof(routing_handler))ptrauth_sign_unauthenticated((void *)routing_handler, ptrauth_key_asia, ptrauth_function_pointer_type_discriminator(typeof(routing_handler)));
#endif

  routing_handler(ctx, entry);
  return;
}
