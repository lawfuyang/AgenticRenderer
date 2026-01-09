#include "Utilities.h"
#include "Renderer.h"

int main(int /*argc*/, char* /*argv*/[])
{
    Renderer renderer{};
    if (!renderer.Initialize())
        return 1;

    renderer.Run();
    renderer.Shutdown();
    return 0;
}