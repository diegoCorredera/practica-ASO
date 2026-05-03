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
int assoofs_sb_set_a_freeinode(struct super_block *sb, uint64_t inode_no);
int assoofs_sb_set_a_freeblock(struct super_block *sb, uint64_t block);
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

int assoofs_sb_get_a_freeinode(struct super_block *sb, unsigned long *inode_no)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;

    printk(KERN_INFO "assoofs: freeinode request\n");

    // Empezamos en 1 porque el 0 es inválido y el 1 suele ser la Raíz
    // Si tu Raíz es el 1, este bucle debería empezar en 2
    for (i = 1; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
    {
        // Comprobamos si el bit 'i' está a 0 (libre)
        if (!(assoofs_sb->free_inodes & (1ULL << i)))
            break;
    }

    if (i >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED) {
        printk(KERN_ERR "assoofs: No quedan inodos libres\n");
        return -ENOSPC;
    }

    *inode_no = i;
    printk(KERN_INFO "assoofs: Inodo %d asignado\n", i);

    // Marcamos como ocupado
    assoofs_sb->free_inodes |= (1ULL << i);

    // PERSISTENCIA: Guardamos el cambio en el superbloque (ahora que el memcpy funciona)
    assoofs_save_sb_info(sb);
    return 0;
}

// Operaciones sobre el superbloque
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

void assoofs_save_sb_info(struct super_block *vsb)
{
struct buffer_head *bh;
    struct assoofs_super_block_info *sb = vsb->s_fs_info;

    printk(KERN_INFO "assoofs: save sb info request\n");

    bh = sb_bread(vsb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
    if (!bh) return;

    // COPIAR los datos de la memoria al buffer del disco
    memcpy(bh->b_data, sb, sizeof(struct assoofs_super_block_info));

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh); // ¡Muy importante para no bloquear el sistema!
}

int assoofs_sb_get_a_freeblock(struct super_block *sb, uint64_t *block)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    int i;
    printk(KERN_INFO "get freeblock request\n");
    // Empezamos en 2 o el número de bloques reservados (SB=0, Inodes=1)
    for (i = 2; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++)
    {
        // Comprobamos si el bit 'i' es 0 (bloque libre)
        // Usamos 1ULL para asegurar que la máscara sea de 64 bits
        if (!(assoofs_sb->free_blocks & (1ULL << i)))
            break; 
    }
    if (i >= ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED) 
    {
        printk(KERN_ERR "assoofs: No quedan bloques libres\n");
        return -ENOSPC;
    }
    *block = i; 
    // Marcamos el bloque como ocupado (ponemos el bit a 1)
    assoofs_sb->free_blocks |= (1ULL << i);
    // Persistimos el cambio en el superbloque
    assoofs_save_sb_info(sb);
    return 0;
}

void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode)
{
    printk(KERN_INFO " add inode reqest");
    struct assoofs_inode_info *inode_info_dest;
    struct buffer_head *bh;
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    if (!bh) return;

    // 1. Apuntamos a la siguiente posición libre usando el contador actual
    inode_info_dest = (struct assoofs_inode_info *)bh->b_data;
    inode_info_dest += assoofs_sb->inodes_count;

    // 2. Copiamos los datos
    memcpy(inode_info_dest, inode, sizeof(struct assoofs_inode_info));

    // 3. Persistimos el bloque de inodos
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh); // ¡No olvides el brelse!

    // 4. Actualizamos y persistimos SIEMPRE el superbloque
    assoofs_sb->inodes_count++;
    assoofs_save_sb_info(sb);
}
int assoofs_save_inode_info(struct super_block *sb, struct assoofs_inode_info *inode_info)
{
    printk(KERN_INFO "save inode info\n");
struct buffer_head *bh;
struct assoofs_inode_info *inode_pos;
bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER); // sb lo recibe assoofs_fill_super como argumento
inode_pos = assoofs_search_inode_info(sb, (struct assoofs_inode_info *)bh->b_data, inode_info);
    if (inode_pos) {
        // 3. COPIAMOS LOS DATOS REALMENTE AL BUFFER
        memcpy(inode_pos, inode_info, sizeof(*inode_pos));
        
        // 4. MARCAMOS COMO SUCIO Y SINCRONIZAMOS
        mark_buffer_dirty(bh);
        sync_dirty_buffer(bh);
    }
    brelse(bh);
    return 0;
}


struct assoofs_inode_info *assoofs_search_inode_info(struct super_block *sb, struct assoofs_inode_info *start, struct assoofs_inode_info *search)
{
printk(KERN_INFO "search inode info request\n");
uint64_t count = 0;
    struct assoofs_super_block_info *afs_sb = sb->s_fs_info;
    // Iteramos por la tabla de inodos usando el contador del superbloque
    while (count < afs_sb->inodes_count) {
        if (start->inode_no == search->inode_no) {
            return start; // Encontrado
        }
        count++;
        start++; // Avanzamos al siguiente inodo en el bloque
    }

    // Si no lo encuentra, es un inodo nuevo: devolvemos la posición donde debería ir
    return start; 
}
/*
 *  Funciones que realizan operaciones sobre ficheros
 */
int assoofs_sb_set_a_freeinode(struct super_block *sb, uint64_t inode_no)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;   
assoofs_sb->free_inodes &= ~(1 << inode_no);
 assoofs_save_sb_info(sb);
 return 0;
}
int assoofs_sb_set_a_freeblock(struct super_block *sb, uint64_t block)
{
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;

    assoofs_sb->free_blocks &= ~(1 << block);
    assoofs_save_sb_info(sb);
 return 0;
}
ssize_t assoofs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos) {
    printk(KERN_INFO "Read request\n");
    struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
    if (*ppos >= inode_info->file_size) return 0;
    struct buffer_head *bh;
char *buffer;
bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
buffer = (char *)bh->b_data;
int nbytes;
buffer+=*ppos; // Incrementamos el buffer para que lea a partir de donde se quedo
nbytes = min((size_t) inode_info->file_size - (size_t) *ppos, len);
// Hay que comparar len con el tama~
copy_to_user(buf, buffer, nbytes);
*ppos += nbytes;
return nbytes;
}

ssize_t assoofs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos) 
{
printk(KERN_INFO "Write request\n");
 struct buffer_head *bh;
struct assoofs_inode_info *inode_info = filp->f_path.dentry->d_inode->i_private;
if (*ppos + len>= ASSOOFS_DEFAULT_BLOCK_SIZE)
{
    printk(KERN_ERR "No hay suficiente espacio en el disco para escribir.\n");
    return -ENOSPC;
}
char *buffer;
bh = sb_bread(filp->f_path.dentry->d_inode->i_sb, inode_info->data_block_number);
buffer = (char *)bh->b_data;
int nbytes;
buffer = (char *)bh->b_data;
buffer += *ppos;
nbytes = min((size_t) inode_info->file_size - (size_t) *ppos, len);
copy_from_user(buffer, buf, len);
*ppos+=len;
mark_buffer_dirty(bh);
sync_dirty_buffer(bh);
struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
inode_info->file_size = *ppos;
assoofs_save_inode_info(sb, inode_info);
return len;
}

/*
 *  Funciones que realizan operaciones sobre directorios
 */
struct dentry *assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) 
{
    struct buffer_head *bh;
    struct super_block *sb = dir->i_sb;
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct assoofs_inode_info *parent_inode_info;
    struct assoofs_dir_record_entry *dir_contents;

    printk(KERN_INFO "assoofs: New directory request\n"); 

    // 1. Crear inodo VFS
    inode = new_inode(sb);
    if (!inode) return ERR_PTR(-ENOMEM);

    inode->i_sb = sb;
    inode->i_op = &assoofs_inode_ops;
    inode->i_fop = &assoofs_dir_operations; // Operaciones de directorio

    // 2. Obtener número de inodo y bloque libres
    assoofs_sb_get_a_freeinode(sb, &inode->i_ino);

    // 3. Info privada
    inode_info = kmalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = S_IFDIR | mode;
    inode_info->dir_children_count = 0;
    inode->i_private = inode_info;

    inode_init_owner(&nop_mnt_idmap, inode, dir, inode_info->mode);
    d_add(dentry, inode);

    assoofs_sb_get_a_freeblock(sb, &inode_info->data_block_number);
    assoofs_add_inode_info(sb, inode_info);

    // 4. Actualizar directorio padre
    parent_inode_info = dir->i_private;
    bh = sb_bread(sb, parent_inode_info->data_block_number);
    
    dir_contents = (struct assoofs_dir_record_entry *)bh->b_data;
    dir_contents += parent_inode_info->dir_children_count;

    dir_contents->inode_no = inode_info->inode_no;
    strcpy(dir_contents->filename, dentry->d_name.name);
    dir_contents->entry_removed = ASSOOFS_FALSE; // <--- ¡Añade esto!

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    // 5. Persistir cambios en el padre
    parent_inode_info->dir_children_count++;
    assoofs_save_inode_info(sb, parent_inode_info);

    return NULL; // Éxito en funciones que devuelven dentry
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
if ((!S_ISDIR(inode_info->mode))) return -1;
struct buffer_head *bh;
int i;
bh = sb_bread(sb, inode_info->data_block_number);
 if (!bh) return -EIO;
 record = (struct assoofs_dir_record_entry *)bh->b_data;
 for (i = ctx->pos; i < inode_info->dir_children_count; i++) 
 {
    if (record->entry_removed == ASSOOFS_FALSE)
    {
        if (!dir_emit(ctx, record->filename, ASSOOFS_FILENAME_MAXLEN, record->inode_no, DT_UNKNOWN)) {
            break; 
        }
        ctx->pos = i + 1;
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
inode_info->mode = S_IFREG | mode; // El segundo mode me llega como argumento
inode_info->file_size = 0;
inode->i_private = inode_info;
inode->i_mode = inode_info->mode;
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
dir_contents->entry_removed = ASSOOFS_FALSE;
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
     struct buffer_head *bh;
    struct super_block *sb = dir->i_sb;
struct inode *inode_remove = dentry->d_inode;
 struct assoofs_inode_info *inode_info_remove = inode_remove->i_private;
 struct assoofs_inode_info *parent_inode_info = dir->i_private;
 struct assoofs_dir_record_entry *dir_contents;
 bh = sb_bread(sb, parent_inode_info->data_block_number);
dir_contents = (struct assoofs_dir_record_entry*)bh->b_data;
int i=0;
 for(i = 0; i < parent_inode_info->dir_children_count; i++)
 {
if (!strcmp(dir_contents->filename, dentry->d_name.name) && dir_contents->inode_no == inode_remove->i_ino){

printk(KERN_INFO "Found dir_record_entry to remove: %s\n", dir_contents->filename);

dir_contents->entry_removed = ASSOOFS_TRUE;
assoofs_sb_set_a_freeinode(sb, inode_info_remove->inode_no);
assoofs_sb_set_a_freeblock(sb, inode_info_remove->data_block_number);
break;

}
dir_contents++;
 }
 mark_buffer_dirty(bh);
 sync_dirty_buffer(bh);
brelse(bh);
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
} __attribute__((packed));

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
    // recogemos el contenido del bloque residente en b_data y hacemos una copia segura
    assoofs_sb = kmalloc(sizeof(struct assoofs_super_block_info), GFP_KERNEL);
    if (!assoofs_sb) {
        brelse(bh);
        return -ENOMEM;
    }
    memcpy(assoofs_sb, bh->b_data, sizeof(struct assoofs_super_block_info));
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
