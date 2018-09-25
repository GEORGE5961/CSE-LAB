#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  
  static uint32_t id = IBLOCK(INODE_NUM, BLOCK_NUM);
  char buf[BLOCK_SIZE];
  for(uint32_t i=0; i<BLOCK_NUM; i++){
      d->read_block(BBLOCK(id), buf);
      uint32_t pos = id % BLOCK_SIZE;
      uint32_t* bits = &((uint32_t*)buf)[pos/sizeof(uint32_t)];
      if((*bits & (1 << pos)) == 0){
          *bits |= (1 << pos);
          d->write_block(BBLOCK(id), buf);
          return id++;
      }
      else{
          id++;
          if(id >= BLOCK_NUM){
              id = id%BLOCK_NUM+IBLOCK(INODE_NUM, BLOCK_NUM);
          }
      }
  }
  return -1;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  blockid_t bblock = BBLOCK(id);
  char buf[BLOCK_SIZE];
  d->read_block(bblock, buf);
  uint32_t pos = id % BLOCK_SIZE;
  uint32_t* bits = &((uint32_t*)buf)[pos/sizeof(uint32_t)];
  *bits &= ~(1 << pos);
  d->write_block(bblock, buf);
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  static uint32_t inum = 0;
  inum++;
  char buf[BLOCK_SIZE];
  struct inode *ino;

  bm->read_block(IBLOCK(inum, BLOCK_NUM), buf);
  ino = (struct inode*)buf + inum%IPB;
  ino->type = type;
  ino->size = 0;
  ino->atime = ino->mtime = ino->ctime = time(NULL);
  bm->write_block(IBLOCK(inum, BLOCK_NUM), buf);
  return inum;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  struct inode *node = get_inode(inum);
  if (node != NULL)
  {
    if (node->type == 1 || node->type == 2)
    {
      node->type = 0;
      node->size = node->atime = node->ctime  = node->mtime = 0;
      put_inode(inum, node);
    }
  }
  else
  {
    printf("Free inode error\n");
  }
  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  struct inode * inode = get_inode(inum);

  if (inode == NULL){
      printf("Cannot find inode");
  }

  *size = inode->size;
  uint32_t nsize = inode->size;

  uint32_t nblock = (nsize + BLOCK_SIZE - 1) / BLOCK_SIZE;
  *buf_out = (char *)malloc(nblock * BLOCK_SIZE);

  if (NDIRECT >= nblock)
  {
    for (uint32_t i = 0; i < nblock; i++)
    {
      bm->read_block(inode->blocks[i], *buf_out + i * BLOCK_SIZE);
    }

    free(inode);
    return;
  }
  else
  {
    for (uint32_t i = 0; i < NDIRECT; i++)
    {
      bm->read_block(inode->blocks[i], *buf_out + i * BLOCK_SIZE);
    }

    blockid_t *buf_tmp = (blockid_t *)malloc(BLOCK_SIZE);
    bm->read_block(inode->blocks[NDIRECT], (char *)buf_tmp);
    for (uint32_t i = 0; i < nblock - NDIRECT; i++)
    {
      bm->read_block(buf_tmp[i], *buf_out + NDIRECT * BLOCK_SIZE + i * BLOCK_SIZE);
    }
    free(buf_tmp);
    free(inode);
    return;
  }
  
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  struct inode * inode = get_inode(inum);
  uint32_t old_num = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  uint32_t new_num = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
  inode->size = size;
  inode->mtime = time(NULL);

  if (inode == NULL){
      printf("Cannot find inode");
  }

  if(new_num <= old_num){
    // new_num <= old_num < NDIRECT
    if(old_num< NDIRECT){
      for (uint32_t i = 0; i < old_num - new_num; i++)
      {
        bm->free_block(inode->blocks[new_num + i]);
      }

      for (uint32_t i = 0; i < new_num; i++)
      {
        bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
      }

      inode->size = size;
      put_inode(inum, inode);
      free(inode);
      return;
    }   
    else{  
      if(new_num <= NDIRECT){
    
        blockid_t *buf_tmp = (blockid_t *)malloc(BLOCK_SIZE);
        bm->read_block(inode->blocks[NDIRECT], (char*)buf_tmp);
        for (uint32_t i = 0; i < old_num - NDIRECT; i++)
        {
          bm->free_block(buf_tmp[i]);
        }
        bm->free_block(inode->blocks[NDIRECT]); 
        
        for (uint32_t i = 0; i < NDIRECT - new_num; i++)
        {
          bm->free_block(inode->blocks[new_num + i]);
        }

        for (uint32_t i = 0; i < new_num; i++)
        {
          bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
        }
        put_inode(inum, inode);
        free(inode);
        return;
      }
      //  NDIRECT < new_num <= old_num
      else{

        blockid_t *buf_tmp = (blockid_t *)malloc(BLOCK_SIZE);
        bm->read_block(inode->blocks[NDIRECT], (char*)buf_tmp);
        for (uint32_t i = 0; i < old_num - new_num; i++)
        {
          bm->free_block(buf_tmp[new_num + i]);
        }

        for (uint32_t i = 0; i < NDIRECT; i++)
        {
          bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
        }

        for (uint32_t i = 0; i < new_num - NDIRECT; i++)
        {
          bm->write_block(buf_tmp[i], buf + (NDIRECT * BLOCK_SIZE) + i * BLOCK_SIZE);
        }

        bm->write_block(inode->blocks[NDIRECT], (char*) buf_tmp);
        put_inode(inum, inode);
        free(inode);
        return;
      }      
    }
  }
  // new_num > old_num
  else{
    // NDIRECT > new_num > old_num
    if (new_num <= NDIRECT){
      for (uint32_t i = 0; i < new_num - old_num; i++)
      {
        inode->blocks[old_num + i] = bm->alloc_block();
      }
      // write blocks
      for (uint32_t i = 0; i < new_num; i++)
      {
        bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
      }
      put_inode(inum, inode);
      free(inode);
      return;

    }
    // new_num > NDIRECT >= old_num
    else if(NDIRECT >= old_num){
      for (uint32_t i = 0; i < NDIRECT - old_num; i++)
      {
        inode->blocks[old_num + i] = bm->alloc_block();
      }
      blockid_t *buf_tmp = (blockid_t *)malloc(BLOCK_SIZE);
      inode->blocks[NDIRECT] = bm->alloc_block();
      for (uint32_t i = 0; i < new_num - NDIRECT; i++)
      {
        buf_tmp[i] = bm->alloc_block();
      }

      for (uint32_t i = 0; i < NDIRECT; i++)
        {
          bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
        }
      for (uint32_t i = 0; i < new_num - NDIRECT; i++)
      {
        bm->write_block(buf_tmp[i], buf + (NDIRECT * BLOCK_SIZE) + i * BLOCK_SIZE);
      }
      bm->write_block(inode->blocks[NDIRECT], (char *)buf_tmp);
      put_inode(inum, inode);
      delete(buf_tmp);
      free(inode);
      return;
      
    }
    // new_num >= old_num > NDIRECT
    else{
      blockid_t *buf_tmp = (blockid_t *)malloc(BLOCK_SIZE);
      for (uint32_t i = 0; i < new_num - old_num; i++)
      {
        buf_tmp[old_num + i] = bm->alloc_block();
      }

      // write direct blocks
      for (uint32_t i = 0; i < NDIRECT; i++)
      {
        bm->write_block(inode->blocks[i], buf + i * BLOCK_SIZE);
      }
      for (uint32_t i = 0; i < new_num - NDIRECT; i++)
      {
        bm->write_block(buf_tmp[i], buf + (NDIRECT * BLOCK_SIZE) + i * BLOCK_SIZE);
      }

      bm->write_block(inode->blocks[NDIRECT], (char *)buf_tmp);
      put_inode(inum, inode);
      free(buf_tmp);
      free(inode);
      return;
    }
  }
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  struct inode* node =  get_inode(inum);
  a.atime = node->atime;
  a.ctime = node->ctime;
  a.mtime = node->mtime;
  a.size = node->size;
  a.type = node->type;
  free(node);
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  
  return;
}
