//
// rm.h
//
//   Record Manager component interface
//
// This file does not include the interface for the RID class.  This is
// found in rm_rid.h
//

#ifndef RM_H
#define RM_H

// Please DO NOT include any files other than redbase.h and pf.h in this
// file.  When you submit your code, the test program will be compiled
// with your rm.h and your redbase.h, along with the standard pf.h that
// was given to you.  Your rm.h, your redbase.h, and the standard pf.h
// should therefore be self-contained (i.e., should not depend upon
// declarations in any other file).

// Do not change the following includes
#include "redbase.h"
#include "rm_rid.h"
#include "pf.h"

/***************************************************************************************************
 *                                   记录管理(RM)的接口
 * 
 * 1.一个页的结构(RM层只有4092字节):
 *      |PF_PageHdr| RM_PageHdr |bitmap | slots ......|
 * 
 * 2.RM_FileHdr独占文件RM层的第一个page(pagenum应该是1,前面还有PF_FileHdr)
 *   但RM_FileHdr中的numPages表示数据页的个数,不包括RM_FileHdr
 * 3.pageNum、slotNum都采用从0开始计
 * **************************************************************************************************/

#define RM_PAGE_LIST_END -1         /* 链表结束=>即文件中分配的page已用完 */
#define RM_SLOT_ALL_USED  -2        /* (page中)已没有空闲slot */



/*******************************************************************
 *                  文件头:文件的第一个page
 * 1.一个关系的记录通常用一个文件存储,文件头指明记录大小
 * 2.文件中的空闲块(page)用空闲链表组织,firstFreePage指向第一个空闲块
 * 3.实际上,文件最前面还有PF_FileHdr;RM_FileHdr只是RM层面的文件头
 * 4.空闲链表的组织方式:RM_FileHdr类似于头结点,firstFreePage指向第一个
 *      数据节点,使用空闲链表时从头开始选取节点;若需要插入节点,头插法!
 * *****************************************************************/
struct RM_FileHdr{
    int firstFreePage=RM_PAGE_LIST_END;   /*空页组成的空闲链表*/
    int numPages=0;                       /*文件共多少数据页(无论空闲与否,注意是RM层!!且不计算RM_FileHdr)*/
    int recordSize=-1;                    /*记录大小*/
};


/****************************************************************************
 *                 页头:页中的第一个数据区域
 * 注:实际上,页的最前面还有一个PF_PageHdr(pf_internal.h);RM_PageHdr只是RM层面的头！
 *    所以,一个page,真正供RM层使用的只有PF_PAGE_SIZE(4092)字节
 * **************************************************************************/
struct RM_PageHdr{
    int numSlots=0;                          /*slot总数(已用的和未用的)*/
    int numFreeSlots=0;                      /*未用的slot,增加这个标志位,不用遍历位图即可知道是否已满/空*/
    //char *bitMap=NULL;                     /*位图,标志后面每一个slot是否占用*/  //不需要这个指针也行
    int nextFreePage=RM_PAGE_LIST_END;       /*组成空闲链表,这里给出下一个空闲页的编号*/

};

//
// RM_Record: RM Record interface
//
class RM_Record {
public:
    RM_Record ();
    ~RM_Record();

    /*设置rec信息,对于数据库中的数据,需要是拷贝而不是引用 => 析构时记得释放内存!!!*/
    RC SetMembers(char* pRecordData,RID recordId,int recordSize);

    // Return the data corresponding to the record.  Sets *pData to the
    // record contents.
    RC GetData(char *&pData) const;

    // Return the RID associated with the record
    RC GetRid (RID &rid) const;

    /* 获取记录大小(字节数)*/
    RC GetRecSize(int& recordSize) const;

private:
    char* pRecData;
    RID rid;            /*rid 包括pageNum 和 slotNum*/
    int recSize;
    bool isValid;       /* 当前记录是否可用(填充了有效数据) */
};

//
// RM_FileHandle: RM File interface => 对应PF层的一个文件,在RM层负责处理文件中的各记录!
//
class RM_FileHandle {
public:
    RM_FileHandle ();
    ~RM_FileHandle();

    // Given a RID, return the record
    RC GetRec     (const RID &rid, RM_Record &rec) const;

    RC InsertRec  (const char *pData, RID &rid);       // Insert a new record

    RC DeleteRec  (const RID &rid);                    // Delete a record
    RC UpdateRec  (const RM_Record &rec);              // Update a record

    // Forces a page (along with any contents stored in this class)
    // from the buffer pool to disk.  Default value forces all pages.
    RC ForcePages (PageNum pageNum = ALL_PAGES);


/************ 自定义的辅助函数,向外提供接口 ***********/
public:
    /*获取成员变量pfFileHandle(指针)*/
    RC GetPpfFileHandle(PF_FileHandle*& pfFileHandle);

    /*获取当前文件中,在RM层存储的*/
    RC GetRmNumPages(int& numPages);

    /*自定义,传入PF_FileHandle,从而将这个RM_FileHandle绑定到对应的文件上*/
    RC Open(PF_FileHandle& pfFileHandle);

    /*判断文件是打开*/
    bool IsOpen();

    /*判断文件头是否改变*/
    bool IsHdrChanged();  

    /*获取RM_FileHandle中的pfFileHandle指针*/
    RC GetPfFileHandle(PF_FileHandle*& pfFileHandle);  
    
    /* 获取RM_FileHandle中的成员变量rmFileHdr*/
    RC GetRmFileHdr(RM_FileHdr& rmFileHdr);


/************* 自定义的一些辅助函数 *******************/
//private:
    /* 一个RM页内的数据可存储记录个数(slots) */
    RC GetPageSlots(int& numSlots) const;

    /* 获取指定页(pageHandle)中某个slotNum对应的数据指针*/
    RC GetSlotData(PF_PageHandle pageHandle,SlotNum slotNum,char *&pRecData) const;

    /*获取一个没有装满的page;如果没有,则需要给文件分配一个新的page*/
    RC GetOneFreePage(PageNum& pageNum);

    /*对于一个给定的页(pageHandle),在其中找一个空闲slot,返回slotNum*/
    RC GetOneFreeSlot(PF_PageHandle pageHandle, SlotNum& slotNum);


/********** 由于RM_FileHandle主要管理文件内的记录,而记录使用了位图=> 需要自定义位图相关操作 **********/
//private:    
    /* 槽数为slots位图占用多少字节(向上取整)*/
    int GetBMapBytes(int slots) const;
    
    /*对于指定的页(pPageData),检查其位图中slotNum对应的槽是否已占用*/
    bool IsSlotUsed(char* pPageData,int slotNum);

    /*对于指定的页(pPageData),将其第slotNum对应的槽标记为已经占用*/
    RC SetSlot(char* pPageData,int slotNum);

    /*对于指定的页(pPageData),将其第slotNum对应的槽标记为未使用; 随之修改页头*/
    RC ResetSlot(char* pPageData,int slotNum);

    /*对于指定的页(pPageData),检查其位图是否已满全部为1(即slot是否已满)*/
    bool IsBMapFull(char* pPageData);

private:
    PF_FileHandle* pfFileHandle;    /* 已经存在的PF 层文件处理器的指针!! */ 
    RM_FileHdr rmFileHdr;           /*RM层文件头,对于一个打开文件,将文件头保存在内存中更方便,从而不必每次都读取文件头*/
    bool bFileOpen;                 /* 文件是否打开 */
    bool bHdrChanged;               /*RM层文件头是否更改*/
};

//
// RM_FileScan: condition-based scan of records in the file(默认扫描过程中,客户端不会关闭文件)
//
class RM_FileScan {
public:
    RM_FileScan  ();
    ~RM_FileScan ();

    /*初始化OpenScan*/
    RC OpenScan  (const RM_FileHandle &fileHandle,
                  AttrType   attrType,
                  int        attrLength,
                  int        attrOffset,         /*attrOffset表示该属性相对于记录的偏移*/
                  CompOp     compOp,             /*比较的方式(大于、小于、等于...)*/
                  void       *value,
                  ClientHint pinHint = NO_HINT); // Initialize a file scan
    RC GetNextRec(RM_Record &rec);               // Get next matching record
    RC CloseScan ();                             // Close the scan

    /*数据库中某个rec的数据,对应属性值是否 与 value是否符合条件compOp*/
    bool IsMatch(char* attr);

/*自定义成员*/
private:
    /*首先,传入的参数(条件/condition)是比较的基准,暂存下来*/
    RM_FileHandle* fileHandle;              /*需要在这个handle对应文件中取找数据*/
    AttrType   attrType;
    int        attrLength;
    int        attrOffset;        
    CompOp     compOp;             
    void       *value;
    ClientHint pinHint;


    PageNum currPageNum;            /*扫描的当前元素所在page*/
    SlotNum currSlotNum;            /*扫描的当前元素所在在slotNum,GetNextRec时从这个slot的后面开始比较*/
    bool bScanOpen;                  /*filescan是否打开*/


};

//
// RM_Manager: provides RM file management
//
class RM_Manager {
public:
    RM_Manager    (PF_Manager &pfm);    /* PF_Manager作为初始化参数! */
    ~RM_Manager   ();

    RC CreateFile (const char *fileName, int recordSize);
    RC DestroyFile(const char *fileName);
    RC OpenFile   (const char *fileName, RM_FileHandle &fileHandle);

    RC CloseFile  (RM_FileHandle &fileHandle);

private:
    PF_Manager* pfManager;        /*PF_Manager的指针,从构造函数中传入*/    
};

//
// Print-error function
//
void RM_PrintError(RC rc);



/************************************* add by cdz ************************************/


/********************** 自定义的RM错误(<0)和警告(>0) ************************************/
#define RM_RECORD_TOO_SMALL     (START_RM_ERR-0)        /*记录过小*/
#define RM_RECORD_TOO_BIG       (START_RM_ERR-1)
#define RM_INVALID_FILENAME     (START_RM_ERR-2)        /*文件名不对*/
#define RM_PF                   (START_RM_ERR-3)        /*在RM层调用PF层组件时出现的错误*/
#define RM_FILE_NOT_OPEN        (START_RM_ERR-4)  
#define RM_FILE_ALREADY_OPEN    (START_RM_ERR-5)  
#define RM_REC_NOT_VALID        (START_RM_ERR-6)        /*Record对象暂时没有有效数据*/
#define RM_RID_NOT_VALID        (START_RM_ERR-7)        /*RID对象暂未填充有效数据*/
#define RM_UNEXPECTED_ERR       (START_RM_ERR-8)        /*RM中意外的错误(预期不会发生的错误)*/
#define RM_REC_SIZE_ERR         (START_RM_ERR-9)        /*记录的大小不对(多半因为它不是对应文件的记录!)*/
#define RM_SCAN_NOT_OPEN        (START_RM_ERR-10)        /*filescan没有打开*/
#define RM_SCAN_ALREADY_OPEN    (START_RM_ERR-11)
#define RM_SACAN_VAL_NULL       (START_RM_ERR-12)       /*filescan初始化时,出入数据为空指针!*/
#define RM_SCAN_INVALID_TYPE    (START_RM_ERR-13)       
#define RM_SCAN_ATTR_TOO_SHORT  (START_RM_ERR-14)
#define RM_SCAN_INVALID_OP      (START_RM_ERR-15)




#define RM_NO_VALID_SLOT        (START_RM_WARN+1)       /*警告:该page中,slot已用完*/
#define RM_SCAN_EOF             (START_RM_WARN+2)       /*filescan时已经遍历完成*/
#define RM_EOF                  (START_RM_WARN+3)        /*扫描到文件的结尾了*/                  
#endif
