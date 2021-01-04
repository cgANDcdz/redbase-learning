//
// File:        pf_buffermgr.h
// Description: PF_BufferMgr class interface
// Authors:     Hugo Rivero (rivero@cs.stanford.edu)
//              Dallan Quass (quass@cs.stanford.edu)
//              Jason McHugh (mchughj@cs.stanford.edu)
//
// 1997: When requesting a page from the buffer manager the page requested
// is now promoted to the MRU slot.
// 1998: Allow chunks from the buffer manager to not be associated with
// a particular file.  Allows students to use main memory chunks that
// are associated with (and limited by) the buffer.
//

#ifndef PF_BUFFERMGR_H
#define PF_BUFFERMGR_H

#include "pf_internal.h"
#include "pf_hashtable.h"

// INVALID_SLOT is used within the PF_BufferMgr class which tracks a list
// of PF_BufPageDesc.  Inside the PF_BufPageDesc are integer "pointers" to
// next and prev items.  INVALID_SLOT is used to indicate no previous or
// next.
#define INVALID_SLOT  (-1)

/*************************************************************************************
 *                                  缓冲区管理器声明
 * 1.缓冲区的组织方式:
 *     => redbase中,对缓冲区的管理,其实就是hash表+双向链表,与leetcode中那道LRU题目总体类似
 *        只是实现细节上有所区别
 * 2.文件系统中文件的组织方式:
 *      |PF_FileHdr | page0 | page1 | page2 | .....| pagen |
 * 3.之后需要考虑RM层的文件头,那么文件实际如下:
 *      |PF_FileHdr | RM_FileHdr(page0) | page1 | page2 | .....| pagen |
 *      即RM_FileHdr页号为0, 而PF_FileHdr没有页号
 * 4.理解使用hashtable的目的:
 *     => 能根据(fd,pageNum)快速找到page在缓冲区中的所有信息(通过slot查找PF_BufPageDesc项)
 * ***********************************************************************************/


//
// PF_BufPageDesc - struct containing data about a page in the buffer
// 表示一个缓冲区页的数据结构
struct PF_BufPageDesc {
    char       *pData;      // page contents=> 使用时动态分配内存,大小为4096,作为一个page在内存的缓冲区
    int        next;        // next in the linked list of buffer pages,使用bufTable下标表示
    int        prev;        // prev in the linked list of buffer pages,使用bufTable下标表示
    int        bDirty;      // TRUE if page is dirty
    short int  pinCount;    // pin count
    PageNum    pageNum;     // page number for this page
    int        fd;          // OS file descriptor of this page
};

//
// PF_BufferMgr - manage the page buffer
//
class PF_BufferMgr {
public:

    PF_BufferMgr     (int numPages);             // Constructor - allocate
                                                  // numPages buffer pages
    ~PF_BufferMgr    ();                         // Destructor

    // Read pageNum into buffer, point *ppBuffer to location
    RC  GetPage      (int fd, PageNum pageNum, char **ppBuffer,
                      int bMultiplePins = TRUE);
    // Allocate a new page in the buffer, point *ppBuffer to its location
    RC  AllocatePage (int fd, PageNum pageNum, char **ppBuffer);

    RC  MarkDirty    (int fd, PageNum pageNum);  // Mark page dirty
    RC  UnpinPage    (int fd, PageNum pageNum);  // Unpin page from the buffer
    RC  FlushPages   (int fd);                   // Flush pages for file

    // Force a page to the disk, but do not remove from the buffer pool
    RC ForcePages    (int fd, PageNum pageNum);


    // Remove all entries from the Buffer Manager.
    RC  ClearBuffer  ();
    // Display all entries in the buffer
    RC PrintBuffer   ();

    // Attempts to resize the buffer to the new size
    RC ResizeBuffer  (int iNewSize);

    // Three Methods for manipulating raw memory buffers.  These memory
    // locations are handled by the buffer manager, but are not
    // associated with a particular file.  These should be used if you
    // want memory that is bounded by the size of the buffer pool.

    // Return the size of the block that can be allocated.
    RC GetBlockSize  (int &length) const;

    // Allocate a memory chunk that lives in buffer manager
    RC AllocateBlock (char *&buffer);
    // Dispose of a memory chunk managed by the buffer manager.
    RC DisposeBlock  (char *buffer);

private:
    RC  InsertFree   (int slot);                 // Insert slot at head of free
    RC  LinkHead     (int slot);                 // Insert slot at head of used
    RC  Unlink       (int slot);                 // Unlink slot
    RC  InternalAlloc(int &slot);                // Get a slot to use

    // Read a page
    RC  ReadPage     (int fd, PageNum pageNum, char *dest);

    // Write a page
    RC  WritePage    (int fd, PageNum pageNum, char *source);

    // Init the page desc entry
    RC  InitPageDesc (int fd, PageNum pageNum, int slot);

    PF_BufPageDesc *bufTable;                     // info on buffer pages => 是数组,PF_BUFFER_SIZE个
    PF_HashTable   hashTable;                     // Hash table object
    int            numPages;                      // # of pages in the buffer
    int            pageSize;                      // Size of pages in the buffer => 通常4096
    int            first;                         // MRU page slot => first、last都是对应used链表
    int            last;                          // LRU page slot
    int            free;                          // head of free list => 空闲链表
};

#endif
