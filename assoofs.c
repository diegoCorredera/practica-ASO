#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"

MODULE_LICENSE("GPL");

/*
 *  Prototipos de funciones
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);
ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
struct dentry *assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir , struct dentry *dentry, umode_t mode);
static int assoofs_remove(struct inode *dir, struct dentry *dentry);
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
static struct inode *assoofs_get_inode(struct super_block *sb, int ino);

/*
 *  Estructuras de datos necesarias
 */

// Definicion del tipo de sistema de archivos assoofs
static struct file_system_type assoofs_type = {
    .owner   = THIS_MODULE,
    .name    = "assoofs",
    .mount   = assoofs_mount,
    .kill_sb = kill_block_super,
};

// Operaciones sobre ficheros
const struct file_operations assoofs_file_operations = {
    .read = assoofs_read,
    .write = assoofs_write,
};

// Operaciones sobre dircctorios
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate_shared = assoofs_iterate,
};
// Operaciones sobre inodos
static struct inode_operations assoofs_inode_ops = {
    .create = assoofs_create,
    .lookup = assoofs_lookup,
    .mkdir = assoofs_mkdir,
    .unlink = assoofs_remove,
    .rmdir = assoofs_remove,
};
// Operaciones sobre el superbloque
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};


/*
 *  Funciones que realizan operaciones sobre ficheros
 */

ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    return 0;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Write request\n");
    return 0;
}

/*
 *  Funciones que realizan operaciones sobre directorios
 */
static struct inode *assoofs_get_inode (struct super_block *sb, int ino)
{
    printk(KERN_INFO "get_inode request\n");
struct assoofs_inode_info *inode_info = NULL;
struct buffer_head *bh;
struct assoofs_inode_info *buffer = NULL;
struct assoofs_dir_record_entry *record;
bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

struct assoofs_super_block_info *afs_sb = sb->s_fs_info;

inode_info = (struct assoofs_inode_info *)bh->b_data;
// recorremos todo
int i =0;


 // nuevo inodo
 struct inode *inode;
 inode=new_inode(sb);   
 inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
 inode->i_sb = sb;
 inode->i_op = &assoofs_inode_ops;
if (S_ISDIR(inode_info->mode))
inode->i_fop = &assoofs_dir_operations;
else if (S_ISREG(inode_info->mode))
inode->i_fop = &assoofs_file_operations;
else
printk(KERN_ERR "Unknown inode type. Neither a directory nor a file.");
 // cogemos info persistente
 //inode->i_private = buffer;
struct timespec64 inode_set_ctime(struct inode *inode, time64_t sec, long nsec);
struct timespec64 inode_set_mtime(struct inode *inode, time64_t sec, long nsec);
struct timespec64 inode_set_atime(struct inode *inode, time64_t sec, long nsec);
 inode->i_private = buffer;
 brelse(bh);
return inode;
}
static int assoofs_iterate(struct file *filp, struct dir_context *ctx) 
{
    printk(KERN_INFO "Iterate request\n");
    struct assoofs_dir_record_entry *record;
    struct inode *inode;
struct super_block *sb;
 struct assoofs_inode_info *inode_info;
inode = filp->f_path.dentry->d_inode;
sb = inode->i_sb;
inode_info = inode->i_private;
if (ctx->pos) return 0;
if ((!S_ISDIR(inode_info->mode))) return -1;
struct buffer_head *bh;
int i;
bh = sb_bread(sb, inode_info->data_block_number);
 record = (struct assoofs_dir_record_entry *)bh->b_data;
 for (i = 0; i < inode_info->dir_children_count; i++) {

if(record->entry_removed == ASSOOFS_FALSE){

dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN);

ctx->pos += sizeof(struct assoofs_dir_record_entry);
brelse(bh);
}

record++;
 }
 brelse(bh);

    return 0;
}

/*
 *  Funciones que realizan operaciones sobre inodos
 */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    printk(KERN_INFO "Lookup request\n");
    struct assoofs_inode_info *parent_info = parent_inode->i_private;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh;
    bh = sb_bread(sb, parent_info->data_block_number);
    struct assoofs_dir_record_entry *record;
record = (struct assoofs_dir_record_entry *)bh->b_data;
int i;
 for (i=0; i < parent_info->dir_children_count; i++)
  {
    if (!strcmp(record->filename, child_dentry->d_name.name) && record->entry_removed == ASSOOFS_FALSE)
    {
        printk(KERN_INFO"entro en inode %d vez\n", i);
    struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Función auxiliar que obtine la informaciónde un inodo a partir de su número de inodo.
    inode_init_owner(&nop_mnt_idmap,inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
    brelse(bh);
    d_add(child_dentry, inode);
    return NULL;
    }
 record++;
}
brelse(bh);

    return NULL;
}


static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    printk(KERN_INFO "New file request\n");
    return 0;
}

struct dentry *assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir , struct dentry *dentry, umode_t mode){
    printk(KERN_INFO "New directory request\n");
    return 0;
}

static int assoofs_remove(struct inode *dir, struct dentry *dentry){
    printk(KERN_INFO "assoofs_remove request\n");
    return 0;
}
/**
 * buscamos info del inodo
 */
struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no) 
{
    printk(KERN_INFO "get_inode_info request\n");
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;

    // Leer el bloque que contiene el almacén de inodos

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    
    inode_info = (struct assoofs_inode_info *)bh->b_data;
   
    // Recorrer los inodos guardados en disco
    for (i = 0; i < afs_sb->inodes_count; i++) 
    {
        printk(KERN_INFO "accedo a  %llu inodo\n",inode_info->inode_no);
        if (inode_info->inode_no == inode_no) 
        {

            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            if (buffer) {
                memcpy(buffer, inode_info, sizeof(*buffer));
            }
            break;
        }
        inode_info++;
    }
    printk(KERN_INFO "brelse inode_info\n");
    brelse(bh);
    return buffer;
}
/*
 *  Inicialización del superbloque
 */
int assoofs_fill_super(struct super_block *sb, void *data, int silent)
 {   
    printk(KERN_INFO "assoofs_fill_super request\n");
    struct buffer_head *bh;//lectura de código
    struct assoofs_super_block_info *assoofs_sb;// info del super bloque
    struct inode *root_inode;// inodo inicial, solo se accede una vez
    struct assoofs_inode_info;// información de inodo
    // Leer bloque 0 NOS DEVUELVE LA POSIBILIDAD DE TOQUETEAR --BUFFER HEAD--
    printk(KERN_INFO "pedimos paso porfaplis de lectura de super bloque\n");
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    if (!bh) return -EIO;
    // recogemos el contenido del bloque residente en b_data y se lo metemos a assoofs_sb
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;
    printk(KERN_INFO "en el superbloque hay assoofs: Inodes count: %llu inodos\n", assoofs_sb->inodes_count);
     brelse(bh);
    // pongo num mágico
    sb->s_magic = ASSOOFS_MAGIC;
    // meto el tamaño de bloque
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    // guardo la información del bloque 0 a las operaciones 
    sb->s_op = &assoofs_sops;
    sb->s_fs_info = assoofs_sb;

    // Crear inodo raíz
    root_inode = new_inode(sb);
    if (!root_inode) return -ENOMEM;
    // propietario
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR );

    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
    root_inode->i_sb = sb;
    root_inode->i_op = &assoofs_inode_ops;
    root_inode->i_fop = &assoofs_dir_operations;
    
    struct timespec64 ts = current_time(root_inode); // fechas.
    inode_set_ctime(root_inode, ts.tv_sec, ts.tv_nsec);
    // Info privada
    printk(KERN_INFO "pedimos paso porfaplis de lectura de super bloque\n");
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER);
    inode_set_ctime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(root_inode, ts.tv_sec, ts.tv_nsec);
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); // Información persistente del
 //inodo
    // Dentry raíz
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) return -ENOMEM;
   
    
    // vamos a hacer como que no ha pasado nada y dejamos todo donde lo encontramos
printk(KERN_INFO "brelse fill super\n");
    
    return 0;
}

/*
 *  Montaje de dispositivos assoofs
 */
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    struct dentry *ret;
    printk(KERN_INFO "assoofs_mount request\n");
    ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
    // Control de errores a partir del valor de retorno. En este caso se puede utilizar la macro IS_ERR: if (IS_ERR(ret)) ...
    return ret;
}



static int __init assoofs_init(void) {
    int ret;
    printk(KERN_INFO "assoofs_init request\n");
    ret = register_filesystem(&assoofs_type);
    // Control de errores a partir del valor de retorno
    return ret;
}

static void __exit assoofs_exit(void) {
    int ret;
    printk(KERN_INFO "assoofs_exit request\n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de retorno
}

module_init(assoofs_init);
module_exit(assoofs_exit);
