#include <fpdfview.h>

int main()
{
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;

    FPDF_InitLibraryWithConfig(&config);
    FPDF_DestroyLibrary();
    return 0;
}
