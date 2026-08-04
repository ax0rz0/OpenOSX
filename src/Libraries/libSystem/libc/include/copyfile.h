/*
 * copyfile(3) - copy a file's data and metadata.
 *
 * The flag values are Apple's, so that software written against macOS
 * compiles and behaves the same way here. See copyfile.c for which of the
 * operations are actually carried out on PureDarwin.
 */

#ifndef _COPYFILE_H_
#define _COPYFILE_H_

#include <sys/types.h>
#include <stdint.h>

__BEGIN_DECLS

typedef struct _copyfile_state *copyfile_state_t;
typedef uint32_t copyfile_flags_t;

int copyfile(const char *from, const char *to, copyfile_state_t state,
             copyfile_flags_t flags);
int fcopyfile(int from_fd, int to_fd, copyfile_state_t state,
              copyfile_flags_t flags);

copyfile_state_t copyfile_state_alloc(void);
int copyfile_state_free(copyfile_state_t state);
int copyfile_state_get(copyfile_state_t state, uint32_t flag, void *dst);
int copyfile_state_set(copyfile_state_t state, uint32_t flag, const void *src);

#define COPYFILE_ACL      (1 << 0)
#define COPYFILE_STAT     (1 << 1)
#define COPYFILE_XATTR    (1 << 2)
#define COPYFILE_DATA     (1 << 3)

#define COPYFILE_SECURITY (COPYFILE_STAT | COPYFILE_ACL)
#define COPYFILE_METADATA (COPYFILE_SECURITY | COPYFILE_XATTR)
#define COPYFILE_ALL      (COPYFILE_METADATA | COPYFILE_DATA)

#define COPYFILE_RECURSIVE            (1 << 15)
#define COPYFILE_CHECK                (1 << 16)
#define COPYFILE_EXCL                 (1 << 17)
#define COPYFILE_NOFOLLOW_SRC         (1 << 18)
#define COPYFILE_NOFOLLOW_DST         (1 << 19)
#define COPYFILE_MOVE                 (1 << 20)
#define COPYFILE_UNLINK               (1 << 21)
#define COPYFILE_NOFOLLOW             (COPYFILE_NOFOLLOW_SRC | COPYFILE_NOFOLLOW_DST)
#define COPYFILE_PACK                 (1 << 22)
#define COPYFILE_UNPACK               (1 << 23)
#define COPYFILE_CLONE                (1 << 24)
#define COPYFILE_CLONE_FORCE          (1 << 25)
#define COPYFILE_RUN_IN_PLACE         (1 << 26)
#define COPYFILE_DATA_SPARSE          (1 << 27)
#define COPYFILE_PRESERVE_DST_TRACKED (1 << 28)
#define COPYFILE_VERBOSE              (1 << 30)

#define COPYFILE_STATE_SRC_FD        1
#define COPYFILE_STATE_SRC_FILENAME  2
#define COPYFILE_STATE_DST_FD        3
#define COPYFILE_STATE_DST_FILENAME  4
#define COPYFILE_STATE_STATUS_CB     6
#define COPYFILE_STATE_STATUS_CTX    7
#define COPYFILE_STATE_COPIED        8

__END_DECLS

#endif /* _COPYFILE_H_ */
