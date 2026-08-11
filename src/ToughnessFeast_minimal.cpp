#include <Debug.h>
// Load-test only: proves RE_Kenshi can init our DLL without hooks.
// Export must be C++ mangled ?startPlugin@@YAXXZ
__declspec(dllexport) void startPlugin()
{
    DebugLog("ToughnessFeast MINIMAL: loaded OK (no hooks)");
}
