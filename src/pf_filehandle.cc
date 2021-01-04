//
// File:        pf_filehandle.cc
// Description: PF_FileHandle class implementation
// Authors:     Hugo Rivero (rivero@cs.stanford.edu)
//              Dallan Quass (quass@cs.stanford.edu)
//

#include <unistd.h>
#include <sys/types.h>
#include "pf_internal.h"
#include "pf_buffermgr.h"

/**************************************************************************************
 *                               文件处理器
 * 作用:与一个打开文件相关联(见PF_Manager::OpenFile),从而处理该文件中的pages(通过PF_PageHandle)
 * 
 * 1.同PF_Manager一样,也有指向PF_BufferMgr的指针;
 * 2.PF_Manager是本类的友元类 => 从而PF_Manager能访问/修改PF_FileHandle的任意成员;
 * 3.重点关注函数GetThisPage、AllocatePage、DisposePage、ForcePages、FlushPages
 * 4.对页号的理解:
 *    文件格式:|PF_FileHdr| page0 | page1 | page2 | page3 | ... | pagen| 
 *    页号范围:[0,numPages-1]; 
 *    但是GetNextPage()可输入current为-1,因为它的下一页就是page0
 *    同理,GetPrevPage()可输入current为numPages,因为它的前一页就是pagen-1
 * 5.理解如何给文件分配新的页的 => 吃透AllocatePage()
 * 6.注意:PF_FileHdr没有算入文件的page,也不会缓存到redbase的缓冲区,所以对其有修改的话,
 *   需要手动写入文件,而不是通过缓冲区flushpages()
 * 7.FlushPages()、ForcePages(fd,pgNum)的区别:
 *    a.前者是将文件的所有页写回磁盘,且会释放缓冲区
 *    b.后者是将文件中指定的页写回磁盘,且不用释放缓冲区
 * ************************************************************************************/





//
// PF_FileHandle
//
// Desc: Default constructor for a file handle object
//       A File object provides access to an open file.
//       It is used to allocate, dispose and fetch pages.
//       It is constructed here but must be passed to PF_Manager::OpenFile() in
//       order to be used to access the pages of a file.
//       It should be passed to PF_Manager::CloseFile() to close the file.
//       A file handle object contains a pointer to the file data stored
//       in the file table managed by PF_Manager.  It passes the file's unix
//       file descriptor to the buffer manager to access pages of the file.
//
PF_FileHandle::PF_FileHandle()
{
   // Initialize local variables
   bFileOpen = FALSE;
   pBufferMgr = NULL;
}

//
// ~PF_FileHandle
//
// Desc: Destroy the file handle object
//       If the file handle object refers to an open file, the file will
//       NOT be closed.
//
PF_FileHandle::~PF_FileHandle()
{
   // Don't need to do anything
}

//
// PF_FileHandle
//
// Desc: copy constructor
// In:   fileHandle - file handle object from which to construct this object
//
PF_FileHandle::PF_FileHandle(const PF_FileHandle &fileHandle)
{
   // Just copy the data members since there is no memory allocation involved
   this->pBufferMgr  = fileHandle.pBufferMgr;
   this->hdr         = fileHandle.hdr;
   this->bFileOpen   = fileHandle.bFileOpen;
   this->bHdrChanged = fileHandle.bHdrChanged;
   this->unixfd      = fileHandle.unixfd;
}

//
// operator=
//
// Desc: overload = operator
//       If this file handle object refers to an open file, the file will
//       NOT be closed.
// In:   fileHandle - file handle object to set this object equal to
// Ret:  reference to *this
//
PF_FileHandle& PF_FileHandle::operator= (const PF_FileHandle &fileHandle)
{
   // Test for self-assignment
   if (this != &fileHandle) {

      // Just copy the members since there is no memory allocation involved
      this->pBufferMgr  = fileHandle.pBufferMgr;
      this->hdr         = fileHandle.hdr;
      this->bFileOpen   = fileHandle.bFileOpen;
      this->bHdrChanged = fileHandle.bHdrChanged;
      this->unixfd      = fileHandle.unixfd;
   }

   // Return a reference to this
   return (*this);
}

//
// GetFirstPage
//
// Desc: Get the first page in a file
//       The file handle must refer to an open file
// Out:  pageHandle - becomes a handle to the first page of the file
//       The referenced page is pinned in the buffer pool.
// Ret:  PF return code
//
// 获取当前文件的第一个page,并将一个PF_PageHandle对象与之绑定
// 会把page自动pin到内存 => 之后需要手动unpin
RC PF_FileHandle::GetFirstPage(PF_PageHandle &pageHandle) const
{
   return (GetNextPage((PageNum)-1, pageHandle));
}

//
// GetLastPage
//
// Desc: Get the last page in a file
//       The file handle must refer to an open file
// Out:  pageHandle - becomes a handle to the last page of the file
//       The referenced page is pinned in the buffer pool.
// Ret:  PF return code
//
// 获取当前文件的最后一个page,并将一个PF_PageHandle对象与之绑定
// 会把page自动pin到内存 => 之后需要手动unpin
RC PF_FileHandle::GetLastPage(PF_PageHandle &pageHandle) const
{
   return (GetPrevPage((PageNum)hdr.numPages, pageHandle));
}

//
// GetNextPage
//
// Desc: Get the next (valid) page after current
//       The file handle must refer to an open file
// In:   current - get the next valid page after this page number
//       current can refer to a page that has been disposed
// Out:  pageHandle - becomes a handle to the next page of the file
//       The referenced page is pinned in the buffer pool.
// Ret:  PF_EOF, or another PF return code
//
// 获取页号current的下一个page,并将一个PF_PageHandle对象与之绑定
// 会把page自动pin到内存 => 之后需要手动unpin
RC PF_FileHandle::GetNextPage(PageNum current, PF_PageHandle &pageHandle) const
{
   int rc;               // return code

   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number (note that -1 is acceptable here)
   if (current != -1 &&  !IsValidPageNum(current))
      return (PF_INVALIDPAGE);

   // Scan the file until a valid used page is found
   for (current++; current < hdr.numPages; current++) {

      // If this is a valid (used) page, we're done
      if (!(rc = GetThisPage(current, pageHandle)))
         return (0);

      // If unexpected error, return it
      if (rc != PF_INVALIDPAGE)
         return (rc);
   }

   // No valid (used) page found
   return (PF_EOF);
}

//
// GetPrevPage
//
// Desc: Get the prev (valid) page after current
//       The file handle must refer to an open file
// In:   current - get the prev valid page before this page number
//       current can refer to a page that has been disposed
// Out:  pageHandle - becomes a handle to the prev page of the file
//       The referenced page is pinned in the buffer pool.
// Ret:  PF_EOF, or another PF return code
//
// 获取页号current的前一个page,并将一个PF_PageHandle对象与之绑定
// 会把page自动pin到内存 => 之后需要手动unpin
RC PF_FileHandle::GetPrevPage(PageNum current, PF_PageHandle &pageHandle) const
{
   int rc;               // return code

   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number (note that hdr.numPages is acceptable here)
   if (current != hdr.numPages &&  !IsValidPageNum(current))
      return (PF_INVALIDPAGE);

   // Scan the file until a valid used page is found
   for (current--; current >= 0; current--) {

      // If this is a valid (used) page, we're done
      if (!(rc = GetThisPage(current, pageHandle)))
         return (0);

      // If unexpected error, return it
      if (rc != PF_INVALIDPAGE)
         return (rc);
   }

   // No valid (used) page found
   return (PF_EOF);
}

//
// GetThisPage
//
// Desc: Get a specific page in a file
//       The file handle must refer to an open file
// In:   pageNum - the number of the page to get
// Out:  pageHandle - becomes a handle to the this page of the file
//                    this function modifies local var's in pageHandle
//       The referenced page is pinned in the buffer pool.
// Ret:  PF return code
//
// 读取指定的页号的page到内存缓冲区(调用pBufferMgr->GetPage(fd,pgNum,pBuf)); 
// 同时将此page与一个PF_PageHandle对象绑定 
// 会自动pin到内存中,需要手动unpin
RC PF_FileHandle::GetThisPage(PageNum pageNum, PF_PageHandle &pageHandle) const
{
   int  rc;               // return code
   char *pPageBuf;        // address of page in buffer pool

   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number
   if (!IsValidPageNum(pageNum))
      return (PF_INVALIDPAGE);

   // Get this page from the buffer manager(GetPage())
   // => 1.如果本来在缓冲区中,则增加pinCount
   //    2.如果不在缓冲区,则读取并pin到缓冲区
   //    3.如果缓冲区已满,需要置换
   if ((rc = pBufferMgr->GetPage(unixfd, pageNum, &pPageBuf)))
      return (rc);

   // If the page is valid, then set pageHandle to this page and return ok
   // PF_PAGE_USED表示当前页可用(只要填充了一点数据都行)
   if (((PF_PageHdr*)pPageBuf)->nextFree == PF_PAGE_USED) {

      // Set the pageHandle local variables
      pageHandle.pageNum = pageNum;
      pageHandle.pPageData = pPageBuf + sizeof(PF_PageHdr);

      // Return ok
      return (0);
   }

   // If the page is *not* a valid one, then unpin the page
   if ((rc = UnpinPage(pageNum)))
      return (rc);

   return (PF_INVALIDPAGE);
}

//
// AllocatePage
//
// Desc: Allocate a new page in the file (may get a page which was
//       previously disposed)
//       The file handle must refer to an open file
// Out:  pageHandle - becomes a handle to the newly-allocated page
//                    this function modifies local var's in pageHandle
// Ret:  PF return code
//
// 给当前文件分配一个page
// 如果文件中有旧的空闲页,则返回旧页,否则在磁盘上分配一个新的页(而不仅是缓冲区中!); 
// 同时将新的page与pageHandle绑定;
// 会将新的页pin内存缓冲区,之后需要手动unpin
RC PF_FileHandle::AllocatePage(PF_PageHandle &pageHandle)
{
   int     rc;               // return code
   int     pageNum;          // new-page number
   char    *pPageBuf;        // address of page in buffer pool

   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // If the free list isn't empty... => 1.文件中尚有空闲页
   if (hdr.firstFree != PF_PAGE_LIST_END) {
      pageNum = hdr.firstFree;

      // Get the first free page into the buffer
      if ((rc = pBufferMgr->GetPage(unixfd,pageNum,&pPageBuf)))
         return (rc);

      // Set the first free page to the next page on the free list
      hdr.firstFree = ((PF_PageHdr*)pPageBuf)->nextFree;
   }
   else {                          // => 2.文件没有空闲页

      // The free list is empty...
      pageNum = hdr.numPages;           // 取最大编号,它是新分配页的编号

      // Allocate a new page in the file
      if ((rc = pBufferMgr->AllocatePage(unixfd,pageNum,&pPageBuf)))
         return (rc);

      // Increment the number of pages for this file
      hdr.numPages++;
   }

   // Mark the header as changed
   bHdrChanged = TRUE;

   // Mark this page as used => 这个空闲页被使用了!
   ((PF_PageHdr *)pPageBuf)->nextFree = PF_PAGE_USED;  

   // Zero out the page data
   memset(pPageBuf + sizeof(PF_PageHdr), 0, PF_PAGE_SIZE);

   // Mark the page dirty because we changed the next pointer
   if ((rc = MarkDirty(pageNum)))
      return (rc);

   // Set the pageHandle local variables
   pageHandle.pageNum = pageNum;
   pageHandle.pPageData = pPageBuf + sizeof(PF_PageHdr);

   // Return ok
   return (0);
}

//
// DisposePage
//
// Desc: Dispose of a page
//       The file handle must refer to an open file
//       PF_PageHandle objects referring to this page should not be used
//       after making this call.
// In:   pageNum - number of page to dispose
// Ret:  PF return code
//
// 释放当前文件中pageNum对应的page(磁盘、缓冲区都要释放!); 
// 必须先向将其从缓冲区中unpin,然后才能释放;
// 释放后将其加入文件的空闲链表;
//  为什么需要释放page? => 应该是从数据库删除数据的情况
RC PF_FileHandle::DisposePage(PageNum pageNum)
{
   int     rc;               // return code
   char    *pPageBuf;        // address of page in buffer pool

   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number
   if (!IsValidPageNum(pageNum))
      return (PF_INVALIDPAGE);

   // Get the page (but don't re-pin it if it's already pinned)
   if ((rc = pBufferMgr->GetPage(unixfd,pageNum,&pPageBuf,FALSE)))
      return (rc);

   // Page must be valid (used)
   if (((PF_PageHdr *)pPageBuf)->nextFree != PF_PAGE_USED) { // 如果这是空闲页

      // Unpin the page
      if ((rc = UnpinPage(pageNum)))
         return (rc);

      // Return page already free
      return (PF_PAGEFREE);
   }

   // Put this page onto the free list
   ((PF_PageHdr *)pPageBuf)->nextFree = hdr.firstFree;
   hdr.firstFree = pageNum;
   bHdrChanged = TRUE;

   // Mark the page dirty because we changed the next pointer
   if ((rc = MarkDirty(pageNum)))
      return (rc);

   // Unpin the page
   if ((rc = UnpinPage(pageNum)))
      return (rc);

   // Return ok
   return (0);
}

//
// MarkDirty
//
// Desc: Mark a page as being dirty
//       The page will then be written back to disk when it is removed from
//       the page buffer
//       The file handle must refer to an open file
// In:   pageNum - number of page to mark dirty
// Ret:  PF return code
// 将其标记为脏
RC PF_FileHandle::MarkDirty(PageNum pageNum) const
{
   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number
   if (!IsValidPageNum(pageNum))
      return (PF_INVALIDPAGE);

   // Tell the buffer manager to mark the page dirty
   return (pBufferMgr->MarkDirty(unixfd, pageNum));
}

//
// UnpinPage
//
// Desc: Unpin a page from the buffer manager.
//       The page is then free to be written back to disk when necessary.
//       PF_PageHandle objects referring to this page should not be used
//       after making this call.
//       The file handle must refer to an open file.
// In:   pageNum - number of the page to unpin
// Ret:  PF return code
//
// 表明此page不再需要缓存在缓冲区中 => 或直接覆盖、或先写回磁盘 
RC PF_FileHandle::UnpinPage(PageNum pageNum) const
{
   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // Validate page number
   if (!IsValidPageNum(pageNum))
      return (PF_INVALIDPAGE);

   // Tell the buffer manager to unpin the page
   return (pBufferMgr->UnpinPage(unixfd, pageNum));
}

//
// FlushPages
//
// Desc: Flush all dirty unpinned pages from the buffer manager for this file
// In:   Nothing
// Ret:  PF_PAGEFIXED warning from buffer manager if pages are pinned or
//       other PF error
//
// 释放文件的所有缓冲区页、以及将文件头写入磁盘文件 => 更恰当地说是将文件的数据刷新到磁盘
// 由于文件头PF_FileHdr不算在page里面,它不会缓存在redbase的缓冲区中
// 所以需要手动将其写入磁盘!!!
RC PF_FileHandle::FlushPages() const
{
   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // If the file header has changed, write it back to the file
   if (bHdrChanged) {

      // First seek to the appropriate place
      if (lseek(unixfd, 0, L_SET) < 0)
         return (PF_UNIX);

      // Write header
      int numBytes = write(unixfd,(char *)&hdr,sizeof(PF_FileHdr));
      if (numBytes < 0)
         return (PF_UNIX);
      if (numBytes != sizeof(PF_FileHdr))
         return (PF_HDRWRITE);

      // This function is declared const, but we need to change the
      // bHdrChanged variable.  Cast away the constness
      PF_FileHandle *dummy = (PF_FileHandle *)this;
      dummy->bHdrChanged = FALSE;
   }

   // Tell Buffer Manager to flush pages
   return (pBufferMgr->FlushPages(unixfd));
}

//
// ForcePages
//
// Desc: If a page is dirty then force the page from the buffer pool
//       onto disk.  The page will not be forced out of the buffer pool.
// In:   The page number, a default value of ALL_PAGES will be used if
//       the client doesn't provide a value.  This will force all pages.
// Ret:  Standard PF errors
//
//
// 将缓冲区中的page(如果脏的话)写回磁盘,然后取消脏位标志;
// 注意同样需要手动写回文件头; 
// 默认当前页所有page 
RC PF_FileHandle::ForcePages(PageNum pageNum) const
{
   // File must be open
   if (!bFileOpen)
      return (PF_CLOSEDFILE);

   // If the file header has changed, write it back to the file
   if (bHdrChanged) {    /* 说明是脏数据 */

      // First seek to the appropriate place
      if (lseek(unixfd, 0, L_SET) < 0)    /*SEEK_SET表示将文件偏移量设置为__offset(此处为0)*/
         return (PF_UNIX);

      // Write header
      int numBytes = write(unixfd,(char *)&hdr,sizeof(PF_FileHdr));
      if (numBytes < 0)
         return (PF_UNIX);
      if (numBytes != sizeof(PF_FileHdr))
         return (PF_HDRWRITE);

      // This function is declared const, but we need to change the
      // bHdrChanged variable.  Cast away the constness
      PF_FileHandle *dummy = (PF_FileHandle *)this;
      dummy->bHdrChanged = FALSE;
   }

   // Tell Buffer Manager to Force the page
   return (pBufferMgr->ForcePages(unixfd, pageNum));
}


//
// IsValidPageNum
//
// Desc: Internal.  Return TRUE if pageNum is a valid page number
//       in the file, FALSE otherwise
// In:   pageNum - page number to test
// Ret:  TRUE or FALSE
//
int PF_FileHandle::IsValidPageNum(PageNum pageNum) const
{
   return (bFileOpen &&
         pageNum >= 0 &&
         pageNum < hdr.numPages);
}

