//
// File:        rm_filehandle.cc
// Description: RM_FileHandle class implementation
// Authors:     Aditya Bhandari (adityasb@stanford.edu)
//
#include<cstring>
#include "rm.h"
using namespace std;

// Default constructor
RM_FileHandle::RM_FileHandle() {
    bFileOpen=false;                           
    bHdrChanged=false;
    rmFileHdr.numPages=0;
    rmFileHdr.firstFreePage=RM_PAGE_LIST_END;
    rmFileHdr.recordSize=-1;
}

// Destructor
RM_FileHandle::~RM_FileHandle() {

}

/*自定义,传入PF_FileHandle,从而将这个RM_FileHandle绑定到对应的文件上*/
RC RM_FileHandle::Open(PF_FileHandle& pfFileHandle){
    if(bFileOpen){
        return RM_FILE_ALREADY_OPEN;
    }

    /* 1.初始化RM_FileHandle的成员pfFileHandle*/
    this->pfFileHandle=&pfFileHandle;

    /* 2.读取pfFileHandle对应文件中,RM层的头信息 => 用于初始化成员rmFileHdr*/
    /* 这个头信息是在RM_Manager中创建文件时,就已经写入! */
    PF_PageHandle pageHandle;
    this->pfFileHandle->GetThisPage(1,pageHandle);  /*0:PF头; 1:RM头*/
    char* pPageData;
    pageHandle.GetData(pPageData);
    memcpy(&rmFileHdr,pPageData,sizeof(RM_FileHdr));

    /* 3.打开文件*/
    bFileOpen=true;
    bHdrChanged=true;

    /* 4.unpin*/
    this->pfFileHandle->UnpinPage(1);

    return OK_RC;
}

// Given a RID, return the record
RC RM_FileHandle::GetRec(const RID &rid, RM_Record &rec) const {
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }

    /*1.获取RID对应的pageNum 和 slotNum*/
    PageNum pageNum; 
    SlotNum slotNum;
    int rc;
    rc=rid.GetPageNum(pageNum);
    if(rc<0) return rc;
    rc=rid.GetSlotNum(slotNum);
    if(rc<0) return rc;

    /* 2.获取rid对应page的PF_PageHandle */
    PF_PageHandle pageHandle;
    rc=pfFileHandle->GetThisPage(pageNum,pageHandle);    /*需要手动unpin!!!*/
    if(rc<0){
        pfFileHandle->UnpinPage(pageNum);
        PF_PrintError(rc);
        return RM_PF;
    }

    /* 3.根据pagehandle、slotNum获取记录数据*/
    char* pRecData;
    rc=GetSlotData(pageHandle,slotNum,pRecData);
    if(rc<0){
        pfFileHandle->UnpinPage(pageNum);
        PF_PrintError(rc);
        return RM_PF;
    }
    rec.SetMembers(pRecData,rid,rmFileHdr.recordSize);  /*SetMembers会拷贝数据,而不是引用page在缓冲区中的数据*/
    
    /* 4.手动unpin数据页*/
    pfFileHandle->UnpinPage(pageNum);
    return OK_RC;
}

// Insert a new record
RC RM_FileHandle::InsertRec(const char *pData, RID &rid) {
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }
    /* 1.获取一个未用完的page */
    PageNum pageNum;
    GetOneFreePage(pageNum);
    PF_PageHandle pageHandle;
    pfFileHandle->GetThisPage(pageNum,pageHandle);  /*要手动unpin*/

    /* 2.在该page中找到一个可用的槽,获得slotNum*/
    SlotNum slotNum;
    GetOneFreeSlot(pageHandle,slotNum);

    /* 3.将插入的记录数据写入缓冲区中page的slotNum对应的位置*/
    char* pPageData;
    pageHandle.GetData(pPageData);
    int slots=GetPageSlots();
    int bmapBytes=GetBMapBytes(slots);
    char* pSlotData=pPageData + sizeof(RM_PageHdr) + bmapBytes + slotNum*rmFileHdr.recordSize;
    memcpy(pSlotData,pData,rmFileHdr.recordSize);

    /* 4.修改位图,slotNum已被占用*/
    SetSlot(pPageData,slotNum);

    /* 5.构造返回的RID数据*/
    rid.SetMembers(pageNum,slotNum);


    /*6.可能存在情况:插入记录后,page满 => 修改RM_FileHdr、RM_PageHdr*/
    if(IsBMapFull(pPageData)){
        RM_PageHdr* pageHdr=(RM_PageHdr*)pPageData;
        rmFileHdr.firstFreePage=pageHdr->nextFreePage;     /*修改文件头中的第一个空闲page*/
        pageHdr->nextFreePage=RM_SLOT_ALL_USED;             /*表示page已满,nextFreePage已经没有意义了*/
        bHdrChanged=true;
    }

    /* 7.page标记为dirty,因为修改了数据*/
    pfFileHandle->MarkDirty(pageNum);

    /* 8.unpin数据页*/
    pfFileHandle->UnpinPage(pageNum);


    return OK_RC;
}

// Delete a record
RC RM_FileHandle::DeleteRec(const RID &rid) {
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }

    /* 1.获取要删除的记录的页号、槽位号*/
    PageNum pageNum;
    SlotNum SlotNum;
    rid.GetPageNum(pageNum);
    rid.GetSlotNum(SlotNum);

    /* 2.获取要删除记录所在的page,以及其数据部分 */
    PF_PageHandle pageHandle;
    pfFileHandle->GetThisPage(pageNum,pageHandle);       /*需要手动unpin*/
    char* pPageData;
    pageHandle.GetData(pPageData);


    /* 4.将slotNum在位图中对应bit置位为0(ResetSlot内部会修改page头)*/
    /* 存在一种特殊情况:page原本是满的,删除一个记录后,变为未满 => 需要将其加入空闲链表*/
    if(IsBMapFull(pPageData)){
        RM_PageHdr* pageHdr=(RM_PageHdr*)pPageData;         
        pageHdr->nextFreePage=rmFileHdr.firstFreePage;   /*类似于头插法*/
        rmFileHdr.firstFreePage=pageNum;
        bHdrChanged=true;
    }
    ResetSlot(pPageData,SlotNum);


    /* 5.由于修改了page信息,需标记为dirty*/
    pfFileHandle->MarkDirty(pageNum);

    /* 6.unpin页*/
    pfFileHandle->UnpinPage(pageNum);

    return OK_RC;
}

// Update a record 
RC RM_FileHandle::UpdateRec(const RM_Record &rec) { 
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }

    int recSize;
    recSize=rec.GetRecSize(recSize);
    if(rmFileHdr.recordSize!=recSize){
        return RM_REC_SIZE_ERR;
    }

    /* 1.获取要更新的记录所在页、槽的信息*/
    RID rid;
    char *pRecData;
    rec.GetRid(rid);
    rec.GetData(pRecData);

    PageNum pageNum;
    SlotNum slotNum;
    rid.GetPageNum(pageNum);
    rid.GetSlotNum(slotNum);

    PF_PageHandle pageHandle;
    pfFileHandle->GetThisPage(pageNum,pageHandle);

    /* 2.将记录信息更新到page中*/
    char* pOldRecData;
    GetSlotData(pageHandle,slotNum,pOldRecData);
    memcpy(pOldRecData,pRecData,recSize);

    /* 3.修改了数据,需要标记为dirty*/
    pfFileHandle->MarkDirty(pageNum);

    /* 4.手动unpin*/
    pfFileHandle->UnpinPage(pageNum);

    return OK_RC;
}

// Forces a page (along with any contents stored in this class)
// from the buffer pool to disk.  Default value forces all pages.
RC RM_FileHandle::ForcePages(PageNum pageNum = ALL_PAGES) {
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }
    pfFileHandle->ForcePages(pageNum);
    
    return OK_RC;
}


/******************************** 自定义的部分辅助函数(公有),向外提供接口 ******************************************************/
/*获取成员变量pfFileHandle(指针)*/
RC RM_FileHandle::GetPpfFileHandle(PF_FileHandle*& pfFileHandle){
    pfFileHandle=this->pfFileHandle;
}

/*获取当前文件中,在RM层存储的*/
RC RM_FileHandle::GetRmNumPages(int& numPages){
    numPages=rmFileHdr.numPages;
}


/*判断文件是打开*/
bool RM_FileHandle::IsOpen(){
    return bFileOpen;
}

/*判断文件头是否改变*/
bool RM_FileHandle::IsHdrChanged(){
    return bHdrChanged;
}   


/*获取RM_FileHandle中的pfFileHandle指针*/
RC RM_FileHandle::GetPfFileHandle(PF_FileHandle*& pfFileHandle){
    pfFileHandle=this->pfFileHandle;
    return OK_RC;
}

/* 获取RM_FileHandle中的成员变量rmFileHdr*/
RC RM_FileHandle::GetRmFileHdr(RM_FileHdr& rmFileHdr){
    rmFileHdr=this->rmFileHdr;
    return OK_RC;
}

/**********************************   自定义的部分辅助函数(//私有)   ******************************************************/

/* 一个RM页内的数据可存储记录个数(slots) */
RC RM_FileHandle::GetPageSlots(int& numSlots)const{
    int bytes_valid=PF_PAGE_SIZE-sizeof(RM_PageHdr);        /*bitmap+slots的总字节数*/
    if(bytes_valid<=rmFileHdr.recordSize){
        return -1;
    }

    int slots=bytes_valid/rmFileHdr.recordSize+1;          /*这个计算肯定偏大,下面不断缩小到合适的大小*/
    
    while(true){
        int slotBytes=slots*rmFileHdr.recordSize;
        int bmapBytes=GetBMapBytes(slots);
        if(slotBytes+bmapBytes < bytes_valid)               /* 如果不能取等,页的末尾会有碎片!*/
            break;
        slots--;
    }
    numSlots=slots;
    return OK_RC;
}

/* 获取指定页(pageHandle)中某个slotNum对应的数据指针;slotNum从0算起 */
RC RM_FileHandle::GetSlotData(PF_PageHandle pageHandle,SlotNum slotNum,char *&pRecData) const{
    char* pPageData;
    RC rc=pageHandle.GetData(pPageData);    /*pPageData是从PF_PageHdr后开始的*/
    if(rc<0){
        PF_PrintError(rc);
        return RM_PF;
    }

    int totalSlots=GetPageSlots();
    int bmapBytes=GetBMapBytes(totalSlots);
    pRecData = pPageData + sizeof(RM_PageHdr) + bmapBytes + slotNum*(rmFileHdr.recordSize);

    return  OK_RC;
}

/*获取一个没有装满的page,返回其pageNum;如果没有,则需要给文件分配一个新的page*/
RC RM_FileHandle::GetOneFreePage(PageNum& pageNum){
    if(!bFileOpen){
        return RM_FILE_NOT_OPEN;
    }
    
    RC rc;

    /* 1.如果文件中尚有未满的page*/
    if(rmFileHdr.firstFreePage!=RM_PAGE_LIST_END){

        /* 1.1 通过RM层文件头得到第一个未满的page*/
        PF_PageHandle pageHandle;
        PageNum freePage=rmFileHdr.firstFreePage;
        rc=pfFileHandle->GetThisPage(freePage,pageHandle);     /* 读取到内存缓冲区pin住 */
        rc=pageHandle.GetPageNum(pageNum);

        /* 1.2 unpin该页数据*/
        pfFileHandle->UnpinPage(freePage);
        return OK_RC;
    }

    /**2.rmFileHdr.firstFreePage==RM_PAGE_LIST_END 或者 rmFileHdr.firstFreePage==0
     *  => 已满或者完全未分配,需要新分配page */
    else{
        PF_PageHandle pageHandle;

        /* 2.1分配一个新的page; 获取其pageNum; 获取其pPageData*/
        char* pPageData;
        rc=pfFileHandle->AllocatePage(pageHandle);  /*AllocatePage会将数据pin到缓冲区*/
        pageHandle.GetPageNum(pageNum);
        pageHandle.GetData(pPageData);

        /* 2.2 将RM层的页头信息写入page*/
        RM_PageHdr rmPageHdr;
        rmPageHdr.numSlots=GetPageSlots();
        rmPageHdr.numFreeSlots=rmPageHdr.numSlots;
        rmPageHdr.nextFreePage=RM_PAGE_LIST_END;            /*新分配的page,肯定是空闲链表的最后一块(而不是用slot已满标志)*/
        //rmPageHdr.bitMap=new char[rmPageHdr.numSlots];
        memcpy(pPageData,&rmPageHdr,sizeof(RM_PageHdr));


        /* 2.3 将该页的位图信息写入(刚分配,全部初始化为0)*/
        int bmapBytes=GetBMapBytes( rmPageHdr.numSlots);
        memset(pPageData+sizeof(RM_PageHdr),0,bmapBytes);

        /* 2.4 由于分配了新page,需要修改RM_FileHdr*/
        rmFileHdr.firstFreePage=pageNum;
        rmFileHdr.numPages++;
        bHdrChanged=true;

        /* 2.5 由于修改了RM层页头,需要标记为dirty*/
        pfFileHandle->MarkDirty(pageNum);

        /* 2.7 unpin该页*/
        pfFileHandle->UnpinPage(pageNum);

        return OK_RC;
    }

    return RM_UNEXPECTED_ERR;
}

/*对于一个给定的页(pageHandle),在其中找一个空闲slot,返回slotNum(slotNum从0算起)*/
RC RM_FileHandle::GetOneFreeSlot(PF_PageHandle pageHandle, SlotNum& slotNum){
    /*1.获取page的数据部分*/
    char* pPageData;
    pageHandle.GetData(pPageData);
    
    /*2.读取page头信息*/
    RM_PageHdr rmPageHdr;
    memcpy(&rmPageHdr,pPageData,sizeof(RM_PageHdr));

    /*3.遍历位图,找到一个空闲slot*/
    for(int i=0;i<rmPageHdr.numSlots;i++){
        if(!IsSlotUsed(pPageData,i)){
            slotNum=i;
            return OK_RC;
        }
    }

    slotNum=RM_SLOT_ALL_USED;
    return OK_RC;
}



/************************************* ops for bitmap(RM页中,位图的操作) **************************************/


/* 槽数为slots位图占用多少字节(向上取整)*/
int RM_FileHandle::GetBMapBytes(int slots) const{
    return (slots/8)+(slots%8!=0);
}

/*对于指定的页(pPageData),检查其位图中slotNum对应的槽是否已占用(slotNum从0算起)*/
bool RM_FileHandle::IsSlotUsed(char* pPageData,int slotNum){
    char lastByte=*(pPageData+slotNum/8);       /*前提是slotNum从0开始计数!!*/
    char mask=1<<(8-slotNum%8-1);
    return mask&lastByte;
}

/*对于指定的页(pPageData),将其位图中slotNum(从0算起)对应的槽标记为已经占用; 同时需要修改页头中numFreeSlots*/
RC RM_FileHandle::SetSlot(char* pPageData,int slotNum){
    char lastByte=*(pPageData+slotNum/8);
    char mask=1<<(8-slotNum%8 -1);
   *(pPageData+slotNum/8)=lastByte | mask;

    RM_PageHdr* rmPageHdr=(RM_PageHdr*)pPageData;
    rmPageHdr->numFreeSlots--;
    return OK_RC;
}


/*对于指定的页(pPageData),将其第slotNum对应的槽标记为未使用(0); 随之修改页头*/
RC ResetSlot(char* pPageData,int slotNum){
    char lastByte=*(pPageData+slotNum/8);
    char mask=1<<(8-slotNum%8 -1);          /*除要置位的bit外全为0*/
    mask=~mask;                             /*除要置位的bit外全为1*/
   *(pPageData+slotNum/8)=lastByte & mask;

    RM_PageHdr* rmPageHdr=(RM_PageHdr*)pPageData;
    rmPageHdr->numFreeSlots++;
    return OK_RC;
}

/*对于指定的页(pPageData),检查其位图是否已满全部为1(即slot是否已满)*/
bool RM_FileHandle::IsBMapFull(char* pPageData){
    // int slots=GetPageSlots();
    // for(int i=0;i<slots;i++){
    //     if(!IsSlotUsed(pPageData,i)) return false; 
    // }
    // return true;
    // 更简单的方式
    RM_PageHdr* pageHdr=(RM_PageHdr *)pPageData;
    return (pageHdr->numFreeSlots==0);
}

