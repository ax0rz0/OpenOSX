#define _FILE_OFFSET_BITS 64
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define APFS_BLOCK_SIZE 4096U
#define APFS_APSB_MAGIC 0x42535041U
#define APFS_OBJECT_TYPE_MASK 0x0000ffffU
#define APFS_OBJECT_TYPE_BTREE 0x00000002U
#define APFS_OBJECT_TYPE_BTREE_NODE 0x00000003U
#define APFS_OBJECT_TYPE_FSTREE 0x0000000eU
#define APFS_BTNODE_ROOT 0x0001U
#define APFS_BTNODE_LEAF 0x0002U
#define APFS_TYPE_INODE 3U
#define APFS_TYPE_FILE_EXTENT 8U
#define APFS_TYPE_DIR_REC 9U
#define APFS_OBJ_TYPE_SHIFT 60U
#define APFS_ROOT_FILEID 2U
#define DT_REG 8U
#define S_IFREG 0100000U

struct record {
	uint8_t key[256];
	uint16_t key_len;
	uint8_t val[512];
	uint16_t val_len;
};

static uint16_t
rd16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t
rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t
rd64(const uint8_t *p)
{
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--)
		v = (v << 8) | p[i];
	return v;
}

static void
wr16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
wr32(uint8_t *p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static void
wr64(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
		p[i] = (uint8_t)(v >> (i * 8));
}

static uint64_t
fletcher64(const uint8_t block[APFS_BLOCK_SIZE])
{
	uint64_t lo = 0;
	uint64_t hi = 0;
	uint64_t check1;
	uint64_t check2;

	for (uint32_t off = 8; off + 4 <= APFS_BLOCK_SIZE; off += 4) {
		lo = (lo + rd32(block + off)) % 0xffffffffULL;
		hi = (hi + lo) % 0xffffffffULL;
	}
	check1 = 0xffffffffULL - ((lo + hi) % 0xffffffffULL);
	check2 = 0xffffffffULL - ((lo + check1) % 0xffffffffULL);
	return (check2 << 32) | check1;
}

static void
update_object_checksum(uint8_t block[APFS_BLOCK_SIZE])
{
	wr64(block, fletcher64(block));
}

static void
die_errno(const char *what)
{
	fprintf(stderr, "apfs_fixture: %s: %s\n", what, strerror(errno));
	exit(1);
}

static void
die(const char *what)
{
	fprintf(stderr, "apfs_fixture: %s\n", what);
	exit(1);
}

static void
read_block(int fd, uint64_t block, uint8_t out[APFS_BLOCK_SIZE])
{
	ssize_t n;

	n = pread(fd, out, APFS_BLOCK_SIZE, (off_t)(block * APFS_BLOCK_SIZE));
	if (n < 0)
		die_errno("pread");
	if (n != APFS_BLOCK_SIZE)
		die("short block read");
}

static void
write_block(int fd, uint64_t block, const uint8_t in[APFS_BLOCK_SIZE])
{
	ssize_t n;

	n = pwrite(fd, in, APFS_BLOCK_SIZE, (off_t)(block * APFS_BLOCK_SIZE));
	if (n < 0)
		die_errno("pwrite");
	if (n != APFS_BLOCK_SIZE)
		die("short block write");
}

static int
block_is_zero(const uint8_t block[APFS_BLOCK_SIZE])
{
	for (uint32_t i = 0; i < APFS_BLOCK_SIZE; i++) {
		if (block[i] != 0)
			return 0;
	}
	return 1;
}

static uint64_t
make_jkey(uint64_t fileid, uint8_t type)
{
	return fileid | ((uint64_t)type << APFS_OBJ_TYPE_SHIFT);
}

static void
append_record(struct record *records, size_t *count, const void *key,
    uint16_t key_len, const void *val, uint16_t val_len)
{
	if (*count >= 32)
		die("too many APFS fixture records");
	if (key_len > sizeof(records[*count].key) ||
	    val_len > sizeof(records[*count].val))
		die("APFS fixture record too large");
	memcpy(records[*count].key, key, key_len);
	memcpy(records[*count].val, val, val_len);
	records[*count].key_len = key_len;
	records[*count].val_len = val_len;
	(*count)++;
}

static void
append_dirent(struct record *records, size_t *count, const char *name,
    uint64_t fileid)
{
	uint8_t key[256];
	uint8_t val[18];
	size_t len = strlen(name);

	if (len + 1 > 255 || sizeof(uint64_t) + sizeof(uint32_t) + len + 1 >
	    sizeof(key))
		die("APFS fixture filename too long");
	memset(key, 0, sizeof(key));
	wr64(key, make_jkey(APFS_ROOT_FILEID, APFS_TYPE_DIR_REC));
	wr32(key + 8, (uint32_t)(len + 1));
	memcpy(key + 12, name, len + 1);

	memset(val, 0, sizeof(val));
	wr64(val, fileid);
	wr16(val + 16, DT_REG);
	append_record(records, count, key, (uint16_t)(12 + len + 1), val,
	    sizeof(val));
}

static void
append_inode(struct record *records, size_t *count, uint64_t fileid,
    uint64_t size)
{
	uint8_t key[8];
	uint8_t val[92];

	memset(key, 0, sizeof(key));
	wr64(key, make_jkey(fileid, APFS_TYPE_INODE));
	memset(val, 0, sizeof(val));
	wr64(val + 0, APFS_ROOT_FILEID);
	wr64(val + 8, fileid);
	wr32(val + 56, 1);
	wr32(val + 72, 1000);
	wr32(val + 76, 100);
	wr16(val + 80, (uint16_t)(S_IFREG | 0644));
	wr64(val + 84, size);
	append_record(records, count, key, sizeof(key), val, sizeof(val));
}

static void
append_extent(struct record *records, size_t *count, uint64_t fileid,
    uint64_t size, uint64_t paddr)
{
	uint8_t key[16];
	uint8_t val[24];

	memset(key, 0, sizeof(key));
	wr64(key, make_jkey(fileid, APFS_TYPE_FILE_EXTENT));
	wr64(key + 8, 0);
	memset(val, 0, sizeof(val));
	wr64(val + 0, size);
	wr64(val + 8, paddr);
	append_record(records, count, key, sizeof(key), val, sizeof(val));
}

static void
patch_root_child_count(struct record *records, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		uint64_t key;

		if (records[i].key_len != 8 || records[i].val_len < 92)
			continue;
		key = rd64(records[i].key);
		if (key == make_jkey(APFS_ROOT_FILEID, APFS_TYPE_INODE))
			wr32(records[i].val + 56, 2);
	}
}

static void
repack_root(uint8_t root[APFS_BLOCK_SIZE], const struct record *records,
    size_t count)
{
	uint16_t table_len = (uint16_t)(count * 8);
	uint16_t key_base = 56 + table_len;
	uint16_t key_off = 0;
	uint16_t val_off = 0;
	uint16_t max_key = 0;
	uint16_t max_val = 0;
	uint16_t val_end = APFS_BLOCK_SIZE - 40;
	uint8_t out[APFS_BLOCK_SIZE];

	if (count > UINT32_MAX / 8)
		die("invalid APFS fixture record count");
	memset(out, 0, sizeof(out));
	memcpy(out, root, 32);
	wr16(out + 32, APFS_BTNODE_ROOT | APFS_BTNODE_LEAF);
	wr16(out + 34, 0);
	wr32(out + 36, (uint32_t)count);
	wr16(out + 40, 0);
	wr16(out + 42, table_len);

	for (size_t i = 0; i < count; i++) {
		const struct record *r = &records[i];

		if (key_base + key_off + r->key_len > val_end)
			die("APFS fixture root key area overflow");
		val_off = (uint16_t)(val_off + r->val_len);
		if (val_off > val_end ||
		    key_base + key_off + r->key_len > val_end - val_off)
			die("APFS fixture root value area overflow");

		wr16(out + 56 + i * 8 + 0, key_off);
		wr16(out + 56 + i * 8 + 2, r->key_len);
		wr16(out + 56 + i * 8 + 4, val_off);
		wr16(out + 56 + i * 8 + 6, r->val_len);
		memcpy(out + key_base + key_off, r->key, r->key_len);
		memcpy(out + val_end - val_off, r->val, r->val_len);
		key_off = (uint16_t)(key_off + r->key_len);
		if (r->key_len > max_key)
			max_key = r->key_len;
		if (r->val_len > max_val)
			max_val = r->val_len;
	}

	wr16(out + 44, key_off);
	wr16(out + 46, (uint16_t)(val_end - val_off - key_base - key_off));
	wr16(out + 48, 0xffff);
	wr16(out + 50, 0);
	wr16(out + 52, 0xffff);
	wr16(out + 54, 0);

	wr32(out + APFS_BLOCK_SIZE - 40, 0);
	wr32(out + APFS_BLOCK_SIZE - 36, APFS_BLOCK_SIZE);
	wr32(out + APFS_BLOCK_SIZE - 32, 0);
	wr32(out + APFS_BLOCK_SIZE - 28, 0);
	wr32(out + APFS_BLOCK_SIZE - 24, max_key);
	wr32(out + APFS_BLOCK_SIZE - 20, max_val);
	wr64(out + APFS_BLOCK_SIZE - 16, count);
	wr64(out + APFS_BLOCK_SIZE - 8, 1);
	update_object_checksum(out);
	memcpy(root, out, sizeof(out));
}

int
main(int argc, char **argv)
{
	static const char hello[] =
	    "hello from the OpenOSX APFS fixture\n";
	static const char notes[] =
	    "This file is baked into the APFS test partition.\n"
	    "It exercises APFS dir lookup plus file extent reads.\n";
	int fd;
	struct stat st;
	uint64_t blocks;
	uint64_t root_paddr = 0;
	uint64_t data_paddr = 0;
	uint8_t block[APFS_BLOCK_SIZE];
	uint8_t root[APFS_BLOCK_SIZE];
	struct record records[32];
	size_t count = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: apfs_fixture apfs.img\n");
		return 2;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		die_errno("open");
	if (fstat(fd, &st) != 0)
		die_errno("fstat");
	blocks = (uint64_t)st.st_size / APFS_BLOCK_SIZE;

	for (uint64_t b = 0; b < blocks; b++) {
		uint32_t object_type;
		uint32_t subtype;

		read_block(fd, b, block);
		object_type = rd32(block + 24) & APFS_OBJECT_TYPE_MASK;
		subtype = rd32(block + 28) & APFS_OBJECT_TYPE_MASK;
		if ((object_type == APFS_OBJECT_TYPE_BTREE ||
		    object_type == APFS_OBJECT_TYPE_BTREE_NODE) &&
		    subtype == APFS_OBJECT_TYPE_FSTREE) {
			root_paddr = b;
			memcpy(root, block, sizeof(root));
			break;
		}
	}
	if (root_paddr == 0)
		die("could not find APFS root tree");
	if ((rd16(root + 32) & (APFS_BTNODE_ROOT | APFS_BTNODE_LEAF)) !=
	    (APFS_BTNODE_ROOT | APFS_BTNODE_LEAF))
		die("APFS fixture only supports a root leaf tree");

	{
		uint32_t nkeys = rd32(root + 36);
		uint16_t table_off = rd16(root + 40);
		uint16_t table_len = rd16(root + 42);
		uint16_t key_base = (uint16_t)(56 + table_off + table_len);
		uint16_t val_end = APFS_BLOCK_SIZE - 40;

		if (nkeys > 24)
			die("unexpected APFS root record count");
		for (uint32_t i = 0; i < nkeys; i++) {
			uint16_t ko = rd16(root + 56 + table_off + i * 8 + 0);
			uint16_t kl = rd16(root + 56 + table_off + i * 8 + 2);
			uint16_t vo = rd16(root + 56 + table_off + i * 8 + 4);
			uint16_t vl = rd16(root + 56 + table_off + i * 8 + 6);

			if (kl == 0 || vl == 0 ||
			    (uint32_t)key_base + ko + kl > APFS_BLOCK_SIZE ||
			    vo > val_end || vl > vo)
				die("invalid APFS root record");
			append_record(records, &count, root + key_base + ko, kl,
			    root + val_end - vo, vl);
		}
	}

	for (uint64_t b = root_paddr + 1; b + 1 < blocks; b++) {
		read_block(fd, b, block);
		if (!block_is_zero(block))
			continue;
		read_block(fd, b + 1, block);
		if (!block_is_zero(block))
			continue;
		data_paddr = b;
		break;
	}
	if (data_paddr == 0)
		die("could not find free APFS fixture data blocks");

	memset(block, 0, sizeof(block));
	memcpy(block, hello, sizeof(hello) - 1);
	write_block(fd, data_paddr, block);
	memset(block, 0, sizeof(block));
	memcpy(block, notes, sizeof(notes) - 1);
	write_block(fd, data_paddr + 1, block);

	patch_root_child_count(records, count);
	append_dirent(records, &count, "hello.txt", 4);
	append_dirent(records, &count, "notes.txt", 5);
	append_inode(records, &count, 4, sizeof(hello) - 1);
	append_extent(records, &count, 4, sizeof(hello) - 1, data_paddr);
	append_inode(records, &count, 5, sizeof(notes) - 1);
	append_extent(records, &count, 5, sizeof(notes) - 1, data_paddr + 1);
	repack_root(root, records, count);
	write_block(fd, root_paddr, root);

	for (uint64_t b = 0; b < blocks; b++) {
		read_block(fd, b, block);
		if (rd32(block + 32) == APFS_APSB_MAGIC) {
			wr64(block + 168, 6);
			wr64(block + 176, 2);
			wr64(block + 184, 2);
			update_object_checksum(block);
			write_block(fd, b, block);
			break;
		}
	}

	if (close(fd) != 0)
		die_errno("close");
	fprintf(stderr,
	    "apfs_fixture: added hello.txt and notes.txt at blocks %llu,%llu\n",
	    (unsigned long long)data_paddr,
	    (unsigned long long)(data_paddr + 1));
	return 0;
}
