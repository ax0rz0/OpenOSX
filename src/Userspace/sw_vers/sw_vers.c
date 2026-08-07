#include <stdio.h>
#include <string.h>

#define PRODUCT_NAME "OpenOSX"
#define PRODUCT_VERSION "11.3"
#define BUILD_VERSION "20E241"

static void
print_usage(void)
{
    fprintf(stderr, "usage: sw_vers [-productName|-productVersion|-buildVersion]\n");
}

int
main(int argc, char **argv)
{
    if (argc == 1) {
        printf("ProductName:\t%s\n", PRODUCT_NAME);
        printf("ProductVersion:\t%s\n", PRODUCT_VERSION);
        printf("BuildVersion:\t%s\n", BUILD_VERSION);
        return 0;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "-productName") == 0) {
            printf("%s\n", PRODUCT_NAME);
            return 0;
        }
        if (strcmp(argv[1], "-productVersion") == 0) {
            printf("%s\n", PRODUCT_VERSION);
            return 0;
        }
        if (strcmp(argv[1], "-buildVersion") == 0) {
            printf("%s\n", BUILD_VERSION);
            return 0;
        }
    }

    print_usage();
    return 1;
}
