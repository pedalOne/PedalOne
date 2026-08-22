#include "ancs_client.h"

// The standalone ANCS test firmware emits health and event diagnostics every
// few seconds. USB CDC output can block when no serial monitor is consuming
// it, visibly freezing the UI. Compile those diagnostics to a no-op here while
// preserving the navigation client itself.
namespace {
class AncsSilentSerial {
 public:
  template <typename... Args>
  size_t printf(const char *, Args...) { return 0; }
  size_t println(const char *) { return 0; }
};
AncsSilentSerial ancsSilentSerial;
}  // namespace

#define Serial ancsSilentSerial
#include "ancs_client_impl.inc"
#undef Serial
