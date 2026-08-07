/* include/config.h.  Generated from config.h.in by configure. */
/* This file was generated for a real macOS build; when host_libxar is
   compiled natively (see host_ld in build.nix's native-ld configure), the
   Darwin-only entries (getattrlist/setattrlist, chflags, lchmod, ACLs,
   Darwin's 6-arg getxattr/setxattr, BSD struct fields) don't exist on
   glibc/Linux at all - not just missing headers, incompatible signatures.
   host_ld only needs xar for basic archive read/write, so disable all of
   that metadata-preservation functionality (already properly #ifdef
   HAVE_*-guarded throughout xar's own sources) rather than hand-porting it. */
#ifdef __APPLE__
/* #undef HAVE_SYS_STATFS_H */
#define HAVE_SYS_XATTR_H 1
/* #undef HAVE_SYS_EXTATTR_H */
#define HAVE_SYS_PARAM_H 1
/* #undef HAVE_LGETXATTR */
/* #undef HAVE_LSETXATTR */
#define HAVE_GETXATTR 1
#define HAVE_SETXATTR 1
#define HAVE_GETATTRLIST 1
#define HAVE_SETATTRLIST 1
#define HAVE_CHFLAGS 1
#define HAVE_STATVFS 1
#define HAVE_STATFS 1
/* #undef HAVE_EXT2FS_EXT2_FS_H */
#define HAVE_STRUCT_STAT_ST_FLAGS 1
/* #undef HAVE_STRUCT_STATVFS_F_FSTYPENAME */
#define HAVE_STRUCT_STATFS_F_FSTYPENAME 1
#define HAVE_SYS_ACL_H 1
/* #undef HAVE_LIBUTIL_H */
#define HAVE_ASPRINTF 1
/* #undef HAVE_LIBBZ2 */
/* #undef HAVE_LIBLZMA */
#define HAVE_LCHOWN 1
#define HAVE_LCHMOD 1
#define HAVE_STRMODE 1
#else
/* #undef HAVE_SYS_STATFS_H */
/* #undef HAVE_SYS_XATTR_H */
/* #undef HAVE_SYS_EXTATTR_H */
#define HAVE_SYS_PARAM_H 1
/* #undef HAVE_LGETXATTR */
/* #undef HAVE_LSETXATTR */
/* #undef HAVE_GETXATTR */
/* #undef HAVE_SETXATTR */
/* #undef HAVE_GETATTRLIST */
/* #undef HAVE_SETATTRLIST */
/* #undef HAVE_CHFLAGS */
#define HAVE_STATVFS 1
/* #undef HAVE_STATFS */
/* #undef HAVE_EXT2FS_EXT2_FS_H */
/* #undef HAVE_STRUCT_STAT_ST_FLAGS */
/* #undef HAVE_STRUCT_STATVFS_F_FSTYPENAME */
/* #undef HAVE_STRUCT_STATFS_F_FSTYPENAME */
/* #undef HAVE_SYS_ACL_H */
/* #undef HAVE_LIBUTIL_H */
#define HAVE_ASPRINTF 1
/* #undef HAVE_LIBBZ2 */
/* #undef HAVE_LIBLZMA */
#define HAVE_LCHOWN 1
/* #undef HAVE_LCHMOD */
/* #undef HAVE_STRMODE */
#endif
#define UID_STRING RId32
#define UID_CAST (uint32_t)
#define GID_STRING PRId32
#define GID_CAST (uint32_t)
#define INO_STRING PRId64
#define INO_HEXSTRING PRIx64
#define INO_CAST (uint64_t)
#define DEV_STRING PRId32
#define DEV_HEXSTRING PRIx32
#define DEV_CAST (uint32_t)
