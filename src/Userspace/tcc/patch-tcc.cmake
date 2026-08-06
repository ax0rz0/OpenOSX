set(tcc_libtcc "${OPENOSX_TCC_SOURCE_COPY}/libtcc.c")

file(READ "${tcc_libtcc}" tcc_libtcc_contents)

string(REPLACE
"ST_FUNC int normalized_PATHCMP(const char *f1, const char *f2)
{
    char *p1, *p2;
    int ret = 1;
    if (!!(p1 = realpath(f1, NULL))) {
        if (!!(p2 = realpath(f2, NULL))) {
            ret = PATHCMP(p1, p2);
            libc_free(p2); /* realpath() requirement */
        }
        libc_free(p1);
    }
    return ret;
}"
"ST_FUNC int normalized_PATHCMP(const char *f1, const char *f2)
{
    enum { TCC_REALPATH_MAX = 4096 };
    char p1[TCC_REALPATH_MAX], p2[TCC_REALPATH_MAX];
    int ret = 1;
    if (realpath(f1, p1) && realpath(f2, p2))
        ret = PATHCMP(p1, p2);
    return ret;
}"
    tcc_libtcc_contents
    "${tcc_libtcc_contents}"
)

file(WRITE "${tcc_libtcc}" "${tcc_libtcc_contents}")
