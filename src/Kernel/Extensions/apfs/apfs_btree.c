#include "apfs.h"

#include <libkern/OSByteOrder.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/ubc.h>
#include <sys/uio.h>
#include <string.h>

#define APFS_RW_MAX_RECORDS 64
#define APFS_RW_MAX_KEY_SIZE 288
#define APFS_RW_MAX_VAL_SIZE 512
#define APFS_RW_FILE_BLOCKS 1

struct apfs_rw_record {
	uint8_t key[APFS_RW_MAX_KEY_SIZE];
	uint16_t key_len;
	uint8_t val[APFS_RW_MAX_VAL_SIZE];
	uint16_t val_len;
};

static int apfs_find_free_block(struct apfs_mount *amp,
    struct apfs_rw_record *records, uint32_t count, uint64_t *block_out);

static uint16_t
le16(uint16_t v)
{
	return OSSwapLittleToHostInt16(v);
}

static uint32_t
le32(uint32_t v)
{
	return OSSwapLittleToHostInt32(v);
}

static uint64_t
le64(uint64_t v)
{
	return OSSwapLittleToHostInt64(v);
}

static int64_t
le64s(int64_t v)
{
	return (int64_t)OSSwapLittleToHostInt64((uint64_t)v);
}

static uint16_t
hle16(uint16_t v)
{
	return OSSwapHostToLittleInt16(v);
}

static uint32_t
hle32(uint32_t v)
{
	return OSSwapHostToLittleInt32(v);
}

static uint64_t
hle64(uint64_t v)
{
	return OSSwapHostToLittleInt64(v);
}

static uint64_t
apfs_fletcher64(const void *data, size_t size)
{
	const uint8_t *bytes = (const uint8_t *)data;
	uint64_t lo = 0;
	uint64_t hi = 0;
	uint64_t check1;
	uint64_t check2;
	size_t offset;

	if (data == NULL || size <= APFS_MAX_CKSUM_SIZE)
		return 0;

	for (offset = APFS_MAX_CKSUM_SIZE; offset + sizeof(uint32_t) <= size;
	    offset += sizeof(uint32_t)) {
		uint32_t word;

		memcpy(&word, bytes + offset, sizeof(word));
		lo = (lo + le32(word)) % 0xffffffffULL;
		hi = (hi + lo) % 0xffffffffULL;
	}

	check1 = 0xffffffffULL - ((lo + hi) % 0xffffffffULL);
	check2 = 0xffffffffULL - ((lo + check1) % 0xffffffffULL);
	return (check2 << 32) | check1;
}

static void
apfs_update_object_checksum(void *object, size_t size)
{
	struct apfs_obj_phys *obj = (struct apfs_obj_phys *)object;
	uint64_t checksum;

	if (object == NULL || size <= sizeof(*obj))
		return;

	checksum = hle64(apfs_fletcher64(object, size));
	memcpy(obj->o_cksum, &checksum, sizeof(checksum));
}

static int
apfs_verify_object_checksum(const void *object, size_t size)
{
	const struct apfs_obj_phys *obj = (const struct apfs_obj_phys *)object;
	uint64_t actual;
	uint64_t expected;

	if (object == NULL || size <= sizeof(*obj))
		return EINVAL;

	memcpy(&expected, obj->o_cksum, sizeof(expected));
	actual = apfs_fletcher64(object, size);
	if (le64(expected) != actual)
		return EINVAL;
	return 0;
}

static uint32_t
apfs_object_type(uint32_t type)
{
	return le32(type) & APFS_OBJECT_TYPE_MASK;
}

static uint64_t
apfs_key_id(uint64_t obj_id_and_type)
{
	return le64(obj_id_and_type) & APFS_OBJ_ID_MASK;
}

static uint8_t
apfs_key_type(uint64_t obj_id_and_type)
{
	return (uint8_t)((le64(obj_id_and_type) & APFS_OBJ_TYPE_MASK) >>
	    APFS_OBJ_TYPE_SHIFT);
}

static int
apfs_read_phys(struct apfs_mount *amp, apfs_paddr_t paddr, void *out,
    size_t out_size)
{
	buf_t bp = NULL;
	int error;

	if (amp == NULL || amp->devvp == NULLVP || out == NULL)
		return EINVAL;
	if (paddr < 0 || out_size > amp->block_size)
		return EINVAL;

	error = (int)buf_meta_bread(amp->devvp, (daddr64_t)paddr,
	    amp->block_size, NOCRED, &bp);
	if (error) {
		if (bp)
			buf_brelse(bp);
		return error;
	}
	memcpy(out, (const void *)buf_dataptr(bp), out_size);
	buf_brelse(bp);
	return 0;
}

static int
apfs_read_object_phys(struct apfs_mount *amp, apfs_paddr_t paddr, void *out)
{
	int error;

	error = apfs_read_phys(amp, paddr, out, amp->block_size);
	if (error)
		return error;
	error = apfs_verify_object_checksum(out, amp->block_size);
	if (error) {
		APFSLOG("bad object checksum at paddr 0x%llx",
		    (unsigned long long)paddr);
		return error;
	}
	return 0;
}

static int
apfs_read_object_prefix(struct apfs_mount *amp, apfs_paddr_t paddr, void *out,
    size_t out_size)
{
	void *block;
	int error;

	if (out == NULL || out_size > amp->block_size)
		return EINVAL;
	block = _MALLOC(amp->block_size, M_TEMP, M_WAITOK);
	if (block == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, paddr, block);
	if (error == 0)
		memcpy(out, block, out_size);
	_FREE(block, M_TEMP);
	return error;
}

static int
apfs_write_phys(struct apfs_mount *amp, apfs_paddr_t paddr, const void *data,
    size_t data_size)
{
	buf_t bp = NULL;
	int error;

	if (amp == NULL || amp->devvp == NULLVP || data == NULL)
		return EINVAL;
	if (paddr < 0 || data_size > amp->block_size)
		return EINVAL;

	error = (int)buf_meta_bread(amp->devvp, (daddr64_t)paddr,
	    amp->block_size, NOCRED, &bp);
	if (error) {
		if (bp)
			buf_brelse(bp);
		return error;
	}
	memcpy((void *)buf_dataptr(bp), data, data_size);
	return buf_bwrite(bp);
}

static int
apfs_uiomove_phys(struct apfs_mount *amp, apfs_paddr_t paddr, size_t offset,
    size_t count, struct uio *uio)
{
	buf_t bp = NULL;
	int error;

	if (amp == NULL || amp->devvp == NULLVP || uio == NULL)
		return EINVAL;
	if (paddr < 0 || offset > amp->block_size ||
	    count > amp->block_size - offset)
		return EINVAL;

	error = (int)buf_meta_bread(amp->devvp, (daddr64_t)paddr,
	    amp->block_size, NOCRED, &bp);
	if (error) {
		if (bp)
			buf_brelse(bp);
		return error;
	}
	error = uiomove((char *)buf_dataptr(bp) + offset, (int)count, uio);
	buf_brelse(bp);
	return error;
}

static int
apfs_uiomove_write_phys(struct apfs_mount *amp, apfs_paddr_t paddr,
    size_t offset, size_t count, struct uio *uio)
{
	buf_t bp = NULL;
	int error;

	if (amp == NULL || amp->devvp == NULLVP || uio == NULL)
		return EINVAL;
	if (paddr < 0 || offset > amp->block_size ||
	    count > amp->block_size - offset)
		return EINVAL;

	error = (int)buf_meta_bread(amp->devvp, (daddr64_t)paddr,
	    amp->block_size, NOCRED, &bp);
	if (error) {
		if (bp)
			buf_brelse(bp);
		return error;
	}
	error = uiomove((char *)buf_dataptr(bp) + offset, (int)count, uio);
	if (error) {
		buf_brelse(bp);
		return error;
	}
	return buf_bwrite(bp);
}

static const struct apfs_btree_info *
apfs_btree_info_for_node(const struct apfs_mount *amp,
    const struct apfs_btree_node_phys *node)
{
	uint16_t flags = le16(node->btn_flags);

	if ((flags & APFS_BTNODE_ROOT) == 0)
		return NULL;
	return (const struct apfs_btree_info *)
	    ((const uint8_t *)node + amp->block_size -
	    sizeof(struct apfs_btree_info));
}

static int
apfs_btree_entry(const struct apfs_mount *amp,
    const struct apfs_btree_node_phys *node, const struct apfs_btree_info *info,
    uint32_t index, const void **key, uint16_t *key_len, const void **val,
    uint16_t *val_len)
{
	const uint8_t *base = (const uint8_t *)node;
	uint16_t flags = le16(node->btn_flags);
	uint32_t nkeys = le32(node->btn_nkeys);
	uint16_t table_off = le16(node->btn_table_space.off);
	uint16_t table_len = le16(node->btn_table_space.len);
	uint32_t data_off = offsetof(struct apfs_btree_node_phys, btn_data);
	uint32_t key_base = data_off + table_off + table_len;
	uint32_t val_end = amp->block_size;
	uint16_t k_off, k_len, v_off, v_len;

	if (index >= nkeys)
		return ERANGE;
	if (flags & APFS_BTNODE_ROOT)
		val_end -= sizeof(struct apfs_btree_info);

	if (flags & APFS_BTNODE_FIXED_KV_SIZE) {
		const struct apfs_kvoff *toc = (const struct apfs_kvoff *)
		    (base + data_off + table_off + index * sizeof(*toc));

		if (info == NULL)
			return EINVAL;
		k_off = le16(toc->k);
		v_off = le16(toc->v);
		k_len = (uint16_t)le32(info->bt_key_size);
		v_len = (uint16_t)le32(info->bt_val_size);
	} else {
		const struct apfs_kvloc *toc = (const struct apfs_kvloc *)
		    (base + data_off + table_off + index * sizeof(*toc));

		k_off = le16(toc->k.off);
		k_len = le16(toc->k.len);
		v_off = le16(toc->v.off);
		v_len = le16(toc->v.len);
	}

	if (k_len == 0 || key_base + k_off + k_len > amp->block_size)
		return EINVAL;
	if (v_off == 0xffff)
		return ENOENT;
	if (v_len == 0 || v_off > val_end || v_len > v_off)
		return EINVAL;

	*key = base + key_base + k_off;
	*key_len = k_len;
	*val = base + val_end - v_off;
	*val_len = v_len;
	return 0;
}

static int
apfs_omap_lookup_tree(struct apfs_mount *amp, apfs_paddr_t tree_paddr,
    apfs_oid_t oid, apfs_xid_t xid, struct apfs_omap_val *out)
{
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	struct apfs_omap_val best;
	apfs_xid_t best_xid = 0;
	uint32_t i;
	int found = 0;
	int error = 0;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, tree_paddr, node);
	if (error)
		goto out;

	if (apfs_object_type(node->btn_o.o_type) != APFS_OBJECT_TYPE_BTREE &&
	    apfs_object_type(node->btn_o.o_type) != APFS_OBJECT_TYPE_BTREE_NODE) {
		error = EINVAL;
		goto out;
	}
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		APFSLOG("omap tree level %u not implemented", le16(node->btn_level));
		error = ENOTSUP;
		goto out;
	}

	info = apfs_btree_info_for_node(amp, node);
	for (i = 0; i < le32(node->btn_nkeys); i++) {
		const struct apfs_omap_key *key;
		const struct apfs_omap_val *val;
		const void *keyp, *valp;
		uint16_t key_len, val_len;
		apfs_oid_t key_oid;
		apfs_xid_t key_xid;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			goto out;
		if (key_len < sizeof(*key) || val_len < sizeof(*val))
			continue;

		key = (const struct apfs_omap_key *)keyp;
		val = (const struct apfs_omap_val *)valp;
		key_oid = le64(key->ok_oid);
		key_xid = le64(key->ok_xid);
		if (key_oid != oid || key_xid > xid)
			continue;
		if (!found || key_xid >= best_xid) {
			memcpy(&best, val, sizeof(best));
			best_xid = key_xid;
			found = 1;
		}
	}

	if (!found) {
		error = ENOENT;
		goto out;
	}

	memcpy(out, &best, sizeof(best));
out:
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_write_omap_tree_with_update(struct apfs_mount *amp,
    apfs_paddr_t src_tree_paddr, apfs_paddr_t dst_tree_paddr, apfs_oid_t oid,
    apfs_xid_t xid, apfs_paddr_t new_paddr)
{
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	apfs_xid_t best_xid = 0;
	uint32_t best_index = UINT32_MAX;
	uint32_t i;
	int error = 0;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, src_tree_paddr, node);
	if (error)
		goto out;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		error = ENOTSUP;
		goto out;
	}

	info = apfs_btree_info_for_node(amp, node);
	for (i = 0; i < le32(node->btn_nkeys); i++) {
		const struct apfs_omap_key *key;
		const void *keyp, *valp;
		uint16_t key_len, val_len;
		apfs_oid_t key_oid;
		apfs_xid_t key_xid;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			goto out;
		if (key_len < sizeof(*key) ||
		    val_len < sizeof(struct apfs_omap_val))
			continue;

		key = (const struct apfs_omap_key *)keyp;
		key_oid = le64(key->ok_oid);
		key_xid = le64(key->ok_xid);
		if (key_oid != oid || key_xid > xid)
			continue;
		if (best_index == UINT32_MAX || key_xid >= best_xid) {
			best_xid = key_xid;
			best_index = i;
		}
	}
	if (best_index == UINT32_MAX) {
		error = ENOENT;
		goto out;
	}

	{
		struct apfs_omap_val *val;
		const void *keyp;
		const void *valp;
		uint16_t key_len, val_len;
		apfs_paddr_t disk_paddr = hle64((uint64_t)new_paddr);

		error = apfs_btree_entry(amp, node, info, best_index, &keyp,
		    &key_len, &valp, &val_len);
		if (error)
			goto out;
		if (val_len < sizeof(*val)) {
			error = EINVAL;
			goto out;
		}
		val = (struct apfs_omap_val *)(uintptr_t)valp;
		memcpy(&val->ov_paddr, &disk_paddr, sizeof(val->ov_paddr));
	}

	apfs_update_object_checksum(node, amp->block_size);
	error = apfs_write_phys(amp, dst_tree_paddr, node, amp->block_size);
out:
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_update_omap_tree_paddr(struct apfs_mount *amp, apfs_paddr_t tree_paddr,
    apfs_oid_t oid, apfs_xid_t xid, apfs_paddr_t new_paddr)
{
	return apfs_write_omap_tree_with_update(amp, tree_paddr, tree_paddr,
	    oid, xid, new_paddr);
}

static int
apfs_publish_container_omap(struct apfs_mount *amp,
    apfs_paddr_t new_container_omap_paddr)
{
	struct apfs_nx_superblock *nx;
	struct apfs_checkpoint_map_phys *cpm;
	void *nx_block;
	void *map_block;
	uint32_t desc_blocks;
	uint32_t old_map_index;
	uint32_t map_index;
	uint32_t nx_index;
	uint32_t next_index;
	apfs_paddr_t desc_base;
	apfs_paddr_t old_map_paddr;
	apfs_paddr_t map_paddr;
	apfs_paddr_t nx_paddr;
	apfs_xid_t new_xid;
	int error;

	desc_blocks = le32(amp->nx.nx_xp_desc_blocks) &
	    APFS_CHECKPOINT_BLOCK_COUNT_MASK;
	desc_base = le64s(amp->nx.nx_xp_desc_base);
	if (desc_blocks < 2 || desc_base < 0)
		return ENOTSUP;

	old_map_index = le32(amp->nx.nx_xp_desc_index) % desc_blocks;
	map_index = le32(amp->nx.nx_xp_desc_next) % desc_blocks;
	nx_index = (map_index + 1) % desc_blocks;
	next_index = (nx_index + 1) % desc_blocks;

	old_map_paddr = desc_base + old_map_index;
	map_paddr = desc_base + map_index;
	nx_paddr = desc_base + nx_index;

	nx_block = _MALLOC(amp->block_size, M_TEMP, M_WAITOK | M_ZERO);
	if (nx_block == NULL)
		return ENOMEM;
	map_block = _MALLOC(amp->block_size, M_TEMP, M_WAITOK | M_ZERO);
	if (map_block == NULL) {
		_FREE(nx_block, M_TEMP);
		return ENOMEM;
	}

	error = apfs_read_object_phys(amp, old_map_paddr, map_block);
	if (error)
		goto out;

	cpm = (struct apfs_checkpoint_map_phys *)map_block;
	if (apfs_object_type(cpm->cpm_o.o_type) !=
	    APFS_OBJECT_TYPE_CHECKPOINT_MAP) {
		error = EINVAL;
		goto out;
	}

	error = apfs_read_object_phys(amp, 0, nx_block);
	if (error)
		goto out;
	memcpy(nx_block, &amp->nx, sizeof(amp->nx));
	nx = (struct apfs_nx_superblock *)nx_block;

	new_xid = le64(amp->nx.nx_o.o_xid) + 1;
	if (new_xid <= amp->xid)
		new_xid = amp->xid + 1;

	cpm->cpm_o.o_xid = hle64(new_xid);
	cpm->cpm_flags = hle32(APFS_CHECKPOINT_MAP_LAST);
	apfs_update_object_checksum(map_block, amp->block_size);
	error = apfs_write_phys(amp, map_paddr, map_block, amp->block_size);
	if (error)
		goto out;

	nx->nx_o.o_xid = hle64(new_xid);
	nx->nx_next_xid = hle64(new_xid + 1);
	nx->nx_omap_oid = hle64(new_container_omap_paddr);
	nx->nx_xp_desc_index = hle32(map_index);
	nx->nx_xp_desc_len = hle32(2);
	nx->nx_xp_desc_next = hle32(next_index);
	apfs_update_object_checksum(nx_block, amp->block_size);
	error = apfs_write_phys(amp, nx_paddr, nx_block, amp->block_size);
	if (error)
		goto out;

	memcpy(&amp->nx, nx, sizeof(amp->nx));
	amp->xid = new_xid;
	amp->container_omap_oid = new_container_omap_paddr;
	amp->container_omap_paddr = new_container_omap_paddr;
	APFSLOG("published container omap=0x%llx via checkpoint map=0x%llx nx=0x%llx xid=%llu next=%u",
	    (unsigned long long)new_container_omap_paddr,
	    (unsigned long long)map_paddr,
	    (unsigned long long)nx_paddr,
	    (unsigned long long)new_xid,
	    next_index);
out:
	_FREE(map_block, M_TEMP);
	_FREE(nx_block, M_TEMP);
	return error;
}

static int
apfs_cow_volume_metadata(struct apfs_mount *amp,
    struct apfs_rw_record *records, uint32_t count, apfs_paddr_t new_root_paddr)
{
	void *block;
	struct apfs_omap_phys *omap;
	struct apfs_superblock *fs;
	uint64_t new_volume_omap_tree_paddr;
	uint64_t new_volume_omap_paddr;
	uint64_t new_fs_paddr;
	uint64_t new_container_omap_tree_paddr;
	uint64_t new_container_omap_paddr;
	int error;

	block = _MALLOC(amp->block_size, M_TEMP, M_WAITOK);
	if (block == NULL)
		return ENOMEM;

	error = apfs_find_free_block(amp, records, count,
	    &new_volume_omap_tree_paddr);
	if (error)
		goto out;
	error = apfs_find_free_block(amp, records, count,
	    &new_volume_omap_paddr);
	if (error)
		goto out;
	error = apfs_find_free_block(amp, records, count, &new_fs_paddr);
	if (error)
		goto out;
	error = apfs_find_free_block(amp, records, count,
	    &new_container_omap_tree_paddr);
	if (error)
		goto out;
	error = apfs_find_free_block(amp, records, count,
	    &new_container_omap_paddr);
	if (error)
		goto out;

	error = apfs_write_omap_tree_with_update(amp,
	    amp->volume_omap_tree_paddr, (apfs_paddr_t)new_volume_omap_tree_paddr,
	    amp->root_tree_oid, amp->xid, new_root_paddr);
	if (error)
		goto out;

	error = apfs_read_object_phys(amp, amp->volume_omap_paddr, block);
	if (error)
		goto out;
	omap = (struct apfs_omap_phys *)block;
	omap->om_o.o_oid = hle64(new_volume_omap_paddr);
	omap->om_tree_oid = hle64(new_volume_omap_tree_paddr);
	apfs_update_object_checksum(block, amp->block_size);
	error = apfs_write_phys(amp, (apfs_paddr_t)new_volume_omap_paddr,
	    block, amp->block_size);
	if (error)
		goto out;

	error = apfs_read_object_phys(amp, amp->fs_paddr, block);
	if (error)
		goto out;
	fs = (struct apfs_superblock *)block;
	fs->apfs_omap_oid = hle64(new_volume_omap_paddr);
	apfs_update_object_checksum(block, amp->block_size);
	error = apfs_write_phys(amp, (apfs_paddr_t)new_fs_paddr, block,
	    amp->block_size);
	if (error)
		goto out;

	error = apfs_write_omap_tree_with_update(amp,
	    amp->container_omap_tree_paddr,
	    (apfs_paddr_t)new_container_omap_tree_paddr, amp->fs_oid,
	    amp->xid, (apfs_paddr_t)new_fs_paddr);
	if (error)
		goto out;

	error = apfs_read_object_phys(amp, amp->container_omap_paddr, block);
	if (error)
		goto out;
	omap = (struct apfs_omap_phys *)block;
	omap->om_o.o_oid = hle64(new_container_omap_paddr);
	omap->om_tree_oid = hle64(new_container_omap_tree_paddr);
	apfs_update_object_checksum(block, amp->block_size);
	error = apfs_write_phys(amp, (apfs_paddr_t)new_container_omap_paddr,
	    block, amp->block_size);
	if (error)
		goto out;

	error = apfs_publish_container_omap(amp,
	    (apfs_paddr_t)new_container_omap_paddr);
	if (error)
		goto out;

	amp->root_tree_paddr = new_root_paddr;
	amp->volume_omap_tree_paddr = (apfs_paddr_t)new_volume_omap_tree_paddr;
	amp->volume_omap_paddr = (apfs_paddr_t)new_volume_omap_paddr;
	amp->volume_omap_oid = new_volume_omap_paddr;
	amp->fs_paddr = (apfs_paddr_t)new_fs_paddr;
	amp->container_omap_tree_paddr =
	    (apfs_paddr_t)new_container_omap_tree_paddr;
	amp->apfs.apfs_omap_oid = hle64(new_volume_omap_paddr);
	APFSLOG("COW metadata root=0x%llx vomap_tree=0x%llx vomap=0x%llx fs=0x%llx comap_tree=0x%llx comap=0x%llx",
	    (unsigned long long)new_root_paddr,
	    (unsigned long long)new_volume_omap_tree_paddr,
	    (unsigned long long)new_volume_omap_paddr,
	    (unsigned long long)new_fs_paddr,
	    (unsigned long long)new_container_omap_tree_paddr,
	    (unsigned long long)new_container_omap_paddr);
out:
	_FREE(block, M_TEMP);
	return error;
}

static uint64_t
apfs_make_jkey(uint64_t fileid, uint8_t type)
{
	return fileid | ((uint64_t)type << APFS_OBJ_TYPE_SHIFT);
}

static int
apfs_append_rw_record(struct apfs_rw_record *records, uint32_t *count,
    const void *key, uint16_t key_len, const void *val, uint16_t val_len)
{
	if (*count >= APFS_RW_MAX_RECORDS)
		return ENOSPC;
	if (key_len > APFS_RW_MAX_KEY_SIZE || val_len > APFS_RW_MAX_VAL_SIZE)
		return EOVERFLOW;
	memcpy(records[*count].key, key, key_len);
	memcpy(records[*count].val, val, val_len);
	records[*count].key_len = key_len;
	records[*count].val_len = val_len;
	(*count)++;
	return 0;
}

static int
apfs_load_root_records(struct apfs_mount *amp, struct apfs_btree_node_phys *node,
    struct apfs_rw_record *records, uint32_t *count)
{
	const struct apfs_btree_info *info;
	uint32_t i, nkeys;
	int error;

	*count = 0;
	error = apfs_read_object_phys(amp, amp->root_tree_paddr, node);
	if (error)
		return error;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0)
		return ENOTSUP;

	info = apfs_btree_info_for_node(amp, node);
	nkeys = le32(node->btn_nkeys);
	for (i = 0; i < nkeys; i++) {
		const void *keyp, *valp;
		uint16_t key_len, val_len;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			return error;
		error = apfs_append_rw_record(records, count, keyp, key_len,
		    valp, val_len);
		if (error)
			return error;
	}
	return 0;
}

static void
apfs_touch_root_dir_record(struct apfs_rw_record *records, uint32_t count,
    int delta)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		uint64_t key;
		uint32_t nchildren;

		if (records[i].key_len < sizeof(struct apfs_j_key) ||
		    records[i].val_len < offsetof(struct apfs_j_inode_val, u) +
		    sizeof(uint32_t))
			continue;
		memcpy(&key, records[i].key, sizeof(key));
		if (apfs_key_id(key) != APFS_ROOT_FILEID ||
		    apfs_key_type(key) != APFS_TYPE_INODE)
			continue;
		memcpy(&nchildren, records[i].val +
		    offsetof(struct apfs_j_inode_val, u), sizeof(nchildren));
		nchildren = le32(nchildren);
		nchildren = (uint32_t)((int)nchildren + delta);
		nchildren = hle32(nchildren);
		memcpy(records[i].val + offsetof(struct apfs_j_inode_val, u),
		    &nchildren, sizeof(nchildren));
		return;
	}
}

static int
apfs_update_records_inode_size(struct apfs_rw_record *records, uint32_t count,
    uint64_t fileid, uint64_t size)
{
	uint32_t i;
	uint64_t disk_size = hle64(size);

	for (i = 0; i < count; i++) {
		uint64_t key;

		if (records[i].key_len < sizeof(struct apfs_j_key) ||
		    records[i].val_len < sizeof(struct apfs_j_inode_val))
			continue;
		memcpy(&key, records[i].key, sizeof(key));
		if (apfs_key_id(key) != fileid ||
		    apfs_key_type(key) != APFS_TYPE_INODE)
			continue;
		memcpy(records[i].val +
		    offsetof(struct apfs_j_inode_val, uncompressed_size),
		    &disk_size, sizeof(disk_size));
		return 0;
	}
	return ENOENT;
}

static int
apfs_repack_root_records(struct apfs_mount *amp,
    struct apfs_btree_node_phys *old_node, struct apfs_rw_record *records,
    uint32_t count)
{
	uint8_t *node;
	struct apfs_btree_info *btinfo;
	uint32_t i;
	uint16_t table_len = (uint16_t)(count * sizeof(struct apfs_kvloc));
	uint16_t key_base = (uint16_t)(offsetof(struct apfs_btree_node_phys,
	    btn_data) + table_len);
	uint16_t key_off = 0;
	uint16_t val_off = 0;
	uint16_t max_key = 0;
	uint16_t max_val = 0;
	uint16_t val_end = (uint16_t)(amp->block_size -
	    sizeof(struct apfs_btree_info));
	uint64_t new_paddr;
	int error = 0;

	if (amp->block_size != APFS_BS_BYTES)
		return ENOTSUP;
	node = (uint8_t *)_MALLOC(amp->block_size, M_TEMP, M_WAITOK | M_ZERO);
	if (node == NULL)
		return ENOMEM;

	memcpy(node, old_node, offsetof(struct apfs_btree_node_phys, btn_data));
	((struct apfs_btree_node_phys *)node)->btn_flags =
	    hle16(APFS_BTNODE_ROOT | APFS_BTNODE_LEAF);
	((struct apfs_btree_node_phys *)node)->btn_level = hle16(0);
	((struct apfs_btree_node_phys *)node)->btn_nkeys = hle32(count);
	((struct apfs_btree_node_phys *)node)->btn_table_space.off = hle16(0);
	((struct apfs_btree_node_phys *)node)->btn_table_space.len =
	    hle16(table_len);

	for (i = 0; i < count; i++) {
		struct apfs_kvloc *toc = (struct apfs_kvloc *)
		    (node + offsetof(struct apfs_btree_node_phys, btn_data) +
		    i * sizeof(*toc));

		if (key_base + key_off + records[i].key_len > val_end) {
			error = ENOSPC;
			goto out;
		}
		val_off = (uint16_t)(val_off + records[i].val_len);
		if (val_off > val_end ||
		    key_base + key_off + records[i].key_len > val_end - val_off) {
			error = ENOSPC;
			goto out;
		}

		toc->k.off = hle16(key_off);
		toc->k.len = hle16(records[i].key_len);
		toc->v.off = hle16(val_off);
		toc->v.len = hle16(records[i].val_len);
		memcpy(node + key_base + key_off, records[i].key,
		    records[i].key_len);
		memcpy(node + val_end - val_off, records[i].val,
		    records[i].val_len);
		key_off = (uint16_t)(key_off + records[i].key_len);
		if (records[i].key_len > max_key)
			max_key = records[i].key_len;
		if (records[i].val_len > max_val)
			max_val = records[i].val_len;
	}

	((struct apfs_btree_node_phys *)node)->btn_free_space.off =
	    hle16(key_off);
	((struct apfs_btree_node_phys *)node)->btn_free_space.len =
	    hle16((uint16_t)(val_end - val_off - key_base - key_off));
	((struct apfs_btree_node_phys *)node)->btn_key_free_list.off =
	    hle16(0xffff);
	((struct apfs_btree_node_phys *)node)->btn_key_free_list.len = hle16(0);
	((struct apfs_btree_node_phys *)node)->btn_val_free_list.off =
	    hle16(0xffff);
	((struct apfs_btree_node_phys *)node)->btn_val_free_list.len = hle16(0);

	btinfo = (struct apfs_btree_info *)(node + amp->block_size -
	    sizeof(*btinfo));
	memset(btinfo, 0, sizeof(*btinfo));
	btinfo->bt_node_size = hle32(amp->block_size);
	btinfo->bt_longest_key = hle32(max_key);
	btinfo->bt_longest_val = hle32(max_val);
	btinfo->bt_key_count = hle64(count);
	btinfo->bt_node_count = hle64(1);

	apfs_update_object_checksum(node, amp->block_size);
	error = apfs_find_free_block(amp, records, count, &new_paddr);
	if (error)
		goto out;
	error = apfs_write_phys(amp, (apfs_paddr_t)new_paddr, node,
	    amp->block_size);
	if (error)
		goto out;
	error = apfs_cow_volume_metadata(amp, records, count,
	    (apfs_paddr_t)new_paddr);
out:
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_record_extent(struct apfs_rw_record *record, uint64_t fileid,
    uint64_t *phys, uint64_t *len)
{
	uint64_t key;
	struct apfs_j_file_extent_val *val;

	if (record->key_len < sizeof(struct apfs_j_file_extent_key) ||
	    record->val_len < sizeof(struct apfs_j_file_extent_val))
		return 0;
	memcpy(&key, record->key, sizeof(key));
	if (apfs_key_id(key) != fileid ||
	    apfs_key_type(key) != APFS_TYPE_FILE_EXTENT)
		return 0;

	val = (struct apfs_j_file_extent_val *)record->val;
	*len = le64(val->len_and_flags) & APFS_FILE_EXTENT_LEN_MASK;
	*phys = le64(val->phys_block_num);
	return 1;
}

static int
apfs_find_file_extent(struct apfs_mount *amp, uint64_t fileid,
    uint64_t *phys, uint64_t *len)
{
	struct apfs_btree_node_phys *node;
	struct apfs_rw_record *records;
	uint32_t count, i;
	int error;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;
	records = (struct apfs_rw_record *)_MALLOC(sizeof(*records) *
	    APFS_RW_MAX_RECORDS, M_TEMP, M_WAITOK | M_ZERO);
	if (records == NULL) {
		_FREE(node, M_TEMP);
		return ENOMEM;
	}
	error = apfs_load_root_records(amp, node, records, &count);
	if (error)
		goto out;
	for (i = 0; i < count; i++) {
		if (apfs_record_extent(&records[i], fileid, phys, len)) {
			error = 0;
			goto out;
		}
	}
	error = ENOENT;
out:
	_FREE(records, M_TEMP);
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_block_is_used_by_extent(struct apfs_rw_record *records, uint32_t count,
    uint64_t block)
{
	uint32_t i;

	for (i = 0; i < count; i++) {
		uint64_t phys, len, blocks;

		if (!apfs_record_extent(&records[i], APFS_ROOT_FILEID, &phys, &len)) {
			uint64_t key;

			if (records[i].key_len < sizeof(struct apfs_j_key))
				continue;
			memcpy(&key, records[i].key, sizeof(key));
			if (apfs_key_type(key) != APFS_TYPE_FILE_EXTENT)
				continue;
			if (!apfs_record_extent(&records[i], apfs_key_id(key),
			    &phys, &len))
				continue;
		}
		blocks = (len + APFS_BS_BYTES - 1) / APFS_BS_BYTES;
		if (block >= phys && block < phys + blocks)
			return 1;
	}
	return 0;
}

static int
apfs_find_spaceman(struct apfs_mount *amp)
{
	struct apfs_obj_phys obj;
	apfs_paddr_t paddr;
	int error;

	if (amp->spaceman_paddr)
		return 0;

	amp->spaceman_oid = le64(amp->nx.nx_spaceman_oid);
	if (amp->spaceman_oid == 0)
		return ENOENT;

	for (paddr = 0; paddr < (apfs_paddr_t)amp->block_count; paddr++) {
		error = apfs_read_phys(amp, paddr, &obj, sizeof(obj));
		if (error)
			return error;
		if (le64(obj.o_oid) == amp->spaceman_oid &&
		    apfs_object_type(obj.o_type) == APFS_OBJECT_TYPE_SPACEMAN) {
			amp->spaceman_paddr = paddr;
			APFSLOG("spaceman oid=0x%llx paddr=0x%llx",
			    (unsigned long long)amp->spaceman_oid,
			    (unsigned long long)amp->spaceman_paddr);
			return 0;
		}
	}
	return ENOENT;
}

static int
apfs_spaceman_alloc_block(struct apfs_mount *amp,
    struct apfs_rw_record *records, uint32_t count, uint64_t *block_out)
{
	struct apfs_spaceman_phys *sm = NULL;
	struct apfs_chunk_info_block *cib = NULL;
	uint8_t *bitmap = NULL;
	uint32_t cib_count, cib_index;
	uint32_t addr_offset;
	int error;

	if (block_out == NULL)
		return EINVAL;
	error = apfs_find_spaceman(amp);
	if (error)
		return error;

	sm = (struct apfs_spaceman_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	cib = (struct apfs_chunk_info_block *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	bitmap = (uint8_t *)_MALLOC(amp->block_size, M_TEMP, M_WAITOK);
	if (sm == NULL || cib == NULL || bitmap == NULL) {
		error = ENOMEM;
		goto out;
	}

	error = apfs_read_object_phys(amp, amp->spaceman_paddr, sm);
	if (error)
		goto out;
	if (apfs_object_type(sm->sm_o.o_type) != APFS_OBJECT_TYPE_SPACEMAN) {
		error = EINVAL;
		goto out;
	}
	if (le32(sm->sm_block_size) != amp->block_size ||
	    le32(sm->sm_blocks_per_chunk) == 0) {
		error = EINVAL;
		goto out;
	}
	if (le32(sm->sm_dev[0].sm_cab_count) != 0) {
		APFSLOG("spaceman CAB allocation not implemented");
		error = ENOTSUP;
		goto out;
	}

	cib_count = le32(sm->sm_dev[0].sm_cib_count);
	addr_offset = le32(sm->sm_dev[0].sm_addr_offset);
	if (addr_offset + cib_count * sizeof(apfs_paddr_t) > amp->block_size) {
		error = EINVAL;
		goto out;
	}

	for (cib_index = 0; cib_index < cib_count; cib_index++) {
		apfs_paddr_t cib_paddr;
		uint32_t chunk_count, chunk_index;

		memcpy(&cib_paddr, (uint8_t *)sm + addr_offset +
		    cib_index * sizeof(cib_paddr), sizeof(cib_paddr));
		cib_paddr = le64s(cib_paddr);
		if (cib_paddr <= 0)
			continue;

		error = apfs_read_object_phys(amp, cib_paddr, cib);
		if (error)
			goto out;
		if (apfs_object_type(cib->cib_o.o_type) !=
		    APFS_OBJECT_TYPE_SPACEMAN_CIB) {
			error = EINVAL;
			goto out;
		}

		chunk_count = le32(cib->cib_chunk_info_count);
		for (chunk_index = 0; chunk_index < chunk_count; chunk_index++) {
			struct apfs_chunk_info *ci;
			apfs_paddr_t bitmap_paddr;
			uint64_t chunk_addr;
			uint32_t block_count, rel;

			if ((uint8_t *)&cib->cib_chunk_info[chunk_index + 1] >
			    (uint8_t *)cib + amp->block_size) {
				error = EINVAL;
				goto out;
			}
			ci = &cib->cib_chunk_info[chunk_index];
			if (le32(ci->ci_free_count) == 0)
				continue;

			bitmap_paddr = le64s(ci->ci_bitmap_addr);
			chunk_addr = le64(ci->ci_addr);
			block_count = le32(ci->ci_block_count);
			if (bitmap_paddr <= 0 || block_count == 0)
				continue;

			error = apfs_read_phys(amp, bitmap_paddr, bitmap,
			    amp->block_size);
			if (error)
				goto out;

			for (rel = 0; rel < block_count; rel++) {
				uint64_t block = chunk_addr + rel;
				uint8_t mask = (uint8_t)(1U << (rel & 7));

				if (block >= amp->block_count)
					break;
				if (bitmap[rel >> 3] & mask)
					continue;
				if (apfs_block_is_used_by_extent(records, count,
				    block))
					continue;

				bitmap[rel >> 3] |= mask;
				ci->ci_free_count =
				    hle32(le32(ci->ci_free_count) - 1);
				sm->sm_dev[0].sm_free_count =
				    hle64(le64(sm->sm_dev[0].sm_free_count) - 1);
				error = apfs_write_phys(amp, bitmap_paddr, bitmap,
				    amp->block_size);
				if (error)
					goto out;
				apfs_update_object_checksum(cib,
				    amp->block_size);
				error = apfs_write_phys(amp, cib_paddr, cib,
				    amp->block_size);
				if (error)
					goto out;
				apfs_update_object_checksum(sm, amp->block_size);
				error = apfs_write_phys(amp, amp->spaceman_paddr,
				    sm, amp->block_size);
				if (error)
					goto out;
				*block_out = block;
				APFSLOG("spaceman allocated block 0x%llx",
				    (unsigned long long)block);
				error = 0;
				goto out;
			}
		}
	}
	error = ENOSPC;

out:
	if (bitmap)
		_FREE(bitmap, M_TEMP);
	if (cib)
		_FREE(cib, M_TEMP);
	if (sm)
		_FREE(sm, M_TEMP);
	return error;
}

static int
apfs_find_zero_free_block(struct apfs_mount *amp, struct apfs_rw_record *records,
    uint32_t count, uint64_t *block_out)
{
	uint8_t *block;
	uint64_t b;
	int error = 0;

	block = (uint8_t *)_MALLOC(amp->block_size, M_TEMP, M_WAITOK);
	if (block == NULL)
		return ENOMEM;

	for (b = (uint64_t)amp->root_tree_paddr + 1; b < amp->block_count; b++) {
		uint32_t i;
		int nonzero = 0;

		if (apfs_block_is_used_by_extent(records, count, b))
			continue;
		error = apfs_read_phys(amp, (apfs_paddr_t)b, block,
		    amp->block_size);
		if (error)
			goto out;
		for (i = 0; i < amp->block_size; i++) {
			if (block[i] != 0) {
				nonzero = 1;
				break;
			}
		}
		if (!nonzero) {
			*block_out = b;
			error = 0;
			goto out;
		}
	}
	error = ENOSPC;
out:
	_FREE(block, M_TEMP);
	return error;
}

static int
apfs_find_free_block(struct apfs_mount *amp, struct apfs_rw_record *records,
    uint32_t count, uint64_t *block_out)
{
	int error;

	error = apfs_spaceman_alloc_block(amp, records, count, block_out);
	if (error == 0)
		return 0;

	APFSLOG("spaceman allocation failed (%d), falling back to zero scan",
	    error);
	return apfs_find_zero_free_block(amp, records, count, block_out);
}

static int
apfs_write_inode_size(struct apfs_mount *amp, uint64_t fileid, uint64_t size)
{
	struct apfs_btree_node_phys *node;
	struct apfs_rw_record *records;
	uint32_t count;
	int error;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;
	records = (struct apfs_rw_record *)_MALLOC(sizeof(*records) *
	    APFS_RW_MAX_RECORDS, M_TEMP, M_WAITOK | M_ZERO);
	if (records == NULL) {
		_FREE(node, M_TEMP);
		return ENOMEM;
	}
	error = apfs_load_root_records(amp, node, records, &count);
	if (error)
		goto out;
	error = apfs_update_records_inode_size(records, count, fileid, size);
	if (error)
		goto out;
	error = apfs_repack_root_records(amp, node, records, count);
out:
	_FREE(records, M_TEMP);
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_zero_file_range(struct apfs_node *apnode, uint64_t start, uint64_t end)
{
	struct apfs_mount *amp = apnode->amp;
	uint64_t phys, len, off;
	int error;

	error = apfs_find_file_extent(amp, apnode->fileid, &phys, &len);
	if (error)
		return error;
	if (end > len)
		return ENOSPC;

	for (off = start; off < end; ) {
		buf_t bp = NULL;
		uint64_t block_index = off / amp->block_size;
		size_t block_off = (size_t)(off % amp->block_size);
		size_t count = amp->block_size - block_off;

		if (count > end - off)
			count = (size_t)(end - off);
		error = (int)buf_meta_bread(amp->devvp,
		    (daddr64_t)(phys + block_index), amp->block_size, NOCRED,
		    &bp);
		if (error) {
			if (bp)
				buf_brelse(bp);
			return error;
		}
		memset((char *)buf_dataptr(bp) + block_off, 0, count);
		error = buf_bwrite(bp);
		if (error)
			return error;
		off += count;
	}
	return 0;
}

static int
apfs_read_omap(struct apfs_mount *amp, apfs_paddr_t paddr,
    struct apfs_omap_phys *omap)
{
	int error = apfs_read_object_prefix(amp, paddr, omap, sizeof(*omap));

	if (error)
		return error;
	if (apfs_object_type(omap->om_o.o_type) != APFS_OBJECT_TYPE_OMAP)
		return EINVAL;
	return 0;
}

static int
apfs_omap_lookup(struct apfs_mount *amp, apfs_paddr_t omap_paddr,
    apfs_oid_t oid, apfs_xid_t xid, struct apfs_omap_val *out,
    apfs_paddr_t *tree_paddr)
{
	struct apfs_omap_phys omap;
	int error;

	error = apfs_read_omap(amp, omap_paddr, &omap);
	if (error)
		return error;
	if (tree_paddr)
		*tree_paddr = le64s(omap.om_tree_oid);
	return apfs_omap_lookup_tree(amp, le64s(omap.om_tree_oid), oid, xid, out);
}

int
apfs_load_volume(struct apfs_mount *amp, __unused vfs_context_t ctx)
{
	struct apfs_omap_phys omap;
	struct apfs_omap_val ov;
	apfs_oid_t fs_oid;
	uint32_t max_fs = amp->max_file_systems;
	int error;

	if (max_fs > APFS_NX_MAX_FILE_SYSTEMS)
		max_fs = APFS_NX_MAX_FILE_SYSTEMS;
	if (max_fs == 0)
		return ENOENT;

	amp->xid = le64(amp->nx.nx_o.o_xid);
	amp->container_omap_oid = le64(amp->nx.nx_omap_oid);
	amp->container_omap_paddr = (apfs_paddr_t)amp->container_omap_oid;

	error = apfs_read_omap(amp, amp->container_omap_paddr, &omap);
	if (error) {
		APFSLOG("container omap 0x%llx read failed: %d",
		    (unsigned long long)amp->container_omap_oid, error);
		return error;
	}
	amp->container_omap_tree_paddr = le64s(omap.om_tree_oid);

	fs_oid = le64(amp->nx.nx_fs_oid[0]);
	error = apfs_omap_lookup_tree(amp, amp->container_omap_tree_paddr,
	    fs_oid, amp->xid, &ov);
	if (error) {
		APFSLOG("volume oid 0x%llx omap lookup failed: %d",
		    (unsigned long long)fs_oid, error);
		return error;
	}

	amp->fs_oid = fs_oid;
	amp->fs_paddr = le64s(ov.ov_paddr);
	error = apfs_read_object_prefix(amp, amp->fs_paddr, &amp->apfs,
	    sizeof(amp->apfs));
	if (error)
		return error;
	if (le32(amp->apfs.apfs_magic) != APFS_APSB_MAGIC)
		return EINVAL;

	amp->volume_omap_oid = le64(amp->apfs.apfs_omap_oid);
	amp->volume_omap_paddr = (apfs_paddr_t)amp->volume_omap_oid;
	amp->root_tree_oid = le64(amp->apfs.apfs_root_tree_oid);

	error = apfs_omap_lookup(amp, amp->volume_omap_paddr,
	    amp->root_tree_oid, amp->xid, &ov, &amp->volume_omap_tree_paddr);
	if (error) {
		APFSLOG("root tree oid 0x%llx omap lookup failed: %d",
		    (unsigned long long)amp->root_tree_oid, error);
		return error;
	}
	amp->root_tree_paddr = le64s(ov.ov_paddr);

	APFSLOG("volume oid=0x%llx paddr=0x%llx omap=0x%llx root_tree=0x%llx->0x%llx",
	    (unsigned long long)amp->fs_oid,
	    (unsigned long long)amp->fs_paddr,
	    (unsigned long long)amp->volume_omap_oid,
	    (unsigned long long)amp->root_tree_oid,
	    (unsigned long long)amp->root_tree_paddr);
	return 0;
}

static enum vtype
apfs_vtype_from_mode(uint16_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFDIR:
		return VDIR;
	case S_IFREG:
		return VREG;
	case S_IFLNK:
		return VLNK;
	case S_IFCHR:
		return VCHR;
	case S_IFBLK:
		return VBLK;
	case S_IFIFO:
		return VFIFO;
	case S_IFSOCK:
		return VSOCK;
	default:
		return VNON;
	}
}

int
apfs_lookup_inode(struct apfs_mount *amp, uint64_t fileid,
    struct apfs_inode_info *info_out)
{
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	uint32_t i;
	int error = 0;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, amp->root_tree_paddr, node);
	if (error)
		goto out;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		error = ENOTSUP;
		goto out;
	}

	info = apfs_btree_info_for_node(amp, node);
	for (i = 0; i < le32(node->btn_nkeys); i++) {
		const struct apfs_j_key *key;
		const struct apfs_j_inode_val *val;
		const void *keyp, *valp;
		uint16_t key_len, val_len;
		uint16_t mode;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			goto out;
		if (key_len < sizeof(*key) || val_len < sizeof(*val))
			continue;

		key = (const struct apfs_j_key *)keyp;
		if (apfs_key_id(key->obj_id_and_type) != fileid ||
		    apfs_key_type(key->obj_id_and_type) != APFS_TYPE_INODE)
			continue;

		val = (const struct apfs_j_inode_val *)valp;
		mode = le16(val->mode);
		memset(info_out, 0, sizeof(*info_out));
		info_out->fileid = fileid;
		info_out->type = apfs_vtype_from_mode(mode);
		info_out->mode = mode & 07777;
		info_out->uid = le32(val->owner);
		info_out->gid = le32(val->group);
		info_out->size = le64(val->uncompressed_size);
		info_out->parent_id = le64(val->parent_id);
		if (info_out->type == VDIR)
			info_out->nlink = (uint32_t)le32((uint32_t)val->u.nchildren) + 2;
		else
			info_out->nlink = (uint32_t)le32((uint32_t)val->u.nlink);
		error = 0;
		goto out;
	}
	error = ENOENT;
out:
	_FREE(node, M_TEMP);
	return error;
}

static int
apfs_emit_dirent(uint64_t fileid, uint8_t type, const char *name,
    uint16_t namelen, struct uio *uio)
{
	struct dirent dent;
	uint16_t reclen;

	if (namelen > NAME_MAX)
		namelen = NAME_MAX;

	memset(&dent, 0, sizeof(dent));
	dent.d_ino = (ino_t)fileid;
	dent.d_type = type;
	dent.d_namlen = (uint8_t)namelen;
	memcpy(dent.d_name, name, namelen);
	dent.d_name[namelen] = '\0';
	reclen = (uint16_t)((offsetof(struct dirent, d_name) + namelen + 1 + 3) & ~3);
	dent.d_reclen = reclen;

	if (uio_resid(uio) < reclen)
		return EMSGSIZE;
	return uiomove((caddr_t)&dent, reclen, uio);
}

static int
apfs_parse_dir_key(const void *keyp, uint16_t key_len, const uint8_t **name,
    uint16_t *namelen)
{
	uint32_t len_hash;

	if (key_len < sizeof(struct apfs_j_key) + sizeof(uint32_t))
		return EINVAL;
	memcpy(&len_hash, (const uint8_t *)keyp + sizeof(struct apfs_j_key),
	    sizeof(len_hash));
	*namelen = (uint16_t)(le32(len_hash) & APFS_DREC_LEN_MASK);
	if (*namelen == 0)
		return EINVAL;
	(*namelen)--;
	if (sizeof(struct apfs_j_key) + sizeof(uint32_t) + *namelen > key_len)
		return EINVAL;
	*name = (const uint8_t *)keyp + sizeof(struct apfs_j_key) +
	    sizeof(uint32_t);
	return 0;
}

int
apfs_lookup_dirent(struct apfs_mount *amp, uint64_t dirid, const char *name,
    size_t namelen, uint64_t *fileid, uint8_t *dtype)
{
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	uint32_t i;
	int error = 0;

	if (amp == NULL || name == NULL || fileid == NULL)
		return EINVAL;
	if (namelen > NAME_MAX)
		return ENAMETOOLONG;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, amp->root_tree_paddr, node);
	if (error)
		goto out;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		error = ENOTSUP;
		goto out;
	}

	info = apfs_btree_info_for_node(amp, node);
	for (i = 0; i < le32(node->btn_nkeys); i++) {
		const struct apfs_j_key *key;
		const struct apfs_j_drec_val *val;
		const uint8_t *entry_name;
		const void *keyp, *valp;
		uint16_t key_len, val_len;
		uint16_t entry_namelen;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			goto out;
		if (val_len < sizeof(*val))
			continue;

		key = (const struct apfs_j_key *)keyp;
		if (apfs_key_id(key->obj_id_and_type) != dirid ||
		    apfs_key_type(key->obj_id_and_type) != APFS_TYPE_DIR_REC)
			continue;
		if (apfs_parse_dir_key(keyp, key_len, &entry_name,
		    &entry_namelen))
			continue;
		if (entry_namelen != namelen ||
		    memcmp(entry_name, name, namelen) != 0)
			continue;

		val = (const struct apfs_j_drec_val *)valp;
		*fileid = le64(val->file_id);
		if (dtype)
			*dtype = (uint8_t)(le16(val->flags) & 0x0f);
		error = 0;
		goto out;
	}
	error = ENOENT;

out:
	_FREE(node, M_TEMP);
	return error;
}

int
apfs_iterate_dir(struct apfs_mount *amp, uint64_t dirid, off_t start_index,
    struct uio *uio, int *numdirent, int *eofflag)
{
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	uint32_t i;
	off_t logical_index = 0;
	int entries = 0;
	int error = 0;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, amp->root_tree_paddr, node);
	if (error)
		goto out;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		error = ENOTSUP;
		goto out;
	}

	info = apfs_btree_info_for_node(amp, node);
	for (i = 0; i < le32(node->btn_nkeys); i++) {
		const struct apfs_j_key *key;
		const struct apfs_j_drec_val *val;
		const uint8_t *name;
		const void *keyp, *valp;
		uint16_t key_len, val_len;
		uint16_t namelen;

		error = apfs_btree_entry(amp, node, info, i, &keyp, &key_len,
		    &valp, &val_len);
		if (error)
			goto out;
		if (val_len < sizeof(*val))
			continue;

		key = (const struct apfs_j_key *)keyp;
		if (apfs_key_id(key->obj_id_and_type) != dirid ||
		    apfs_key_type(key->obj_id_and_type) != APFS_TYPE_DIR_REC)
			continue;

		if (logical_index++ < start_index)
			continue;

		if (apfs_parse_dir_key(keyp, key_len, &name, &namelen))
			continue;

		val = (const struct apfs_j_drec_val *)valp;
		error = apfs_emit_dirent(le64(val->file_id),
		    (uint8_t)(le16(val->flags) & 0x0f), (const char *)name,
		    namelen, uio);
		if (error == EMSGSIZE) {
			error = 0;
			goto out;
		}
		if (error)
			goto out;
		entries++;
	}

out:
	_FREE(node, M_TEMP);
	if (numdirent)
		*numdirent = entries;
	if (eofflag)
		*eofflag = (error == 0);
	return error;
}

int
apfs_read_file(struct apfs_node *apnode, struct uio *uio)
{
	struct apfs_mount *amp;
	struct apfs_btree_node_phys *node;
	const struct apfs_btree_info *info;
	uint64_t filesize;
	int error = 0;

	if (apnode == NULL || uio == NULL)
		return EINVAL;
	amp = apnode->amp;
	if (amp == NULL)
		return EINVAL;
	filesize = apnode->size;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;

	error = apfs_read_object_phys(amp, amp->root_tree_paddr, node);
	if (error)
		goto out;
	if ((le16(node->btn_flags) & APFS_BTNODE_LEAF) == 0) {
		error = ENOTSUP;
		goto out;
	}
	info = apfs_btree_info_for_node(amp, node);

	while (uio_resid(uio) > 0 && (uint64_t)uio_offset(uio) < filesize) {
		uint64_t file_off = (uint64_t)uio_offset(uio);
		uint64_t best_logical = 0;
		uint64_t best_len = 0;
		uint64_t best_phys = 0;
		uint32_t i;
		int found = 0;

		for (i = 0; i < le32(node->btn_nkeys); i++) {
			const struct apfs_j_file_extent_key *key;
			const struct apfs_j_file_extent_val *val;
			const void *keyp, *valp;
			uint16_t key_len, val_len;
			uint64_t logical, len, end;

			error = apfs_btree_entry(amp, node, info, i, &keyp,
			    &key_len, &valp, &val_len);
			if (error)
				goto out;
			if (key_len < sizeof(*key) || val_len < sizeof(*val))
				continue;

			key = (const struct apfs_j_file_extent_key *)keyp;
			if (apfs_key_id(key->hdr.obj_id_and_type) !=
			    apnode->fileid ||
			    apfs_key_type(key->hdr.obj_id_and_type) !=
			    APFS_TYPE_FILE_EXTENT)
				continue;

			val = (const struct apfs_j_file_extent_val *)valp;
			logical = le64(key->logical_addr);
			len = le64(val->len_and_flags) &
			    APFS_FILE_EXTENT_LEN_MASK;
			end = logical + len;
			if (len == 0 || file_off < logical || file_off >= end)
				continue;
			best_logical = logical;
			best_len = len;
			best_phys = le64(val->phys_block_num);
			found = 1;
			break;
		}

		if (!found) {
			error = EIO;
			goto out;
		} else {
			uint64_t extent_off = file_off - best_logical;
			uint64_t avail = best_len - extent_off;
			uint64_t remain = filesize - file_off;
			uint64_t block_index = extent_off / amp->block_size;
			size_t block_off = (size_t)(extent_off % amp->block_size);
			size_t count = amp->block_size - block_off;

			if (avail < count)
				count = (size_t)avail;
			if (remain < count)
				count = (size_t)remain;
			if ((uint64_t)uio_resid(uio) < count)
				count = (size_t)uio_resid(uio);

			error = apfs_uiomove_phys(amp,
			    (apfs_paddr_t)(best_phys + block_index), block_off,
			    count, uio);
			if (error)
				goto out;
		}
	}

out:
	_FREE(node, M_TEMP);
	return error;
}

int
apfs_write_file(struct apfs_node *apnode, struct uio *uio)
{
	struct apfs_mount *amp;
	uint64_t phys, len;
	uint64_t old_size;
	int error;
	int dirty = 0;

	if (apnode == NULL || uio == NULL)
		return EINVAL;
	amp = apnode->amp;
	if (amp == NULL)
		return EINVAL;
	if (uio_offset(uio) < 0)
		return EINVAL;
	if ((uint64_t)uio_offset(uio) > apnode->size)
		return EFBIG;

	error = apfs_find_file_extent(amp, apnode->fileid, &phys, &len);
	if (error)
		return error;
	old_size = apnode->size;

	while (uio_resid(uio) > 0) {
		uint64_t file_off = (uint64_t)uio_offset(uio);
		uint64_t block_index;
		size_t block_off, count;

		if (file_off >= len) {
			error = ENOSPC;
			break;
		}
		block_index = file_off / amp->block_size;
		block_off = (size_t)(file_off % amp->block_size);
		count = amp->block_size - block_off;
		if ((uint64_t)count > len - file_off)
			count = (size_t)(len - file_off);
		if ((uint64_t)uio_resid(uio) < count)
			count = (size_t)uio_resid(uio);

		error = apfs_uiomove_write_phys(amp,
		    (apfs_paddr_t)(phys + block_index), block_off, count, uio);
		if (error)
			break;
		if ((uint64_t)uio_offset(uio) > apnode->size)
			apnode->size = (uint64_t)uio_offset(uio);
		dirty = 1;
	}

	if (dirty && apnode->size != old_size) {
		int size_error = apfs_write_inode_size(amp, apnode->fileid,
		    apnode->size);

		if (size_error == 0)
			ubc_setsize(apnode->vp, apnode->size);
		if (error == 0)
			error = size_error;
	}
	return error;
}

int
apfs_set_file_size(struct apfs_node *apnode, uint64_t size)
{
	struct apfs_mount *amp;
	uint64_t phys, len;
	uint64_t old_size;
	int error;

	if (apnode == NULL || apnode->amp == NULL)
		return EINVAL;
	if (apnode->type != VREG)
		return EISDIR;
	amp = apnode->amp;
	error = apfs_find_file_extent(amp, apnode->fileid, &phys, &len);
	if (error)
		return error;
	if (size > len)
		return ENOSPC;

	old_size = apnode->size;
	if (size > old_size) {
		error = apfs_zero_file_range(apnode, old_size, size);
		if (error)
			return error;
	} else if (size < old_size) {
		error = apfs_zero_file_range(apnode, size, old_size);
		if (error)
			return error;
	}

	error = apfs_write_inode_size(amp, apnode->fileid, size);
	if (error)
		return error;
	apnode->size = size;
	ubc_setsize(apnode->vp, apnode->size);
	return 0;
}

int
apfs_create_file(struct apfs_node *dir, const char *name, size_t namelen,
    mode_t mode, uid_t uid, gid_t gid, uint64_t *fileid_out)
{
	struct apfs_mount *amp;
	struct apfs_btree_node_phys *node;
	struct apfs_rw_record *records = NULL;
	uint32_t count, i;
	uint64_t fileid = APFS_ROOT_FILEID;
	uint64_t existing;
	uint64_t data_block;
	uint8_t zero[APFS_BS_BYTES];
	uint8_t drec_key[sizeof(struct apfs_j_key) + sizeof(uint32_t) +
	    NAME_MAX + 1];
	struct apfs_j_drec_val drec_val;
	struct apfs_j_inode_val inode_val;
	struct apfs_j_file_extent_key extent_key;
	struct apfs_j_file_extent_val extent_val;
	uint64_t jkey;
	uint32_t len_hash;
	int error;

	if (dir == NULL || dir->amp == NULL || name == NULL || fileid_out == NULL)
		return EINVAL;
	if (dir->type != VDIR || dir->fileid != APFS_ROOT_FILEID)
		return ENOTSUP;
	if (namelen == 0 || namelen > NAME_MAX)
		return ENAMETOOLONG;

	amp = dir->amp;
	error = apfs_lookup_dirent(amp, dir->fileid, name, namelen, &existing,
	    NULL);
	if (error == 0)
		return EEXIST;
	if (error != ENOENT)
		return error;

	node = (struct apfs_btree_node_phys *)_MALLOC(amp->block_size, M_TEMP,
	    M_WAITOK);
	if (node == NULL)
		return ENOMEM;
	records = (struct apfs_rw_record *)_MALLOC(sizeof(*records) *
	    APFS_RW_MAX_RECORDS, M_TEMP, M_WAITOK | M_ZERO);
	if (records == NULL) {
		_FREE(node, M_TEMP);
		return ENOMEM;
	}

	error = apfs_load_root_records(amp, node, records, &count);
	if (error)
		goto out;

	for (i = 0; i < count; i++) {
		uint64_t key;

		if (records[i].key_len < sizeof(struct apfs_j_key))
			continue;
		memcpy(&key, records[i].key, sizeof(key));
		if (apfs_key_id(key) > fileid)
			fileid = apfs_key_id(key);
	}
	fileid++;

	error = apfs_find_free_block(amp, records, count, &data_block);
	if (error)
		goto out;
	memset(zero, 0, sizeof(zero));
	error = apfs_write_phys(amp, (apfs_paddr_t)data_block, zero,
	    amp->block_size);
	if (error)
		goto out;

	memset(drec_key, 0, sizeof(drec_key));
	jkey = hle64(apfs_make_jkey(APFS_ROOT_FILEID, APFS_TYPE_DIR_REC));
	memcpy(drec_key, &jkey, sizeof(jkey));
	len_hash = hle32((uint32_t)(namelen + 1));
	memcpy(drec_key + sizeof(struct apfs_j_key), &len_hash,
	    sizeof(len_hash));
	memcpy(drec_key + sizeof(struct apfs_j_key) + sizeof(uint32_t), name,
	    namelen);
	memset(&drec_val, 0, sizeof(drec_val));
	drec_val.file_id = hle64(fileid);
	drec_val.flags = hle16(DT_REG);

	memset(&inode_val, 0, sizeof(inode_val));
	inode_val.parent_id = hle64(APFS_ROOT_FILEID);
	inode_val.private_id = hle64(fileid);
	inode_val.u.nlink = hle32(1);
	inode_val.owner = hle32(uid);
	inode_val.group = hle32(gid);
	inode_val.mode = hle16((uint16_t)(S_IFREG | (mode & 07777)));
	inode_val.uncompressed_size = hle64(0);

	memset(&extent_key, 0, sizeof(extent_key));
	extent_key.hdr.obj_id_and_type =
	    hle64(apfs_make_jkey(fileid, APFS_TYPE_FILE_EXTENT));
	extent_key.logical_addr = hle64(0);
	memset(&extent_val, 0, sizeof(extent_val));
	extent_val.len_and_flags = hle64(amp->block_size * APFS_RW_FILE_BLOCKS);
	extent_val.phys_block_num = hle64(data_block);

	error = apfs_append_rw_record(records, &count, drec_key,
	    (uint16_t)(sizeof(struct apfs_j_key) + sizeof(uint32_t) +
	    namelen + 1), &drec_val, (uint16_t)sizeof(drec_val));
	if (error)
		goto out;
	jkey = hle64(apfs_make_jkey(fileid, APFS_TYPE_INODE));
	error = apfs_append_rw_record(records, &count, &jkey, sizeof(jkey),
	    &inode_val, (uint16_t)sizeof(inode_val));
	if (error)
		goto out;
	error = apfs_append_rw_record(records, &count, &extent_key,
	    (uint16_t)sizeof(extent_key), &extent_val,
	    (uint16_t)sizeof(extent_val));
	if (error)
		goto out;

	apfs_touch_root_dir_record(records, count, 1);
	error = apfs_repack_root_records(amp, node, records, count);
	if (error)
		goto out;
	*fileid_out = fileid;

out:
	if (records)
		_FREE(records, M_TEMP);
	_FREE(node, M_TEMP);
	return error;
}
