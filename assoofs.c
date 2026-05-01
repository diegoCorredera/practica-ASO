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
int assoofs_sb_get_a_freeinode(struct super_block *sb, unsigned long *inode);
void assoofs_save_sb_info(struct super_block *vsb);
int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block);
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info);
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct
assoofs_inode_info *search);
 /*  Estructuras de datos necesarias
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

int assoofs_sb_get_a_freeinode(struct super_block *sb, unsigned long *inode)
{
    printk(KERN_INFO "freeinode request\n");
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    // recorremos todo y comprobamos primero que no hemos superado el límite
    int i=0;
    for (i = 1; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
    {
    if (~(assoofs_sb->free_inodes) & (1 << i))
        {
        break;
        }
    }
*inode = i;
printk(KERN_INFO "inodo  %d libre\n", i);
assoofs_sb->free_inodes |= (1 << i);
assoofs_save_sb_info(sb);
return 0;
}
// Operaciones sobre el superbloque
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

void assoofs_save_sb_info(struct super_block *vsb)
{
     printk(KERN_INFO "save sb info request\n");
     struct buffer_head *bh;
struct assoofs_super_block_info *sb = vsb->s_fs_info; // Información persistente del superbloque en memoria
bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
bh->b_data = (char *)sb; // Sobreescribo los datos de disco con la información en memoria
mark_buffer_dirty(bh);
sync_dirty_buffer(bh);

}

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block)
{
printk(KERN_INFO "get freeblock request\n"); 
struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
int i;
for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
{
    if (~(assoofs_sb->free_blocks) & (1 << i))
    break; // cuando aparece el primer bit 1 en free_block dejamos de recorrer el mapa de bits, i tiene la posición
}
*block = i; // Escribimos el valor de i en la dirección de memoria indicada como segundo argumento en la función
printk(KERN_INFO "bloque %d libre\n", i);
assoofs_sb->free_blocks |= (1 << i);

assoofs_save_sb_info(sb);
return 0;
}
void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode)
{
struct assoofs_inode_info *inode_info = NULL;
struct buffer_head *bh;
struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
printk(KERN_INFO "addd inode info\n");
 bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
 // obtenemos puntero al final del bloque para meterlo
 inode_info = (struct assoofs_inode_info *)bh->b_data;
 inode_info += assoofs_sb->inodes_count;
 memcpy(inode_info, inode, sizeof(struct assoofs_inode_info));
 // actualizamos la dirección a sucio o usado
 mark_buffer_dirty(bh);
sync_dirty_buffer(bh);
// miramos que no se exceda la info del bloque, porque si no hay que actualizarlo entero
if (assoofs_sb->inodes_count <= inode->inode_no)
    {
    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);
    }
}

int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
    printk(KERN_INFO "save inode info\n");
struct buffer_head *bh;
struct assoofs_inode_info *inode_pos;
struct assoofs_super_block_info *assoofs_sb;
bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); // sb lo recibe assoofs_fill_super como argumento
    inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);
    memcpy(inode_pos, inode_info, sizeof(*inode_pos));
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    return 0;
}
struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
printk(KERN_INFO "search inode info request\n");
struct buffer_head *bh;
struct assoofs_inode_info *inode_pos;
struct assoofs_super_block_info *assoofs_sb;
bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); // sb lo recibe assoofs_fill_super como argumento
assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;
uint64_t count = 0;
while (start->inode_no != search->inode_no && count < ((struct assoofs_super_block_info *)sb->s_fs_info)->inodes_count)
 {
    count++;
    start++;
 }

if (start->inode_no == search->inode_no)
return start;
    else
return NULL;
}
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
struct dentry *assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir , struct dentry *dentry, umode_t mode)
{
   printk(KERN_INFO "new directory request\n"); 
    struct buffer_head *bh;
struct super_block *sb;
    struct inode *inode;
sb = dir->i_sb;
inode = new_inode(sb);
inode->i_sb = sb;
struct timespec64 ts = current_time(inode);
inode_set_ctime(inode, ts.tv_sec, ts.tv_nsec);
inode_set_mtime(inode, ts.tv_sec, ts.tv_nsec);
inode_set_atime(inode, ts.tv_sec, ts.tv_nsec);

inode->i_op = &assoofs_inode_ops;
assoofs_sb_get_a_freeinode(sb, &inode->i_ino); // Obtenemos el numero de inodo
// vamos a coger la info del inodo (pero desde 0)
struct assoofs_inode_info *inode_info;
inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
inode_info->inode_no = inode->i_ino;
inode_info->dir_children_count = 0;
inode_info->mode =  S_IFDIR | mode; // El segundo mode me llega como argumento
inode_info->file_size = 0;
inode->i_private = inode_info;
// como son ficheros ahora hacemos lo siguiente
inode->i_fop=&assoofs_dir_operations;
// y bueno no olvidemos que es un inodo nuevo no root
inode_init_owner(&nop_mnt_idmap, inode, dir, inode_info->mode);
d_add(dentry, inode);
 //printk(KERN_INFO "vamo a pillar bloquecito bien bakano\n");
assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
// toca meterle el inodo bien de persistente
assoofs_add_inode_info(sb, inode_info);
//printk(KERN_INFO "vamo a modificacionar el directorio padre\n");
struct assoofs_inode_info *parent_inode_info;
struct assoofs_dir_record_entry *dir_contents;
parent_inode_info = dir->i_private;
bh = sb_bread(sb, parent_inode_info->data_block_number);
dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
dir_contents += parent_inode_info->dir_children_count;
dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.
strcpy(dir_contents->filename, dentry->d_name.name);
mark_buffer_dirty(bh);
 sync_dirty_buffer(bh);
 brelse(bh);
 // actualizamos información del inodo pai
 
parent_inode_info->dir_children_count++;
assoofs_save_inode_info(sb, parent_inode_info);
    return 0;
}
static struct inode *assoofs_get_inode(struct super_block *sb, int ino)
{
    struct inode *inode;
    struct assoofs_inode_info *inode_info;

    printk(KERN_INFO "assoofs: get_inode request para inodo %d\n", ino);

    // 1. Obtenemos la info persistente
    inode_info = assoofs_get_inode_info(sb, ino);
    
    // SI FALLA LA LECTURA, NO CONTINUAMOS
    if (!inode_info) {
        printk(KERN_ERR "assoofs: ERROR - No se encontro info para el inodo %d\n", ino);
        return NULL; 
    }

    // 2. Creamos el inodo VFS
    inode = new_inode(sb);
    if (!inode) return NULL;

    // 3. Rellenamos datos básicos
    inode->i_ino = ino;
    inode->i_sb = sb;
    inode->i_private = inode_info;
    inode->i_mode = inode_info->mode; // Copiamos el modo del disco al inodo VFS

    // 4. Asignamos operaciones (Asegúrate de que estas estructuras existan en tu .c)
    inode->i_op = &assoofs_inode_ops;
    
    if (S_ISDIR(inode->i_mode)) {
        inode->i_fop = &assoofs_dir_operations;
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_fop = &assoofs_file_operations;
    }

    // 5. Tiempos (Usando la sintaxis moderna)
    struct timespec64 ts = current_time(inode);
    inode_set_atime_to_ts(inode, ts);
    inode_set_mtime_to_ts(inode, ts);
    inode_set_ctime_to_ts(inode, ts);

    return inode;
}



static int assoofs_iterate(struct file *filp, struct dir_context *ctx) 
{
    printk(KERN_INFO "Iterate request\n");
    struct assoofs_dir_record_entry *record;
struct super_block *sb;
struct inode *inode;
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
 for (i = 0; i < inode_info->dir_children_count; i++) 
 {
      if (ctx->pos >= inode_info->dir_children_count) 
        return 0;
    if(record->entry_removed == ASSOOFS_FALSE)
    {
    if (!dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN)) {
            break; 
        }
        ctx->pos++; // Solo incrementamos si el emit tuvo éxito
    }   
record++;
 }
 brelse(bh);
    return 0;
}

/*
 *  Funciones que realizan operaciones sobre inodos
 */
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
printk(KERN_INFO "Assoofs: Lookup request para '%s'\n", child_dentry->d_name.name);
struct assoofs_inode_info *parent_info = parent_inode->i_private;
struct super_block *sb = parent_inode->i_sb;
struct assoofs_inode_info *inode_info = NULL;
struct buffer_head *bh;
bh = sb_bread(sb, parent_info->data_block_number);
struct assoofs_dir_record_entry *record;
record = (struct assoofs_dir_record_entry *)bh->b_data;
int i;
 for (i=0; i < parent_info->dir_children_count; i++)
  {
    if (strncmp(record->filename, child_dentry->d_name.name, ASSOOFS_FILENAME_MAXLEN) == 0 && record->entry_removed == ASSOOFS_FALSE) 
    {
        printk(KERN_INFO"entro en inode %d vez\n", i);
    struct inode *inode = assoofs_get_inode(sb, record->inode_no); // Función auxiliar que obtine la informaciónde un inodo a partir de su número de inodo.
    inode_init_owner(&nop_mnt_idmap,inode, parent_inode, ((struct assoofs_inode_info *)inode->i_private)->mode);
    d_add(child_dentry, inode);
    return NULL;
    }
 record++;
    }
    brelse(bh);
    return NULL;
}


static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) 
{
    printk(KERN_INFO "New file request\n");
    struct buffer_head *bh;
struct super_block *sb;
    struct inode *inode;
sb = dir->i_sb;
inode = new_inode(sb);
inode->i_sb = sb;
struct timespec64 ts = current_time(inode);
inode_set_ctime(inode, ts.tv_sec, ts.tv_nsec);
inode_set_mtime(inode, ts.tv_sec, ts.tv_nsec);
inode_set_atime(inode, ts.tv_sec, ts.tv_nsec);

inode->i_op = &assoofs_inode_ops;
assoofs_sb_get_a_freeinode(sb, &inode->i_ino); // Obtenemos el numero de inodo
// vamos a coger la info del inodo (pero desde 0)
struct assoofs_inode_info *inode_info;
inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
inode_info->inode_no = inode->i_ino;
inode_info->mode =  mode; // El segundo mode me llega como argumento
inode_info->file_size = 0;
inode->i_private = inode_info;
inode->i_mode = S_IFDIR | 0755; 
// como son ficheros ahora hacemos lo siguiente
inode->i_fop=&assoofs_file_operations;
// y bueno no olvidemos que es un inodo nuevo no root
inode_init_owner(&nop_mnt_idmap, inode, dir, inode_info->mode);
d_add(dentry, inode);
 //printk(KERN_INFO "vamo a pillar bloquecito bien bakano\n");
assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
// toca meterle el inodo bien de persistente
assoofs_add_inode_info(sb, inode_info);
//printk(KERN_INFO "vamo a modificacionar el directorio padre\n");
struct assoofs_inode_info *parent_inode_info;
struct assoofs_dir_record_entry *dir_contents;
parent_inode_info = dir->i_private;
bh = sb_bread(sb, parent_inode_info->data_block_number);
dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
dir_contents += parent_inode_info->dir_children_count;
dir_contents->inode_no = inode_info->inode_no; // inode_info es la información persistente del inodo creado en el paso 2.
strcpy(dir_contents->filename, dentry->d_name.name);
mark_buffer_dirty(bh);
 sync_dirty_buffer(bh);
 brelse(bh);
 // actualizamos información del inodo pai
parent_inode_info->dir_children_count++;
assoofs_save_inode_info(sb, parent_inode_info);
assoofs_save_sb_info(sb); 
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
    struct buffer_head *bh;
    struct assoofs_inode_info *cur_inode;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    struct assoofs_inode_info *buffer = NULL;
    int i;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    if (!bh) return NULL;

    for (i = 0; i < afs_sb->inodes_count; i++) 
    {
        // Calculamos la dirección del inodo 'i' nada más empezar
        cur_inode = (struct assoofs_inode_info *)(bh->b_data + (i * sizeof(struct assoofs_inode_info)));

        if (cur_inode->inode_no == inode_no) 
        {
            buffer = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
            if (buffer) {
                memcpy(buffer, cur_inode, sizeof(struct assoofs_inode_info));
            }
            break;
        }
    }
    
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
   // printk(KERN_INFO "pedimos paso porfaplis de lectura de super bloque\n");
    //casteo
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    if (!bh) return -EIO;
    // recogemos el contenido del bloque residente en b_data y se lo metemos a assoofs_sb
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;
   // printk(KERN_INFO "en el superbloque hay assoofs: Inodes count: %llu inodos\n", assoofs_sb->inodes_count);
     brelse(bh);
    // pongo num mágico
    sb->s_magic = ASSOOFS_MAGIC;
    // meto el tamaño de bloque
    sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
    // guardo la información del bloque 0 a las operaciones 
    sb->s_op = &assoofs_sops;
    // ahí queda guardado
    sb->s_fs_info = assoofs_sb;
    // Crear inodo raíz
    root_inode = new_inode(sb);
    if (!root_inode) return -ENOMEM;
    // propietario es el i-nodo raiz en este caso
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR );
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
    root_inode->i_sb = sb;
    root_inode->i_op = &assoofs_inode_ops;
    root_inode->i_fop = &assoofs_dir_operations;
    //root_inode->i_mode = S_IFDIR | 0755; 
    struct timespec64 ts = current_time(root_inode); // fechas.
    inode_set_ctime(root_inode, ts.tv_sec, ts.tv_nsec);
    // Info privada
   // printk(KERN_INFO "pedimos paso porfaplis de lectura de super bloque\n");
    inode_set_ctime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_mtime(root_inode, ts.tv_sec, ts.tv_nsec);
    inode_set_atime(root_inode, ts.tv_sec, ts.tv_nsec);
    root_inode->i_private = assoofs_get_inode_info(sb, ASSOOFS_ROOTDIR_INODE_NUMBER); // Información persistente del
 //inodo
    // metemos el inodo raíz en el árbol
    
        //printk(KERN_INFO "el root es: %lu",root_inode->i_ino);
        sb->s_root = d_make_root(root_inode);
        if (!sb->s_root) {
    printk(KERN_ERR "FALLO CRITICO: d_make_root devolvió NULL\n");
    return -ENOMEM;
}
// Forzamos las operaciones otra vez por si acaso

  // printk(KERN_INFO "el root ahora es: %lu",root_inode->i_ino);
    // vamos a hacer como que no ha pasado nada y dejamos todo donde lo encontramos
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
    printk(KERN_INFO "FIUUU SALIERAMOS CIII :·3\n");
    ret = unregister_filesystem(&assoofs_type);
    // Control de errores a partir del valor de retorno
}

module_init(assoofs_init);
module_exit(assoofs_exit);
