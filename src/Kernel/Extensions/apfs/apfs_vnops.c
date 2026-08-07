#include "apfs.h"

#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/unistd.h>
#include <sys/vnode_if.h>
#include <string.h>

int (**apfs_vnodeop_p)(void *);

#define APFS_DIRENT_SZ(dp) \
	((sizeof(struct dirent) - NAME_MAX) + (((dp)->d_namlen + 1 + 3) & ~3))

static int
apfs_emit_dirent(ino_t ino, const char *name, struct uio *uio)
{
	struct dirent dent;
	size_t namelen = strlen(name);

	if (namelen > NAME_MAX)
		return EINVAL;

	memset(&dent, 0, sizeof(dent));
	dent.d_ino = ino;
	dent.d_type = DT_DIR;
	dent.d_namlen = (uint8_t)namelen;
	dent.d_reclen = APFS_DIRENT_SZ(&dent);
	strlcpy(dent.d_name, name, sizeof(dent.d_name));

	if (uio_resid(uio) < dent.d_reclen)
		return EMSGSIZE;
	return uiomove((caddr_t)&dent, dent.d_reclen, uio);
}

int
apfs_vget(struct apfs_mount *amp, uint64_t fileid, vnode_t dvp, vnode_t *vpp)
{
	struct vnode_fsparam vfsp;
	struct apfs_node *node;
	struct apfs_inode_info info;
	int error;

	if (amp == NULL || vpp == NULL)
		return EINVAL;
	error = apfs_lookup_inode(amp, fileid, &info);
	if (error)
		return error;

	node = (struct apfs_node *)_MALLOC(sizeof(*node), M_TEMP,
	    M_WAITOK | M_ZERO);
	if (node == NULL)
		return ENOMEM;

	node->amp = amp;
	node->fileid = fileid;
	node->type = info.type;
	node->mode = info.mode;
	node->uid = info.uid;
	node->gid = info.gid;
	node->size = info.size;
	node->nlink = info.nlink;
	node->parent_id = info.parent_id;

	memset(&vfsp, 0, sizeof(vfsp));
	vfsp.vnfs_mp = amp->mp;
	vfsp.vnfs_vtype = node->type;
	vfsp.vnfs_str = APFS_MODULE_NAME;
	vfsp.vnfs_dvp = dvp;
	vfsp.vnfs_fsnode = node;
	vfsp.vnfs_vops = apfs_vnodeop_p;
	vfsp.vnfs_markroot = (fileid == APFS_ROOT_FILEID);
	vfsp.vnfs_marksystem = 0;
	vfsp.vnfs_rdev = 0;
	vfsp.vnfs_filesize = (off_t)node->size;
	vfsp.vnfs_cnp = NULL;
	vfsp.vnfs_flags = VNFS_ADDFSREF | VNFS_NOCACHE;

	error = vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vfsp, vpp);
	if (error) {
		_FREE(node, M_TEMP);
		return error;
	}

	node->vp = *vpp;
	vnode_settag(*vpp, VT_OTHER);
	return 0;
}

static int
apfs_vnop_default(__unused struct vnop_generic_args *ap)
{
	return ENOTSUP;
}

static int
apfs_vnop_lookup(struct vnop_lookup_args *ap)
{
	struct componentname *cnp = ap->a_cnp;
	struct apfs_node *dnode = VTOAPFS(ap->a_dvp);
	uint64_t fileid;
	int error;

	*ap->a_vpp = NULLVP;
	if (dnode == NULL)
		return EINVAL;
	if (dnode->type != VDIR)
		return ENOTDIR;

	if (cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		error = vnode_get(ap->a_dvp);
		if (error)
			return error;
		*ap->a_vpp = ap->a_dvp;
		return 0;
	}
	if (cnp->cn_namelen == 2 && cnp->cn_nameptr[0] == '.' &&
	    cnp->cn_nameptr[1] == '.') {
		fileid = dnode->parent_id ? dnode->parent_id : APFS_ROOT_FILEID;
		if (fileid == dnode->fileid) {
			error = vnode_get(ap->a_dvp);
			if (error)
				return error;
			*ap->a_vpp = ap->a_dvp;
			return 0;
		}
		return apfs_vget(dnode->amp, fileid, NULLVP, ap->a_vpp);
	}

	error = apfs_lookup_dirent(dnode->amp, dnode->fileid,
	    cnp->cn_nameptr, cnp->cn_namelen, &fileid, NULL);
	if (error) {
		if (error == ENOENT && (cnp->cn_flags & ISLASTCN) &&
		    (cnp->cn_nameiop == CREATE || cnp->cn_nameiop == RENAME))
			return EJUSTRETURN;
		return error;
	}
	return apfs_vget(dnode->amp, fileid, ap->a_dvp, ap->a_vpp);
}

static int
apfs_vnop_open(__unused struct vnop_open_args *ap)
{
	return 0;
}

static int
apfs_vnop_close(__unused struct vnop_close_args *ap)
{
	return 0;
}

static int
apfs_vnop_getattr(struct vnop_getattr_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct vnode_attr *vap = ap->a_vap;
	struct apfs_node *node = VTOAPFS(vp);
	struct apfs_mount *amp = node ? node->amp : VFSTOAPFS(vnode_mount(vp));
	uint32_t iosize = (amp && amp->block_size) ? amp->block_size : APFS_BS_BYTES;

	if (node == NULL)
		return EINVAL;

	VATTR_RETURN(vap, va_type, node->type);
	VATTR_RETURN(vap, va_rdev, 0);
	VATTR_RETURN(vap, va_nlink, node->nlink ? node->nlink : 1);
	VATTR_RETURN(vap, va_total_size, node->size);
	VATTR_RETURN(vap, va_data_size, node->size);
	VATTR_RETURN(vap, va_total_alloc, node->size);
	VATTR_RETURN(vap, va_data_alloc, node->size);
	VATTR_RETURN(vap, va_iosize, iosize);
	VATTR_RETURN(vap, va_uid, node->uid);
	VATTR_RETURN(vap, va_gid, node->gid);
	VATTR_RETURN(vap, va_mode, node->mode);
	VATTR_RETURN(vap, va_fileid, node->fileid);
	VATTR_RETURN(vap, va_linkid, node->fileid);
	VATTR_RETURN(vap, va_parentid,
	    node->parent_id ? node->parent_id : APFS_ROOT_FILEID);
	if (amp) {
		VATTR_RETURN(vap, va_fsid, vfs_statfs(amp->mp)->f_fsid.val[0]);
		VATTR_RETURN(vap, va_fsid64, vfs_statfs(amp->mp)->f_fsid);
	}
	VATTR_RETURN(vap, va_filerev, 0);
	VATTR_RETURN(vap, va_gen, 0);
	VATTR_RETURN(vap, va_flags, 0);
	return 0;
}

static int
apfs_vnop_readdir(struct vnop_readdir_args *ap)
{
	vnode_t vp = ap->a_vp;
	struct apfs_node *node = VTOAPFS(vp);
	struct uio *uio = ap->a_uio;
	off_t offset = uio_offset(uio);
	int error = 0;
	int entries = 0;
	int real_entries = 0;
	int real_eof = 1;

	if (ap->a_flags & (VNODE_READDIR_EXTENDED | VNODE_READDIR_REQSEEKOFF))
		return EINVAL;
	if (node == NULL)
		return EINVAL;
	if (node->type != VDIR)
		return ENOTDIR;

	if (offset == 0) {
		error = apfs_emit_dirent((ino_t)node->fileid, ".", uio);
		if (error)
			goto out;
		offset++;
		entries++;
	}
	if (offset == 1) {
		error = apfs_emit_dirent((ino_t)APFS_ROOT_FILEID, "..", uio);
		if (error)
			goto out;
		offset++;
		entries++;
	}
	if (offset >= 2) {
		error = apfs_iterate_dir(node->amp, node->fileid, offset - 2,
		    uio, &real_entries, &real_eof);
		if (error)
			goto out;
		offset += real_entries;
		entries += real_entries;
	}

out:
	if (error == EMSGSIZE)
		error = 0;
	uio_setoffset(uio, offset);
	if (ap->a_eofflag)
		*ap->a_eofflag = (offset >= 2 && real_eof);
	if (ap->a_numdirent)
		*ap->a_numdirent = entries;
	return error;
}

static int
apfs_vnop_read(struct vnop_read_args *ap)
{
	struct apfs_node *node = VTOAPFS(ap->a_vp);
	struct uio *uio = ap->a_uio;

	if (node == NULL || uio == NULL)
		return EINVAL;
	if (node->type == VDIR)
		return EISDIR;
	if (node->type != VREG)
		return ENOTSUP;
	if (uio_offset(uio) < 0)
		return EINVAL;
	if ((uint64_t)uio_offset(uio) >= node->size)
		return 0;
	return apfs_read_file(node, uio);
}

static int
apfs_vnop_write(struct vnop_write_args *ap)
{
	struct apfs_node *node = VTOAPFS(ap->a_vp);
	struct uio *uio = ap->a_uio;

	if (node == NULL || uio == NULL)
		return EINVAL;
	if (node->type == VDIR)
		return EISDIR;
	if (node->type != VREG)
		return ENOTSUP;
	if (ap->a_ioflag & IO_APPEND)
		uio_setoffset(uio, (off_t)node->size);
	return apfs_write_file(node, uio);
}

static int
apfs_vnop_setattr(struct vnop_setattr_args *ap)
{
	struct apfs_node *node = VTOAPFS(ap->a_vp);
	struct vnode_attr *vap = ap->a_vap;
	int error;

	if (node == NULL || vap == NULL)
		return EINVAL;
	if (VATTR_IS_ACTIVE(vap, va_data_size)) {
		error = apfs_set_file_size(node, vap->va_data_size);
		if (error)
			return error;
		VATTR_SET_SUPPORTED(vap, va_data_size);
	}
	if (VATTR_IS_ACTIVE(vap, va_mode)) {
		node->mode = vap->va_mode & 07777;
		VATTR_SET_SUPPORTED(vap, va_mode);
	}
	return 0;
}

static int
apfs_vnop_create(struct vnop_create_args *ap)
{
	struct apfs_node *dnode = VTOAPFS(ap->a_dvp);
	struct vnode_attr *vap = ap->a_vap;
	mode_t mode = 0644;
	uid_t uid = 0;
	gid_t gid = 0;
	uint64_t fileid;
	int error;

	*ap->a_vpp = NULLVP;
	if (dnode == NULL)
		return EINVAL;
	if (dnode->type != VDIR)
		return ENOTDIR;
	if (VATTR_IS_ACTIVE(vap, va_type) && vap->va_type != VREG)
		return ENOTSUP;
	if (ap->a_cnp->cn_namelen == 0 || ap->a_cnp->cn_namelen > NAME_MAX)
		return ENAMETOOLONG;
	if (VATTR_IS_ACTIVE(vap, va_mode))
		mode = vap->va_mode & 07777;
	if (VATTR_IS_ACTIVE(vap, va_uid))
		uid = vap->va_uid;
	if (VATTR_IS_ACTIVE(vap, va_gid))
		gid = vap->va_gid;

	error = apfs_create_file(dnode, ap->a_cnp->cn_nameptr,
	    ap->a_cnp->cn_namelen, mode, uid, gid, &fileid);
	if (error)
		return error;

	VATTR_SET_SUPPORTED(vap, va_type);
	VATTR_SET_SUPPORTED(vap, va_mode);
	VATTR_SET_SUPPORTED(vap, va_uid);
	VATTR_SET_SUPPORTED(vap, va_gid);
	return apfs_vget(dnode->amp, fileid, ap->a_dvp, ap->a_vpp);
}

static int
apfs_vnop_fsync(__unused struct vnop_fsync_args *ap)
{
	return 0;
}

static int
apfs_vnop_reclaim(struct vnop_reclaim_args *ap)
{
	struct apfs_node *node = VTOAPFS(ap->a_vp);

	vnode_removefsref(ap->a_vp);
	vnode_clearfsnode(ap->a_vp);
	if (node)
		_FREE(node, M_TEMP);
	return 0;
}

static int
apfs_vnop_pathconf(struct vnop_pathconf_args *ap)
{
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return 0;
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return 0;
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		return 0;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return 0;
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return 0;
	default:
		return EINVAL;
	}
}

#define VOPFUNC int (*)(void *)

static const struct vnodeopv_entry_desc apfs_vnodeop_entries[] = {
	{ &vnop_default_desc, (VOPFUNC)apfs_vnop_default },
	{ &vnop_lookup_desc, (VOPFUNC)apfs_vnop_lookup },
	{ &vnop_create_desc, (VOPFUNC)apfs_vnop_create },
	{ &vnop_open_desc, (VOPFUNC)apfs_vnop_open },
	{ &vnop_close_desc, (VOPFUNC)apfs_vnop_close },
	{ &vnop_getattr_desc, (VOPFUNC)apfs_vnop_getattr },
	{ &vnop_setattr_desc, (VOPFUNC)apfs_vnop_setattr },
	{ &vnop_read_desc, (VOPFUNC)apfs_vnop_read },
	{ &vnop_write_desc, (VOPFUNC)apfs_vnop_write },
	{ &vnop_readdir_desc, (VOPFUNC)apfs_vnop_readdir },
	{ &vnop_fsync_desc, (VOPFUNC)apfs_vnop_fsync },
	{ &vnop_reclaim_desc, (VOPFUNC)apfs_vnop_reclaim },
	{ &vnop_pathconf_desc, (VOPFUNC)apfs_vnop_pathconf },
	{ NULL, NULL }
};

struct vnodeopv_desc apfs_vnodeop_opv_desc = {
	&apfs_vnodeop_p, apfs_vnodeop_entries
};
